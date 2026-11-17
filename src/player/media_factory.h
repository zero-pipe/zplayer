#ifndef ZP_MEDIA_FACTORY_H
#define ZP_MEDIA_FACTORY_H

#include <QString>

namespace zp {

class IMediaPlayer;

// Which concrete engine to use. Only one place decides; UI never names a
// concrete engine class.
enum class MediaBackend {
    Auto,      // probe availability in preference order
    QtAV,
    LibMpv     // reserved for the future libmpv implementation
};

namespace media {

// Create a media player for the given backend (Auto picks the best available).
IMediaPlayer *createPlayer(MediaBackend backend = MediaBackend::Auto);

// Resolve Auto to a concrete available backend name.
QString resolveBackendName(MediaBackend backend);

} // namespace media
} // namespace zp

#endif // ZP_MEDIA_FACTORY_H
