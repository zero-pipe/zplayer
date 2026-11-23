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
};

} // namespace zp

#endif // ZP_PLAYER_WINDOW_H
