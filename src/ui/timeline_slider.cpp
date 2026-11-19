#include "ui/timeline_slider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

namespace zp {

TimelineSlider::TimelineSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent)
{
    setCursor(Qt::PointingHandCursor);
}

void TimelineSlider::setSegments(const QVector<TimelineSegment> &segments)
{
    m_segments = segments;
    update();
}

void TimelineSlider::clearSegments()
{
    if (m_segments.isEmpty()) {
        return;
    }
    m_segments.clear();
    update();
}

void TimelineSlider::paintEvent(QPaintEvent *event)
{
    QSlider::paintEvent(event);
    if (m_segments.isEmpty() || m_duration <= 0.0) {
        return;
    }

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                 QStyle::SC_SliderGroove, this);
    if (groove.isEmpty()) {
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QColor personColor(46, 204, 113, 200);   // online green
    for (const TimelineSegment &seg : m_segments) {
        const double a = qBound(0.0, seg.start / m_duration, 1.0);
        const double b = qBound(0.0, seg.end / m_duration, 1.0);
        const int x1 = groove.left() + int(a * groove.width());
        const int x2 = groove.left() + int(b * groove.width());
        QRect r(x1, groove.top(), qMax(2, x2 - x1), groove.height());
        // Higher peak count = more opaque, so crowds stand out.
        QColor c = personColor;
        c.setAlpha(qBound(90, 90 + seg.peak * 40, 220));
        p.fillRect(r, c);
    }
}

void TimelineSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_segments.isEmpty()) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                     QStyle::SC_SliderGroove, this);
        if (!groove.isEmpty() && m_duration > 0.0) {
            const double frac = double(event->pos().x() - groove.left())
                                / qMax(1, groove.width());
            const double t = qBound(0.0, frac, 1.0) * m_duration;
            for (const TimelineSegment &seg : m_segments) {
                if (t >= seg.start && t <= seg.end) {
                    // Jump to the start of the hit segment; the slider value
                    // follows so the user sees where they went.
                    setValue(int(seg.start * 1000.0));
                    emit segmentActivated(seg.start);
                    event->accept();
                    return;
                }
            }
        }
    }
    QSlider::mousePressEvent(event);
}

} // namespace zp
