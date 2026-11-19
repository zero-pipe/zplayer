#include "analysis/analysis_service.h"

#include "player/frame_grabber.h"

#include <QDateTime>
#include <QFile>
#include <QThread>
#include <QtDebug>

#include <memory>

namespace zp {

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------
class AnalysisService::Worker : public QThread {
public:
    explicit Worker(AnalysisService *svc) : m_svc(svc) {}
protected:
    void run() override { m_svc->processLoop(); }
private:
    AnalysisService *m_svc;
};

AnalysisService::AnalysisService(QObject *parent)
    : QObject(parent)
{
    m_trace = qEnvironmentVariableIsSet("ZP_AI_TRACE");
    m_worker = new Worker(this);
    m_worker->start();
}

AnalysisService::~AnalysisService()
{
    {
        QMutexLocker lock(&m_mutex);
        m_quit = true;
        m_cv.wakeAll();
    }
    m_worker->wait();
    delete m_worker;
}

void AnalysisService::setAnalyzer(IFrameAnalyzer *analyzer)
{
    QMutexLocker lock(&m_mutex);
    m_analyzer = analyzer;
}

IFrameAnalyzer *AnalysisService::analyzer() const
{
    QMutexLocker lock(&m_mutex);
    return m_analyzer;
}

void AnalysisService::setTargetFps(double fps)
{
    m_targetFps = qMax(0.0, fps);
}

void AnalysisService::setRoi(quint64 playerId, const QRectF &roi)
{
    QMutexLocker lock(&m_mutex);
    if (roi.isEmpty()) {
        m_rois.remove(playerId);
    } else {
        m_rois.insert(playerId, roi.normalized());
    }
}

void AnalysisService::setSourceAnalyzer(quint64 playerId, IFrameAnalyzer *analyzer)
{
    QMutexLocker lock(&m_mutex);
    if (analyzer) {
        m_sourceAnalyzers.insert(playerId, analyzer);
    } else {
        m_sourceAnalyzers.remove(playerId);
    }
}

void AnalysisService::attach(IMediaPlayer *player, const ChannelRef &ref)
{
    if (!player || !player->supportsFrameTap()) {
        return;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_refs.insert(player->playerId(), ref);
    }
    player->setFrameSink(this);
}

void AnalysisService::detach(IMediaPlayer *player)
{
    if (!player) {
        return;
    }
    player->setFrameSink(nullptr);
    forget(player->playerId());
}

void AnalysisService::forget(quint64 playerId)
{
    {
        QMutexLocker lock(&m_mutex);
        m_refs.remove(playerId);
        m_rois.remove(playerId);
        m_sourceAnalyzers.remove(playerId);
    }
    m_lastAcceptedMs.remove(playerId);
}

void AnalysisService::detachAll()
{
    {
        QMutexLocker lock(&m_mutex);
        m_refs.clear();
        m_rois.clear();
        m_sourceAnalyzers.clear();
    }
    m_lastAcceptedMs.clear();
}

ChannelRef AnalysisService::refFor(quint64 playerId) const
{
    QMutexLocker lock(&m_mutex);
    return m_refs.value(playerId);
}

// ---------------------------------------------------------------------------
// Render-thread entry point: throttle -> GPU downscale -> queue.
// ---------------------------------------------------------------------------
void AnalysisService::onVideoFrame(const VideoFrame &frame)
{
    if (m_targetFps <= 0.0) {
        return;
    }
    QRectF roi;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_analyzer && !m_sourceAnalyzers.contains(frame.playerId)) {
            return;   // nothing to do until a model is plugged in
        }
        roi = m_rois.value(frame.playerId);
    }

    // Per-source rate limit. Live frame rates are 25-60 fps; analysis wants a
    // few fps at most, and this is what keeps 16 tiles affordable.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int intervalMs = int(1000.0 / m_targetFps);
    qint64 &last = m_lastAcceptedMs[frame.playerId];
    if (last != 0 && now - last < intervalMs) {
        return;
    }
    last = now;

    // Crop = the picture area, narrowed to the tile's ROI when the user has
    // framed one. The result carries this rect so overlays map boxes back even
    // when the analyzed area is a sub-region of the tile.
    QRectF crop = frame.videoRect;
    if (!roi.isEmpty()) {
        const QRectF narrowed = crop.intersected(roi);
        if (narrowed.width() >= 0.02 && narrowed.height() >= 0.02) {
            crop = narrowed;
        }
    }
    const QImage image = grabFrameToImage(frame, m_maxWidth, crop);
    if (image.isNull()) {
        if (m_trace) {
            qDebug().noquote() << "[ai] grab failed player=" << frame.playerId
                               << "videoRect=" << frame.videoRect << "crop=" << crop;
        }
        return;
    }
    if (m_trace) {
        qDebug().noquote() << "[ai] grab player=" << frame.playerId
                           << "img=" << image.size() << "crop=" << crop;
    }

    AnalysisFrame job;
    job.playerId = frame.playerId;
    job.frameSerial = frame.serial;
    job.pts = frame.pts;
    job.image = image;
    job.videoRect = crop;

    QMutexLocker lock(&m_mutex);
    // Fairness across tiles: keep at most one pending frame per source, so a
    // slow analyzer cannot let one busy source fill the queue and starve the
    // other fifteen. The freshest frame is the one worth keeping.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).playerId == job.playerId) {
            m_queue.removeAt(i);
            ++m_dropped;
            break;
        }
    }
    if (m_queue.size() >= m_maxQueue) {
        // Analyzer is behind: drop the oldest, keep the freshest picture.
        m_queue.dequeue();
        ++m_dropped;
    }
    m_queue.enqueue(job);
    m_cv.wakeOne();
}

// ---------------------------------------------------------------------------
// Worker thread: dequeue -> analyze -> emit (queued to the GUI thread).
// ---------------------------------------------------------------------------
void AnalysisService::processLoop()
{
    for (;;) {
        AnalysisFrame frame;
        IFrameAnalyzer *analyzer = nullptr;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && !m_quit) {
                m_cv.wait(&m_mutex);
            }
            if (m_quit) {
                return;
            }
            frame = m_queue.dequeue();
            // A source with its own analyzer (e.g. OCR on one tile) overrides
            // the shared default.
            analyzer = m_sourceAnalyzers.value(frame.playerId, m_analyzer);
            if (analyzer) {
                ++m_dispatched;
            }
        }
        if (!analyzer) {
            continue;
        }

        AnalysisResult result = analyzer->analyze(frame);
        result.playerId = frame.playerId;
        result.frameSerial = frame.frameSerial;
        result.pts = frame.pts;
        result.videoRect = frame.videoRect;

        // Track ids are what make boxes persistent across frames (and let
        // evidence spans / AI skip ignore a single missed detection). dt comes
        // from media time when available, wall clock otherwise; a seek makes
        // the pts jump, which resets the per-track motion instead of teleporting
        // boxes across the frame. OCR text boxes are not tracked — they are
        // per-line readings, not physical targets.
        const bool track = analyzer->name() != QLatin1String("ocr");
        if (track) {
            ByteTracker &tracker = m_trackers[frame.playerId];
            double dt = 0.25;
            const bool havePts = frame.pts >= 0.0
                                 && m_trackerLastPts.contains(frame.playerId);
            if (havePts) {
                dt = frame.pts - m_trackerLastPts.value(frame.playerId);
                if (dt < 0.0 || dt > 2.0) {
                    tracker.reset();   // seek / discontinuity
                    dt = 0.25;
                }
            }
            if (frame.pts >= 0.0) {
                m_trackerLastPts.insert(frame.playerId, frame.pts);
            }
            tracker.update(result.boxes, dt);
        }

        // Dev aid (ZP_AI_TRACE=1): one line per analyzed frame, so per-source
        // cadence and detection counts are visible in the ZP_LOG file.
        if (m_trace) {
            QStringList labels;
            for (const AnalysisBox &b : result.boxes) {
                labels << QStringLiteral("%1(%2)").arg(b.label).arg(b.classId);
                if (labels.size() >= 4) {
                    break;
                }
            }
            qDebug().noquote() << "[ai] result player=" << frame.playerId
                               << "pts=" << QString::number(frame.pts, 'f', 2)
                               << "boxes=" << result.boxes.size()
                               << "analyzer=" << analyzer->name()
                               << labels.join(QLatin1Char(','));
        }

        // Auto-connection: receivers living in the GUI thread get this queued.
        emit resultReady(refFor(frame.playerId), result);
    }
}

// ---------------------------------------------------------------------------
// Dev helper: pipeline smoke test without a model.
// ---------------------------------------------------------------------------
namespace {

class StatsAnalyzer : public IFrameAnalyzer {
public:
    explicit StatsAnalyzer(const QString &logPath)
    {
        m_log.setFileName(logPath);
        m_log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    }

    AnalysisResult analyze(const AnalysisFrame &frame) override
    {
        const QImage &img = frame.image;
        quint64 sum = 0;
        int count = 0;
        for (int y = 0; y < img.height(); y += 8) {
            const uchar *line = img.constScanLine(y);
            for (int x = 0; x < img.width(); x += 8) {
                const uchar *px = line + x * 4;
                sum += (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                ++count;
            }
        }
        const double luma = count ? double(sum) / count : 0.0;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        Stat &s = m_stats[frame.playerId];
        ++s.frames;
        if (s.since == 0) {
            s.since = now;
        }
        if (now - s.since >= 1000) {
            const double fps = s.frames * 1000.0 / double(now - s.since);
            const QString line = QStringLiteral(
                                     "[analyze] player=%1 pts=%2 %3x%4 fps=%5 luma=%6\n")
                                     .arg(frame.playerId)
                                     .arg(frame.pts, 0, 'f', 2)
                                     .arg(img.width())
                                     .arg(img.height())
                                     .arg(fps, 0, 'f', 1)
                                     .arg(luma, 0, 'f', 1);
            qInfo().noquote() << line.trimmed();
            if (m_log.isOpen()) {
                m_log.write(line.toUtf8());
                m_log.flush();
            }
            s.frames = 0;
            s.since = now;
        }
        return AnalysisResult();
    }

    QString name() const override { return QStringLiteral("stats"); }

private:
    struct Stat {
        qint64 since = 0;
        int frames = 0;
    };
    QHash<quint64, Stat> m_stats;
    QFile m_log;
};

} // namespace

IFrameAnalyzer *createDebugStatsAnalyzer()
{
    static std::unique_ptr<StatsAnalyzer> s_analyzer;
    // ZP_ANALYZE=<path>: append per-source stats to <path> once per second.
    const QString path = qEnvironmentVariable("ZP_ANALYZE");
    if (path.isEmpty()) {
        return nullptr;
    }
    if (!s_analyzer) {
        s_analyzer.reset(new StatsAnalyzer(path));
    }
    return s_analyzer.get();
}

} // namespace zp
