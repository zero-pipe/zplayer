#ifndef ZP_ANALYSIS_SERVICE_H
#define ZP_ANALYSIS_SERVICE_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QWaitCondition>

#include "analysis/analyzer.h"
#include "platform/platform_api.h"
#include "player/media_player.h"

namespace zp {

class IFrameAnalyzer;

// ---------------------------------------------------------------------------
// Per-window AI scheduler.
//
// All tiles share one analyzer and one worker thread. The service:
//   * receives every rendered frame through the video frame tap,
//   * throttles each source to a target analysis rate (default 4 fps),
//   * downscales on the GPU before the readback,
//   * keeps a small bounded queue — when the analyzer falls behind, the oldest
//     pending frame is dropped, because for live video the freshest frame is
//     the one worth analyzing,
//   * tags results with the owning ChannelRef and emits them on the GUI thread.
//
// Attach/detach happen on the GUI thread (tile lifecycle). Frame taps arrive on
// the render thread, which for QOpenGLWidget is also the GUI thread; the
// CPU-heavy part always runs on the worker.
// ---------------------------------------------------------------------------
class AnalysisService : public QObject, public IVideoFrameSink {
    Q_OBJECT
public:
    explicit AnalysisService(QObject *parent = nullptr);
    ~AnalysisService() override;

    // Not owned. May be null (service idles). Safe to set at any time.
    void setAnalyzer(IFrameAnalyzer *analyzer);
    IFrameAnalyzer *analyzer() const;

    // Analysis cadence per source. 0 disables analysis on new frames.
    void setTargetFps(double fps);
    double targetFps() const { return m_targetFps; }

    // GPU downscale target before readback; 0 = native tile size.
    void setMaxAnalysisWidth(int px) { m_maxWidth = px; }
    int maxAnalysisWidth() const { return m_maxWidth; }

    // Route a player's frames to this service. No-op for engines without a
    // frame tap. The player must stay alive until detach()/forget().
    void attach(IMediaPlayer *player, const ChannelRef &ref);
    void detach(IMediaPlayer *player);
    // Bookkeeping-only removal, for when the player object is already gone
    // (e.g. from a QObject::destroyed hook).
    void forget(quint64 playerId);
    void detachAll();

    // IVideoFrameSink — runs on the render thread.
    void onVideoFrame(const VideoFrame &frame) override;

    quint64 dispatchedCount() const { return m_dispatched; }
    quint64 droppedCount() const { return m_dropped; }

signals:
    void resultReady(const zp::ChannelRef &ref, const zp::AnalysisResult &result);

private:
    class Worker;   // QThread running processLoop()
    friend class Worker;

    void processLoop();                 // worker thread body
    ChannelRef refFor(quint64 playerId) const;

    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QQueue<AnalysisFrame> m_queue;
    QHash<quint64, ChannelRef> m_refs;
    IFrameAnalyzer *m_analyzer = nullptr;
    bool m_quit = false;

    // GUI-thread only.
    QHash<quint64, qint64> m_lastAcceptedMs;
    double m_targetFps = 4.0;
    int m_maxWidth = 960;
    int m_maxQueue = 4;

    // Counters (guarded by m_mutex).
    quint64 m_dispatched = 0;
    quint64 m_dropped = 0;

    Worker *m_worker = nullptr;
};

// Dev helper: ZP_ANALYZE=<file> installs a trivial analyzer that appends one
// line per source per second (frame rate, size, mean luma) to <file>, so the
// pipeline can be verified before a real model is wired in. Returns nullptr
// when the variable is unset.
IFrameAnalyzer *createDebugStatsAnalyzer();

} // namespace zp

#endif // ZP_ANALYSIS_SERVICE_H
