#ifndef ZP_MPV_MEDIA_PLAYER_H
#define ZP_MPV_MEDIA_PLAYER_H

#include "player/media_player.h"

#include <QPointer>

namespace zp {

// IMediaPlayer implementation backed by libmpv (render API over OpenGL).
// This is the primary engine; QtAV is kept only as a legacy fallback.
class MpvMediaPlayer : public IMediaPlayer {
    Q_OBJECT
public:
    explicit MpvMediaPlayer(QObject *parent = nullptr);
    ~MpvMediaPlayer() override;

    IMediaRenderer *renderer() override;
    void open(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void setMuted(bool muted) override;
    void setVolume(int volume) override;
    bool grabSnapshot(const QString &filePath) override;
    MediaState state() const override { return m_state; }
    QString backendName() const override { return QStringLiteral("libmpv"); }

    // libmpv renders into an FBO/texture we own, so every frame can be tapped.
    bool supportsFrameTap() const override { return true; }

    // Playback control: exact seek, rate, and timeline reporting.
    bool supportsPlaybackControl() const override { return true; }
    bool seek(double seconds) override;
    void setSpeed(double speed) override;
    double speed() const override;
    double position() const override;
    double duration() const override;

    // Zoom to a tile-normalized rect (ROI inspection) and GPU sharpening.
    bool supportsVideoCrop() const override { return true; }
    bool setVideoCrop(const QRectF &rect) override;
    bool supportsShader() const override { return true; }
    void setShader(const QString &filePath) override;

    // Playback position in seconds, or -1 when unknown. Read per-frame by the
    // renderer to stamp VideoFrame::pts; cheap (an mpv property get).
    double currentTimePos() const;

    // Where the picture (minus letterbox bars) sits inside a render target of
    // the given pixel size, normalized 0..1 with the origin at the top-left.
    // Derived from mpv's `osd-dimensions` (authoritative, accounts for
    // rotation and aspect) with a `video-params/aspect` fallback.
    QRectF videoRectNormalized(int fboW, int fboH) const;

    // The renderer reports its current target size so crop mapping can convert
    // a tile-normalized rect into video pixels.
    void noteRenderSize(int fboW, int fboH);

protected:
    void applyVideoSettings() override;

private slots:
    void onMpvEvents();

private:
    class Renderer;
    struct MpvHolder;
    void setState(MediaState s);
    void handleEvent(void *event);
    bool applyCrop();            // push m_crop to mpv (retries until possible)
    void loadPending();          // loadfile once the render context is ready
    void onRenderReady();        // called by the Renderer after GL init
    void applyExternalSubtitle(); // sub-add the configured external subtitle file

public:
    // Raw mpv handle, for the Renderer's render-context creation.
    void *mpvHandle() const;

private:
    Renderer *m_renderer = nullptr;
    MpvHolder *m_holder = nullptr;
    MediaState m_state = MediaState::Idle;
    bool m_renderReady = false;  // mpv_render_context created
    QString m_pendingUrl;        // URL waiting for the render context

    // Video placement, maintained from mpv property events.
    QRectF m_videoRectPx;        // osd-dimensions, in render-target pixels
    double m_aspect = 0.0;       // display aspect of the picture (0 = unknown)
    int m_fboW = 0;              // last render-target size (device pixels)
    int m_fboH = 0;
    QRectF m_crop;               // active crop, tile-normalized (empty = none)
    bool m_cropApplied = false;  // pushed to mpv already
};

} // namespace zp

#endif // ZP_MPV_MEDIA_PLAYER_H
