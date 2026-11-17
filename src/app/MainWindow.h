#ifndef ZP_MAIN_WINDOW_H
#define ZP_MAIN_WINDOW_H

#include <QMainWindow>
#include <QHash>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QComboBox;
QT_END_NAMESPACE

namespace zp {

class DeviceTreeWidget;
class VideoGrid;
class PtzWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(IPlatformApi *api, QWidget *parent = nullptr);
    ~MainWindow() override;

    void openDirect(const QUrl &url);

private slots:
    void onDeviceTreeReady(const QList<DeviceNode> &roots);
    void onDeviceActivated(const QString &deviceId, const QString &name, bool ptzCapable);
    void onStreamUrlReady(const QString &deviceId, const QUrl &url);
    void onActiveDeviceChanged(const QString &deviceId, bool ptzCapable);
    void onPtzCommand(const QString &deviceId, const zp::PtzCommand &cmd);
    void onPtzAcknowledged(const QString &deviceId, int cmdType, int speed);
    void onError(const QString &deviceId, const QString &message);
    void onLayoutChanged(int index);

private:
    void buildUi();
    void toast(const QString &text);

    IPlatformApi *m_api = nullptr;
    DeviceTreeWidget *m_tree = nullptr;
    VideoGrid *m_grid = nullptr;
    PtzWidget *m_ptz = nullptr;
    QComboBox *m_layoutBox = nullptr;

    QHash<QString, QString> m_names;
    QHash<QString, bool> m_ptzCapable;
    QString m_activeDeviceId;
};

} // namespace zp

#endif // ZP_MAIN_WINDOW_H
