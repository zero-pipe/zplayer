#ifndef ZP_PTZ_WIDGET_H
#define ZP_PTZ_WIDGET_H

#include <QWidget>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QPushButton;
class QSlider;
class QLabel;
QT_END_NAMESPACE

namespace zp {

// Right panel: PTZ control. Commands are sent through the platform
// (pass-through), never directly to a device.
class PtzWidget : public QWidget {
    Q_OBJECT
public:
    explicit PtzWidget(QWidget *parent = nullptr);

    void setTarget(const QString &deviceId, bool ptzCapable);

signals:
    void commandIssued(const QString &deviceId, const zp::PtzCommand &cmd);

private:
    void send(PtzCommand::Type type);
    void button(PtzCommand::Type type, const QString &text, int row, int col);

    QString m_deviceId;
    bool m_enabled = false;
    int m_speed = 5;

    QLabel *m_info = nullptr;
    QSlider *m_speedSlider = nullptr;
    QList<QPushButton *> m_buttons;
};

} // namespace zp

#endif // ZP_PTZ_WIDGET_H
