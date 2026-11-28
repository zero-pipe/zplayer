#include "ui/ptz_panel.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace zp {

PtzPanel::PtzPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RightPanel"));
    setMinimumWidth(280);
    setMaximumWidth(360);

    const ThemeTokens t = ThemeManager::instance().tokens();

    // ---- Header: 云台控制 ----
    auto *head = new QWidget(this);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(16, 0, 16, 0);
    auto *title = new QLabel(tr("云台控制"), head);
    title->setObjectName(QStringLiteral("PanelTitle"));
    hl->addWidget(title);
    hl->addStretch(1);
    head->setFixedHeight(52);
    head->setStyleSheet(QStringLiteral("border-bottom:1px solid %1;").arg(t.divider.name()));

    // ---- Target line (hidden until a channel is selected) ----
    m_target = new QLabel(this);
    m_target->setObjectName(QStringLiteral("TargetLabel"));
    m_target->setWordWrap(true);
    m_target->setContentsMargins(16, 10, 16, 10);
    m_target->setVisible(false);

    // 能力说明条：通道不支持 PTZ 时显示内联提示并禁用控制区，
    // 取代生硬的悬浮错误提示。
    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("PtzNotice"));
    m_notice->setContentsMargins(16, 8, 16, 8);
    m_notice->setWordWrap(true);
    m_notice->setVisible(false);

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
    vl->addWidget(head);
    vl->addWidget(m_target);
    vl->addWidget(m_notice);

    auto *body = new QWidget(this);
    auto *bv = new QVBoxLayout(body);
    bv->setContentsMargins(16, 14, 16, 16);
    bv->setSpacing(12);

    // ================= Direction pad + zoom column =================
    auto *ctlRow = new QHBoxLayout();
    ctlRow->setSpacing(16);

    // 3x3 directional grid
    m_dpadHost = new QWidget(body);
    auto *dp = new QGridLayout(m_dpadHost);
    dp->setContentsMargins(0, 0, 0, 0);
    dp->setSpacing(6);

    using T = PtzCommand::Type;
    const QColor ic = t.text2;
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(7, ic), T::UpLeft,    tr("左上")), 0, 0);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(0, ic), T::Up,        tr("上")),   0, 1);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(1, ic), T::UpRight,   tr("右上")), 0, 2);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(6, ic), T::Left,      tr("左")),   1, 0);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzStop(ic),     T::Stop,      tr("停止")), 1, 1);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(2, ic), T::Right,     tr("右")),   1, 2);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(5, ic), T::DownLeft,  tr("左下")), 2, 0);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(4, ic), T::Down,      tr("下")),   2, 1);
    dp->addWidget(makeDirButton(m_dpadHost, icons::ptzArrow(3, ic), T::DownRight, tr("右下")), 2, 2);
    ctlRow->addWidget(m_dpadHost);

    // Zoom column (+ / -), top-aligned with the d-pad. The magnifier disc icons
    // make the meaning clear without a text label.
    m_zoomHost = new QWidget(body);
    auto *zv = new QVBoxLayout(m_zoomHost);
    zv->setContentsMargins(0, 0, 0, 0);
    zv->setSpacing(6);
    zv->addWidget(makeDirButton(m_zoomHost, icons::ptzZoom(true, ic),  T::ZoomIn,  tr("放大")));
    zv->addWidget(makeDirButton(m_zoomHost, icons::ptzZoom(false, ic), T::ZoomOut, tr("缩小")));
    zv->addStretch(1);
    ctlRow->addWidget(m_zoomHost);

    bv->addLayout(ctlRow);

    // ================= Speed =================
    m_speedHost = new QWidget(body);
    auto *sv = new QVBoxLayout(m_speedHost);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(6);
    auto *speedTitle = new QLabel(tr("云台速度"), m_speedHost);
    speedTitle->setObjectName(QStringLiteral("SectionTitle"));
    sv->addWidget(speedTitle);
    m_speedSlider = new QSlider(Qt::Horizontal, m_speedHost);
    m_speedSlider->setRange(1, 10);
    m_speedSlider->setValue(m_baseSpeed);
    m_speedValue = new QLabel(tr("%1 级").arg(m_baseSpeed), m_speedHost);
    m_speedValue->setObjectName(QStringLiteral("SpeedValue"));
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
        m_baseSpeed = v;
        m_speedValue->setText(tr("%1 级").arg(v));
    });
    auto *speedRow = new QHBoxLayout();
    speedRow->addWidget(m_speedSlider, 1);
    speedRow->addWidget(m_speedValue);
    sv->addLayout(speedRow);
    bv->addWidget(m_speedHost);

    // ================= Tabs: 预置位 / 巡航 / 守望 =================
    auto *tabBar = new QTabBar(body);
    tabBar->setObjectName(QStringLiteral("PtzTabs"));
    tabBar->addTab(tr("预置位"));
    tabBar->addTab(tr("巡航"));
    tabBar->addTab(tr("守望"));
    tabBar->setExpanding(false);
    bv->addWidget(tabBar);

    m_tabs = new QStackedWidget(body);

    // --- Presets page ---
    auto *presetPage = new QWidget(m_tabs);
    auto *pp = new QVBoxLayout(presetPage);
    pp->setContentsMargins(0, 4, 0, 0);
    pp->setSpacing(8);

    auto *presetTools = new QHBoxLayout();
    auto *addBtn = new QToolButton(presetPage);
    addBtn->setObjectName(QStringLiteral("GhostBtn"));
    addBtn->setText(tr("＋ 新增"));
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QToolButton::clicked, this, [this]() {
        if (m_ptzEnabled && m_channel.isValid()) {
            emit presetAdd(m_channel, nextPresetId());
        }
    });
    auto *delBtn = new QToolButton(presetPage);
    delBtn->setObjectName(QStringLiteral("GhostBtn"));
    delBtn->setText(tr("删除"));
    delBtn->setToolTip(tr("删除选中的预置位"));
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setEnabled(false);
    presetTools->addWidget(addBtn);
    presetTools->addWidget(delBtn);
    presetTools->addStretch(1);
    pp->addLayout(presetTools);

    m_presetList = new QListWidget(presetPage);
    m_presetList->setObjectName(QStringLiteral("PresetList"));
    connect(m_presetList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        if (it && m_ptzEnabled && m_channel.isValid()) {
            emit presetCall(m_channel, it->data(Qt::UserRole).toInt());
        }
    });
    connect(m_presetList, &QListWidget::itemSelectionChanged, this, [this, delBtn]() {
        delBtn->setEnabled(m_ptzEnabled && m_presetList->currentItem() != nullptr);
    });
    connect(delBtn, &QToolButton::clicked, this, [this]() {
        if (auto *it = m_presetList->currentItem()) {
            if (m_ptzEnabled && m_channel.isValid()) {
                emit presetDelete(m_channel, it->data(Qt::UserRole).toInt());
            }
        }
    });
    pp->addWidget(m_presetList, 1);
    m_tabs->addWidget(presetPage);

    // --- Cruise / Guard placeholders ---
    auto makePlaceholder = [this](const QString &text) {
        auto *w = new QWidget(m_tabs);
        auto *l = new QVBoxLayout(w);
        l->addStretch(1);
        auto *lab = new QLabel(text, w);
        lab->setAlignment(Qt::AlignCenter);
        lab->setWordWrap(true);
        lab->setObjectName(QStringLiteral("VsHint"));
        l->addWidget(lab);
        l->addStretch(1);
        return w;
    };
    m_tabs->addWidget(makePlaceholder(tr("巡航功能暂未接入平台")));
    m_tabs->addWidget(makePlaceholder(tr("守望功能暂未接入平台")));

    connect(tabBar, &QTabBar::currentChanged, m_tabs, &QStackedWidget::setCurrentIndex);
    bv->addWidget(m_tabs, 1);

    vl->addWidget(body, 1);

    updateEnabled();
}

QToolButton *PtzPanel::makeDirButton(QWidget *parent, const QPixmap &icon,
                                     PtzCommand::Type type, const QString &tip)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("PtzDirBtn"));
    b->setIcon(QIcon(icon));
    b->setIconSize(icon.size() / icon.devicePixelRatio());
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setAutoRepeat(false);
    // Press-and-hold to move; release stops.
    connect(b, &QToolButton::pressed, this, [this, type]() { emitDir(type); });
    connect(b, &QToolButton::released, this, [this, type]() {
        if (type != PtzCommand::Stop) {
            emitDir(PtzCommand::Stop);
        }
    });
    return b;
}

void PtzPanel::emitDir(PtzCommand::Type type)
{
    if (!m_ptzEnabled || !m_channel.isValid()) {
        return;
    }
    PtzCommand c;
    c.type = type;
    c.speed = m_baseSpeed;
    emit commandIssued(m_channel, c);
}

int PtzPanel::nextPresetId() const
{
    int maxId = 0;
    for (const PresetInfo &p : m_presets) {
        maxId = qMax(maxId, p.id);
    }
    return maxId + 1;
}

void PtzPanel::setTarget(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    m_channel = ch;
    m_name = name;
    m_ptzEnabled = ch.isValid() && ptzCapable;

    const ThemeTokens t = ThemeManager::instance().tokens();
    if (!ch.isValid()) {
        m_target->setText(QString());
        m_target->setVisible(false);
        m_notice->setText(tr("请先选择一路通道，再使用云台控制"));
        m_notice->setVisible(true);
    } else if (ptzCapable) {
        m_target->setText(name.isEmpty() ? ch.channelId : name);
        m_target->setStyleSheet(QStringLiteral("color:%1;font-size:12.5px;font-weight:600;").arg(t.accent.name()));
        m_target->setVisible(true);
        m_notice->setVisible(false);
    } else {
        m_target->setText(name.isEmpty() ? ch.channelId : name);
        m_target->setStyleSheet(QStringLiteral("color:%1;font-size:12px;").arg(t.text2.name()));
        m_target->setVisible(true);
        m_notice->setText(tr("当前通道不支持 PTZ，请换用支持云台的通道"));
        m_notice->setVisible(true);
    }
    updateEnabled();

    // Refresh the preset list for the newly selected channel.
    m_presets.clear();
    refreshPresets();
    if (m_ptzEnabled && m_channel.isValid()) {
        emit presetQuery(m_channel);
    }
}

void PtzPanel::setPresets(const QList<PresetInfo> &presets)
{
    m_presets = presets;
    refreshPresets();
}

void PtzPanel::refreshPresets()
{
    if (!m_presetList) {
        return;
    }
    m_presetList->clear();
    for (const PresetInfo &p : m_presets) {
        auto *it = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(p.id, 2, 10, QLatin1Char('0')).arg(p.name), m_presetList);
        it->setData(Qt::UserRole, p.id);
    }
}

void PtzPanel::updateEnabled()
{
    if (m_dpadHost) m_dpadHost->setEnabled(m_ptzEnabled);
    if (m_zoomHost) m_zoomHost->setEnabled(m_ptzEnabled);
    if (m_speedHost) m_speedHost->setEnabled(m_ptzEnabled);
    if (m_tabs) m_tabs->setEnabled(m_ptzEnabled);
}

} // namespace zp
