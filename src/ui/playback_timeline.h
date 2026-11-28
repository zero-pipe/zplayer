#ifndef ZP_PLAYBACK_TIMELINE_H
#define ZP_PLAYBACK_TIMELINE_H

#include <QWidget>
#include <QList>
#include "platform/platform_api.h"

namespace zp {

// 录像回放时间轴：以「一天」为标尺，绘制录像片段色块与播放头。
// 点击/拖动 -> seekRequested(绝对 ms)。纯绘制，不持有播放状态。
class PlaybackTimeline : public QWidget {
    Q_OBJECT
public:
    explicit PlaybackTimeline(QWidget *parent = nullptr);

    void setDay(qint64 dayStartMs, qint64 dayEndMs);
    void setSegments(const QList<RecordingSegment> &segments);
    void setPosition(qint64 posMs);   // 仅更新播放头，不发信号
    // 当前正在播放的片段（recordId）；-1 或 0 表示无。高亮显示。
    void setActiveSegment(qlonglong recordId);
    // 目标时刻落在哪个片段内；不在任何片段内时返回最近的片段下标。
    int segmentIndexAt(qint64 ms) const;
    QString segmentTip(qint64 ms) const;
    QSize sizeHint() const override { return QSize(400, 72); }
    QSize minimumSizeHint() const override { return QSize(120, 72); }

signals:
    void seekRequested(qint64 absMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    qint64 xToMs(int x) const;
    int msToX(qint64 ms) const;
    const RecordingSegment *segmentAt(qint64 ms) const;

    qint64 m_dayStart = 0;
    qint64 m_dayEnd = 0;
    qint64 m_pos = 0;
    QList<RecordingSegment> m_segs;
    bool m_dragging = false;
    qlonglong m_activeRecordId = 0;
};

} // namespace zp

#endif // ZP_PLAYBACK_TIMELINE_H
