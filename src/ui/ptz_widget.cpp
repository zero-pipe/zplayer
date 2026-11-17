#include "ui/ptz_widget.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

namespace zp {

PtzWidget::PtzWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *title = new QLabel(tr("云台控制"), this);
    title->setStyleSheet(QStringLiteral("font-weight:bold;"));

    m_info = new QLabel(tr("未选择通道"), this);
    m_info->setStyleSheet(QStringLiteral("color:#9e9e9e;"));

    auto *grid = new QGridLayout();
    grid->setSpacing(6);

    auto addButton = [this, grid](PtzCommand::Type type, const QString &text, int row, int col) {
        auto *btn = new QPushButton(text, this);
        btn->setMinimumSize(48, 36);
        m_buttons << btn;
        grid->addWidget(btn, row, col);

        if (type == PtzCommand::Up || type == PtzCommand::Down
                || type == PtzCommand::Left || type == PtzCommand::Right) {
            connect(btn, &QPushButton::pressed, this, [this, type]() { send(type); });
            connect(btn, &QPushButton::released, this, [this]() { send(PtzCommand::Stop); });
        } else {
            connect(btn, &QPushButton::clicked, this, [this, type]() { send(type); });
        }
    };

    addButton(PtzCommand::Up, QStringLiteral("▲"), 0, 1);
    addButton(PtzCommand::Left, QStringLiteral("◀"), 1, 0);
    addButton(PtzCommand::Stop, tr("停"), 1, 1);
    addButton(PtzCommand::Right, QStringLiteral("▶"), 1, 2);
    addButton(PtzCommand::Down, QStringLiteral("▼"), 2, 1);
    addButton(PtzCommand::ZoomIn, tr("变倍+"), 3, 0);
    addButton(PtzCommand::ZoomOut, tr("变倍−"), 3, 2);

    auto *speedLabel = new QLabel(tr("速度"), this);
    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(1, 10);
    m_speedSlider->setValue(m_speed);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) { m_speed = v; });

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->setSpacing(8);
    vl->addWidget(title);
    vl->addWidget(m_info);
    vl->addLayout(grid);
    vl->addSpacing(6);
    vl->addWidget(speedLabel);
    vl->addWidget(m_speedSlider);
    vl->addStretch(1);

    setTarget(QString(), false);
}

void PtzWidget::setTarget(const QString &deviceId, bool ptzCapable)
{
    m_deviceId = deviceId;
    m_enabled = !deviceId.isEmpty() && ptzCapable;

    if (deviceId.isEmpty()) {
        m_info->setText(tr("未选择通道"));
        m_info->setStyleSheet(QStringLiteral("color:#9e9e9e;"));
    } else if (ptzCapable) {
        m_info->setText(tr("通道: %1").arg(deviceId));
        m_info->setStyleSheet(QStringLiteral("color:#4caf50;"));
    } else {
        m_info->setText(tr("通道 %1 不支持云台").arg(deviceId));
        m_info->setStyleSheet(QStringLiteral("color:#f44336;"));
    }

    for (QPushButton *btn : m_buttons) {
        btn->setEnabled(m_enabled);
    }
    m_speedSlider->setEnabled(m_enabled);
}

void PtzWidget::send(PtzCommand::Type type)
{
    if (!m_enabled || m_deviceId.isEmpty()) {
        return;
    }
    PtzCommand cmd;
    cmd.type = type;
    cmd.speed = m_speed;
    emit commandIssued(m_deviceId, cmd);
}

} // namespace zp
