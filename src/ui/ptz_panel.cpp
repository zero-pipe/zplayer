#include "ui/ptz_panel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace zp {

namespace {
QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("SectionTitle"));
    return l;
}
} // namespace

PtzPanel::PtzPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightPanel"));

    auto *header = new QLabel(tr("控制面板"), this);
    header->setObjectName(QStringLiteral("PanelHeader"));

    m_target = new QLabel(tr("未选择通道"), this);
    m_target->setObjectName(QStringLiteral("TargetLabel"));
    m_target->setWordWrap(true);

    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("ControlTabs"));

    // ---------------- PTZ tab ----------------
    m_ptzSection = new QWidget(tabs);
    auto *pv = new QVBoxLayout(m_ptzSection);
    pv->setContentsMargins(4, 12, 4, 4);
    pv->setSpacing(10);

    auto *pad = new QGridLayout();
    pad->setSpacing(8);
    pad->addWidget(makeDirButton(QString::fromUtf8("↖"), tr("左上"), PtzCommand::UpLeft), 0, 0);
    pad->addWidget(makeDirButton(QString::fromUtf8("▲"), tr("上"), PtzCommand::Up), 0, 1);
    pad->addWidget(makeDirButton(QString::fromUtf8("↗"), tr("右上"), PtzCommand::UpRight), 0, 2);
    pad->addWidget(makeDirButton(QString::fromUtf8("◀"), tr("左"), PtzCommand::Left), 1, 0);

    auto *stopBtn = new QToolButton(m_ptzSection);
    stopBtn->setObjectName(QStringLiteral("PtzCenter"));
    stopBtn->setText(tr("停"));
    stopBtn->setCursor(Qt::PointingHandCursor);
    connect(stopBtn, &QToolButton::clicked, this, [this]() { send(PtzCommand::Stop); });
    m_dirButtons.append(stopBtn);
    pad->addWidget(stopBtn, 1, 1);

    pad->addWidget(makeDirButton(QString::fromUtf8("▶"), tr("右"), PtzCommand::Right), 1, 2);
    pad->addWidget(makeDirButton(QString::fromUtf8("↙"), tr("左下"), PtzCommand::DownLeft), 2, 0);
    pad->addWidget(makeDirButton(QString::fromUtf8("▼"), tr("下"), PtzCommand::Down), 2, 1);
    pad->addWidget(makeDirButton(QString::fromUtf8("↘"), tr("右下"), PtzCommand::DownRight), 2, 2);
    pv->addLayout(pad);

    // Zoom row
    auto *zoomRow = new QHBoxLayout();
    zoomRow->setSpacing(8);
    auto *zoomIn = new QToolButton(m_ptzSection);
    zoomIn->setObjectName(QStringLiteral("PtzBtn"));
    zoomIn->setText(tr("变倍 +"));
    zoomIn->setToolTip(tr("放大"));
    zoomIn->setCursor(Qt::PointingHandCursor);
    zoomIn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    zoomIn->setMinimumHeight(38);
    connect(zoomIn, &QToolButton::pressed, this, [this]() { send(PtzCommand::ZoomIn); });
    connect(zoomIn, &QToolButton::released, this, [this]() { send(PtzCommand::Stop); });
    m_dirButtons.append(zoomIn);

    auto *zoomOut = new QToolButton(m_ptzSection);
    zoomOut->setObjectName(QStringLiteral("PtzBtn"));
    zoomOut->setText(tr("变倍 −"));
    zoomOut->setToolTip(tr("缩小"));
    zoomOut->setCursor(Qt::PointingHandCursor);
    zoomOut->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    zoomOut->setMinimumHeight(38);
    connect(zoomOut, &QToolButton::pressed, this, [this]() { send(PtzCommand::ZoomOut); });
    connect(zoomOut, &QToolButton::released, this, [this]() { send(PtzCommand::Stop); });
    m_dirButtons.append(zoomOut);

    zoomRow->addWidget(zoomIn, 1);
    zoomRow->addWidget(zoomOut, 1);
    pv->addLayout(zoomRow);

    // Speed
    pv->addWidget(sectionTitle(tr("速度"), m_ptzSection));
    auto *speedRow = new QHBoxLayout();
    m_speedSlider = new QSlider(Qt::Horizontal, m_ptzSection);
    m_speedSlider->setRange(1, 10);
    m_speedSlider->setValue(m_speed);
    m_speedValue = new QLabel(QString::number(m_speed), m_ptzSection);
    m_speedValue->setObjectName(QStringLiteral("SpeedValue"));
    m_speedValue->setFixedWidth(20);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_speed = v;
        m_speedValue->setText(QString::number(v));
    });
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);
    pv->addLayout(speedRow);

    pv->addStretch(1);
    tabs->addTab(m_ptzSection, tr("云台"));

    // ---------------- Presets tab ----------------
    auto *presetWidget = new QWidget(tabs);
    auto *prv = new QVBoxLayout(presetWidget);
    prv->setContentsMargins(4, 12, 4, 4);
    prv->setSpacing(8);

    m_presetList = new QListWidget(presetWidget);
    m_presetList->setStyleSheet(QStringLiteral(
        "QListWidget{background:#0E141B;border:1px solid #232D3A;border-radius:8px;padding:4px;}"
        "QListWidget::item{padding:7px 8px;border-radius:6px;color:#C7D2DE;}"
        "QListWidget::item:hover{background:#18202B;}"
        "QListWidget::item:selected{background:rgba(47,129,247,0.20);color:#fff;}"));
    prv->addWidget(m_presetList, 1);

    auto *btnRow = new QHBoxLayout();
    auto *createBtn = new QPushButton(tr("新增"), presetWidget);
    createBtn->setObjectName(QStringLiteral("GhostBtn"));
    createBtn->setCursor(Qt::PointingHandCursor);
    auto *delBtn = new QPushButton(tr("删除"), presetWidget);
    delBtn->setObjectName(QStringLiteral("GhostBtn"));
    delBtn->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(createBtn);
    btnRow->addWidget(delBtn);
    prv->addLayout(btnRow);

    connect(m_presetList, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        if (m_channel.isValid() && it) {
            emit callPresetRequested(m_channel, it->data(Qt::UserRole).toInt());
        }
    });
    connect(m_presetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (m_channel.isValid() && it) {
            emit callPresetRequested(m_channel, it->data(Qt::UserRole).toInt());
        }
    });
    connect(createBtn, &QPushButton::clicked, this, [this]() {
        if (!m_channel.isValid()) return;
        // Create into the first unused slot id (1..255).
        int nextId = 1;
        for (int i = 0; i < m_presetList->count(); ++i) {
            nextId = qMax(nextId, m_presetList->item(i)->data(Qt::UserRole).toInt() + 1);
        }
        emit addPresetRequested(m_channel, qMin(nextId, 255));
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *it = m_presetList->currentItem();
        if (m_channel.isValid() && it) {
            emit deletePresetRequested(m_channel, it->data(Qt::UserRole).toInt());
        }
    });

    tabs->addTab(presetWidget, tr("预置位"));

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(12, 14, 12, 12);
    vl->setSpacing(10);
    vl->addWidget(header);
    vl->addWidget(m_target);
    vl->addWidget(tabs, 1);

    updateEnabled();
}

QToolButton *PtzPanel::makeDirButton(const QString &glyph, const QString &tip,
                                    PtzCommand::Type type)
{
    auto *btn = new QToolButton(m_ptzSection);
    btn->setObjectName(QStringLiteral("PtzBtn"));
    btn->setText(glyph);
    btn->setToolTip(tip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn->setMinimumHeight(38);
    connect(btn, &QToolButton::pressed, this, [this, type]() { send(type); });
    connect(btn, &QToolButton::released, this, [this]() { send(PtzCommand::Stop); });
    m_dirButtons.append(btn);
    return btn;
}

void PtzPanel::setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    m_channel = ch;
    m_name = name;
    m_ptzEnabled = ch.isValid() && ptzCapable;

    if (!ch.isValid()) {
        m_target->setText(tr("未选择通道"));
        m_target->setProperty("state", QStringLiteral("none"));
    } else if (ptzCapable) {
        m_target->setText(tr("通道：%1").arg(name.isEmpty() ? ch.channelId : name));
        m_target->setProperty("state", QStringLiteral("ok"));
    } else {
        m_target->setText(tr("%1 · 不支持云台").arg(name.isEmpty() ? ch.channelId : name));
        m_target->setProperty("state", QStringLiteral("bad"));
    }
    m_target->style()->unpolish(m_target);
    m_target->style()->polish(m_target);

    if (ch.isValid()) {
        emit queryPresetsRequested(ch);
    } else {
        m_presetList->clear();
    }
    updateEnabled();
}

void PtzPanel::setPresets(const QList<PresetInfo> &presets)
{
    m_presetList->clear();
    for (const PresetInfo &p : presets) {
        auto *item = new QListWidgetItem(p.name, m_presetList);
        item->setData(Qt::UserRole, p.id);
        item->setToolTip(tr("双击调用 · ID %1").arg(p.id));
    }
    if (presets.isEmpty()) {
        auto *item = new QListWidgetItem(tr("暂无预置位"), m_presetList);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor(0x5F, 0x6B, 0x7A));
    }
}

void PtzPanel::updateEnabled()
{
    for (QToolButton *b : m_dirButtons) {
        b->setEnabled(m_ptzEnabled);
    }
    m_speedSlider->setEnabled(m_ptzEnabled);
}

void PtzPanel::send(PtzCommand::Type type)
{
    if (!m_ptzEnabled || !m_channel.isValid()) {
        return;
    }
    PtzCommand cmd;
    cmd.type = type;
    cmd.speed = m_speed;
    emit commandIssued(m_channel, cmd);
}

void PtzPanel::refreshPresets()
{
    if (m_channel.isValid()) {
        emit queryPresetsRequested(m_channel);
    }
}

} // namespace zp