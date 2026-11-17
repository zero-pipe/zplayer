#include "player/qtav_media_player.h"

#include <QtAV/AVPlayer.h>
#include <QtAV/VideoOutput.h>
#include <QtAV/AVError.h>
#include <QtAV/AudioOutput.h>

namespace zp {

// ---------------------------------------------------------------------------
// Renderer: owns the QtAV VideoOutput and exposes its widget.
// ---------------------------------------------------------------------------
class QtavMediaPlayer::Renderer : public IMediaRenderer {
public:
    explicit Renderer(QObject *owner)
    {
        m_output = new QtAV::VideoOutput(owner);
        m_widget = m_output->widget();
        if (m_widget) {
            m_widget->setObjectName(QStringLiteral("TileVideo"));
            m_widget->setMinimumSize(80, 45);
        }
    }

    QWidget *widget() override { return m_widget; }
    QtAV::VideoOutput *output() const { return m_output; }

private:
    QtAV::VideoOutput *m_output = nullptr;
    QWidget *m_widget = nullptr;
};

// ---------------------------------------------------------------------------
// Holder: a plain struct hiding QtAV types from the public header.
// ---------------------------------------------------------------------------
struct QtavMediaPlayer::AVPlayerHolder {
    QtAV::AVPlayer *player = nullptr;
};

QtavMediaPlayer::QtavMediaPlayer(QObject *parent)
    : IMediaPlayer(parent)
{
    m_renderer = new Renderer(this);

    m_holder = new AVPlayerHolder();
    m_holder->player = new QtAV::AVPlayer(this);
    m_holder->player->setRenderer(m_renderer->output());

    QtAV::AVPlayer *p = m_holder->player;
    connect(p, &QtAV::AVPlayer::started, this, [this]() {
        setState(MediaState::Playing);
        emit started();
    });
    connect(p, &QtAV::AVPlayer::stopped, this, [this]() {
        if (m_state == MediaState::Failed) {
            return;
        }
        setState(MediaState::Stopped);
        emit stopped();
    });
    connect(p, &QtAV::AVPlayer::paused, this, [this]() {
        setState(MediaState::Paused);
        emit paused();
    });
    connect(p, &QtAV::AVPlayer::error, this, [this](const QtAV::AVError &e) {
        setState(MediaState::Failed);
        emit failed(e.string());
    });
}

QtavMediaPlayer::~QtavMediaPlayer()
{
    if (m_holder) {
        if (m_holder->player) {
            m_holder->player->stop();
        }
        delete m_holder;
    }
}

IMediaRenderer *QtavMediaPlayer::renderer()
{
    return m_renderer;
}

void QtavMediaPlayer::open(const QUrl &url)
{
    if (!url.isValid()) {
        setState(MediaState::Failed);
        emit failed(tr("地址无效"));
        return;
    }
    setState(MediaState::Opening);
    m_holder->player->setFile(url.toString());
}

void QtavMediaPlayer::play()
{
    if (m_holder && m_holder->player) {
        m_holder->player->play();
    }
}

void QtavMediaPlayer::pause()
{
    if (m_holder && m_holder->player) {
        m_holder->player->pause(true);
    }
}

void QtavMediaPlayer::stop()
{
    if (m_holder && m_holder->player) {
        m_holder->player->stop();
    }
}

void QtavMediaPlayer::setMuted(bool muted)
{
    if (m_holder && m_holder->player && m_holder->player->audio()) {
        m_holder->player->audio()->setMute(muted);
    }
}

void QtavMediaPlayer::setVolume(int volume)
{
    if (m_holder && m_holder->player && m_holder->player->audio()) {
        m_holder->player->audio()->setVolume(qBound(0, volume, 100) / 100.0);
    }
}

void QtavMediaPlayer::setState(MediaState s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
}

} // namespace zp
