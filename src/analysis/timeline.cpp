#include "analysis/timeline.h"

#include <algorithm>

namespace zp {

namespace {

QVector<TimelineSegment> segmentize(const QVector<PresenceSample> &samples,
                                    TimelineCategory category,
                                    int (*countOf)(const PresenceSample &),
                                    double mergeGap)
{
    QVector<TimelineSegment> out;
    for (const PresenceSample &s : samples) {
        const int n = countOf(s);
        if (n <= 0) {
            continue;
        }
        if (!out.isEmpty() && s.t - out.last().end <= mergeGap) {
            out.last().end = qMax(out.last().end, s.t);
            out.last().peak = qMax(out.last().peak, n);
        } else {
            TimelineSegment seg;
            seg.start = s.t;
            seg.end = s.t;
            seg.peak = n;
            seg.category = category;
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

} // namespace

QVector<TimelineSegment> buildTimeline(const QVector<PresenceSample> &samples,
                                       double mergeGap)
{
    QVector<PresenceSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
              [](const PresenceSample &a, const PresenceSample &b) { return a.t < b.t; });

    QVector<TimelineSegment> out =
        segmentize(sorted, TimelineCategory::Person,
                   [](const PresenceSample &s) { return s.persons; }, mergeGap);
    out += segmentize(sorted, TimelineCategory::Vehicle,
                      [](const PresenceSample &s) { return s.vehicles; }, mergeGap);
    out += segmentize(sorted, TimelineCategory::Animal,
                      [](const PresenceSample &s) { return s.animals; }, mergeGap);
    std::sort(out.begin(), out.end(), [](const TimelineSegment &a, const TimelineSegment &b) {
        return a.start < b.start;
    });
    return out;
}

} // namespace zp
