#ifndef ZP_TIMELINE_H
#define ZP_TIMELINE_H

#include <QVector>

namespace zp {

// What a timeline highlight is about; drives the colour on the transport bar.
enum class TimelineCategory {
    Person = 0,    // blue
    Vehicle,       // green
    Animal,        // orange
};

// One observation from the offline scan: how many targets of each category
// were on screen at a media time. Samples arrive at the analysis cadence (a
// few per second of *media* time when scanning at speed).
struct PresenceSample {
    double t = 0.0;     // media seconds
    int persons = 0;
    int vehicles = 0;
    int animals = 0;
};

// A contiguous stretch of media where at least one target was present.
// Consecutive samples separated by less than `mergeGap` seconds are merged, so
// walking through the scene yields one segment instead of a dozen blips.
struct TimelineSegment {
    double start = 0.0;
    double end = 0.0;
    int peak = 0;       // max simultaneous targets in the segment
    TimelineCategory category = TimelineCategory::Person;
};

// Segments per category, in time order. Samples may arrive with small
// out-of-order jitter; they are sorted before segmenting.
QVector<TimelineSegment> buildTimeline(const QVector<PresenceSample> &samples,
                                       double mergeGap = 3.0);

} // namespace zp

#endif // ZP_TIMELINE_H
