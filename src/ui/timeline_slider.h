#ifndef ZP_TIMELINE_SLIDER_H
#define ZP_TIMELINE_SLIDER_H

#include <QSlider>
#include <QVector>

#include "analysis/timeline.h"

namespace zp {

// Transport slider that also shows the offline-scan results: person-present
// segments are painted over the groove, so the user can see at a glance where
// something happened and jump straight to it.
class TimelineSlider : public QSlider {
    Q_OBJECT
public:
    explicit TimelineSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

    void setSegments(const QVector<TimelineSegment> &segments);
    void clearSegments();
    void setMediaDuration(double seconds) { m_duration = seconds; update(); }

signals:
    // Clicked on (or inside) a person segment; seconds = segment start.
    void segmentActivated(double seconds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QVector<TimelineSegment> m_segments;
    double m_duration = 0.0;
};

} // namespace zp

#endif // ZP_TIMELINE_SLIDER_H
