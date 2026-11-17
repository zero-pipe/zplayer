#include "ui/ptz_panel.h"
#include "ui/ptz_joystick.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

namespace zp {

PtzPanel::PtzPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightPanel"));
    setMinimumWidth(300);
    setMaximumWidth(360);

    const ThemeTokens t = ThemeManager::instance().tokens();

    // ---- Header: title + reset ----
    auto *head = new QWidget(this);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(16, 0, 16, 0);
    auto *title = new QLabel(tr("云台控制"), head);
    title->setObjectName(QStringLiteral("PanelTitle"));
    auto *tag = new QLabel(QStringLiteral("PTZ"), head);
    tag->setStyleSheet(QStringLiteral("color:%1;background:%2;border-radius:5px;padding:1px 6px;font-size:9.5px;font-weight:700;")
                       .arg(t.accent.name(), t.accentSoft.name(QColor::HexArgb)));
    hl->addWidget(title);
    hl->addWidget(tag);
    hl->addStretch(1);
    head->setFixedHeight(52);
    head->setStyleSheet(QStringLiteral("border-bottom:1px solid %1;").arg(t.divider.name()));

    // ---- Target line ----
    m_target = new QLabel(tr("未选择通道"), this);
    m_target->setObjectName(QStringLiteral("TargetLabel"));
    m_target->setWordWrap(true);
    m_target->setContentsMargins(16, 10, 16, 10);

    // ---- Joystick + readout ----
    auto *stickRow = new QHBoxLayout();
    stickRow->setSpacing(14);

    m_stick = new PtzJoystick(this);
    m_stick->setFixedSize(150, 150);
    stickRow->addWidget(m_stick);

    auto *side = new QVBoxLayout();
    side->setSpacing(8);

    auto mkRow = [&](const QString &k, QLabel **valOut) {
        auto *row = new QHBoxLayout();
        auto *kl = new QLabel(k, this);
        kl->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(t.text2.name()));
        auto *v = new QLabel(QStringLiteral("--"), this);
        v->setStyleSheet(QStringLiteral("color:%1;font-family:monospace;font-weight:600;font-size:12px;").arg(t.text.name()));
        row->addWidget(kl);
        row->addStretch(1);
        row->addWidget(v);
        side->addLayout(row);
        *valOut = v;
    };
    mkRow(tr("水平 Pan"), &m_pan);
    mkRow(tr("俯仰 Tilt"), &m_tilt);
    mkRow(tr("转速"), &m_spd);

    side->addSpacing(4);
    auto *cont = new QLabel(tr("连续模式"), this);
    cont->setAlignment(Qt::AlignCenter);
    cont->setStyleSheet(QStringLiteral("color:%1;border:1px solid %2;background:%3;border-radius:8px;padding:6px;font-size:12px;")
                        .arg(t.accent.name(), t.accent.name(), t.accentSoft.name(QColor::HexArgb)));
    side->addWidget(cont);
    side->addStretch(1);
    stickRow->addLayout(side, 1);

    // ---- Speed ----
    auto *speedTitle = new QLabel(tr("基座速度"), this);
    speedTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(1, 10);
    m_speedSlider->setValue(m_baseSpeed);
    m_speedValue = new QLabel(tr("%1 级").arg(m_baseSpeed), this);
    m_speedValue->setObjectName(QStringLiteral("SpeedValue"));
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_baseSpeed = v;
        m_speedValue->setText(tr("%1 级").arg(v));
    });
    auto *speedRow = new QHBoxLayout();
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(head);
    vl->addWidget(m_target);

    auto *body = new QWidget(this);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(16, 14, 16, 16);
    bv->setSpacing(10);
    auto *stickTitle = new QLabel(tr("无级矢量摇杆"), body);
    stickTitle->setObjectName(QStringLiteral("SectionTitle"));
    bv->addWidget(stickTitle);
    bv->addLayout(stickRow);
    bv->addSpacing(6);
    bv->addWidget(speedTitle);
    bv->addLayout(speedRow);
    bv->addStretch(1);
    vl->addWidget(body, 1);

    connect(m_stick, &PtzJoystick::steer, this, [this](const zp::PtzCommand &cmd) {
        if (!m_ptzEnabled || !m_channel.isValid()) {
            return;
        }
        PtzCommand c = cmd;
        applySpeedToCommand(c);
        emit commandIssued(m_channel, c);
    });
    connect(m_stick, &PtzJoystick::moved, this, [this](int pan, int tilt, int spd) {
        m_pan->setText(pan == 0 ? QStringLiteral("--") : QStringLiteral("%1°").arg(pan));
        m_tilt->setText(tilt == 0 ? QStringLiteral("--") : QStringLiteral("%1°").arg(tilt));
        m_spd->setText(QStringLiteral("%1/8").arg(spd));
    });

    updateEnabled();
}

void PtzPanel::applySpeedToCommand(PtzCommand &cmd) const
{
    if (cmd.type == PtzCommand::Stop) {
        return;
    }
    // Joystick magnitude drives speed; blend with base speed setting so the
    // slider acts as a global cap/scaler.
    const int scaled = qBound(1, (cmd.speed * m_baseSpeed) / 5, 10);
    cmd.speed = scaled;
}

void PtzPanel::setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    m_channel = ch;
    m_name = name;
    m_ptzEnabled = ch.isValid() && ptzCapable;

    const ThemeTokens t = ThemeManager::instance().tokens();
    if (!ch.isValid()) {
        m_target->setText(tr("未选择通道"));
        m_target->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(t.text2.name()));
    } else if (ptzCapable) {
        m_target->setText(name.isEmpty() ? ch.channelId : name);
        m_target->setStyleSheet(QStringLiteral("color:%1;font-size:12.5px;font-weight:600;").arg(t.accent.name()));
    } else {
        m_target->setText(tr("%1 · 不支持云台").arg(name.isEmpty() ? ch.channelId : name));
        m_target->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(t.danger.name()));
    }
    updateEnabled();
}

void PtzPanel::updateEnabled()
{
    m_stick->setEnabled(m_ptzEnabled);
    m_speedSlider->setEnabled(m_ptzEnabled);
}

} // namespace zp
