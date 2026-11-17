#include "ui/ptz_joystick.h"
#include "ui/theme.h"

#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QtMath>

namespace zp {

namespace {
constexpr int kRepeatMs = 120;   // re-send cadence while held
} // namespace

PtzJoystick::PtzJoystick(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(150, 150);
    setCursor(Qt::OpenHandCursor);
    m_repeat = new QTimer(this);
    m_repeat->setInterval(kRepeatMs);
    connect(m_repeat, &QTimer::timeout, this, [this]() {
        if (m_dragging && m_lastCmd.type != PtzCommand::Stop) {
            emit steer(m_lastCmd);
        }
    });
}

void PtzJoystick::setEnabled(bool on)
{
    m_enabled = on;
    if (!on) {
        m_dragging = false;
        m_knob = QPointF(0, 0);
        m_repeat->stop();
        update();
    }
    QWidget::setEnabled(on);
}

void PtzJoystick::setMaxSpeed(int maxSpeed)
{
    Q_UNUSED(maxSpeed)
}

void PtzJoystick::paintEvent(QPaintEvent *)
{
    const ThemeTokens t = ThemeManager::instance().tokens();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c = rect().center();
    const qreal R = qMin(width(), height()) / 2.0 - 6;
    m_radius = R - 28;

    // Base disc
    QRadialGradient grad(c, R);
    grad.setColorAt(0, t.bgPanel);
    grad.setColorAt(1, t.bgInput);
    p.setBrush(grad);
    p.setPen(QPen(t.border, 1));
    p.drawEllipse(c, R, R);

    // Dashed outer ring + inner ring
    QPen dash(t.borderStrong, 1, Qt::DashLine);
    p.setPen(dash);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, R - 12, R - 12);
    p.setPen(QPen(t.border, 1));
    p.drawEllipse(c, R * 0.42, R * 0.42);

    // Cross lines
    QColor cross = t.border;
    cross.setAlpha(120);
    p.setPen(QPen(cross, 1));
    p.drawLine(QPointF(c.x() - R + 4, c.y()), QPointF(c.x() + R - 4, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - R + 4), QPointF(c.x(), c.y() + R - 4));

    // Labels
    p.setPen(t.muted);
    QFont f = font();
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(c.x() - 20, c.y() - R + 8, 40, 12), Qt::AlignCenter, QStringLiteral("UP"));
    p.drawText(QRectF(c.x() - 20, c.y() + R - 20, 40, 12), Qt::AlignCenter, QStringLiteral("DN"));
    p.drawText(QRectF(c.x() - R + 6, c.y() - 8, 16, 16), Qt::AlignCenter, QStringLiteral("L"));
    p.drawText(QRectF(c.x() + R - 22, c.y() - 8, 16, 16), Qt::AlignCenter, QStringLiteral("R"));

    // Knob
    const QPointF kc = c + m_knob;
    QRadialGradient kg(kc - QPointF(m_knob.x() * 0.1, m_knob.y() * 0.1), 26);
    kg.setColorAt(0, t.accent2);
    kg.setColorAt(1, t.accent);
    p.setPen(Qt::NoPen);
    p.setBrush(kg);
    p.drawEllipse(kc, 21, 21);
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawEllipse(kc, 8, 8);
}

void PtzJoystick::mousePressEvent(QMouseEvent *e)
{
    if (!m_enabled || e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
    updateFromPos(e->localPos());
    m_repeat->start();
}

void PtzJoystick::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        updateFromPos(e->localPos());
    }
}

void PtzJoystick::mouseReleaseEvent(QMouseEvent *e)
{
    Q_UNUSED(e)
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    m_knob = QPointF(0, 0);
    setCursor(Qt::OpenHandCursor);
    m_repeat->stop();
    update();

    PtzCommand stop;
    stop.type = PtzCommand::Stop;
    emit steer(stop);
    emit moved(0, 0, 0);
}

void PtzJoystick::updateFromPos(const QPointF &pos)
{
    const QPointF c = rect().center();
    QPointF delta = pos - c;
    const qreal len = std::hypot(delta.x(), delta.y());

    // Deadzone in the very center.
    if (len < 6) {
        m_knob = QPointF(0, 0);
        m_lastCmd = PtzCommand();
        update();
        emit moved(0, 0, 0);
        return;
    }

    if (len > m_radius) {
        delta *= m_radius / len;
    }
    m_knob = delta;

    m_lastCmd = makeCommand(delta);
    update();
    emit steer(m_lastCmd);

    const int panDeg = int(delta.x() / m_radius * 180.0);
    const int tiltDeg = int(-delta.y() / m_radius * 45.0);
    emit moved(panDeg, tiltDeg, m_lastCmd.speed);
}

PtzCommand PtzJoystick::makeCommand(const QPointF &delta) const
{
    PtzCommand cmd;
    const qreal len = std::hypot(delta.x(), delta.y());
    const qreal mag = qBound(0.0, len / m_radius, 1.0);

    // Speed 1..10 from magnitude (minimum 1 when leaving deadzone).
    cmd.speed = qBound(1, int(std::ceil(mag * 10)), 10);

    // Angle: 0 = up, clockwise. Map to 8 directions.
    qreal ang = std::atan2(delta.x(), -delta.y()) * 180.0 / M_PI; // up=0, right=90
    if (ang < 0) ang += 360;
    int sector = int((ang + 22.5) / 45.0) % 8;
    static const PtzCommand::Type dirs[8] = {
        PtzCommand::Up, PtzCommand::UpRight, PtzCommand::Right, PtzCommand::DownRight,
        PtzCommand::Down, PtzCommand::DownLeft, PtzCommand::Left, PtzCommand::UpLeft
    };
    cmd.type = dirs[sector];
    return cmd;
}

} // namespace zp
