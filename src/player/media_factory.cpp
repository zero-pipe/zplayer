#include "player/media_factory.h"
#include "player/frame_grabber.h"
#include "player/media_player.h"

#ifdef ZP_WITH_MPV
#include "player/backends/mpv_media_player.h"
#endif
#ifdef ZP_WITH_QTAV
#include "player/backends/qtav_media_player.h"
#endif

namespace zp {
namespace media {

namespace {
// A null-safe placeholder used only if no engine is compiled in.
class NullMediaPlayer : public IMediaPlayer {
public:
    IMediaRenderer *renderer() override { return nullptr; }
    void open(const QUrl &) override { emit failed(tr("无可用媒体引擎")); }
    void play() override {}
    void pause() override {}
    void stop() override {}
    void setMuted(bool) override {}
    void setVolume(int) override {}
    MediaState state() const override { return MediaState::Failed; }
    QString backendName() const override { return QStringLiteral("none"); }
};
} // namespace

IMediaPlayer *createPlayer(MediaBackend backend)
{
    IMediaPlayer *player = nullptr;

    switch (backend) {
    case MediaBackend::QtAV:
#ifdef ZP_WITH_QTAV
        player = new QtavMediaPlayer();
#endif
        break;
    case MediaBackend::LibMpv:
#ifdef ZP_WITH_MPV
        player = new MpvMediaPlayer();
#endif
        break;
    case MediaBackend::Auto:
    default:
        // Primary engine is libmpv; QtAV is a legacy fallback.
#ifdef ZP_WITH_MPV
        player = new MpvMediaPlayer();
#elif defined(ZP_WITH_QTAV)
        player = new QtavMediaPlayer();
#endif
        break;
    }

    if (!player) {
        player = new NullMediaPlayer();
    }

    // Dev diagnostic: ZP_TAP=<dir> dumps one frame per player (see frame_grabber).
    if (player->supportsFrameTap()) {
        if (IVideoFrameSink *tap = debugFrameTap()) {
            player->setFrameSink(tap);
        }
    }

    return player;
}

QString resolveBackendName(MediaBackend backend)
{
    switch (backend) {
    case MediaBackend::QtAV:  return QStringLiteral("QtAV");
    case MediaBackend::LibMpv: return QStringLiteral("libmpv");
    case MediaBackend::Auto:
    default:
#ifdef ZP_WITH_MPV
        return QStringLiteral("libmpv");
#elif defined(ZP_WITH_QTAV)
        return QStringLiteral("QtAV");
#else
        return QStringLiteral("none");
#endif
    }
}

} // namespace media
} // namespace zp
