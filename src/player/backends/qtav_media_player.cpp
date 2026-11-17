#include "player/backends/qtav_media_player.h"

#include <QtAV/AVPlayer.h>
#include <QtAV/VideoOutput.h>
#include <QtAV/AVError.h>
#include <QtAV/AudioOutput.h>
#include <QtAV/SubtitleFilter.h>

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
    QtAV::SubtitleFilter *subFilter = nullptr;
};

QtavMediaPlayer::QtavMediaPlayer(QObject *parent)
    : IMediaPlayer(parent)
{
    m_renderer = new Renderer(this);

    m_holder = new AVPlayerHolder();
    m_holder->player = new QtAV::AVPlayer(this);
    m_holder->player->setRenderer(m_renderer->output());

    // External-subtitle filter: lives for the whole session, configured via
    // applyVideoSettings().
    m_holder->subFilter = new QtAV::SubtitleFilter(this);
    m_holder->player->installFilter(m_holder->subFilter);

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

// Map the engine-neutral settings onto QtAV setters. Picture adjustments are
// -100..100 (same scale as ours). Hardware decode is expressed as a decoder
// priority list; external subtitles go through the SubtitleFilter.
void QtavMediaPlayer::applyVideoSettings()
{
    if (!m_holder || !m_holder->player) {
        return;
    }
    QtAV::AVPlayer *p = m_holder->player;

    // --- Picture adjustments ---
    const FilterSettings &f = m_settings.filters;
    p->setBrightness(qBound(-100, f.brightness, 100));
    p->setContrast(qBound(-100, f.contrast, 100));
    p->setSaturation(qBound(-100, f.saturation, 100));
    // QtAV documents setHue() as not implemented; call anyway (no-op safe).

    // --- Hardware decoding via decoder priority ---
    // Names accepted: "FFmpeg", "CUDA", "DXVA", "D3D11", "VAAPI", "VDA",
    // "VideoToolbox", ...  Unknown/unsupported names are simply skipped.
    QStringList prio;
    switch (m_settings.hwdec) {
    case HwDecode::Disabled:      prio << QStringLiteral("FFmpeg"); break;
    case HwDecode::D3D11VA:       prio << QStringLiteral("D3D11") << QStringLiteral("FFmpeg"); break;
    case HwDecode::Cuda:          prio << QStringLiteral("CUDA") << QStringLiteral("FFmpeg"); break;
    case HwDecode::Dxva2:         prio << QStringLiteral("DXVA") << QStringLiteral("FFmpeg"); break;
    case HwDecode::VideoToolbox:  prio << QStringLiteral("VideoToolbox") << QStringLiteral("FFmpeg"); break;
    case HwDecode::Vaapi:         prio << QStringLiteral("VAAPI") << QStringLiteral("FFmpeg"); break;
    case HwDecode::Auto:
    default:
        prio << QStringLiteral("CUDA") << QStringLiteral("D3D11") << QStringLiteral("DXVA")
             << QStringLiteral("VAAPI") << QStringLiteral("FFmpeg");
        break;
    }
    p->setVideoDecoderPriority(prio);

    // --- Subtitles ---
    const SubtitleSettings &sub = m_settings.subtitle;
    if (m_holder->subFilter) {
        m_holder->subFilter->setEnabled(sub.enabled);
        m_holder->subFilter->setAutoLoad(sub.autoLoadInFolder);
        m_holder->subFilter->setFile(sub.externalFile);
    }
    if (sub.enabled && sub.track >= 0) {
        p->setSubtitleStream(sub.track);
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
