#include "player/media_factory.h"
#include "player/media_player.h"
#include "player/qtav_media_player.h"

namespace zp {
namespace media {

IMediaPlayer *createPlayer(MediaBackend backend)
{
    switch (backend) {
    case MediaBackend::QtAV:
        return new QtavMediaPlayer();
    case MediaBackend::LibMpv:
        // Reserved: return new MpvMediaPlayer(); once implemented.
        return new QtavMediaPlayer();
    case MediaBackend::Auto:
    default:
        // Preference order: libmpv (future) -> QtAV. For now QtAV is the only
        // available backend.
        return new QtavMediaPlayer();
    }
}

QString resolveBackendName(MediaBackend backend)
{
    if (backend == MediaBackend::LibMpv) {
        return QStringLiteral("libmpv");
    }
    return QStringLiteral("QtAV");
}

} // namespace media
} // namespace zp
