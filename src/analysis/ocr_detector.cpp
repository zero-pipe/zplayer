#include "analysis/ocr_detector.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#ifdef ZP_WITH_ORT
#include <onnxruntime_c_api.h>

// Same toolchain constraint as the object detector: the bundled DirectML EP
// header drags in <DirectML.h>; we only need the append call.
extern "C" OrtStatus *ORT_API_CALL
OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions *options, int device_id);
#endif

namespace zp {

namespace {

// ---------------------------------------------------------------------------
// Small helpers shared by the detector and recognizer.
// ---------------------------------------------------------------------------

#ifdef ZP_WITH_ORT
struct OrtSessionWrapper {
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *memInfo = nullptr;
    OrtAllocator *allocator = nullptr;
    std::string inputName;
    std::string outputName;
    std::vector<int64_t> inputShape;

    bool create(OrtEnv *sharedEnv, const OrtApi *sharedApi, const QString &path,
                bool useDml, QString *error)
    {
        api = sharedApi;
        env = sharedEnv;
        if (api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memInfo)
            != nullptr) {
            if (error) *error = QStringLiteral("内存信息创建失败");
            return false;
        }

        OrtSessionOptions *opts = nullptr;
        if (api->CreateSessionOptions(&opts) != nullptr) {
            if (error) *error = QStringLiteral("会话选项创建失败");
            return false;
        }
        api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);
        api->SetIntraOpNumThreads(opts, 1);

        if (useDml) {
            // The bundled DirectML EP hangs on the PP-OCR models, so this is
            // opt-in only (ZP_OCR_DML=1); CPU is the default here.
            if (OrtSessionOptionsAppendExecutionProvider_DML(opts, 0) != nullptr) {
                // rejected: default CPU provider is used
            }
        }

        const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
        OrtStatus *st = api->CreateSession(env, nativePath.c_str(), opts, &session);
        api->ReleaseSessionOptions(opts);
        if (st) {
            if (error) *error = QString::fromUtf8(api->GetErrorMessage(st));
            api->ReleaseStatus(st);
            return false;
        }

        if (api->CreateAllocator(session, memInfo, &allocator) != nullptr) {
            if (error) *error = QStringLiteral("分配器创建失败");
            return false;
        }

        size_t count = 0;
        if (api->SessionGetInputCount(session, &count) != nullptr || count == 0) {
            if (error) *error = QStringLiteral("模型没有输入");
            return false;
        }
        char *name = nullptr;
        if (api->SessionGetInputName(session, 0, allocator, &name) != nullptr) {
            if (error) *error = QStringLiteral("读取输入名失败");
            return false;
        }
        inputName = name;
        api->AllocatorFree(allocator, name);

        OrtTypeInfo *info = nullptr;
        if (api->SessionGetInputTypeInfo(session, 0, &info) == nullptr && info) {
            const OrtTensorTypeAndShapeInfo *shape = nullptr;
            if (api->CastTypeInfoToTensorInfo(info, &shape) == nullptr && shape) {
                size_t dims = 0;
                api->GetDimensionsCount(shape, &dims);
                inputShape.assign(dims, 0);
                api->GetDimensions(shape, inputShape.data(), dims);
            }
            api->ReleaseTypeInfo(info);
        }

        size_t outCount = 0;
        if (api->SessionGetOutputCount(session, &outCount) != nullptr || outCount == 0) {
            if (error) *error = QStringLiteral("模型没有输出");
            return false;
        }
        name = nullptr;
        if (api->SessionGetOutputName(session, 0, allocator, &name) != nullptr) {
            if (error) *error = QStringLiteral("读取输出名失败");
            return false;
        }
        outputName = name;
        api->AllocatorFree(allocator, name);
        return true;
    }

    // Runs one float tensor and returns the output shape plus a copy of the
    // data (the OrtValue is released before returning).
    bool run(std::vector<float> &input, const int64_t dims[4],
             std::vector<float> *output, std::vector<int64_t> *outShape, QString *error)
    {
        OrtValue *in = nullptr;
        if (api->CreateTensorWithDataAsOrtValue(
                memInfo, input.data(), input.size() * sizeof(float), dims, 4,
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in) != nullptr) {
            if (error) *error = QStringLiteral("输入张量创建失败");
            return false;
        }
        const char *inNames[] = {inputName.c_str()};
        const char *outNames[] = {outputName.c_str()};
        OrtValue *out = nullptr;
        OrtStatus *st = api->Run(session, nullptr, inNames, &in, 1, outNames, 1, &out);
        api->ReleaseValue(in);
        if (st) {
            if (error) *error = QString::fromUtf8(api->GetErrorMessage(st));
            api->ReleaseStatus(st);
            return false;
        }

        OrtTensorTypeAndShapeInfo *info = nullptr;
        if (api->GetTensorTypeAndShape(out, &info) != nullptr) {
            api->ReleaseValue(out);
            if (error) *error = QStringLiteral("读取输出形状失败");
            return false;
        }
        size_t dimCount = 0;
        api->GetDimensionsCount(info, &dimCount);
        outShape->assign(dimCount, 0);
        api->GetDimensions(info, outShape->data(), dimCount);
        api->ReleaseTensorTypeAndShapeInfo(info);

        const float *data = nullptr;
        if (api->GetTensorMutableData(out, reinterpret_cast<void **>(
                                          const_cast<float **>(&data))) != nullptr
            || !data) {
            api->ReleaseValue(out);
            if (error) *error = QStringLiteral("读取输出数据失败");
            return false;
        }
        const size_t total = std::accumulate(outShape->begin(), outShape->end(),
                                             size_t(1), std::multiplies<size_t>());
        output->assign(data, data + total);
        api->ReleaseValue(out);
        return true;
    }
};
#endif

// A detected text line in original-image pixels.
struct TextLine {
    QRectF rect;
    float detScore = 0.f;
};

} // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct OcrDetector::Impl {
#ifdef ZP_WITH_ORT
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSessionWrapper det;
    OrtSessionWrapper rec;

    void release()
    {
        if (api) {
            if (det.allocator) { api->AllocatorFree(det.allocator, nullptr); }
            if (det.memInfo) { api->ReleaseMemoryInfo(det.memInfo); }
            if (det.session) { api->ReleaseSession(det.session); }
            det = OrtSessionWrapper();
            if (rec.allocator) { api->AllocatorFree(rec.allocator, nullptr); }
            if (rec.memInfo) { api->ReleaseMemoryInfo(rec.memInfo); }
            if (rec.session) { api->ReleaseSession(rec.session); }
            rec = OrtSessionWrapper();
            if (env) { api->ReleaseEnv(env); env = nullptr; }
        }
    }
#endif

    QStringList dict;        // recognized characters, index 0 = blank
    QString backend;
    QString detPath, recPath;
    float detThresh = 0.30f;
    float minTextScore = 0.50f;
};

OcrDetector::OcrDetector()
    : m(new Impl)
{
}

OcrDetector::~OcrDetector()
{
#ifdef ZP_WITH_ORT
    if (m->api) {
        m->release();
    }
#endif
}

QString OcrDetector::name() const { return QStringLiteral("ocr"); }
bool OcrDetector::isLoaded() const
{
#ifdef ZP_WITH_ORT
    return m->det.session && m->rec.session;
#else
    return false;
#endif
}
QString OcrDetector::backendName() const { return m->backend; }

void OcrDetector::setDetThreshold(float t) { m->detThresh = qBound(0.05f, t, 0.95f); }
void OcrDetector::setMinTextScore(float s) { m->minTextScore = qBound(0.f, s, 0.99f); }

bool OcrDetector::load(const QString &detModel, const QString &recModel,
                       const QString &dictPath, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
#ifndef ZP_WITH_ORT
    Q_UNUSED(detModel)
    Q_UNUSED(recModel)
    Q_UNUSED(dictPath)
    return fail(QStringLiteral("未编译 ONNX Runtime 支持"));
#else
    m->release();
    m->detPath = detModel;
    m->recPath = recModel;

    // ---- dictionary ----
    m->dict.clear();
    QFile dictFile(dictPath);
    if (!dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("字符表无法读取：%1").arg(dictPath));
    }
    QTextStream ts(&dictFile);
    ts.setCodec("UTF-8");
    m->dict.append(QString());   // index 0: CTC blank
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (!line.isNull() && !line.isEmpty()) {
            m->dict.append(line);
        }
    }
    if (m->dict.size() < 100) {
        return fail(QStringLiteral("字符表内容过少：%1").arg(dictPath));
    }

    if (!QFileInfo::exists(detModel)) {
        return fail(QStringLiteral("检测模型不存在：%1").arg(detModel));
    }
    if (!QFileInfo::exists(recModel)) {
        return fail(QStringLiteral("识别模型不存在：%1").arg(recModel));
    }

    const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api) {
        return fail(QStringLiteral("ONNX Runtime API 版本不匹配"));
    }
    m->api = api;
    if (api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "zero-player-ocr", &m->env) != nullptr) {
        return fail(QStringLiteral("ONNX Runtime 环境创建失败"));
    }

    QString err;
    const bool useDml = qEnvironmentVariableIsSet("ZP_OCR_DML");
    if (!m->det.create(m->env, api, detModel, useDml, &err)) {
        return fail(QStringLiteral("检测模型加载失败：%1").arg(err));
    }
    if (!m->rec.create(m->env, api, recModel, useDml, &err)) {
        return fail(QStringLiteral("识别模型加载失败：%1").arg(err));
    }
    m->backend = useDml ? QStringLiteral("DirectML") : QStringLiteral("CPU");
    return true;
#endif
}

AnalysisResult OcrDetector::analyze(const AnalysisFrame &frame)
{
    AnalysisResult result;
    result.playerId = frame.playerId;
    result.frameSerial = frame.frameSerial;
    result.pts = frame.pts;

#ifdef ZP_WITH_ORT
    const QImage &img = frame.image;
    if (!m->det.session || !m->rec.session || img.isNull()) {
        return result;
    }
    const bool trace = qEnvironmentVariableIsSet("ZP_AI_TRACE");
    if (trace) {
        qInfo().noquote() << "[ocr] start player=" << frame.playerId
                          << "size=" << img.size();
    }
    const OrtApi *api = m->api;

    // ---- detection input: keep aspect, cap the long side, round to 32 ----
    const int srcW = img.width();
    const int srcH = img.height();
    const int maxSide = 960;
    double scale = std::min(1.0, double(maxSide) / std::max(srcW, srcH));
    int dstW = qMax(32, int(std::lround(srcW * scale / 32.0)) * 32);
    int dstH = qMax(32, int(std::lround(srcH * scale / 32.0)) * 32);
    // Dynamic-shape exports accept anything; fixed shapes ask for theirs.
    if (m->det.inputShape.size() == 4) {
        if (m->det.inputShape[2] > 0) dstH = int(m->det.inputShape[2]);
        if (m->det.inputShape[3] > 0) dstW = int(m->det.inputShape[3]);
    }

    const int plane = dstW * dstH;
    std::vector<float> input(size_t(plane) * 3, 0.f);
    float *rP = input.data();
    float *gP = input.data() + plane;
    float *bP = input.data() + 2 * plane;
    const QImage rgb = img.convertToFormat(QImage::Format_RGBA8888);
    // Det input is bilinear-resized implicitly by sampling; the analysis frames
    // are already ~960 wide, so nearest is visually neutral and cheap.
    for (int y = 0; y < dstH; ++y) {
        const int sy = qMin(srcH - 1, y * srcH / dstH);
        const uchar *row = rgb.constScanLine(sy);
        for (int x = 0; x < dstW; ++x) {
            const int sx = qMin(srcW - 1, x * srcW / dstW);
            const uchar *px = row + sx * 4;
            const int i = y * dstW + x;
            rP[i] = (px[0] / 255.f - 0.485f) / 0.229f;
            gP[i] = (px[1] / 255.f - 0.456f) / 0.224f;
            bP[i] = (px[2] / 255.f - 0.406f) / 0.225f;
        }
    }

    const int64_t detDims[4] = {1, 3, dstH, dstW};
    std::vector<float> detOut;
    std::vector<int64_t> detShape;
    QString err;
    if (!m->det.run(input, detDims, &detOut, &detShape, &err)) {
        if (trace) {
            qInfo().noquote() << "[ocr] det failed:" << err;
        }
        return result;
    }
    if (trace) {
        qInfo().noquote() << "[ocr] det done in=" << dstW << "x" << dstH
                          << "out=" << QVector<qint64>(detShape.begin(), detShape.end());
    }

    // ---- DB postprocess: threshold + connected components ----
    int mapH = 0, mapW = 0;
    const float *prob = nullptr;
    if (detShape.size() == 4) {
        mapH = int(detShape[2]);
        mapW = int(detShape[3]);
    } else if (detShape.size() == 3) {
        mapH = int(detShape[1]);
        mapW = int(detShape[2]);
    }
    if (mapH <= 0 || mapW <= 0 || size_t(mapH) * mapW > detOut.size()) {
        return result;
    }
    prob = detOut.data();

    std::vector<uint8_t> visited(size_t(mapH) * mapW, 0);
    std::vector<TextLine> lines;
    std::vector<int> stack;
    const double rx = double(srcW) / mapW;    // map cell -> source pixel
    const double ry = double(srcH) / mapH;

    for (int my = 0; my < mapH; ++my) {
        for (int mx = 0; mx < mapW; ++mx) {
            const size_t idx = size_t(my) * mapW + mx;
            if (visited[idx] || prob[idx] < m->detThresh) {
                continue;
            }
            // flood fill one text region
            visited[idx] = 1;
            stack.assign(1, int(idx));
            int minX = mx, maxX = mx, minY = my, maxY = my;
            int count = 0;
            double scoreSum = 0.0;
            while (!stack.empty()) {
                const int cur = stack.back();
                stack.pop_back();
                const int cy = cur / mapW;
                const int cx = cur % mapW;
                ++count;
                scoreSum += prob[cur];
                minX = qMin(minX, cx); maxX = qMax(maxX, cx);
                minY = qMin(minY, cy); maxY = qMax(maxY, cy);
                const int nx[4] = {cx - 1, cx + 1, cx, cx};
                const int ny[4] = {cy, cy, cy - 1, cy + 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx[k] < 0 || nx[k] >= mapW || ny[k] < 0 || ny[k] >= mapH) {
                        continue;
                    }
                    const size_t ni = size_t(ny[k]) * mapW + nx[k];
                    if (!visited[ni] && prob[ni] >= m->detThresh) {
                        visited[ni] = 1;
                        stack.push_back(int(ni));
                    }
                }
            }
            const int bw = maxX - minX + 1;
            const int bh = maxY - minY + 1;
            if (bw < 3 || bh < 3 || count < 12) {
                continue;   // noise
            }
            if (bw < bh && bh > 6 * bw) {
                continue;   // vertical sliver, not scene text
            }
            // unclip: grow the region proportionally to its area/perimeter so
            // glyph edges are not clipped before recognition.
            const double area = double(bw) * bh;
            double expand = area * 1.4 / (2.0 * (bw + bh));
            expand = qBound(1.0, expand, 6.0);
            QRectF line((minX - expand) * rx, (minY - expand) * ry,
                        (bw + 2.0 * expand) * rx, (bh + 2.0 * expand) * ry);
            line = line.intersected(QRectF(0, 0, srcW, srcH));
            if (line.width() < 6 || line.height() < 6) {
                continue;
            }
            TextLine tl;
            tl.rect = line;
            tl.detScore = float(scoreSum / count);
            lines.push_back(tl);
        }
    }

    if (lines.empty()) {
        return result;
    }
    // Reading order: top to bottom, then left to right.
    std::sort(lines.begin(), lines.end(), [](const TextLine &a, const TextLine &b) {
        if (std::abs(a.rect.center().y() - b.rect.center().y()) < a.rect.height() * 0.5) {
            return a.rect.left() < b.rect.left();
        }
        return a.rect.top() < b.rect.top();
    });
    if (lines.size() > 24) {
        lines.resize(24);
    }

    // ---- recognition per line ----
    const int recH = 48;
    const bool recDynamicW =
        m->rec.inputShape.size() == 4 && m->rec.inputShape[3] <= 0;
    const int recFixedW =
        (!recDynamicW && m->rec.inputShape.size() == 4 && m->rec.inputShape[3] > 0)
            ? int(m->rec.inputShape[3]) : 320;
    for (const TextLine &tl : lines) {
        const QRect srcRect(int(tl.rect.x()), int(tl.rect.y()),
                            qMax(1, int(tl.rect.width())), qMax(1, int(tl.rect.height())));
        // Dynamic-width models get the line at its natural aspect (rounded to a
        // multiple of 8) — squashing to a fixed width costs accuracy on plates
        // and signs. Fixed-width exports fall back to a scaled crop.
        int lineW = recFixedW;
        if (recDynamicW) {
            const double aspect = double(srcRect.width()) / qMax(1, srcRect.height());
            lineW = qBound(16, int(std::lround(recH * aspect / 8.0)) * 8, 640);
        }
        const int recW = lineW;
        QImage crop = rgb.copy(srcRect).scaled(recW, recH, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation);
        const int rPlane = recW * recH;
        std::vector<float> recInput(size_t(rPlane) * 3, 0.f);
        for (int y = 0; y < recH; ++y) {
            const uchar *row = crop.constScanLine(y);
            for (int x = 0; x < recW; ++x) {
                const uchar *px = row + x * 4;
                const int i = y * recW + x;
                recInput[i] = (px[0] / 255.f - 0.5f) / 0.5f;
                recInput[rPlane + i] = (px[1] / 255.f - 0.5f) / 0.5f;
                recInput[2 * rPlane + i] = (px[2] / 255.f - 0.5f) / 0.5f;
            }
        }
        const int64_t recDims[4] = {1, 3, recH, recW};
        std::vector<float> recOut;
        std::vector<int64_t> recShape;
        if (!m->rec.run(recInput, recDims, &recOut, &recShape, &err)) {
            continue;
        }
        if (recShape.size() != 3) {
            continue;
        }
        const int steps = int(recShape[1]);
        const int classes = int(recShape[2]);
        if (steps <= 0 || classes <= 1) {
            continue;
        }

        QString text;
        double confSum = 0.0;
        int confCount = 0;
        int prev = 0;
        for (int t = 0; t < steps; ++t) {
            const float *row = recOut.data() + size_t(t) * classes;
            int best = 0;
            float bestP = row[0];
            for (int c = 1; c < classes; ++c) {
                if (row[c] > bestP) {
                    bestP = row[c];
                    best = c;
                }
            }
            if (best != 0 && best != prev) {
                // Model class 1..N maps to dict entries 1..N (dict[0] is the
                // CTC blank, so the mapping is direct), last class = space.
                if (best < m->dict.size()) {
                    text += m->dict.at(best);
                } else {
                    text += QLatin1Char(' ');
                }
                confSum += bestP;
                ++confCount;
            }
            prev = best;
        }
        if (text.isEmpty() || confCount == 0) {
            continue;
        }
        const float score = float(confSum / confCount);
        if (score < m->minTextScore) {
            continue;
        }

        AnalysisBox box;
        box.rect = QRectF(tl.rect.x() / srcW, tl.rect.y() / srcH,
                          tl.rect.width() / srcW, tl.rect.height() / srcH);
        box.classId = kTextClassId;
        box.label = text;
        box.score = score;
        result.boxes.push_back(box);
    }

    QStringList joined;
    for (const AnalysisBox &b : result.boxes) {
        joined.append(b.label);
    }
    result.label = joined.join(QStringLiteral(" | "));
    if (trace) {
        qInfo().noquote() << "[ocr] done boxes=" << result.boxes.size()
                          << "text=" << result.label;
    }
#else
    Q_UNUSED(frame)
#endif
    return result;
}

// ---------------------------------------------------------------------------
// Model discovery
// ---------------------------------------------------------------------------
bool findDefaultOcrModels(QString *detModel, QString *recModel, QString *dictPath,
                          QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    QStringList dirs;
    const QString envDir = qEnvironmentVariable("ZP_OCR_DIR");
    if (!envDir.isEmpty()) {
        dirs << envDir;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs << appDir + QStringLiteral("/models/ocr")
             << appDir + QStringLiteral("/../3rdpart/models/ocr")
             << appDir + QStringLiteral("/../../3rdpart/models/ocr");
    }
    dirs << QStringLiteral("3rdpart/models/ocr");

    for (const QString &dir : dirs) {
        const QDir d(dir);
        const QString det = d.filePath(QStringLiteral("ch_PP-OCRv4_det_mobile.onnx"));
        const QString rec = d.filePath(QStringLiteral("ch_PP-OCRv4_rec_mobile.onnx"));
        const QString dict = d.filePath(QStringLiteral("ppocr_keys_v1.txt"));
        if (QFileInfo::exists(det) && QFileInfo::exists(rec) && QFileInfo::exists(dict)) {
            if (detModel) *detModel = det;
            if (recModel) *recModel = rec;
            if (dictPath) *dictPath = dict;
            return true;
        }
    }
    return fail(QStringLiteral("未找到 OCR 模型（ZP_OCR_DIR 或 models/ocr/ 下的 "
                               "ch_PP-OCRv4_det_mobile.onnx 等三个文件）"));
}

} // namespace zp
