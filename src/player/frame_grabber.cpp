#include "player/frame_grabber.h"

#include <QDir>
#include <QSet>
#include <QtGlobal>

#include <memory>

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

namespace zp {

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

namespace {

// Scratch FBO used to downscale a frame on the GPU before the readback. One
// per GL context; kept alive across calls because analysis runs continuously.
struct ScaleTarget {
    QOpenGLContext *ctx = nullptr;
    GLuint fbo = 0;
    GLuint tex = 0;
    int w = 0;
    int h = 0;
};
ScaleTarget g_scale;

// Makes sure g_scale holds an RGBA8 target of w x h for the current context.
// Leaves the caller's framebuffer binding untouched.
bool ensureScaleTarget(QOpenGLContext *ctx, int w, int h)
{
    QOpenGLFunctions *f = ctx->functions();
    if (g_scale.ctx != ctx) {
        // Context was replaced (widget re-created): the old GL objects died with
        // it, so just forget them rather than deleting against the wrong context.
        g_scale = ScaleTarget();
        g_scale.ctx = ctx;
    }
    if (g_scale.fbo && g_scale.tex && g_scale.w == w && g_scale.h == h) {
        return true;
    }

    GLint prevFbo = 0;
    ctx->extraFunctions()->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    if (!g_scale.fbo) {
        f->glGenFramebuffers(1, &g_scale.fbo);
    }
    if (!g_scale.tex) {
        f->glGenTextures(1, &g_scale.tex);
    }
    f->glBindTexture(GL_TEXTURE_2D, g_scale.tex);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glBindTexture(GL_TEXTURE_2D, 0);

    f->glBindFramebuffer(GL_FRAMEBUFFER, g_scale.fbo);
    f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                              g_scale.tex, 0);
    const bool complete =
        f->glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    f->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    g_scale.w = w;
    g_scale.h = h;
    return complete;
}

} // namespace

QImage grabFrameToImage(const VideoFrame &frame, int maxWidth)
{
    if (!frame.textureId || frame.width <= 0 || frame.height <= 0) {
        return QImage();
    }
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return QImage();
    }
    QOpenGLFunctions *f = ctx->functions();
    QOpenGLExtraFunctions *ef = ctx->extraFunctions();

    GLint srcFbo = 0;
    ef->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &srcFbo);

    int outW = frame.width;
    int outH = frame.height;
    bool scaled = false;
    if (maxWidth > 0 && frame.width > maxWidth
        && ensureScaleTarget(ctx, maxWidth,
                             qMax(1, int(frame.height * (double(maxWidth) / frame.width))))) {
        outW = g_scale.w;
        outH = g_scale.h;
        ef->glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(srcFbo));
        ef->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scale.fbo);
        ef->glBlitFramebuffer(0, 0, frame.width, frame.height, 0, 0, outW, outH,
                              GL_COLOR_BUFFER_BIT, GL_LINEAR);
        ef->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(srcFbo));
        scaled = true;
    }

    QImage img(outW, outH, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return QImage();
    }

    if (scaled) {
        ef->glBindFramebuffer(GL_FRAMEBUFFER, g_scale.fbo);
    }
    ef->glReadBuffer(GL_COLOR_ATTACHMENT0);
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // The tap runs while the frame's FBO is still bound, so the current read
    // framebuffer already holds the picture — the texture id is only a
    // validity check here.
    f->glReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    if (scaled) {
        ef->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(srcFbo));
    }
    // GL rows run bottom-up.
    return img.mirrored(false, true);
}

namespace {

class DebugFrameTap : public IVideoFrameSink {
public:
    explicit DebugFrameTap(const QString &dir) : m_dir(dir) {}

    void onVideoFrame(const VideoFrame &frame) override
    {
        if (m_saved.contains(frame.playerId)) {
            return;
        }
        // Skip the black frames before the decoder produces real content: wait
        // for a second of playback, or a few frames when pts is unknown (live).
        if (frame.pts >= 0.0 ? frame.pts < 1.0 : frame.serial < 60) {
            return;
        }
        const QImage img = grabFrameToImage(frame);
        if (img.isNull()) {
            return;
        }
        m_saved.insert(frame.playerId);
        img.save(QStringLiteral("%1/tap_p%2_s%3.png")
                     .arg(m_dir)
                     .arg(frame.playerId)
                     .arg(frame.serial),
                 "PNG");
    }

private:
    QString m_dir;
    QSet<quint64> m_saved;
};

} // namespace

IVideoFrameSink *debugFrameTap()
{
    static std::unique_ptr<DebugFrameTap> s_tap;
    const QString dir = qEnvironmentVariable("ZP_TAP");
    if (dir.isEmpty()) {
        return nullptr;
    }
    if (!s_tap) {
        QDir().mkpath(dir);
        s_tap.reset(new DebugFrameTap(dir));
    }
    return s_tap.get();
}

} // namespace zp
