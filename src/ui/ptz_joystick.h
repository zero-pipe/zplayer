#ifndef ZP_PTZ_JOYSTICK_H
#define ZP_PTZ_JOYSTICK_H

#include <QWidget>
#include "platform/platform_api.h"

namespace zp {

// A circular vector joystick: drag the knob away from center to steer;
// direction is the angle, magnitude is the speed. Emits continuous commands
// while held and a Stop on release. Falls back to disabled when no target.
class PtzJoystick : public QWidget {
    Q_OBJECT
public:
    explicit PtzJoystick(QWidget *parent = nullptr);

    void setEnabled(bool on);            // when false, knob stays centered
    void setMaxSpeed(int maxSpeed);      // maps knob distance to 1..maxSpeed

signals:
    // Emitted repeatedly while dragging: type + speed(1..10)
    void steer(const zp::PtzCommand &cmd);
    // Emitted on drag magnitude/angle change (for live readout).
    void moved(int panDeg, int tiltDeg, int speedLevel);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    void updateFromPos(const QPointF &pos);
    PtzCommand makeCommand(const QPointF &delta) const;

    bool m_enabled = true;
    bool m_dragging = false;
    QPointF m_knob;      // current knob offset from center (px)
    qreal m_radius = 55; // max knob travel

    QTimer *m_repeat = nullptr;
    PtzCommand m_lastCmd;
};

} // namespace zp

#endif // ZP_PTZ_JOYSTICK_H
