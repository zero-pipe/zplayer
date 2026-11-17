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
    explicit IMediaPlayer(QObject *parent = nullptr) : QObject(parent) {}
    ~IMediaPlayer() override = default;

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

    virtual MediaState state() const = 0;

    // Human-readable backend name for diagnostics ("QtAV", "libmpv", ...).
    virtual QString backendName() const = 0;

signals:
    void stateChanged(zp::MediaState state);
    void started();
    void paused();
    void stopped();
    void failed(const QString &message);

protected:
    // Backends override to push m_settings into their engine. Called by
    // setVideoSettings(); default is a no-op.
    virtual void applyVideoSettings() {}

    VideoSettings m_settings;
};

} // namespace zp

Q_DECLARE_METATYPE(zp::MediaState)

#endif // ZP_MEDIA_PLAYER_H
