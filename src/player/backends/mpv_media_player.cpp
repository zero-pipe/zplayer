#include "player/backends/mpv_media_player.h"
#include "player/backends/mpv/qthelper.hpp"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QMetaObject>
#include <QByteArray>
#include <QTimer>

#include <cstring>

namespace zp {

// ---------------------------------------------------------------------------
// The QOpenGLWidget that hosts the mpv render context.
// ---------------------------------------------------------------------------
class MpvMediaPlayer::Renderer : public QOpenGLWidget, public IMediaRenderer {
public:
    explicit Renderer(MpvMediaPlayer *owner, QWidget *parent = nullptr)
        : QOpenGLWidget(parent), m_owner(owner)
    {
        setObjectName(QStringLiteral("TileVideo"));
        setMinimumSize(80, 45);
    }

    QWidget *widget() override { return this; }

    void initializeGL() override
    {
        mpv_opengl_init_params gl_init_params[1] = { &Renderer::getProcAddress, nullptr };
        mpv_render_param params[]{
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        if (mpv_render_context_create(&m_gl, static_cast<mpv_handle *>(m_owner->mpvHandle()),
                                      params) < 0) {
            return;
        }
        mpv_render_context_set_update_callback(
            m_gl, &Renderer::onMpvRenderUpdate, reinterpret_cast<void *>(this));
        m_owner->onRenderReady();
    }

    void paintGL() override
    {
        if (!m_gl) {
            return;
        }
        mpv_opengl_fbo mpfbo{
            static_cast<int>(defaultFramebufferObject()),
            static_cast<int>(width() * devicePixelRatioF()),
            static_cast<int>(height() * devicePixelRatioF()), 0};
        int flip_y{1};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(m_gl, params);
    }

    void maybeUpdate()
    {
        if (window() && window()->isMinimized()) {
            makeCurrent();
            paintGL();
            context()->swapBuffers(context()->surface());
            doneCurrent();
        } else {
            update();
        }
    }

    void releaseGl()
    {
        if (m_gl) {
            makeCurrent();
            mpv_render_context_free(m_gl);
            m_gl = nullptr;
            doneCurrent();
        }
    }

    ~Renderer() override { releaseGl(); }

private:
    static void *getProcAddress(void *, const char *name)
    {
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) {
            return nullptr;
        }
        return reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name)));
    }

    static void onMpvRenderUpdate(void *ctx)
    {
        Renderer *self = static_cast<Renderer *>(ctx);
        QMetaObject::invokeMethod(self, [self]() { self->maybeUpdate(); },
                                  Qt::QueuedConnection);
    }

    MpvMediaPlayer *m_owner = nullptr;
    mpv_render_context *m_gl = nullptr;
};

// ---------------------------------------------------------------------------
// Holds the mpv_handle (hidden from the public header).
// ---------------------------------------------------------------------------
struct MpvMediaPlayer::MpvHolder {
    mpv_handle *h = nullptr;
};

// mpv needs a C callback; forward to the owning object.
namespace {
void mpvWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvMediaPlayer *>(ctx), "onMpvEvents",
                              Qt::QueuedConnection);
}
} // namespace

void *MpvMediaPlayer::mpvHandle() const
{
    return m_holder ? m_holder->h : nullptr;
}

MpvMediaPlayer::MpvMediaPlayer(QObject *parent)
    : IMediaPlayer(parent)
{
    m_holder = new MpvHolder();
    m_holder->h = mpv_create();
    if (m_holder->h) {
        mpv_set_option_string(m_holder->h, "vo", "libmpv");
        mpv_set_option_string(m_holder->h, "terminal", "no");
        mpv_set_option_string(m_holder->h, "keep-open", "no");
        mpv_set_option_string(m_holder->h, "hwdec", "auto-safe");
        mpv_initialize(m_holder->h);
        mpv_observe_property(m_holder->h, 0, "time-pos", MPV_FORMAT_DOUBLE);
        mpv_set_wakeup_callback(m_holder->h, mpvWakeup, this);
        applyVideoSettings();  // push defaults before the first loadfile
    }

    m_renderer = new Renderer(this);
    // The renderer is a widget; it is reparented into the tile at attach time,
    // so the tile's widget tree may destroy it before the player does. Track
    // that via destroyed() to avoid a dangling pointer.
    connect(m_renderer, &QObject::destroyed, this, [this]() { m_renderer = nullptr; });
    m_renderer->hide();
}

MpvMediaPlayer::~MpvMediaPlayer()
{
    // The renderer (if still alive) owns its GL context and frees it in its
    // destructor. If the tile already destroyed it, m_renderer is null here.
    if (m_holder) {
        if (m_holder->h) {
            mpv_set_wakeup_callback(m_holder->h, nullptr, nullptr);
            mpv_terminate_destroy(m_holder->h);
        }
        delete m_holder;
    }
    delete m_renderer;
}

IMediaRenderer *MpvMediaPlayer::renderer()
{
    return m_renderer;
}

void MpvMediaPlayer::open(const QUrl &url)
{
    if (!m_holder || !m_holder->h) {
        setState(MediaState::Failed);
        emit failed(tr("mpv 未初始化"));
        return;
    }
    if (!url.isValid()) {
        setState(MediaState::Failed);
        emit failed(tr("地址无效"));
        return;
    }
    setState(MediaState::Opening);
    m_pendingUrl = url.toString();
    if (m_renderReady) {
        loadPending();
    }
    // Otherwise the Renderer's initializeGL will call onRenderReady() ->
    // loadPending() once the mpv render context exists.
}

void MpvMediaPlayer::onRenderReady()
{
    m_renderReady = true;
    if (!m_pendingUrl.isEmpty()) {
        loadPending();
    }
}

void MpvMediaPlayer::loadPending()
{
    if (!m_holder || !m_holder->h || m_pendingUrl.isEmpty()) {
        return;
    }
    const QString s = m_pendingUrl;
    m_pendingUrl.clear();
    QByteArray path = s.toUtf8();
    const char *cmd[] = {"loadfile", path.constData(), nullptr};
    mpv_command(m_holder->h, cmd);
}

void MpvMediaPlayer::play()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    const char *cmd[] = {"set", "pause", "no", nullptr};
    mpv_command(m_holder->h, cmd);
}

void MpvMediaPlayer::pause()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    const char *cmd[] = {"set", "pause", "yes", nullptr};
    mpv_command(m_holder->h, cmd);
}

void MpvMediaPlayer::stop()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    const char *cmd[] = {"stop", nullptr};
    mpv_command(m_holder->h, cmd);
}

void MpvMediaPlayer::setMuted(bool muted)
{
    if (m_holder && m_holder->h) {
        mpv_set_property_string(m_holder->h, "mute", muted ? "yes" : "no");
    }
}

void MpvMediaPlayer::setVolume(int volume)
{
    if (m_holder && m_holder->h) {
        double v = qBound(0, volume, 100);
        mpv_set_property(m_holder->h, "volume", MPV_FORMAT_DOUBLE, &v);
    }
}

// Map the engine-neutral settings onto libmpv properties. Most of these are
// live properties, so they take effect on the current stream immediately and
// persist into the next loadfile().
void MpvMediaPlayer::applyVideoSettings()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    mpv_handle *h = m_holder->h;

    // --- Hardware decoding ---
    const char *hw = "auto-safe";
    switch (m_settings.hwdec) {
    case HwDecode::Disabled:      hw = "no"; break;
    case HwDecode::D3D11VA:       hw = "d3d11va"; break;
    case HwDecode::Cuda:          hw = "cuda"; break;
    case HwDecode::Dxva2:         hw = "dxva2"; break;
    case HwDecode::VideoToolbox:  hw = "videotoolbox"; break;
    case HwDecode::Vaapi:         hw = "vaapi"; break;
    case HwDecode::Auto:
    default:                      hw = "auto-safe"; break;
    }
    mpv_set_property_string(h, "hwdec", hw);

    // --- Picture adjustments (mpv equalizer properties, -100..100) ---
    const FilterSettings &f = m_settings.filters;
    double b = qBound(-100, f.brightness, 100);
    double c = qBound(-100, f.contrast, 100);
    double s = qBound(-100, f.saturation, 100);
    double hu = qBound(-100, f.hue, 100);
    mpv_set_property(h, "brightness", MPV_FORMAT_DOUBLE, &b);
    mpv_set_property(h, "contrast", MPV_FORMAT_DOUBLE, &c);
    mpv_set_property(h, "saturation", MPV_FORMAT_DOUBLE, &s);
    mpv_set_property(h, "hue", MPV_FORMAT_DOUBLE, &hu);

    // --- Raw filter chain ("" clears it) ---
    mpv_set_property_string(h, "vf", f.rawChain.join(QLatin1Char(',')).toUtf8().constData());

    // --- Deinterlace ---
    mpv_set_property_string(h, "deinterlace", m_settings.deinterlace ? "yes" : "no");

    // --- Subtitles ---
    const SubtitleSettings &sub = m_settings.subtitle;
    mpv_set_property_string(h, "sub-visibility", sub.enabled ? "yes" : "no");
    mpv_set_property_string(h, "sub-auto",
                            sub.autoLoadInFolder ? "fuzzy" : "no");
    if (!sub.fontFamily.isEmpty()) {
        mpv_set_property_string(h, "sub-font", sub.fontFamily.toUtf8().constData());
    }
    // Track selection: -1 = auto, otherwise a specific sid.
    if (!sub.enabled) {
        mpv_set_property_string(h, "sid", "no");
    } else if (sub.track >= 0) {
        int64_t sid = sub.track;
        mpv_set_property(h, "sid", MPV_FORMAT_INT64, &sid);
    } else {
        mpv_set_property_string(h, "sid", "auto");
    }

    // External subtitle file: added once the file is loaded (tracked there too).
    applyExternalSubtitle();
}

void MpvMediaPlayer::applyExternalSubtitle()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    const QString file = m_settings.subtitle.externalFile;
    if (file.isEmpty() || m_state == MediaState::Idle || m_state == MediaState::Stopped) {
        return;
    }
    const char *cmd[] = {"sub-add", file.toUtf8().constData(), "select", nullptr};
    mpv_command(m_holder->h, cmd);
}

void MpvMediaPlayer::onMpvEvents()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    while (true) {
        mpv_event *event = mpv_wait_event(m_holder->h, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handleEvent(event);
    }
}

void MpvMediaPlayer::handleEvent(void *ev)
{
    mpv_event *event = static_cast<mpv_event *>(ev);
    switch (event->event_id) {
    case MPV_EVENT_START_FILE:
        setState(MediaState::Opening);
        break;
    case MPV_EVENT_FILE_LOADED:
        setState(MediaState::Playing);
        // A freshly loaded file needs the settings re-applied: hwdec/vf are
        // read at load time, and an external subtitle must be re-added.
        applyVideoSettings();
        emit started();
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        if (m_state == MediaState::Opening) {
            setState(MediaState::Playing);
            emit started();
        }
        break;
    case MPV_EVENT_END_FILE: {
        auto *ef = static_cast<mpv_event_end_file *>(event->data);
        if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
            setState(MediaState::Failed);
            emit failed(QString::fromLatin1(mpv_error_string(ef->error)));
        } else {
            setState(MediaState::Stopped);
            emit stopped();
        }
        break;
    }
    default:
        break;
    }
}

void MpvMediaPlayer::setState(MediaState s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
}

} // namespace zp
