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
QT_END_NAMESPACE

namespace zp {

class DeviceTreeView;
class VideoGrid;
class PtzPanel;
class Toast;

class PlayerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PlayerWindow(IPlatformApi *api, const Session &session, QWidget *parent = nullptr);
    ~PlayerWindow() override;

    // Handle a command-line media target (local file / direct URL).
    void openDirect(const QUrl &url);

    // Dev helper: fill the grid with N demo tiles (for layout verification).
    void fillDemo(const QUrl &url, int count);
    void setDemoLayout(int index);

signals:
    void logoutRequested();

private slots:
    void onChildrenReady(const QString &parentId, const QList<DeviceNode> &nodes);
    void onChannelActivated(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void onPlayStarted(const zp::ChannelRef &ch, const QUrl &url, const QString &transport);
    void onStreamFailed(const zp::ChannelRef &ch, const QString &message);
    void onActiveChannelChanged(const zp::ChannelRef &ch, const QString &name, bool ptzCapable);
    void onPtzCommand(const zp::ChannelRef &ch, const zp::PtzCommand &cmd);
    void onPtzAcknowledged(const zp::ChannelRef &ch, int cmdType, int speed);
    void onQueryPresets(const zp::ChannelRef &ch);
    void onPresetsReady(const zp::ChannelRef &ch, const QList<zp::PresetInfo> &presets);
    void onCallPreset(const zp::ChannelRef &ch, int presetId);
    void onAddPreset(const zp::ChannelRef &ch, int presetId);
    void onDeletePreset(const zp::ChannelRef &ch, int presetId);
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

    IPlatformApi *m_api = nullptr;
    Session m_session;

    DeviceTreeView *m_tree = nullptr;
    VideoGrid *m_grid = nullptr;
    PtzPanel *m_ptz = nullptr;
    Toast *m_toast = nullptr;

    QLabel *m_groupChip = nullptr;
    QLineEdit *m_search = nullptr;
    QButtonGroup *m_layoutGroup = nullptr;

    QHash<QString, QString> m_channelNames;   // ch.key() -> name
    QHash<QString, bool> m_channelPtz;
    ChannelRef m_active;
    ChannelRef m_pending;                     // channel awaiting playStarted
    QString m_pendingName;
    bool m_pendingPtz = false;
};

} // namespace zp

#endif // ZP_PLAYER_WINDOW_H
