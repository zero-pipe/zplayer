#ifndef ZP_TIMELINE_H
#define ZP_TIMELINE_H

#include <QVector>

namespace zp {

// One observation from the offline scan: how many people were on screen at a
// media time. Samples arrive at the analysis cadence (a few per second of
// *media* time when scanning at speed).
struct PresenceSample {
    double t = 0.0;     // media seconds
    int persons = 0;    // simultaneous person detections
};

// A contiguous stretch of media where people are present. Consecutive samples
// separated by less than `mergeGap` seconds are merged, so walking through the
// scene yields one segment instead of a dozen blips.
struct TimelineSegment {
    double start = 0.0;
    double end = 0.0;
    int peak = 0;       // max simultaneous people in the segment
};

// Segments with at least one person, in time order. Samples may arrive with
// small out-of-order jitter; they are sorted before segmenting.
QVector<TimelineSegment> buildPersonTimeline(const QVector<PresenceSample> &samples,
                                             double mergeGap = 1.5);

} // namespace zp

#endif // ZP_TIMELINE_H
