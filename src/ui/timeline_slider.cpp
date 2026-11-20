#include "ui/timeline_slider.h"

#include <QMouseEvent>
#include <QToolTip>
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
    for (const TimelineSegment &seg : m_segments) {
        const double a = qBound(0.0, seg.start / m_duration, 1.0);
        const double b = qBound(0.0, seg.end / m_duration, 1.0);
        const int x1 = groove.left() + int(a * groove.width());
        const int x2 = groove.left() + int(b * groove.width());
        QRect r(x1, groove.top(), qMax(2, x2 - x1), groove.height());
        // Category colours follow the control legend: person blue, vehicle
        // green, animal orange. More targets = more opaque.
        QColor c;
        switch (seg.category) {
        case TimelineCategory::Vehicle: c = QColor(46, 204, 113); break;
        case TimelineCategory::Animal: c = QColor(245, 166, 35); break;
        case TimelineCategory::Person:
        default: c = QColor(47, 129, 247); break;
        }
        c.setAlpha(qBound(110, 110 + seg.peak * 35, 220));
        p.fillRect(r, c);
    }
}

double TimelineSlider::timeAt(int x) const
{
    if (m_duration <= 0.0) {
        return -1.0;
    }
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt,
                                                 QStyle::SC_SliderGroove, this);
    if (groove.isEmpty()) {
        return -1.0;
    }
    const double frac = double(x - groove.left()) / qMax(1, groove.width());
    return qBound(0.0, frac, 1.0) * m_duration;
}

QString TimelineSlider::formatTime(double seconds)
{
    const int t = qMax(0, int(seconds + 0.5));
    const int h = t / 3600;
    const int m = (t / 60) % 60;
    const int sec = t % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h)
            .arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(sec, 2, 10, QLatin1Char('0'));
}

void TimelineSlider::mouseMoveEvent(QMouseEvent *event)
{
    const double t = timeAt(event->pos().x());
    if (t >= 0.0) {
        // Precise scrub position under the pointer, like desktop players.
        QToolTip::showText(event->globalPos(), formatTime(t), this);
    } else {
        QToolTip::hideText();
    }
    QSlider::mouseMoveEvent(event);
}

void TimelineSlider::leaveEvent(QEvent *event)
{
    QToolTip::hideText();
    QSlider::leaveEvent(event);
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
