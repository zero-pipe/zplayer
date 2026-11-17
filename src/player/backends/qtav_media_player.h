#ifndef ZP_QTAV_MEDIA_PLAYER_H
#define ZP_QTAV_MEDIA_PLAYER_H

#include "player/media_player.h"

namespace zp {

// IMediaPlayer implementation backed by QtAV (AVPlayer + VideoOutput).
// This is the ONLY translation unit that includes QtAV headers.
class QtavMediaPlayer : public IMediaPlayer {
    Q_OBJECT
public:
    explicit QtavMediaPlayer(QObject *parent = nullptr);
    ~QtavMediaPlayer() override;

    IMediaRenderer *renderer() override;
    void open(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void setMuted(bool muted) override;
    void setVolume(int volume) override;
    MediaState state() const override { return m_state; }
    QString backendName() const override { return QStringLiteral("QtAV"); }

protected:
    void applyVideoSettings() override;

private:
    class Renderer;
    struct AVPlayerHolder;

    void setState(MediaState s);

    Renderer *m_renderer = nullptr;
    AVPlayerHolder *m_holder = nullptr;   // hides QtAV types from the header
    MediaState m_state = MediaState::Idle;
};

} // namespace zp

#endif // ZP_QTAV_MEDIA_PLAYER_H
