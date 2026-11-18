#ifndef ZP_PLAYER_WINDOW_H
#define ZP_PLAYER_WINDOW_H

#include <QMainWindow>
#include <QHash>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QToolButton;
class QButtonGroup;
class QSizeGrip;
QT_END_NAMESPACE

namespace zp {

class DeviceTreeView;
class VideoGrid;
class PtzPanel;
class Toast;
class AnalysisService;
class IFrameAnalyzer;

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

    // Frameless custom title bar helpers.
    void toggleMaximize();
    void updateWindowButtons();

    IPlatformApi *m_api = nullptr;
    Session m_session;

    DeviceTreeView *m_tree = nullptr;
    VideoGrid *m_grid = nullptr;
    PtzPanel *m_ptz = nullptr;
    Toast *m_toast = nullptr;
    AnalysisService *m_analysis = nullptr;   // per-window AI scheduler
    IFrameAnalyzer *m_detector = nullptr;    // owned; YOLO analyzer (may be null)

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
    ChannelRef m_pending;                     // channel awaiting playStarted
    QString m_pendingName;
    bool m_pendingPtz = false;
};

} // namespace zp

#endif // ZP_PLAYER_WINDOW_H
