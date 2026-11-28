#include "ui/playback_timeline.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>
#include <QToolTip>

namespace zp {

PlaybackTimeline::PlaybackTimeline(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(58);
}

void PlaybackTimeline::setDay(qint64 dayStartMs, qint64 dayEndMs)
{
    m_dayStart = dayStartMs;
    m_dayEnd = dayEndMs > dayStartMs ? dayEndMs : dayStartMs + 1;
    m_pos = dayStartMs;
    update();
}

void PlaybackTimeline::setSegments(const QList<RecordingSegment> &segments)
{
    m_segs = segments;
    update();
}

void PlaybackTimeline::setPosition(qint64 posMs)
{
    m_pos = posMs;
    update();
}

qint64 PlaybackTimeline::xToMs(int x) const
{
    const double w = qMax(1, width());
    double ratio = double(x) / w;
    ratio = qBound(0.0, ratio, 1.0);
    return m_dayStart + qint64(ratio * double(m_dayEnd - m_dayStart));
}

int PlaybackTimeline::msToX(qint64 ms) const
{
    const qint64 span = qMax<qint64>(1, m_dayEnd - m_dayStart);
    double ratio = double(ms - m_dayStart) / double(span);
    ratio = qBound(0.0, ratio, 1.0);
    return int(ratio * width());
}

void PlaybackTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int W = width();
    const int H = height();

    // 背景
    p.fillRect(rect(), QColor(0x11, 0x16, 0x1f));

    // 小时刻度 + 标签
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(QColor(0x5c, 0x6b, 0x82));
    const qint64 hourMs = 3600LL * 1000;
    for (qint64 t = m_dayStart; t <= m_dayEnd; t += hourMs) {
        const int x = msToX(t);
        p.drawLine(x, 18, x, H - 10);
        const int hh = int((t - m_dayStart) / hourMs);
        const QString label = QStringLiteral("%1:00").arg(hh, 2, 10, QLatin1Char('0'));
        p.drawText(QRect(x + 2, 0, 40, 16), Qt::AlignLeft | Qt::AlignVCenter, label);
    }

    // 轨道
    const int trackTop = 22;
    const int trackBot = H - 12;
    p.fillRect(QRect(0, trackTop, W, trackBot - trackTop), QColor(0x1a, 0x22, 0x30));

    // 录像片段
    p.setPen(Qt::NoPen);
    for (const RecordingSegment &s : m_segs) {
        qint64 a = qMax(s.startMs, m_dayStart);
        qint64 b = qMin(s.endMs, m_dayEnd);
        if (b <= a) continue;
        int x1 = msToX(a);
        int x2 = msToX(b);
        if (x2 <= x1) x2 = x1 + 1;
        QColor c(0x25, 0x63, 0xeb, 200); // 蓝
        p.setBrush(c);
        p.drawRoundedRect(QRectF(x1, trackTop, x2 - x1, trackBot - trackTop), 2, 2);
    }

    // 播放头
    const int px = msToX(m_pos);
    p.setPen(QPen(QColor(0xef, 0x44, 0x44), 2));
    p.drawLine(px, trackTop - 4, px, trackBot + 2);
    QPolygon tri;
    tri << QPoint(px - 6, trackTop - 8) << QPoint(px + 6, trackTop - 8) << QPoint(px, trackTop);
    p.setBrush(QColor(0xef, 0x44, 0x44));
    p.setPen(Qt::NoPen);
    p.drawPolygon(tri);
}

void PlaybackTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    m_dragging = true;
    m_pos = xToMs(int(event->localPos().x()));
    update();
}

void PlaybackTimeline::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_pos = xToMs(int(event->localPos().x()));
    update();
    const QString tip = QDateTime::fromMSecsSinceEpoch(m_pos)
                            .toString(QStringLiteral("HH:mm:ss"));
    QToolTip::showText(event->globalPos(), tip, this);
}

void PlaybackTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) {
        return;
    }
    m_dragging = false;
    m_pos = xToMs(int(event->localPos().x()));
    update();
    emit seekRequested(m_pos);
}

} // namespace zp
