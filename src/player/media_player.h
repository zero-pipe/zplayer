#ifndef ZP_MEDIA_PLAYER_H
#define ZP_MEDIA_PLAYER_H

#include <QObject>
#include <QUrl>
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QColor>

namespace zp {

// ---------------------------------------------------------------------------
// Engine-neutral video settings
//
// Exposed through IMediaPlayer::setVideoSettings(). The values are phrased in
// terms common to all backends; each adapter maps them to its own native knobs
// (libmpv properties, QtAV setters, ...). Settings are stored by the engine and
// applied to the current stream when possible, and always carried into the next
// open()/play(). UI code never mentions a specific engine.
// ---------------------------------------------------------------------------

// Hardware decoding strategy. Not every backend/platform supports every mode;
// unsupported requests degrade to a safe software/auto path, never an error.
enum class HwDecode {
    Auto,      // let the engine decide (safe default)
    Disabled,  // software decode only
    D3D11VA,   // Windows: D3D11 video decode
    Cuda,      // NVIDIA NVDEC/CUDA
    Dxva2,     // Windows: DXVA2 (older)
    VideoToolbox, // macOS
    Vaapi      // Linux
};

struct SubtitleSettings {
    bool enabled = true;
    QString externalFile;      // absolute path to an external subtitle file ("" = none)
    int track = -1;            // in-container track index (-1 = auto / first)
    bool autoLoadInFolder = true; // pick up a matching .srt/.ass next to the media
    QString fontFamily;        // "" = engine default
};

struct FilterSettings {
    // Simple picture adjustments (typical range -100..100, 0 = neutral).
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    int hue = 0;
    // Extra raw filter-chain entries, in the engine's own filter syntax. Kept
    // opaque on purpose so advanced callers can use backend-specific filters;
    // adapters that cannot parse them simply ignore this list.
    QStringList rawChain;
};

struct VideoSettings {
    HwDecode hwdec = HwDecode::Auto;
    SubtitleSettings subtitle;
    FilterSettings filters;
    bool deinterlace = false;  // enable a deinterlacer for interlaced sources
};

// ---------------------------------------------------------------------------
// Media engine abstraction
//
// UI code (VideoTile etc.) depends only on IMediaPlayer + IMediaRenderer, never
// on a concrete engine. QtAV is one implementation (QtavMediaPlayer); libmpv /
// ZMS+FFmpeg can be added later without touching the UI.
//
// Contract:
//   1. Caller obtains a renderer surface widget to place in its layout.
//   2. Caller attaches the engine to the surface (once, after both exist).
//   3. Caller calls open(url) then play(). Engine reports state via signals.
// ---------------------------------------------------------------------------

enum class MediaState {
    Idle,        // no media loaded / stopped
    Opening,     // open() in progress (network handshake)
    Playing,     // decoding & rendering
    Paused,
    Stopped,
    Failed
};

// ---------------------------------------------------------------------------
// Frame tap (AI / analysis hook)
//
// A backend that owns its render pipeline (libmpv over OpenGL) can hand every
// rendered frame to an IVideoFrameSink while the GL context is current. This is
// the single extension point local inference is built on: analyzers receive a
// GPU texture and decide themselves whether to run on it, downscale it, or read
// it back to CPU.
//
// Contract:
//   * Called on the renderer thread (for QOpenGLWidget: the GUI thread), inside
//     the widget's paint, with the render context current.
//   * The texture is owned by the backend and is only valid during the call.
//     Sinks that need to keep the pixels must copy (GPU copy, PBO readback, ...).
//   * Sinks must be cheap: no blocking, no synchronous inference. Queue work.
//   * A sink may install itself on any number of players at once.
// ---------------------------------------------------------------------------
struct VideoFrame {
    unsigned int textureId = 0;   // GL_TEXTURE_2D holding the composited frame
    int width = 0;                // texture size in device pixels
    int height = 0;
    double pts = -1.0;            // seconds, media time (-1 = unknown)
    quint64 serial = 0;           // monotonically increasing per player
    quint64 playerId = 0;         // IMediaPlayer::playerId() of the source
};

class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual void onVideoFrame(const VideoFrame &frame) = 0;
};

// The drawable surface a renderer occupies inside the UI.
class IMediaRenderer {
public:
    virtual ~IMediaRenderer() = default;

    // Owning widget to insert into a layout (never null once created).
    virtual QWidget *widget() = 0;
};

// A single-stream media player engine.
class IMediaPlayer : public QObject {
    Q_OBJECT
public:
    explicit IMediaPlayer(QObject *parent = nullptr)
        : QObject(parent), m_playerId(nextPlayerId()) {}
    ~IMediaPlayer() override = default;

    // Process-wide unique id. Analysis results coming back from a frame sink
    // are tagged with it, so one shared analyzer can route them to the right
    // tile without holding a pointer to the player.
    quint64 playerId() const { return m_playerId; }

    // The renderer owned by this player. The caller inserts widget() into its
    // layout; the player renders into it.
    virtual IMediaRenderer *renderer() = 0;

    // Open a source (file or network URL). Does not necessarily start decoding
    // until play(). idempotent: opening a new URL replaces the current source.
    virtual void open(const QUrl &url) = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    virtual void setMuted(bool muted) = 0;
    virtual void setVolume(int volume /*0..100*/) = 0;

    // Apply engine-neutral video settings. Safe to call before or during
    // playback; implementations store them and (re)apply to the live stream
    // where the backend allows, and always carry them into the next open().
    void setVideoSettings(const VideoSettings &settings) { m_settings = settings; applyVideoSettings(); }
    VideoSettings videoSettings() const { return m_settings; }

    // ---- Playback control (seek / speed / position) ----
    //
    // Live streams have no meaningful duration, so UI must branch on
    // supportsPlaybackControl() and treat duration() <= 0 as unknown. These are
    // non-pure with inert defaults so legacy backends keep compiling unchanged.
    virtual bool supportsPlaybackControl() const { return false; }

    // Seek to an absolute media position in seconds. Returns false when the
    // seek was not dispatched.
    virtual bool seek(double /*seconds*/) { return false; }

    // Playback rate (1.0 = normal). Values <= 0 are clamped away.
    virtual void setSpeed(double /*speed*/) {}
    virtual double speed() const { return 1.0; }

    // Media timeline, in seconds. position() == -1 when unknown.
    virtual double position() const { return -1.0; }
    virtual double duration() const { return -1.0; }

    // Capture the current video frame to an image file. Returns true if the
    // request was dispatched (some engines, e.g. mpv, write asynchronously).
    // Default implementation grabs the renderer widget; engines may override
    // with a native capture (better quality / no UI chrome).
    virtual bool grabSnapshot(const QString &filePath)
    {
        if (!filePath.isEmpty()) {
            if (IMediaRenderer *r = renderer()) {
                if (QWidget *w = r->widget()) {
                    return w->grab().save(filePath);
                }
            }
        }
        return false;
    }

    // ---- Frame tap ----
    // Install (or clear, with nullptr) the sink that receives every rendered
    // frame. Backends without a GPU frame path simply ignore it; check
    // supportsFrameTap() to branch in UI code. Pass a sink object owned by the
    // caller; it must outlive the player or be cleared before destruction.
    void setFrameSink(IVideoFrameSink *sink) { m_frameSink = sink; }
    IVideoFrameSink *frameSink() const { return m_frameSink; }

    // True when onVideoFrame() will actually be called for this backend.
    virtual bool supportsFrameTap() const { return false; }

    virtual MediaState state() const = 0;

    // Human-readable backend name for diagnostics ("QtAV", "libmpv", ...).
    virtual QString backendName() const = 0;

signals:
    void stateChanged(zp::MediaState state);
    void started();
    void paused();
    void stopped();
    void failed(const QString &message);

    // Playback timeline. positionChanged fires several times per second while
    // playing; durationChanged when the engine learns/knows the media length
    // (-1 while unknown). speedChanged after setSpeed() takes effect.
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void speedChanged(double speed);

protected:
    // Backends override to push m_settings into their engine. Called by
    // setVideoSettings(); default is a no-op.
    virtual void applyVideoSettings() {}

    VideoSettings m_settings;
    IVideoFrameSink *m_frameSink = nullptr;

private:
    static quint64 nextPlayerId()
    {
        static quint64 s_next = 0;
        return ++s_next;
    }
    quint64 m_playerId = 0;
};

} // namespace zp

Q_DECLARE_METATYPE(zp::MediaState)

#endif // ZP_MEDIA_PLAYER_H
