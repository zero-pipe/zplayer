#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/device_tree_view.h"
#include "ui/ptz_panel.h"
#include "ui/toast.h"
#include "ui/icons.h"
#include "player/video_grid.h"

#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace zp {

namespace {
const char *kLayoutNames[] = {"1×1", "2×2", "3×3", "4×4", "1+5"};
const VideoGrid::Layout kLayoutKinds[] = {
    VideoGrid::L1x1, VideoGrid::L2x2, VideoGrid::L3x3, VideoGrid::L4x4, VideoGrid::L1p5
};
} // namespace

PlayerWindow::PlayerWindow(IPlatformApi *api, const Session &session, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_session(session)
{
    setObjectName(QStringLiteral("PlayerWindow"));
    setWindowTitle(tr("ZeroPlayer 安防客户端"));
    resize(1440, 880);
    setMinimumSize(1024, 640);

    buildUi();

    connect(m_api, &IPlatformApi::childrenReady, this, &PlayerWindow::onChildrenReady);
    connect(m_api, &IPlatformApi::playStarted, this, &PlayerWindow::onPlayStarted);
    connect(m_api, &IPlatformApi::streamFailed, this, &PlayerWindow::onStreamFailed);
    connect(m_api, &IPlatformApi::ptzAcknowledged, this, &PlayerWindow::onPtzAcknowledged);
    connect(m_api, &IPlatformApi::presetsReady, this, &PlayerWindow::onPresetsReady);
    connect(m_api, &IPlatformApi::sessionExpired, this, &PlayerWindow::onSessionExpired);
    connect(m_api, &IPlatformApi::errorOccurred, this, &PlayerWindow::onError);

    // Initial catalog load + restore last layout.
    const int savedLayout = SessionManager::instance().lastLayout();
    m_grid->setLayoutKind(kLayoutKinds[qBound(0, savedLayout, 4)]);
    setActiveLayoutButton(qBound(0, savedLayout, 4));

    toastInfo(tr("正在加载组织目录…"));
    m_api->fetchChildren(QString());
}

PlayerWindow::~PlayerWindow()
{
    if (m_grid) {
        m_grid->stopAll();
    }
}

void PlayerWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("RootArea"));
    setCentralWidget(root);

    auto *rootLay = new QVBoxLayout(root);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ---------------- Top bar ----------------
    auto *topBar = new QWidget(root);
    topBar->setObjectName(QStringLiteral("TopBar"));
    topBar->setFixedHeight(56);
    auto *tb = new QHBoxLayout(topBar);
    tb->setContentsMargins(16, 0, 16, 0);
    tb->setSpacing(12);

    auto *logo = new QLabel(topBar);
    logo->setPixmap(icons::appLogo(28));
    logo->setFixedSize(28, 28);
    auto *appName = new QLabel(QStringLiteral("ZeroPlayer"), topBar);
    appName->setObjectName(QStringLiteral("AppName"));
    tb->addWidget(logo);
    tb->addWidget(appName);

    tb->addSpacing(6);
    m_groupChip = new QLabel(tr("根资源组"), topBar);
    m_groupChip->setObjectName(QStringLiteral("GroupChip"));
    tb->addWidget(m_groupChip);

    tb->addStretch(1);

    m_search = new QLineEdit(topBar);
    m_search->setObjectName(QStringLiteral("SearchBox"));
    m_search->setPlaceholderText(tr("搜索通道…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(200);
    tb->addWidget(m_search);

    tb->addSpacing(6);

    // Layout segmented control
    m_layoutGroup = new QButtonGroup(this);
    m_layoutGroup->setExclusive(true);
    auto *layoutRow = new QHBoxLayout();
    layoutRow->setSpacing(4);
    for (int i = 0; i < 5; ++i) {
        auto *b = new QToolButton(topBar);
        b->setObjectName(QStringLiteral("LayoutBtn"));
        b->setText(QLatin1String(kLayoutNames[i]));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tr("分屏布局 %1").arg(QLatin1String(kLayoutNames[i])));
        m_layoutGroup->addButton(b, i);
        layoutRow->addWidget(b);
    }
    connect(m_layoutGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &PlayerWindow::onLayoutButtonClicked);
    tb->addLayout(layoutRow);

    tb->addSpacing(6);

    auto *userBtn = new QToolButton(topBar);
    userBtn->setObjectName(QStringLiteral("UserBtn"));
    userBtn->setText(QString::fromUtf8("👤 %1").arg(m_session.username.isEmpty()
                                                ? QStringLiteral("user")
                                                : m_session.username));
    userBtn->setCursor(Qt::PointingHandCursor);
    userBtn->setPopupMode(QToolButton::InstantPopup);
    auto *userMenu = new QMenu(userBtn);
    auto *logoutAct = userMenu->addAction(tr("退出登录"));
    connect(logoutAct, &QAction::triggered, this, [this]() {
        if (m_grid) m_grid->stopAll();
        emit logoutRequested();
    });
    userBtn->setMenu(userMenu);
    tb->addWidget(userBtn);

    rootLay->addWidget(topBar);

    // ---------------- Body: 3 columns ----------------
    auto *body = new QWidget(root);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_tree = new DeviceTreeView(body);
    m_tree->setMinimumWidth(240);
    m_tree->setMaximumWidth(420);

    m_grid = new VideoGrid(body);

    m_ptz = new PtzPanel(body);
    m_ptz->setMinimumWidth(260);
    m_ptz->setMaximumWidth(360);

    auto *splitter = new QSplitter(Qt::Horizontal, body);
    splitter->setObjectName(QStringLiteral("BodySplitter"));
    splitter->setHandleWidth(1);
    splitter->addWidget(m_tree);
    splitter->addWidget(m_grid);
    splitter->addWidget(m_ptz);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({280, 880, 300});

    bodyLay->addWidget(splitter);
    rootLay->addWidget(body, 1);

    // ---------------- Toast ----------------
    m_toast = new Toast(this);

    // ---------------- Wiring ----------------
    connect(m_tree, &DeviceTreeView::channelActivated, this, &PlayerWindow::onChannelActivated);
    connect(m_tree, &DeviceTreeView::childrenNeeded, this, [this](const QString &parentId) {
        m_api->fetchChildren(parentId);
    });
    connect(m_search, &QLineEdit::textChanged, m_tree, [this](const QString &t) {
        Q_UNUSED(t)
        // The tree filters itself; mirror the text into its line edit.
        if (auto *le = m_tree->findChild<QLineEdit *>(QStringLiteral("SearchBox"))) {
            le->setText(m_search->text());
        }
    });

    connect(m_grid, &VideoGrid::activeChannelChanged, this, &PlayerWindow::onActiveChannelChanged);
    connect(m_grid, &VideoGrid::retryRequested, this, &PlayerWindow::onRetryRequested);
    connect(m_grid, &VideoGrid::maximizeRequested, this, &PlayerWindow::onMaximizeRequested);
    connect(m_grid, &VideoGrid::streamClosed, this, [this](const zp::ChannelRef &ch) {
        m_api->stopPlay(ch);
    });
    connect(m_grid, &VideoGrid::layoutChanged, this, [this](int index) {
        SessionManager::instance().setLastLayout(index);
        setActiveLayoutButton(index);
    });

    connect(m_ptz, &PtzPanel::commandIssued, this, &PlayerWindow::onPtzCommand);
    connect(m_ptz, &PtzPanel::queryPresetsRequested, this, &PlayerWindow::onQueryPresets);
    connect(m_ptz, &PtzPanel::callPresetRequested, this, &PlayerWindow::onCallPreset);
    connect(m_ptz, &PtzPanel::addPresetRequested, this, &PlayerWindow::onAddPreset);
    connect(m_ptz, &PtzPanel::deletePresetRequested, this, &PlayerWindow::onDeletePreset);

    statusBar()->showMessage(tr("已登录：%1").arg(m_session.username));
}

void PlayerWindow::setActiveLayoutButton(int index)
{
    if (QAbstractButton *b = m_layoutGroup->button(index)) {
        b->setChecked(true);
    }
}

void PlayerWindow::onLayoutButtonClicked(int index)
{
    if (index < 0 || index >= 5) {
        return;
    }
    m_grid->setLayoutKind(kLayoutKinds[index]);
    toastInfo(tr("分屏布局：%1").arg(QLatin1String(kLayoutNames[index])));
}

void PlayerWindow::onChildrenReady(const QString &parentId, const QList<DeviceNode> &nodes)
{
    if (parentId.isEmpty()) {
        m_tree->setRootNodes(nodes);
        if (nodes.isEmpty()) {
            toastError(tr("未获取到组织目录"));
        } else {
            toastSuccess(tr("组织目录已加载"));
        }
    } else {
        m_tree->addChildren(parentId, nodes);
    }
}

void PlayerWindow::onChannelActivated(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    if (!ch.isValid()) {
        return;
    }
    m_groupChip->setText(name);
    m_channelNames.insert(ch.key(), name);
    m_channelPtz.insert(ch.key(), ptzCapable);

    if (m_grid->contains(ch)) {
        // Already playing: just focus it.
        m_grid->focusChannel(ch);
        return;
    }

    m_pending = ch;
    m_pendingName = name;
    m_pendingPtz = ptzCapable;
    m_grid->showLoadingPlaceholder(ch, name);
    toastInfo(tr("正在取流：%1").arg(name));
    m_api->startPlay(ch);
}

void PlayerWindow::onPlayStarted(const ChannelRef &ch, const QUrl &url, const QString &transport)
{
    Q_UNUSED(transport)
    const QString name = m_channelNames.value(ch.key(), ch.channelId);
    const bool ptz = m_channelPtz.value(ch.key(), true);
    m_grid->openStream(ch, name, ptz, url);
}

void PlayerWindow::onStreamFailed(const ChannelRef &ch, const QString &message)
{
    m_grid->markFailed(ch, message);
    toastError(tr("播放失败：%1").arg(message));
}

void PlayerWindow::onActiveChannelChanged(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    m_active = ch;
    m_ptz->setTarget(ch, name, ptzCapable);
    if (ch.isValid()) {
        setWindowTitle(tr("ZeroPlayer — %1").arg(name));
        statusBar()->showMessage(tr("当前通道：%1  [%2/%3]").arg(name, ch.deviceId, ch.channelId));
    } else {
        setWindowTitle(tr("ZeroPlayer 安防客户端"));
    }
}

void PlayerWindow::onPtzCommand(const ChannelRef &ch, const PtzCommand &cmd)
{
    if (!ch.isValid()) {
        return;
    }
    m_api->ptzControl(ch, cmd);
}

void PlayerWindow::onPtzAcknowledged(const ChannelRef &, int, int)
{
    // Silent success; PTZ is fire-and-forget with immediate feedback.
}

void PlayerWindow::onQueryPresets(const ChannelRef &ch)
{
    m_api->queryPresets(ch);
}

void PlayerWindow::onPresetsReady(const ChannelRef &ch, const QList<PresetInfo> &presets)
{
    if (ch == m_active) {
        m_ptz->setPresets(presets);
    }
}

void PlayerWindow::onCallPreset(const ChannelRef &ch, int presetId)
{
    m_api->callPreset(ch, presetId);
    toastInfo(tr("调用预置位 %1").arg(presetId));
}

void PlayerWindow::onAddPreset(const ChannelRef &ch, int presetId)
{
    m_api->addPreset(ch, presetId);
    toastInfo(tr("保存预置位 %1").arg(presetId));
}

void PlayerWindow::onDeletePreset(const ChannelRef &ch, int presetId)
{
    m_api->deletePreset(ch, presetId);
    toastInfo(tr("删除预置位 %1").arg(presetId));
}

void PlayerWindow::onMaximizeRequested(const ChannelRef &ch)
{
    m_grid->setSingleChannel(ch);
    setActiveLayoutButton(0);
}

void PlayerWindow::onRetryRequested(const ChannelRef &ch)
{
    m_grid->markConnecting(ch);
    m_api->startPlay(ch);
}

void PlayerWindow::onSessionExpired()
{
    toastError(tr("登录已过期，请重新登录"));
    if (m_grid) m_grid->stopAll();
    emit logoutRequested();
}

void PlayerWindow::onError(const QString &scope, const QString &message)
{
    Q_UNUSED(scope)
    toastError(message);
}

void PlayerWindow::openDirect(const QUrl &url)
{
    if (!url.isValid()) {
        return;
    }
    const QString id = QStringLiteral("_direct");
    ChannelRef ref;
    ref.deviceId = id;
    ref.channelId = url.fileName().isEmpty() ? id : url.fileName();
    const QString name = url.fileName().isEmpty() ? url.toString() : url.fileName();
    m_grid->openStream(ref, name, false, url);
    toastInfo(tr("直接播放：%1").arg(name));
}

void PlayerWindow::fillDemo(const QUrl &url, int count)
{
    for (int i = 0; i < count; ++i) {
        ChannelRef ref;
        ref.deviceId = QStringLiteral("_demo%1").arg(i);
        ref.channelId = QStringLiteral("ch%1").arg(i);
        m_grid->openStream(ref, tr("演示通道 %1").arg(i + 1), true, url);
    }
    toastInfo(tr("已填充 %1 路演示通道").arg(count));
}

void PlayerWindow::setDemoLayout(int index)
{
    onLayoutButtonClicked(qBound(0, index, 4));
}

void PlayerWindow::toastInfo(const QString &text)    { m_toast->showMessage(text, Toast::Info); }
void PlayerWindow::toastSuccess(const QString &text) { m_toast->showMessage(text, Toast::Success); }
void PlayerWindow::toastError(const QString &text)   { m_toast->showMessage(text, Toast::Error); }

} // namespace zp
