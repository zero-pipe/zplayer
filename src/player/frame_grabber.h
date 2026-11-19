#ifndef ZP_FRAME_GRABBER_H
#define ZP_FRAME_GRABBER_H

#include <QImage>
#include <QRectF>
#include <QString>

#include "player/media_player.h"

namespace zp {

// ---------------------------------------------------------------------------
// CPU-side frame extraction
//
// The frame tap hands out a GPU texture. Analyzers that work on CPU images
// (OCR, classical CV, saving evidence) need pixels instead. This is the one
// place that does the (stalling) GPU->CPU readback, so the cost stays visible
// and can be replaced by a PBO/async path later without touching callers.
//
// Must be called from IVideoFrameSink::onVideoFrame — that is, on the render
// thread, with the render context current and the frame's FBO still bound.
//
// maxWidth > 0 downscales on the GPU (blit with linear filtering) before the
// readback, so the CPU/PCIe cost stays proportional to the analysis size
// instead of the tile size. 0 reads the frame at its native size.
//
// srcRect crops the frame before scaling, normalized 0..1 with the origin at
// the top-left (pass frame.videoRect to drop letterbox bars; pass an empty
// rect for the whole frame).
QImage grabFrameToImage(const VideoFrame &frame, int maxWidth = 0,
                        const QRectF &srcRect = QRectF());

// Dev/diagnostic helper. When the ZP_TAP=<dir> environment variable is set,
// returns a sink that writes the first frame of every player to
// <dir>/tap_p<playerId>_s<serial>.png, then goes quiet. Returns nullptr when
// the variable is unset, so production runs pay nothing.
IVideoFrameSink *debugFrameTap();

} // namespace zp

#endif // ZP_FRAME_GRABBER_H
