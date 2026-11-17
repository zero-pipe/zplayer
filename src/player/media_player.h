#ifndef ZP_MEDIA_PLAYER_H
#define ZP_MEDIA_PLAYER_H

#include <QObject>
#include <QUrl>
#include <QWidget>

namespace zp {

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

    virtual MediaState state() const = 0;

    // Human-readable backend name for diagnostics ("QtAV", "libmpv", ...).
    virtual QString backendName() const = 0;

signals:
    void stateChanged(zp::MediaState state);
    void started();
    void paused();
    void stopped();
    void failed(const QString &message);
};

} // namespace zp

Q_DECLARE_METATYPE(zp::MediaState)

#endif // ZP_MEDIA_PLAYER_H
