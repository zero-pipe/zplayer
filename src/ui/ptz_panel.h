#ifndef ZP_PTZ_PANEL_H
#define ZP_PTZ_PANEL_H

#include <QWidget>
#include "platform/platform_api.h"

QT_BEGIN_NAMESPACE
class QSlider;
class QLabel;
QT_END_NAMESPACE

namespace zp {

class PtzJoystick;

// Right panel: telemetry joystick + speed. Commands go through the platform
// (pass-through). Phase 1 scope: direction + speed only (no presets/lens).
class PtzPanel : public QWidget {
    Q_OBJECT
public:
    explicit PtzPanel(QWidget *parent = nullptr);

    void setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable);

signals:
    void commandIssued(const zp::ChannelRef &ch, const zp::PtzCommand &cmd);

private:
    void applySpeedToCommand(PtzCommand &cmd) const;
    void updateEnabled();

    ChannelRef m_channel;
    QString m_name;
    bool m_ptzEnabled = false;
    int m_baseSpeed = 5;

    QLabel *m_target = nullptr;
    PtzJoystick *m_stick = nullptr;
    QLabel *m_pan = nullptr;
    QLabel *m_tilt = nullptr;
    QLabel *m_spd = nullptr;
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedValue = nullptr;
};

} // namespace zp

#endif // ZP_PTZ_PANEL_H
