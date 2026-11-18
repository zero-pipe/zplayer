#include "player/frame_grabber.h"

#include <QDir>
#include <QSet>
#include <QtGlobal>

#include <memory>

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>

namespace zp {

QImage grabFrameToImage(const VideoFrame &frame)
{
    if (!frame.textureId || frame.width <= 0 || frame.height <= 0) {
        return QImage();
    }
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return QImage();
    }
    QImage img(frame.width, frame.height, QImage::Format_RGBA8888);
    if (img.isNull()) {
        return QImage();
    }
    QOpenGLFunctions *f = ctx->functions();
    if (QOpenGLExtraFunctions *ef = ctx->extraFunctions()) {
        ef->glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    f->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // The tap runs while the frame's FBO is still bound, so the current read
    // framebuffer already holds the picture — hence the texture id is only
    // used as a validity check here.
    f->glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                    img.bits());
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
