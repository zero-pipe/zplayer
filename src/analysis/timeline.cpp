#include "analysis/timeline.h"

#include <algorithm>

namespace zp {

QVector<TimelineSegment> buildPersonTimeline(const QVector<PresenceSample> &samples,
                                             double mergeGap)
{
    QVector<PresenceSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
              [](const PresenceSample &a, const PresenceSample &b) { return a.t < b.t; });

    QVector<TimelineSegment> out;
    for (const PresenceSample &s : sorted) {
        if (s.persons <= 0) {
            continue;
        }
        if (!out.isEmpty() && s.t - out.last().end <= mergeGap) {
            out.last().end = qMax(out.last().end, s.t);
            out.last().peak = qMax(out.last().peak, s.persons);
        } else {
            TimelineSegment seg;
            seg.start = s.t;
            seg.end = s.t;
            seg.peak = s.persons;
            out.append(seg);
        }
    }
    // A single sample is a point in time; give it a visible minimum width.
    for (TimelineSegment &seg : out) {
        if (seg.end - seg.start < 0.5) {
            seg.end = seg.start + 0.5;
        }
    }
    return out;
}

} // namespace zp
