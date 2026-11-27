#ifndef ZP_PLAYER_WINDOW_H
#define ZP_PLAYER_WINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QToolButton>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QToolButton;
class QButtonGroup;
class QSizeGrip;
class QTabWidget;
class QScrollArea;
class QSplitter;
class QVBoxLayout;
class QStackedWidget;
class QPushButton;
class QDateEdit;
class QTimer;
QT_END_NAMESPACE

namespace zp {

class DeviceTreeView;
class VideoGrid;
class VideoTile;
class PtzPanel;
class Toast;
class AnalysisService;
class IFrameAnalyzer;
class TrainingToolWindow;
class PlaybackTimeline;

// Slim vertical strip shown when the right tool dock is collapsed: icon on
// top, optional count just below. (A stock QToolButton with text-under-icon
// spreads the two across the full height, which reads badly in a tall strip.)
class DockStripButton : public QToolButton {
public:
    explicit DockStripButton(QWidget *parent = nullptr) : QToolButton(parent) {}
    void setCount(int n) { m_count = n; update(); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_count = 0;
};

class PlayerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PlayerWindow(IPlatformApi *api, const Session &session, QWidget *parent = nullptr);
    ~PlayerWindow() override;

    // Handle a command-line media target (local file / direct URL).
    void openDirect(const QUrl &url);

    // Open a local video file on demand. This is the same rendering/analysis
    // path as a live channel: analysis code never needs to know the difference.
    void openLocalFile(const QString &path);

    // Dev helper: fill the grid with N demo tiles (for layout verification).
    void fillDemo(const QUrl &url, int count);
    // Evidence review dialog for a tile (shared by the button and dev hooks).
    void openEvidenceDialog(VideoTile *tile);
    void setDemoLayout(int index);

signals:
    void logoutRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openSettings();
    void onChildrenReady(const QString &parentId, const QList<DeviceNode> &nodes);
    void onChannelActivated(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void onPlayStarted(const zp::ChannelRef &ch, const QUrl &url, const QString &transport);
    void onStreamFailed(const zp::ChannelRef &ch, const QString &message);
    void onActiveChannelChanged(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void onPtzCommand(const zp::ChannelRef &ch, const zp::PtzCommand &cmd);
    void onPtzAcknowledged(const zp::ChannelRef &ch, int cmdType, int speed);
    void onMaximizeRequested(const zp::ChannelRef &ch);
    void onRetryRequested(const zp::ChannelRef &ch);
    void onSessionExpired();
    void onError(const QString &scope, const QString &message);
    void onLayoutButtonClicked(int index);

    // 顶部三模式：0 实时预览 / 1 录像回放 / AI 打开训练窗口
    void setMode(int mode);
    void onModeAi();
    // 录像回放
    void queryPlaybackDay();
    void onPbSegmentClicked(qlonglong recordId, qint64 startMs);
    void onPbSeek(qint64 absMs);
    void onPbStop();
    void onPbPlayPause();
    void onPbTick();
    void onRecordingsReady(const zp::ChannelRef &ch,
                           const QList<zp::RecordingSegment> &segments);
    void onPlaybackStarted(const zp::ChannelRef &ch, qlonglong recordId,
                           const QUrl &url, const QString &transport,
                           qint64 segStartMs, qint64 segDurationMs);
    void onPlaybackStopped(const zp::ChannelRef &ch, qlonglong recordId);

private:
    void buildUi();
    void setActiveLayoutButton(int index);
    void toastInfo(const QString &text);
    void toastSuccess(const QString &text);
    void toastError(const QString &text);
    void openTrainingTool();
    void updateSampleBadge();
    void refreshSampleList();
    void selectSampleTab();
    void syncSampleTab(int count);
    void collapseToolDock();
    void expandToolDock();
    void reloadDetector();
    void rebuildSegmentList(const QList<zp::RecordingSegment> &segments);
    void updatePbTimeLabel();
    void snapshotActiveTile();

    // Frameless custom title bar helpers.
    void toggleMaximize();
    void updateWindowButtons();

    IPlatformApi *m_api = nullptr;
    Session m_session;

    DeviceTreeView *m_tree = nullptr;
    VideoGrid *m_grid = nullptr;
    PtzPanel *m_ptz = nullptr;
    Toast *m_toast = nullptr;
    QTabWidget *m_toolDock = nullptr;
    QScrollArea *m_sampleScroll = nullptr;
    QVBoxLayout *m_sampleLay = nullptr;   // holds the sample cards + trailing stretch
    QWidget *m_samplePage = nullptr;      // "样本" tab body (added dynamically)
    QLabel *m_sampleCount = nullptr;      // header count text inside the tab
    DockStripButton *m_dockReopen = nullptr;  // slim strip shown while the dock is collapsed
    QSplitter *m_bodySplitter = nullptr;  // tree | grid | tool dock | reopen strip
    bool m_dockCollapsed = false;
    int m_dockWidth = 320;                // last expanded dock width
    int m_sampleShown = -1;          // last rendered sample count
    TrainingToolWindow *m_trainTool = nullptr;
    AnalysisService *m_analysis = nullptr;   // per-window AI scheduler
    IFrameAnalyzer *m_detector = nullptr;    // owned; YOLO analyzer (may be null)
    IFrameAnalyzer *m_ocr = nullptr;         // owned; scene-text OCR (may be null)

    QButtonGroup *m_layoutGroup = nullptr;

    // 顶部模式切换 + 中央/右侧堆叠
    int m_mode = 0;                       // 0 实时 / 1 回放
    QButtonGroup *m_modeGroup = nullptr;
    QStackedWidget *m_centerStack = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    QWidget *m_pbFilter = nullptr;         // 左侧回放查询条件（仅回放模式显示）
    QDateEdit *m_pbDate = nullptr;

    // 回放页
    VideoGrid *m_pbGrid = nullptr;
    PlaybackTimeline *m_pbTimeline = nullptr;
    QLabel *m_pbChannelLabel = nullptr;
    QLabel *m_pbTimeLabel = nullptr;
    QLabel *m_pbSegHeader = nullptr;       // 右侧片段列表标题
    QVBoxLayout *m_pbSegLay = nullptr;     // 右侧片段列表容器
    QToolButton *m_pbPlayBtn = nullptr;
    QTimer *m_pbTimer = nullptr;
    bool m_pbPlaying = false;
    qint64 m_pbDayStart = 0;
    qint64 m_pbDayEnd = 0;
    qint64 m_pbSegStart = 0;               // 当前片段绝对起点
    qint64 m_pbSegDur = 0;                 // 当前片段时长 ms
    qint64 m_pbPlayBaseWall = 0;           // 进度基准墙钟
    qint64 m_pbPlayBasePos = 0;            // 基准位置（绝对 ms）
    QList<zp::RecordingSegment> m_pbSegs;  // 当前日片段

    // Custom (frameless) title bar state.
    QWidget *m_topBar = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QSizeGrip *m_sizeGrip = nullptr;
    bool m_dragging = false;
    QPoint m_pressGlobal;
    QPoint m_dragOffset;
    QRect m_restoreGeom;

    QHash<QString, QString> m_channelNames;   // ch.key() -> name
    QHash<QString, bool> m_channelPtz;
    ChannelRef m_active;
    bool m_activePtzCapable = false;
    // The PTZ panel is only meaningful for a live channel that is actually on
    // screen: hidden otherwise (real-time previews cannot pause; recorded
    // playback has no PTZ).
    void updatePtzPanel();
    ChannelRef m_pending;                     // channel awaiting playStarted
    QString m_pendingName;
    bool m_pendingPtz = false;

    // 云端回放状态
    ChannelRef m_pbChannel;                   // 查询片段所用的通道
    ChannelRef m_pbRef;                       // 回放瓦片使用的合成 ref（与实时区分）
    qlonglong m_pbRecordId = 0;               // 当前回放片段 id
};

} // namespace zp

#endif // ZP_PLAYER_WINDOW_H
