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
    }
    m_lastAcceptedMs.remove(playerId);
}

void AnalysisService::detachAll()
{
    {
        QMutexLocker lock(&m_mutex);
        m_refs.clear();
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
    {
        QMutexLocker lock(&m_mutex);
        if (!m_analyzer) {
            return;   // nothing to do until a model is plugged in
        }
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

    // GPU readback with downscale. This is the one GPU->CPU stall on the
    // render thread; keeping it at analysis size and a few fps is what makes
    // it acceptable. A PBO/async path can replace it without changing callers.
    const QImage image = grabFrameToImage(frame, m_maxWidth);
    if (image.isNull()) {
        return;
    }

    AnalysisFrame job;
    job.playerId = frame.playerId;
    job.frameSerial = frame.serial;
    job.pts = frame.pts;
    job.image = image;

    QMutexLocker lock(&m_mutex);
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
            analyzer = m_analyzer;
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
