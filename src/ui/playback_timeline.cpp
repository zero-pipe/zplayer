#include "ui/playback_timeline.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>
#include <QToolTip>
#include <limits>
#include "ui/theme.h"

namespace zp {

PlaybackTimeline::PlaybackTimeline(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(58);

    // Custom-painted: the global stylesheet cannot restyle this widget, so
    // repaint explicitly when the theme preset changes.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, static_cast<void (QWidget::*)()>(&QWidget::update));
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

void PlaybackTimeline::setActiveSegment(qlonglong recordId)
{
    if (m_activeRecordId == recordId) {
        return;
    }
    m_activeRecordId = recordId;
    update();
}

const RecordingSegment *PlaybackTimeline::segmentAt(qint64 ms) const
{
    for (const RecordingSegment &s : m_segs) {
        if (ms >= s.startMs && ms < s.endMs) {
            return &s;
        }
    }
    return nullptr;
}

int PlaybackTimeline::segmentIndexAt(qint64 ms) const
{
    for (int i = 0; i < m_segs.size(); ++i) {
        const RecordingSegment &s = m_segs.at(i);
        if (ms >= s.startMs && ms < s.endMs) {
            return i;
        }
    }
    // 落在空隙：取最近的片段（之前优先于之后），让"拖哪里播哪里"总有着落。
    int best = -1;
    qint64 bestGap = std::numeric_limits<qint64>::max();
    for (int i = 0; i < m_segs.size(); ++i) {
        const RecordingSegment &s = m_segs.at(i);
        const qint64 gap = ms < s.startMs ? s.startMs - ms : ms - s.endMs;
        if (gap < bestGap) {
            bestGap = gap;
            best = i;
        }
    }
    return best;
}

QString PlaybackTimeline::segmentTip(qint64 ms) const
{
    const int idx = segmentIndexAt(ms);
    if (idx < 0) {
        return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss"));
    }
    const RecordingSegment &s = m_segs.at(idx);
    const QString when = QDateTime::fromMSecsSinceEpoch(ms)
                             .toString(QStringLiteral("HH:mm:ss"));
    const QString range = QStringLiteral("%1 - %2")
        .arg(QDateTime::fromMSecsSinceEpoch(s.startMs).toString(QStringLiteral("HH:mm:ss")),
             QDateTime::fromMSecsSinceEpoch(s.endMs).toString(QStringLiteral("HH:mm:ss")));
    if (ms >= s.startMs && ms < s.endMs) {
        return QStringLiteral("%1 · 片段 %2").arg(when, range);
    }
    return QStringLiteral("%1 · 最近片段 %2").arg(when, range);
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

    const ThemeTokens &tk = ThemeManager::instance().tokens();

    // 背景：浅色模式使用亮色面板，夜晚模式沿用深色轨道底色。
    p.fillRect(rect(), tk.timelineBg);

    // 小时刻度 + 标签
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    p.setPen(tk.timelineTick);
    const qint64 hourMs = 3600LL * 1000;
    for (qint64 t = m_dayStart; t <= m_dayEnd; t += hourMs) {
        const int x = msToX(t);
        p.drawLine(x, 18, x, H - 10);
        const int hh = int((t - m_dayStart) / hourMs);
        const QString label = QStringLiteral("%1:00").arg(hh, 2, 10, QLatin1Char('0'));
        p.drawText(QRect(x + 2, 0, 40, 16), Qt::AlignLeft | Qt::AlignVCenter, label);
    }

    // 轨道凹槽：同样按主题取色，浅色下不再是整块黑底。
    const int trackTop = 22;
    const int trackBot = H - 12;
    p.fillRect(QRect(0, trackTop, W, trackBot - trackTop), tk.timelineTrack);

    // 录像片段：当前播放段用高亮描边 + 实心填充，其余保持主题蓝。
    p.setPen(Qt::NoPen);
    for (const RecordingSegment &s : m_segs) {
        qint64 a = qMax(s.startMs, m_dayStart);
        qint64 b = qMin(s.endMs, m_dayEnd);
        if (b <= a) continue;
        int x1 = msToX(a);
        int x2 = msToX(b);
        if (x2 <= x1) x2 = x1 + 1;
        QRectF segRect(x1, trackTop, x2 - x1, trackBot - trackTop);
        const bool active = s.recordId != 0 && s.recordId == m_activeRecordId;
        QColor c = active ? tk.accent : tk.timelineClip;
        c.setAlpha(active ? 230 : 200);
        p.setBrush(c);
        p.drawRoundedRect(segRect, 2, 2);
        if (active) {
            p.setPen(QPen(tk.accent2, 2));
            p.drawRoundedRect(segRect.adjusted(1, 1, -1, -1), 2, 2);
            p.setPen(Qt::NoPen);
        }
    }

    // 播放头：高对比红指针，两种主题下都醒目。
    const int px = msToX(m_pos);
    p.setPen(QPen(tk.timelinePlayhead, 2));
    p.drawLine(px, trackTop - 4, px, trackBot + 2);
    QPolygon tri;
    tri << QPoint(px - 6, trackTop - 8) << QPoint(px + 6, trackTop - 8) << QPoint(px, trackTop);
    p.setBrush(tk.timelinePlayhead);
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
    if (m_dragging) {
        m_pos = xToMs(int(event->localPos().x()));
        update();
    }
    // 悬停也提示会落到哪个片段；拖动中只更新预览，真正起播在 release。
    QToolTip::showText(event->globalPos(), segmentTip(xToMs(int(event->localPos().x()))), this);
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

void PlaybackTimeline::leaveEvent(QEvent *)
{
    QToolTip::hideText();
}

} // namespace zp
