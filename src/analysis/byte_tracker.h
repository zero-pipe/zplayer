#ifndef ZP_BYTE_TRACKER_H
#define ZP_BYTE_TRACKER_H

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QVector>

#include "analysis/analyzer.h"

namespace zp {

// ---------------------------------------------------------------------------
// ByteTrack-lite: online multi-object tracking over detection boxes.
//
// The detector only sees a few frames per second per source, so tracks are
// what give the UI stable ids ("#7 人") and let evidence spans and the AI
// fast-forward survive a missed detection. The scheme follows ByteTrack's two
// stages — high-confidence boxes are matched first, then low-confidence boxes
// are used to keep tracks alive while they pass behind something — but
// association is greedy IoU instead of a Hungarian solver, which is plenty at
// 3-5 fps and keeps this dependency-free.
//
// Motion is a constant-velocity model in normalized coordinates: rects are
// predicted by the last velocity, and a match blends the prediction with the
// measurement (exponential smoothing). Class ids never mix.
//
// Not thread-safe; one instance per source, used by a single thread.
// ---------------------------------------------------------------------------
class ByteTracker {
public:
    ByteTracker();

    // Forget all tracks (e.g. on seek or file change).
    void reset();

    // Assigns trackId on the boxes in place. `dt` is seconds since the last
    // update (<= 0 falls back to a sane default). Boxes keep their order.
    void update(QVector<AnalysisBox> &boxes, double dt);

    int activeTrackCount() const { return m_tracks.size(); }
    int totalCreated() const { return m_nextId - 1; }

private:
    struct Track {
        int id = -1;
        int classId = -1;
        QRectF rect;            // normalized
        QPointF vel;            // normalized units per second
        float score = 0.f;
        int hits = 0;
        int missed = 0;
    };

    QRectF predict(const Track &t, double dt) const;
    void touch(Track &t, const AnalysisBox &box, double dt);
    QVector<Track> m_tracks;
    int m_nextId = 1;

    float m_highThresh = 0.50f;
    float m_lowThresh = 0.15f;
    float m_iouThresh = 0.30f;
    int m_maxMissed = 12;       // ~3 s at 4 fps
};

} // namespace zp

#endif // ZP_BYTE_TRACKER_H
