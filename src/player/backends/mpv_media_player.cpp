#include "player/backends/mpv_media_player.h"
#include "player/backends/mpv/qthelper.hpp"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLWidget>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QMetaObject>
#include <QByteArray>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstring>

// Desktop-GL / GLES3 framebuffer targets used by glBindFramebuffer when
// presenting the frame texture. Defined locally where the platform headers
// are the ES2 subset.
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

namespace zp {

// ---------------------------------------------------------------------------
// The QOpenGLWidget that hosts the mpv render context.
//
// mpv renders into a framebuffer object that WE own, whose color attachment is
// a texture we keep a handle to. That is what makes the frame tap possible (and
// later, zero-copy GPU interop): the texture id is ours, not QOpenGLWidget's
// private one. After mpv renders, the texture is blitted to the widget's
// default FBO for display.
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
        const int w = static_cast<int>(width() * devicePixelRatioF());
        const int h = static_cast<int>(height() * devicePixelRatioF());
        if (w <= 0 || h <= 0) {
            return;
        }
        ensureRenderTarget(w, h);

        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        mpv_opengl_fbo mpfbo{static_cast<int>(m_fbo), w, h, 0};
        int flip_y{1};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(m_gl, params);

        // mpv may leave another framebuffer bound after rendering; make sure
        // the frame FBO is the one both drawn into and read from below.
        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glViewport(0, 0, w, h);

        m_owner->noteRenderSize(w, h);

        // ---- Frame tap: the frame is complete and still in our texture. ----
        if (IVideoFrameSink *sink = m_owner->frameSink()) {
            VideoFrame frame;
            frame.textureId = m_tex;
            frame.width = w;
            frame.height = h;
            frame.pts = m_owner->currentTimePos();
            frame.serial = ++m_frameSerial;
            frame.playerId = m_owner->playerId();
            frame.videoRect = m_owner->videoRectNormalized(w, h);
            sink->onVideoFrame(frame);
        }

        // ---- Present: blit our texture onto the widget's default FBO. ----
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
        QOpenGLContext::currentContext()->extraFunctions()->glBlitFramebuffer(
            0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
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
            QOpenGLFunctions *f = context()->functions();
            if (m_fbo) {
                f->glDeleteFramebuffers(1, &m_fbo);
                m_fbo = 0;
            }
            if (m_tex) {
                f->glDeleteTextures(1, &m_tex);
                m_tex = 0;
            }
            mpv_render_context_free(m_gl);
            m_gl = nullptr;
            doneCurrent();
        }
    }

    ~Renderer() override { releaseGl(); }

    // GL texture that holds the most recent frame (0 before the first paint).
    unsigned int frameTexture() const { return m_tex; }

private:
    // (Re)allocate the FBO + texture when the widget size changes.
    void ensureRenderTarget(int w, int h)
    {
        if (m_fbo && m_tex && m_texW == w && m_texH == h) {
            return;
        }
        QOpenGLFunctions *f = context()->functions();
        if (!m_fbo) {
            f->glGenFramebuffers(1, &m_fbo);
        }
        if (!m_tex) {
            f->glGenTextures(1, &m_tex);
        }
        f->glBindTexture(GL_TEXTURE_2D, m_tex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                        GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->glBindTexture(GL_TEXTURE_2D, 0);

        f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                  m_tex, 0);
        f->glViewport(0, 0, w, h);
        f->glClearColor(0.f, 0.f, 0.f, 1.f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

        m_texW = w;
        m_texH = h;
    }

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
    unsigned int m_fbo = 0;       // our render target
    unsigned int m_tex = 0;       // its color attachment (frame texture)
    int m_texW = 0;
    int m_texH = 0;
    quint64 m_frameSerial = 0;
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
        mpv_set_option_string(m_holder->h, "keep-open", "yes");
        mpv_set_option_string(m_holder->h, "hwdec", "auto-safe");

        // --- Live latency budget ---
        // mpv's stock defaults (demuxer-readahead-secs=1 + cache-secs=auto)
        // add roughly a second on top of the server's own end-to-end delay,
        // which is most of the gap between ZMS (WebRTC ~0.06s / FLV ~0.45s)
        // and what the desktop client shows. 0.5s readahead keeps enough
        // margin to absorb jitter on a LAN/wired link while cutting the
        // steady-state delay roughly in half. Applies to playback too: the
        // recordings come off the same media server, so the same budget is a
        // reasonable compromise rather than a separate profile.
        // Bound the memory rather than the time so a high-bitrate stream
        // cannot balloon the cache while the readahead window is small.
        mpv_set_option_string(m_holder->h, "cache", "yes");
        mpv_set_option_string(m_holder->h, "demuxer-readahead-secs", "0.5");
        mpv_set_option_string(m_holder->h, "demuxer-max-bytes", "16MiB");
        mpv_set_option_string(m_holder->h, "demuxer-max-back-bytes", "8MiB");
        // Resume as soon as the readahead window is filled instead of waiting
        // for the (now tiny) cache to reach an internal target.
        mpv_set_option_string(m_holder->h, "cache-pause-initial", "no");

        mpv_initialize(m_holder->h);
        mpv_observe_property(m_holder->h, 0, "time-pos", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_holder->h, 0, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_holder->h, 0, "speed", MPV_FORMAT_DOUBLE);
        // With keep-open=yes the file pauses at the end instead of ending, so
        // watch eof-reached to report the real (paused) state to the UI.
        mpv_observe_property(m_holder->h, 0, "eof-reached", MPV_FORMAT_FLAG);
        // Picture placement inside the render target (letterbox bars).
        mpv_observe_property(m_holder->h, 0, "video-params/aspect", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_holder->h, 0, "osd-dimensions", MPV_FORMAT_NODE);
        mpv_set_wakeup_callback(m_holder->h, mpvWakeup, this);
        // Dev diagnostics: forward mpv's own log when tracing is on, so audio
        // filter / shader problems are visible in the ZP_LOG file.
        if (qEnvironmentVariableIsSet("ZP_AI_TRACE")) {
            mpv_request_log_messages(m_holder->h, "info");
        }
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
    // Order matters: mpv_render_context_free() talks to the mpv core, so the
    // render context has to go before the handle is terminated. The widget
    // owns that context, hence the renderer is destroyed first. (Freeing the
    // context after mpv_terminate_destroy() corrupts the heap and the process
    // dies with a fast-fail, which is exactly what happened when a tile was
    // replaced while playing.)
    delete m_renderer;
    m_renderer = nullptr;
    if (m_holder) {
        if (m_holder->h) {
            mpv_set_wakeup_callback(m_holder->h, nullptr, nullptr);
            mpv_terminate_destroy(m_holder->h);
        }
        delete m_holder;
    }
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
    if (mpv_command(m_holder->h, cmd) >= 0 && m_state == MediaState::Paused) {
        // Resuming is the common case here (a fresh file goes through
        // file-loaded -> PLAYBACK_RESTART instead). Report the transition so
        // the UI flips the play/pause icon immediately.
        setState(MediaState::Playing);
    }
}

void MpvMediaPlayer::pause()
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    const char *cmd[] = {"set", "pause", "yes", nullptr};
    if (mpv_command(m_holder->h, cmd) >= 0 && m_state == MediaState::Playing) {
        // Without this the engine keeps reporting "playing" and the tile's
        // play/pause button can never switch back (it would call pause again).
        setState(MediaState::Paused);
        emit paused();
    }
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

bool MpvMediaPlayer::muted() const
{
    if (!m_holder || !m_holder->h) {
        return false;
    }
    int flag = 0;
    if (mpv_get_property(m_holder->h, "mute", MPV_FORMAT_FLAG, &flag) < 0) {
        return false;
    }
    return flag != 0;
}

void MpvMediaPlayer::setVolume(int volume)
{
    if (m_holder && m_holder->h) {
        double v = qBound(0, volume, 100);
        mpv_set_property(m_holder->h, "volume", MPV_FORMAT_DOUBLE, &v);
    }
}

// Native snapshot: let mpv write the current decoded frame ("video" = no OSD
// or subtitles). Asynchronous — the file appears shortly after this returns.
bool MpvMediaPlayer::grabSnapshot(const QString &filePath){
    if (!m_holder || !m_holder->h || filePath.isEmpty()) {
        return false;
    }
    QString local = filePath;
    local.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QByteArray path = local.toUtf8();
    const char *cmd[] = {"screenshot-to-file", path.constData(), "video", nullptr};
    return mpv_command(m_holder->h, cmd) >= 0;
}

double MpvMediaPlayer::currentTimePos() const
{
    if (!m_holder || !m_holder->h) {
        return -1.0;
    }
    double pos = -1.0;
    if (mpv_get_property(m_holder->h, "time-pos", MPV_FORMAT_DOUBLE, &pos) < 0) {
        return -1.0;
    }
    return pos;
}

// ---------------------------------------------------------------------------
// Picture placement (letterbox detection)
// ---------------------------------------------------------------------------
QRectF MpvMediaPlayer::videoRectNormalized(int fboW, int fboH) const
{
    const QRectF full(0.0, 0.0, 1.0, 1.0);
    if (fboW <= 0 || fboH <= 0) {
        return full;
    }

    // Preferred: mpv's own idea of the OSD/video area in render-target pixels.
    // Guard against stale values from a previous size: anything larger than the
    // target means the rect predates the current FBO.
    if (!m_videoRectPx.isEmpty()) {
        const QRectF r(m_videoRectPx.x() / fboW, m_videoRectPx.y() / fboH,
                       m_videoRectPx.width() / fboW, m_videoRectPx.height() / fboH);
        if (r.right() <= 1.02 && r.bottom() <= 1.02 && r.width() > 0.02
            && r.height() > 0.02) {
            return r.intersected(full);
        }
    }

    // Fallback: letterbox from the display aspect ratio (panscan is off, so the
    // picture is centered and fully visible).
    if (m_aspect > 0.0) {
        const double boxAspect = double(fboW) / fboH;
        if (m_aspect >= boxAspect) {
            const double h = boxAspect / m_aspect;   // bars top/bottom
            return QRectF(0.0, (1.0 - h) * 0.5, 1.0, h);
        }
        const double w = m_aspect / boxAspect;       // bars left/right
        return QRectF((1.0 - w) * 0.5, 0.0, w, 1.0);
    }

    return full;
}

void MpvMediaPlayer::noteRenderSize(int fboW, int fboH)
{
    m_fboW = fboW;
    m_fboH = fboH;
    // A crop requested before the first frame could not be mapped to video
    // pixels yet; retry on every paint until it lands (cheap, and it stops
    // once applied).
    if (!m_crop.isEmpty() && !m_cropApplied) {
        applyCrop();
    }
}

// ---------------------------------------------------------------------------
// Zoom to ROI + GPU shaders
// ---------------------------------------------------------------------------
bool MpvMediaPlayer::setVideoCrop(const QRectF &rect)
{
    if (!m_holder || !m_holder->h) {
        return false;
    }
    m_crop = rect.normalized();
    m_cropApplied = false;
    if (m_crop.isEmpty()) {
        mpv_set_property_string(m_holder->h, "video-crop", "");
        return true;
    }
    return applyCrop();
}

// Maps the tile-normalized crop rect into video pixels and pushes it to mpv.
// Safe to call repeatedly; returns true once the property was set.
bool MpvMediaPlayer::applyCrop()
{
    if (!m_holder || !m_holder->h || m_crop.isEmpty()) {
        return false;
    }
    if (m_fboW <= 0 || m_fboH <= 0) {
        return false;   // no frame seen yet; noteRenderSize() retries
    }

    // Without any video parameters the placement is still unknown (the full
    // rect is only the placeholder), and cropping against it would pick the
    // wrong region. Wait for video-params; noteRenderSize() retries.
    if (m_videoRectPx.isEmpty() && m_aspect <= 0.0) {
        return false;
    }

    // Placement must be computed WITHOUT the crop applied: once cropping is
    // live, osd-dimensions describes the cropped picture, so re-deriving the
    // rect from it would feed the mapping its own output. The aspect ratio is
    // crop-independent, so the uncropped letterbox rect comes from aspect
    // alone; osd-dimensions is only a fallback for exotic cases (rotation).
    QRectF vr;
    if (m_aspect > 0.0) {
        const double boxAspect = double(m_fboW) / m_fboH;
        if (m_aspect >= boxAspect) {
            const double h = boxAspect / m_aspect;
            vr = QRectF(0.0, (1.0 - h) * 0.5, 1.0, h);
        } else {
            const double w = m_aspect / boxAspect;
            vr = QRectF((1.0 - w) * 0.5, 0.0, w, 1.0);
        }
    } else {
        vr = videoRectNormalized(m_fboW, m_fboH);
    }
    if (vr.width() <= 0.0 || vr.height() <= 0.0) {
        return false;
    }

    // The crop property is in source-video pixels, independent of window size.
    int64_t vw = 0, vh = 0;
    if (mpv_get_property(m_holder->h, "width", MPV_FORMAT_INT64, &vw) < 0
        || mpv_get_property(m_holder->h, "height", MPV_FORMAT_INT64, &vh) < 0
        || vw <= 0 || vh <= 0) {
        return false;
    }

    QRectF rel((m_crop.x() - vr.x()) / vr.width(),
               (m_crop.y() - vr.y()) / vr.height(),
               m_crop.width() / vr.width(),
               m_crop.height() / vr.height());
    rel = rel.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (rel.width() <= 0.01 || rel.height() <= 0.01) {
        return false;
    }
    const int x = qBound(0, int(rel.x() * vw), int(vw) - 16);
    const int y = qBound(0, int(rel.y() * vh), int(vh) - 16);
    const int w = qBound(16, int(rel.width() * vw), int(vw) - x);
    const int h = qBound(16, int(rel.height() * vh), int(vh) - y);
    const QByteArray spec = QStringLiteral("%1x%2+%3+%4").arg(w).arg(h).arg(x).arg(y)
                                .toUtf8();
    if (qEnvironmentVariableIsSet("ZP_AI_TRACE")) {
        qInfo().noquote() << "[crop] roi=" << m_crop << "videoRect=" << vr
                          << "video=" << vw << "x" << vh << "spec=" << spec;
    }
    if (mpv_set_property_string(m_holder->h, "video-crop", spec.constData()) < 0) {
        return false;
    }
    m_cropApplied = true;
    return true;
}

void MpvMediaPlayer::setShader(const QString &filePath)
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    mpv_set_property_string(m_holder->h, "glsl-shaders", filePath.toUtf8().constData());
}

// ---------------------------------------------------------------------------
// Voice denoising (rnnoise through mpv's lavfi arnndn filter)
// ---------------------------------------------------------------------------
namespace {

// Model lookup: ZP_RNNOISE_MODEL, then models/rnnoise next to the app or in
// the source tree (bd = balanced quality model from the arnndn-model set).
QString findDenoiseModel()
{
    const QString env = qEnvironmentVariable("ZP_RNNOISE_MODEL");
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return env;
    }
    QStringList dirs;
    const QString app = QCoreApplication::applicationDirPath();
    if (!app.isEmpty()) {
        dirs << app + QStringLiteral("/models/rnnoise")
             << app + QStringLiteral("/../3rdpart/models/rnnoise")
             << app + QStringLiteral("/../../3rdpart/models/rnnoise")
             << app + QStringLiteral("/3rdpart/models/rnnoise");
    }
    dirs << QStringLiteral("3rdpart/models/rnnoise");
    for (const QString &dir : dirs) {
        for (const char *name : {"bd.rnnn", "std.rnnn", "cb.rnnn", "mp.rnnn"}) {
            const QString path = QDir(dir).filePath(QString::fromLatin1(name));
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return QString();
}

} // namespace

bool MpvMediaPlayer::setDenoise(bool on)
{
    if (!m_holder || !m_holder->h) {
        return false;
    }
    if (!on) {
        mpv_set_property_string(m_holder->h, "af", "");
        m_denoise = false;
        return true;
    }

    const QString model = findDenoiseModel();
    if (model.isEmpty()) {
        return false;
    }
    QString p = model;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // avfilter's graph parser needs ':' escaped, and the single quotes keep the
    // value together: arnndn=m='F\:/path/model.rnnn' (verified with ffmpeg).
    p.replace(QLatin1String(":"), QLatin1String("\\:"));
    const QString spec = QStringLiteral("lavfi=[arnndn=m='%1']").arg(p);
    if (mpv_set_property_string(m_holder->h, "af", spec.toUtf8().constData()) < 0) {
        return false;
    }
    if (denoiseEnabled()) {
        m_denoise = true;
        if (qEnvironmentVariableIsSet("ZP_AI_TRACE")) {
            qInfo().noquote() << "[audio] denoise on:" << spec;
        }
        return true;
    }
    mpv_set_property_string(m_holder->h, "af", "");
    if (qEnvironmentVariableIsSet("ZP_AI_TRACE")) {
        qInfo().noquote() << "[audio] denoise rejected for model" << model;
    }
    return false;
}

bool MpvMediaPlayer::denoiseEnabled() const
{
    if (!m_holder || !m_holder->h) {
        return false;
    }
    char *af = mpv_get_property_string(m_holder->h, "af");
    if (!af) {
        return false;
    }
    const bool on = QByteArray(af).contains("arnndn");
    mpv_free(af);
    return on;
}

// ---------------------------------------------------------------------------
// Playback control (seek / speed / timeline)
// ---------------------------------------------------------------------------
bool MpvMediaPlayer::seek(double seconds)
{
    if (!m_holder || !m_holder->h || seconds < 0.0) {
        return false;
    }
    mpv_handle *h = m_holder->h;
    // With keep-open the file pauses at EOF. Dragging the playhead (typically
    // back to the start) should resume playback rather than leave it parked.
    int eof = 0;
    const bool wasAtEof =
        mpv_get_property(h, "eof-reached", MPV_FORMAT_FLAG, &eof) >= 0 && eof != 0;

    // Writing time-pos performs an exact (keyframe-accurate) seek, which is
    // what evidence / fast-forward needs (relative seeks would drift).
    double pos = seconds;
    const bool ok = mpv_set_property(h, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0;
    if (ok && wasAtEof) {
        const char *cmd[] = {"set", "pause", "no", nullptr};
        mpv_command(h, cmd);
        setState(MediaState::Playing);
    }
    return ok;
}

void MpvMediaPlayer::setSpeed(double speed)
{
    if (!m_holder || !m_holder->h) {
        return;
    }
    double v = qBound(0.01, speed, 100.0);
    mpv_set_property(m_holder->h, "speed", MPV_FORMAT_DOUBLE, &v);
}

double MpvMediaPlayer::speed() const
{
    if (!m_holder || !m_holder->h) {
        return 1.0;
    }
    double v = 1.0;
    if (mpv_get_property(m_holder->h, "speed", MPV_FORMAT_DOUBLE, &v) < 0) {
        return 1.0;
    }
    return v;
}

double MpvMediaPlayer::position() const
{
    return currentTimePos();
}

double MpvMediaPlayer::duration() const
{
    if (!m_holder || !m_holder->h) {
        return -1.0;
    }
    double d = -1.0;
    if (mpv_get_property(m_holder->h, "duration", MPV_FORMAT_DOUBLE, &d) < 0) {
        return -1.0;
    }
    return d;
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
        // Properties from the previous file are stale; they will be observed
        // again once the new video parameters are known.
        m_videoRectPx = QRectF();
        m_aspect = 0.0;
        break;
    case MPV_EVENT_FILE_LOADED:
        setState(MediaState::Playing);
        // A freshly loaded file needs the settings re-applied: hwdec/vf are
        // read at load time, and an external subtitle must be re-added.
        applyVideoSettings();
        emit started();
        emit durationChanged(duration());
        emit speedChanged(speed());
        // Video size and placement are new; re-derive the crop against them.
        if (!m_crop.isEmpty()) {
            m_cropApplied = false;
            applyCrop();
        }
        break;
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *pc = static_cast<mpv_event_property *>(event->data);
        if (!pc || !pc->name || !pc->data) {
            break;
        }
        const QByteArray name(pc->name);
        if (pc->format == MPV_FORMAT_DOUBLE) {
            const double value = *static_cast<double *>(pc->data);
            if (name == QByteArrayLiteral("time-pos")) {
                emit positionChanged(value);
            } else if (name == QByteArrayLiteral("duration")) {
                emit durationChanged(value);
            } else if (name == QByteArrayLiteral("speed")) {
                emit speedChanged(value);
            } else if (name == QByteArrayLiteral("video-params/aspect")) {
                m_aspect = value;
            }
        } else if (pc->format == MPV_FORMAT_FLAG
                   && name == QByteArrayLiteral("eof-reached")) {
            // keep-open pauses at the end; reflect that so the play/pause icon
            // flips to "play" and the user can restart by seeking back.
            if (*static_cast<int *>(pc->data) != 0 && m_state == MediaState::Playing) {
                setState(MediaState::Paused);
                emit paused();
            }
        } else if (pc->format == MPV_FORMAT_NODE
                   && name == QByteArrayLiteral("osd-dimensions")) {
            // Map of x0/y0/x1/y1 in render-target pixels: the picture area,
            // i.e. what is left after letterbox/pillarbox bars.
            const mpv_node *node = static_cast<const mpv_node *>(pc->data);
            if (!node || node->format != MPV_FORMAT_NODE_MAP) {
                break;
            }
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            bool hasX1 = false, hasY1 = false;
            for (int i = 0; i < node->u.list->num; ++i) {
                const char *key = node->u.list->keys[i];
                const mpv_node &val = node->u.list->values[i];
                if (!key || val.format != MPV_FORMAT_INT64) {
                    continue;
                }
                const QByteArray k(key);
                const double v = double(val.u.int64);
                if (k == QByteArrayLiteral("x0")) { x0 = v; }
                else if (k == QByteArrayLiteral("y0")) { y0 = v; }
                else if (k == QByteArrayLiteral("x1")) { x1 = v; hasX1 = true; }
                else if (k == QByteArrayLiteral("y1")) { y1 = v; hasY1 = true; }
            }
            if (hasX1 && hasY1 && x1 > x0 && y1 > y0) {
                m_videoRectPx = QRectF(x0, y0, x1 - x0, y1 - y0);
            }
        }
        break;
    }
    case MPV_EVENT_PLAYBACK_RESTART:
        if (m_state == MediaState::Opening) {
            setState(MediaState::Playing);
            emit started();
        }
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        auto *lm = static_cast<mpv_event_log_message *>(event->data);
        if (lm && lm->text) {
            qInfo().noquote() << "[mpv]" << lm->level << QString::fromUtf8(lm->prefix ? lm->prefix : "")
                              << QString::fromUtf8(lm->text).trimmed();
        }
        break;
    }
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
