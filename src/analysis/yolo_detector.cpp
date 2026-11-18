#include "analysis/yolo_detector.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

#ifdef ZP_WITH_ORT
#include <onnxruntime_c_api.h>

// The bundled DirectML EP header pulls in <DirectML.h> (and <d3d12.h>), which
// the VS2017 toolchain's Windows SDK does not ship. We only ever use the
// device-index form of the append call, so declare that single export here and
// keep the build free of the DirectX SDK.
extern "C" OrtStatus *ORT_API_CALL
OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions *options, int device_id);
#endif

namespace zp {

namespace {

// Fixed palette for classes; visually distinct, works on light and dark video.
const QColor kPalette[] = {
    QColor(0, 200, 255),  QColor(255, 138, 0),  QColor(90, 220, 120),
    QColor(255, 92, 141), QColor(150, 120, 255), QColor(255, 214, 0),
    QColor(0, 230, 190),  QColor(255, 120, 120), QColor(120, 180, 255),
};

QColor colorForClass(int id)
{
    if (id < 0) {
        return kPalette[0];
    }
    return kPalette[id % int(sizeof(kPalette) / sizeof(kPalette[0]))];
}

// COCO 80 class names, in the id order YOLOv8/YOLO11 emit.
const char *const kCocoNames[80] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train",
    "truck", "boat", "traffic light", "fire hydrant", "stop sign",
    "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag",
    "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite",
    "baseball bat", "baseball glove", "skateboard", "surfboard",
    "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon",
    "bowl", "banana", "apple", "sandwich", "orange", "broccoli", "carrot",
    "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant",
    "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote",
    "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

// Chinese labels for the classes this product actually surfaces (people,
// animals, vehicles). Everything else falls back to the English name.
struct NameZh { int id; const char *zh; };
const NameZh kZhNames[] = {
    {0, "人"}, {1, "自行车"}, {2, "汽车"}, {3, "摩托车"}, {4, "飞机"},
    {5, "公交车"}, {6, "火车"}, {7, "卡车"}, {8, "船"}, {14, "鸟"},
    {15, "猫"}, {16, "狗"}, {17, "马"}, {18, "羊"}, {19, "牛"},
    {20, "大象"}, {21, "熊"}, {22, "斑马"}, {23, "长颈鹿"}, {24, "背包"},
    {25, "雨伞"}, {28, "行李箱"},
};

QString zhFor(int id)
{
    for (const NameZh &n : kZhNames) {
        if (n.id == id) {
            return QString::fromUtf8(n.zh);
        }
    }
    return QString();
}

struct Candidate {
    float x1, y1, x2, y2;
    float score;
    int cls;
};

float iou(const Candidate &a, const Candidate &b)
{
    const float x1 = std::max(a.x1, b.x1);
    const float y1 = std::max(a.y1, b.y1);
    const float x2 = std::min(a.x2, b.x2);
    const float y2 = std::min(a.y2, b.y2);
    const float w = std::max(0.f, x2 - x1);
    const float h = std::max(0.f, y2 - y1);
    const float inter = w * h;
    const float areaA = std::max(0.f, a.x2 - a.x1) * std::max(0.f, a.y2 - a.y1);
    const float areaB = std::max(0.f, b.x2 - b.x1) * std::max(0.f, b.y2 - b.y1);
    const float denom = areaA + areaB - inter;
    return denom > 0.f ? inter / denom : 0.f;
}

} // namespace

QColor YoloDetector::classColor(int classId)
{
    return colorForClass(classId);
}

QString YoloDetector::classNameFor(int classId)
{
    if (classId >= 0 && classId < 80) {
        return QString::fromLatin1(kCocoNames[classId]);
    }
    return QString::number(classId);
}

QString YoloDetector::localizedClassName(int classId)
{
    const QString zh = zhFor(classId);
    return zh.isEmpty() ? classNameFor(classId) : zh;
}

// ---------------------------------------------------------------------------
// Implementation (ONNX Runtime C API — the C++ API needs a newer toolchain).
// ---------------------------------------------------------------------------
struct YoloDetector::Impl {
#ifdef ZP_WITH_ORT
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *memInfo = nullptr;
    OrtAllocator *allocator = nullptr;
    std::string inputName;
    std::string outputName;
    std::vector<int64_t> inputShape;

    void release()
    {
        if (api) {
            if (allocator) { api->AllocatorFree(allocator, nullptr); allocator = nullptr; }
            if (memInfo) { api->ReleaseMemoryInfo(memInfo); memInfo = nullptr; }
            if (session) { api->ReleaseSession(session); session = nullptr; }
            if (env) { api->ReleaseEnv(env); env = nullptr; }
        }
    }
#endif
    int inputW = 640;
    int inputH = 640;
    int numClasses = 0;
    QStringList names;
    QString modelPath;
    QString backend;
    float conf = 0.30f;
    float nms = 0.45f;
    QList<int> classFilter;
};

YoloDetector::YoloDetector()
    : m(new Impl)
{
}

YoloDetector::~YoloDetector()
{
#ifdef ZP_WITH_ORT
    if (m->api) {
        m->release();
    }
#endif
}

bool YoloDetector::isLoaded() const
{
#ifdef ZP_WITH_ORT
    return m->session != nullptr;
#else
    return false;
#endif
}

QString YoloDetector::backendName() const { return m->backend; }
QString YoloDetector::modelPath() const { return m->modelPath; }
QString YoloDetector::name() const { return QStringLiteral("yolo"); }

void YoloDetector::setConfidenceThreshold(float t) { m->conf = qBound(0.01f, t, 0.99f); }
void YoloDetector::setNmsThreshold(float t) { m->nms = qBound(0.01f, t, 0.99f); }
void YoloDetector::setClassFilter(const QList<int> &classIds) { m->classFilter = classIds; }

bool YoloDetector::load(const QString &modelPath, const QString &classesPath,
                        QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

#ifndef ZP_WITH_ORT
    Q_UNUSED(modelPath)
    Q_UNUSED(classesPath)
    return fail(QStringLiteral("未编译 ONNX Runtime 支持"));
#else
    m->release();
    m->backend.clear();
    m->modelPath = modelPath;

    // ---- class names ----
    m->names.clear();
    if (!classesPath.isEmpty()) {
        QFile f(classesPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!line.isEmpty()) {
                    m->names.append(line);
                }
            }
        }
    }
    if (m->names.isEmpty()) {
        return fail(QStringLiteral("类别文件为空或无法读取：%1").arg(classesPath));
    }
    m->numClasses = m->names.size();

    if (!QFileInfo::exists(modelPath)) {
        return fail(QStringLiteral("模型文件不存在：%1").arg(modelPath));
    }

    const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api) {
        return fail(QStringLiteral("ONNX Runtime API 版本不匹配"));
    }
    m->api = api;

    OrtStatus *status = nullptr;
    auto check = [&](OrtStatus *st) {
        if (st) {
            if (error) {
                *error = QString::fromUtf8(api->GetErrorMessage(st));
            }
            api->ReleaseStatus(st);
            return false;
        }
        return true;
    };
    auto checkStatus = [&](OrtStatus *st) -> bool {
        status = st;
        return check(st);
    };

    if (!checkStatus(api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "zero-player", &m->env))) {
        return false;
    }

    OrtSessionOptions *opts = nullptr;
    if (!checkStatus(api->CreateSessionOptions(&opts))) {
        return false;
    }
    api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);
    api->SetIntraOpNumThreads(opts, 1);

    // Prefer DirectML; fall back to CPU when the machine/EP rejects it. With
    // no EP appended the session simply uses the default CPU provider.
    OrtStatus *dmlStatus = OrtSessionOptionsAppendExecutionProvider_DML(opts, 0);
    if (dmlStatus == nullptr) {
        m->backend = QStringLiteral("DirectML");
    } else {
        api->ReleaseStatus(dmlStatus);
        m->backend = QStringLiteral("CPU");
    }

    // ORTCHAR_T is wchar_t on Windows for this ORT build.
    const std::wstring nativePath = QDir::toNativeSeparators(modelPath).toStdWString();
    if (!checkStatus(api->CreateSession(m->env, nativePath.c_str(), opts, &m->session))) {
        api->ReleaseSessionOptions(opts);
        return false;
    }
    api->ReleaseSessionOptions(opts);

    if (!checkStatus(api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                              &m->memInfo))) {
        return false;
    }
    if (!checkStatus(api->CreateAllocator(m->session, m->memInfo, &m->allocator))) {
        return false;
    }

    // ---- input ----
    size_t inputCount = 0;
    if (!checkStatus(api->SessionGetInputCount(m->session, &inputCount)) || inputCount == 0) {
        return fail(QStringLiteral("模型没有输入"));
    }
    char *inputName = nullptr;
    if (!checkStatus(api->SessionGetInputName(m->session, 0, m->allocator, &inputName))) {
        return false;
    }
    m->inputName = inputName;
    api->AllocatorFree(m->allocator, inputName);

    OrtTypeInfo *typeInfo = nullptr;
    if (checkStatus(api->SessionGetInputTypeInfo(m->session, 0, &typeInfo))) {
        const OrtTensorTypeAndShapeInfo *shape = nullptr;
        if (checkStatus(api->CastTypeInfoToTensorInfo(typeInfo, &shape)) && shape) {
            size_t dims = 0;
            api->GetDimensionsCount(shape, &dims);
            std::vector<int64_t> d(dims, 0);
            api->GetDimensions(shape, d.data(), dims);
            if (d.size() == 4) {
                if (d[2] > 0) m->inputH = int(d[2]);
                if (d[3] > 0) m->inputW = int(d[3]);
            }
        }
        api->ReleaseTypeInfo(typeInfo);
    }

    // ---- output name ----
    size_t outputCount = 0;
    if (!checkStatus(api->SessionGetOutputCount(m->session, &outputCount)) || outputCount == 0) {
        return fail(QStringLiteral("模型没有输出"));
    }
    char *outputName = nullptr;
    if (!checkStatus(api->SessionGetOutputName(m->session, 0, m->allocator, &outputName))) {
        return false;
    }
    m->outputName = outputName;
    api->AllocatorFree(m->allocator, outputName);

    return true;
#endif
}

AnalysisResult YoloDetector::analyze(const AnalysisFrame &frame)
{
    AnalysisResult result;
    result.playerId = frame.playerId;
    result.frameSerial = frame.frameSerial;
    result.pts = frame.pts;

#ifdef ZP_WITH_ORT
    const QImage &img = frame.image;
    if (!m->session || img.isNull()) {
        return result;
    }
    const OrtApi *api = m->api;

    const int dstW = m->inputW;
    const int dstH = m->inputH;
    const int plane = dstW * dstH;
    std::vector<float> input(size_t(plane) * 3, 114.f / 255.f);

    // ---- letterbox ----
    const float scale = std::min(float(dstW) / img.width(), float(dstH) / img.height());
    const int nw = qMax(1, int(std::lround(img.width() * scale)));
    const int nh = qMax(1, int(std::lround(img.height() * scale)));
    const int padX = (dstW - nw) / 2;
    const int padY = (dstH - nh) / 2;
    float *rPlane = input.data();
    float *gPlane = input.data() + plane;
    float *bPlane = input.data() + 2 * plane;
    for (int y = 0; y < nh; ++y) {
        const int sy = qMin(img.height() - 1, int(y / scale));
        const uchar *row = img.constScanLine(sy);
        const int dy = (padY + y) * dstW + padX;
        for (int x = 0; x < nw; ++x) {
            const int sx = qMin(img.width() - 1, int(x / scale));
            const uchar *px = row + sx * 4;   // RGBA8888
            rPlane[dy + x] = px[0] / 255.f;
            gPlane[dy + x] = px[1] / 255.f;
            bPlane[dy + x] = px[2] / 255.f;
        }
    }

    // ---- run ----
    const int64_t dims[4] = {1, 3, dstH, dstW};
    OrtValue *inputTensor = nullptr;
    if (api->CreateTensorWithDataAsOrtValue(m->memInfo, input.data(),
                                            input.size() * sizeof(float), dims, 4,
                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                            &inputTensor) != nullptr) {
        return result;
    }

    const char *inputNames[] = {m->inputName.c_str()};
    const char *outputNames[] = {m->outputName.c_str()};
    OrtValue *outputTensor = nullptr;
    OrtStatus *runStatus = api->Run(m->session, nullptr, inputNames, &inputTensor, 1,
                                    outputNames, 1, &outputTensor);
    api->ReleaseValue(inputTensor);
    if (runStatus != nullptr) {
        api->ReleaseStatus(runStatus);
        return result;
    }

    // ---- decode ----
    OrtTensorTypeAndShapeInfo *outInfo = nullptr;
    if (api->GetTensorTypeAndShape(outputTensor, &outInfo) != nullptr) {
        api->ReleaseValue(outputTensor);
        return result;
    }
    size_t dimCount = 0;
    api->GetDimensionsCount(outInfo, &dimCount);
    std::vector<int64_t> od(dimCount, 0);
    api->GetDimensions(outInfo, od.data(), dimCount);

    const float *outData = nullptr;
    if (api->GetTensorMutableData(outputTensor, reinterpret_cast<void **>(
                                      const_cast<float **>(&outData))) != nullptr
        || !outData) {
        api->ReleaseTensorTypeAndShapeInfo(outInfo);
        api->ReleaseValue(outputTensor);
        return result;
    }

    const int attrs = 4 + m->numClasses;
    bool transposed = false;     // false: [1, attrs, anchors]; true: [1, anchors, attrs]
    int anchors = 0;
    if (dimCount == 3) {
        if (od[1] == attrs) {
            anchors = int(od[2]);
        } else if (od[2] == attrs) {
            transposed = true;
            anchors = int(od[1]);
        }
    }
    if (anchors <= 0) {
        api->ReleaseTensorTypeAndShapeInfo(outInfo);
        api->ReleaseValue(outputTensor);
        return result;
    }

    auto valueAt = [&](int anchor, int attr) -> float {
        return transposed ? outData[size_t(anchor) * attrs + attr]
                          : outData[size_t(attr) * anchors + anchor];
    };

    std::vector<Candidate> cands;
    cands.reserve(256);
    for (int a = 0; a < anchors; ++a) {
        int bestCls = -1;
        float bestScore = 0.f;
        for (int c = 0; c < m->numClasses; ++c) {
            const float s = valueAt(a, 4 + c);
            if (s > bestScore) {
                bestScore = s;
                bestCls = c;
            }
        }
        if (bestScore < m->conf || bestCls < 0) {
            continue;
        }
        if (!m->classFilter.isEmpty() && !m->classFilter.contains(bestCls)) {
            continue;
        }
        const float cx = valueAt(a, 0);
        const float cy = valueAt(a, 1);
        const float bw = valueAt(a, 2);
        const float bh = valueAt(a, 3);
        cands.push_back({cx - bw / 2.f, cy - bh / 2.f, cx + bw / 2.f, cy + bh / 2.f,
                         bestScore, bestCls});
    }
    api->ReleaseTensorTypeAndShapeInfo(outInfo);
    api->ReleaseValue(outputTensor);

    // ---- per-class NMS ----
    std::sort(cands.begin(), cands.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    std::vector<Candidate> kept;
    std::vector<bool> removed(cands.size(), false);
    for (size_t i = 0; i < cands.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        kept.push_back(cands[i]);
        for (size_t j = i + 1; j < cands.size(); ++j) {
            if (!removed[j] && cands[j].cls == cands[i].cls
                && iou(cands[i], cands[j]) > m->nms) {
                removed[j] = true;
            }
        }
    }

    // ---- un-letterbox + normalize to the analyzed image ----
    const float imgW = float(img.width());
    const float imgH = float(img.height());
    result.boxes.reserve(int(kept.size()));
    for (const Candidate &c : kept) {
        float x1 = (c.x1 - padX) / scale;
        float y1 = (c.y1 - padY) / scale;
        float x2 = (c.x2 - padX) / scale;
        float y2 = (c.y2 - padY) / scale;
        x1 = qBound(0.f, x1, imgW);
        y1 = qBound(0.f, y1, imgH);
        x2 = qBound(0.f, x2, imgW);
        y2 = qBound(0.f, y2, imgH);
        if (x2 - x1 < 4.f || y2 - y1 < 4.f) {
            continue;
        }
        AnalysisBox box;
        box.rect = QRectF(x1 / imgW, y1 / imgH, (x2 - x1) / imgW, (y2 - y1) / imgH);
        box.classId = c.cls;
        const QString zh = localizedClassName(c.cls);
        box.label = (!zh.isEmpty() && zh != QString::number(c.cls))
                        ? zh
                        : m->names.value(c.cls, QString::number(c.cls));
        box.score = c.score;
        result.boxes.push_back(box);
    }
#endif
    return result;
}

// ---------------------------------------------------------------------------
// Default model discovery
// ---------------------------------------------------------------------------
bool findDefaultYoloModel(QString *modelPath, QString *classesPath, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    const QString envModel = qEnvironmentVariable("ZP_MODEL");
    if (!envModel.isEmpty()) {
        if (!QFileInfo::exists(envModel)) {
            return fail(QStringLiteral("ZP_MODEL 指向的模型不存在：%1").arg(envModel));
        }
        if (modelPath) *modelPath = envModel;
        if (classesPath) *classesPath = qEnvironmentVariable("ZP_CLASSES");
        return true;
    }

    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs << appDir + QStringLiteral("/models")
             << appDir + QStringLiteral("/../3rdpart/models")
             << appDir + QStringLiteral("/../../3rdpart/models")
             << appDir + QStringLiteral("/3rdpart/models");
    }
    dirs << QStringLiteral("3rdpart/models");   // dev run from the repo root

    const QStringList modelNames = {QStringLiteral("yolo11n.onnx"),
                                    QStringLiteral("yolov8n.onnx")};
    for (const QString &dir : dirs) {
        const QDir d(dir);
        for (const QString &name : modelNames) {
            const QString path = d.filePath(name);
            if (QFileInfo::exists(path)) {
                if (modelPath) *modelPath = path;
                if (classesPath) {
                    const QString cls = d.filePath(QStringLiteral("coco.names"));
                    *classesPath = QFileInfo::exists(cls) ? cls : QString();
                }
                return true;
            }
        }
    }
    return fail(QStringLiteral("未找到 YOLO 模型（可在 ZP_MODEL 指定，或放置于 models/）"));
}

} // namespace zp
