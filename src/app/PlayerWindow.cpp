#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/device_tree_view.h"
#include "ui/ptz_panel.h"
#include "ui/toast.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/video_settings_dialog.h"
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
const char *kLayoutNames[] = {"1x1", "2x2", "3x3", "4x4", "1+5"};
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
    auto *appName = new QLabel(QStringLiteral("ZeroEngine"), topBar);
    appName->setObjectName(QStringLiteral("AppName"));
    tb->addWidget(logo);
    tb->addWidget(appName);

    tb->addStretch(1);

    // Status: online channels
    auto *statusLabel = new QLabel(topBar);
    statusLabel->setObjectName(QStringLiteral("TopStatus"));
    statusLabel->setText(QString::fromUtf8("● ") + tr("已连接"));
    statusLabel->setStyleSheet(QStringLiteral("color:%1;font-size:12px;")
                               .arg(ThemeManager::instance().tokens().text2.name()));
    tb->addWidget(statusLabel);
    tb->addSpacing(8);

    // Settings (theme switch + server) gear
    auto *settingsBtn = new QToolButton(topBar);
    settingsBtn->setObjectName(QStringLiteral("IconBtn"));
    settingsBtn->setIcon(QIcon(icons::glyph(icons::Gear, ThemeManager::instance().tokens().text2, 18)));
    settingsBtn->setIconSize(QSize(18, 18));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(tr("设置"));
    settingsBtn->setPopupMode(QToolButton::InstantPopup);
    auto *settingsMenu = new QMenu(settingsBtn);
    auto *videoAct = settingsMenu->addAction(tr("视频设置…"));
    connect(videoAct, &QAction::triggered, this, [this]() {
        VideoSettingsDialog dlg(this);
        dlg.setSettings(m_grid ? m_grid->videoSettings() : VideoSettings());
        connect(&dlg, &VideoSettingsDialog::settingsChanged, this,
                [this](const VideoSettings &s) {
                    if (m_grid) m_grid->setVideoSettings(s);
                });
        dlg.exec();
    });
    auto *themeAct = settingsMenu->addAction(tr("切换主题（深/浅）"));
    connect(themeAct, &QAction::triggered, this, [this]() {
        const QString cur = ThemeManager::instance().presetName();
        const QString next = (cur == QLatin1String("dark")) ? QStringLiteral("light")
                                                            : QStringLiteral("dark");
        ThemeManager::instance().setPreset(next);
        SessionManager::instance().setTheme(next);
    });
    settingsBtn->setMenu(settingsMenu);
    tb->addWidget(settingsBtn);

    tb->addSpacing(4);

    auto *userBtn = new QToolButton(topBar);
    userBtn->setObjectName(QStringLiteral("UserBtn"));
    userBtn->setText(m_session.username.isEmpty() ? QStringLiteral("user")
                                                   : m_session.username);
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

    // ---------------- Workspace toolbar (layout + stream) ----------------
    auto *wsToolbar = new QWidget(root);
    wsToolbar->setObjectName(QStringLiteral("WsToolbar"));
    wsToolbar->setFixedHeight(52);
    auto *wt = new QHBoxLayout(wsToolbar);
    wt->setContentsMargins(14, 0, 14, 0);
    wt->setSpacing(8);

    m_layoutGroup = new QButtonGroup(this);
    m_layoutGroup->setExclusive(true);
    // Clear text labels so the split count is obvious at a glance.
    static const char *kLayoutLabels[] = {"1", "4", "9", "16", "1+5"};
    for (int i = 0; i < 5; ++i) {
        auto *b = new QToolButton(wsToolbar);
        b->setObjectName(QStringLiteral("LayoutBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tr("分屏布局 %1").arg(QLatin1String(kLayoutNames[i])));
        b->setText(QString::fromLatin1(kLayoutLabels[i]));
        m_layoutGroup->addButton(b, i);
        wt->addWidget(b);
    }
    if (auto *b = m_layoutGroup->button(2)) b->setChecked(true);  // default 3x3
    connect(m_layoutGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &PlayerWindow::onLayoutButtonClicked);

    wt->addStretch(1);

    rootLay->addWidget(wsToolbar);

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

    connect(m_grid, &VideoGrid::activeChannelChanged, this, &PlayerWindow::onActiveChannelChanged);
    connect(m_grid, &VideoGrid::retryRequested, this, &PlayerWindow::onRetryRequested);
    connect(m_grid, &VideoGrid::maximizeRequested, this, &PlayerWindow::onMaximizeRequested);
    connect(m_grid, &VideoGrid::snapshotTaken, this,
            [this](const zp::ChannelRef &, const QString &path, bool ok) {
        if (ok) {
            toastSuccess(tr("已抓图：%1").arg(path));
        } else {
            toastError(tr("抓图失败"));
        }
    });
    connect(m_grid, &VideoGrid::streamClosed, this, [this](const zp::ChannelRef &ch) {
        m_api->stopPlay(ch);
    });
    connect(m_grid, &VideoGrid::layoutChanged, this, [this](int index) {
        SessionManager::instance().setLastLayout(index);
        setActiveLayoutButton(index);
    });

    connect(m_ptz, &PtzPanel::commandIssued, this, &PlayerWindow::onPtzCommand);
    connect(m_ptz, &PtzPanel::presetQuery, this, [this](const zp::ChannelRef &ch) {
        m_api->queryPresets(ch);
    });
    connect(m_ptz, &PtzPanel::presetCall, this, [this](const zp::ChannelRef &ch, int id) {
        m_api->callPreset(ch, id);
    });
    connect(m_ptz, &PtzPanel::presetAdd, this, [this](const zp::ChannelRef &ch, int id) {
        m_api->addPreset(ch, id);
    });
    connect(m_ptz, &PtzPanel::presetDelete, this, [this](const zp::ChannelRef &ch, int id) {
        m_api->deletePreset(ch, id);
    });
    connect(m_api, &IPlatformApi::presetsReady, m_ptz, [this](const zp::ChannelRef &ch,
                                                             const QList<zp::PresetInfo> &list) {
        if (ch.key() == m_active.key()) {
            m_ptz->setPresets(list);
        }
    });

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
