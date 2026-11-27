#ifndef ZP_PLAYBACK_DIALOG_H
#define ZP_PLAYBACK_DIALOG_H

#include <QDialog>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QDateEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QLabel;
class QTimer;
QT_END_NAMESPACE

namespace zp {

// 云端回放：按日查片段 → 播放 → 绝对时间拖动定位。
// 本类只管界面与时间轴；HTTP 由 PlayerWindow 转给 IPlatformApi。
class PlaybackDialog : public QDialog {
    Q_OBJECT
public:
    explicit PlaybackDialog(QWidget *parent = nullptr);

    void setChannelName(const QString &name);
    void setSegments(const QList<RecordingSegment> &segs);
    // 起播/定位完成：segStartMs 为当前播放位置（绝对 ms），segDurationMs 为片段时长。
    void setPlaying(qlonglong recordId, qint64 segStartMs, qint64 segDurationMs);
    void setPlaybackError(const QString &msg);
    void setBusy(bool busy);

signals:
    void queryRequested(qint64 startMs, qint64 endMs);
    void playSegmentRequested(qlonglong recordId, qint64 segStartMs);
    void seekRequested(qint64 absMs);
    void stopRequested();

private slots:
    void onQuery();
    void onPlaySelected();
    void onStop();
    void onSliderReleased();
    void onTick();

private:
    void updatePlayhead();

    QDateEdit *m_date = nullptr;
    QListWidget *m_list = nullptr;
    QPushButton *m_queryBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_tick = nullptr;

    QList<RecordingSegment> m_segs;

    bool m_playing = false;
    qlonglong m_recordId = 0;
    qint64 m_segStart = 0;   // 当前片段绝对起点 ms
    qint64 m_segDuration = 0; // ms
    qint64 m_playBasePos = 0; // 基准位置（绝对 ms）
    qint64 m_playBaseWall = 0; // 该基准位置对应的墙钟 ms
};

} // namespace zp

#endif // ZP_PLAYBACK_DIALOG_H
