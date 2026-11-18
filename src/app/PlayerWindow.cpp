#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/device_tree_view.h"
#include "ui/ptz_panel.h"
#include "ui/toast.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/settings_dialog.h"
#include "player/video_grid.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizeGrip>
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
    // Frameless: the native title bar is dropped and replaced by the custom bar
    // built in buildUi() (brand + theme/settings/user + window controls).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("Zero AI Player"));
    resize(1440, 880);
    setMinimumSize(1024, 640);
    setAcceptDrops(true);

    // Local on-demand playback entry: Ctrl+O opens a recording. Dropping a file
    // on the window does the same. Both land in the normal grid path, so AI
    // analysis treats a file exactly like a live channel.
    auto *openAct = new QAction(this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("打开视频文件"),
            QString(),
            tr("视频文件 (*.mp4 *.mkv *.ts *.mov *.avi *.flv *.wmv *.m4v);;所有文件 (*.*)"));
        if (!path.isEmpty()) {
            openLocalFile(path);
        }
    });
    addAction(openAct);

    buildUi();

    // Frameless windows lose the native resize border; a floating grip in the
    // bottom-right corner keeps manual resizing available.
    m_sizeGrip = new QSizeGrip(this);
    m_sizeGrip->setFixedSize(16, 16);
    m_sizeGrip->raise();

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
    // 1px inset leaves room for the window's own border (see theme.qss).
    rootLay->setContentsMargins(1, 1, 1, 0);
    rootLay->setSpacing(0);

    // ---------------- Title bar (brand + actions + window controls) ----------------
    auto *topBar = new QWidget(root);
    topBar->setObjectName(QStringLiteral("TitleBar"));
    topBar->setFixedHeight(56);
    m_topBar = topBar;
    topBar->installEventFilter(this);
    auto *tb = new QHBoxLayout(topBar);
    tb->setContentsMargins(16, 0, 8, 0);
    tb->setSpacing(12);

    auto *logo = new QLabel(topBar);
    logo->setPixmap(icons::appLogo(28));
    logo->setFixedSize(28, 28);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *appName = new QLabel(QStringLiteral("Zero AI Player"), topBar);
    appName->setObjectName(QStringLiteral("AppName"));
    appName->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    tb->addWidget(logo);
    tb->addWidget(appName);

    tb->addStretch(1);

    // Theme quick-toggle (sun/moon) — icon button, toggles directly.
    auto *themeBtn = new QToolButton(topBar);
    themeBtn->setObjectName(QStringLiteral("IconBtn"));
    themeBtn->setIconSize(QSize(18, 18));
    themeBtn->setCursor(Qt::PointingHandCursor);
    themeBtn->setToolTip(tr("切换主题（深/浅）"));
    auto refreshThemeIcon = [themeBtn]() {
        const bool dark = ThemeManager::instance().presetName() == QLatin1String("dark");
        themeBtn->setIcon(QIcon(icons::glyph(dark ? icons::Sun : icons::Moon,
                                             ThemeManager::instance().tokens().text2, 18)));
    };
    refreshThemeIcon();
    connect(themeBtn, &QToolButton::clicked, this, [this]() {
        const QString cur = ThemeManager::instance().presetName();
        const QString next = (cur == QLatin1String("dark")) ? QStringLiteral("light")
                                                            : QStringLiteral("dark");
        ThemeManager::instance().setPreset(next);
        SessionManager::instance().setTheme(next);
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            themeBtn, refreshThemeIcon);
    tb->addWidget(themeBtn);
    tb->addSpacing(4);

    // Settings gear — opens the settings window directly (no popup menu).
    auto *settingsBtn = new QToolButton(topBar);
    settingsBtn->setObjectName(QStringLiteral("IconBtn"));
    settingsBtn->setIcon(QIcon(icons::glyph(icons::Gear,
                                            ThemeManager::instance().tokens().text2, 18)));
    settingsBtn->setIconSize(QSize(18, 18));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(tr("设置"));
    connect(settingsBtn, &QToolButton::clicked, this, [this]() {
        SettingsDialog dlg(m_grid ? m_grid->videoSettings() : VideoSettings(),
                           ThemeManager::instance().presetName(), this);
        connect(&dlg, &SettingsDialog::videoSettingsChanged, this,
                [this](const VideoSettings &s) {
                    if (m_grid) m_grid->setVideoSettings(s);
                });
        connect(&dlg, &SettingsDialog::themeChanged, this, [this](const QString &name) {
            ThemeManager::instance().setPreset(name);
            SessionManager::instance().setTheme(name);
        });
        dlg.exec();
    });
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

    tb->addSpacing(6);

    // ---- Window controls: minimize / maximize-restore / close ----
    const ThemeTokens &tk = ThemeManager::instance().tokens();
    auto makeWinBtn = [&](icons::Glyph g, const QString &tip, const char *name) {
        auto *b = new QToolButton(topBar);
        b->setObjectName(QString::fromLatin1(name));
        b->setIcon(QIcon(icons::glyph(g, tk.text2, 14)));
        b->setIconSize(QSize(14, 14));
        b->setFixedSize(44, 32);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        return b;
    };
    auto *minBtn = makeWinBtn(icons::WinMin, tr("最小化"), "WinBtn");
    auto *maxBtn = makeWinBtn(icons::WinMax, tr("最大化"), "WinBtn");
    auto *closeBtn = makeWinBtn(icons::Close, tr("关闭"), "WinClose");
    m_maxBtn = maxBtn;

    connect(minBtn, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maxBtn, &QToolButton::clicked, this, &PlayerWindow::toggleMaximize);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::close);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this, minBtn, maxBtn,
                                                                          closeBtn]() {
        updateWindowButtons();
        const ThemeTokens &t = ThemeManager::instance().tokens();
        minBtn->setIcon(QIcon(icons::glyph(icons::WinMin, t.text2, 14)));
        closeBtn->setIcon(QIcon(icons::glyph(icons::Close, t.text2, 14)));
    });

    tb->addSpacing(4);
    tb->addWidget(minBtn);
    tb->addWidget(maxBtn);
    tb->addWidget(closeBtn);

    rootLay->addWidget(topBar);

    // ---------------- Body: 3 columns ----------------
    auto *body = new QWidget(root);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);

    m_tree = new DeviceTreeView(body);
    m_tree->setMinimumWidth(240);
    m_tree->setMaximumWidth(420);

    // Middle column: a slim split-layout bar above the video grid.
    auto *centerCol = new QWidget(body);
    auto *centerLay = new QVBoxLayout(centerCol);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    auto *layoutBar = new QWidget(centerCol);
    layoutBar->setObjectName(QStringLiteral("LayoutBar"));
    layoutBar->setFixedHeight(44);
    auto *lb = new QHBoxLayout(layoutBar);
    lb->setContentsMargins(12, 0, 12, 0);
    lb->setSpacing(8);

    m_layoutGroup = new QButtonGroup(this);
    m_layoutGroup->setExclusive(true);
    // Clear text labels so the split count is obvious at a glance.
    static const char *kLayoutLabels[] = {"1", "4", "9", "16", "1+5"};
    for (int i = 0; i < 5; ++i) {
        auto *b = new QToolButton(layoutBar);
        b->setObjectName(QStringLiteral("LayoutBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tr("分屏布局 %1").arg(QLatin1String(kLayoutNames[i])));
        b->setText(QString::fromLatin1(kLayoutLabels[i]));
        m_layoutGroup->addButton(b, i);
        lb->addWidget(b);
    }
    if (auto *b = m_layoutGroup->button(2)) b->setChecked(true);  // default 3x3
    connect(m_layoutGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &PlayerWindow::onLayoutButtonClicked);
    lb->addStretch(1);

    m_grid = new VideoGrid(centerCol);
    centerLay->addWidget(layoutBar);
    centerLay->addWidget(m_grid, 1);

    m_ptz = new PtzPanel(body);
    m_ptz->setMinimumWidth(260);
    m_ptz->setMaximumWidth(360);

    // Thin collapse handle docked on the PTZ panel's left edge: click to fold
    // the panel away, click again to bring it back. Stays visible either way.
    auto *ptzHandle = new QToolButton(body);
    ptzHandle->setObjectName(QStringLiteral("PtzHandle"));
    ptzHandle->setCursor(Qt::PointingHandCursor);
    ptzHandle->setFixedWidth(16);
    ptzHandle->setIconSize(QSize(14, 14));
    ptzHandle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto updateHandle = [this, ptzHandle]() {
        const bool shown = m_ptz && m_ptz->isVisible();
        ptzHandle->setIcon(QIcon(icons::chevron(shown,
                             ThemeManager::instance().tokens().text2, 14)));
        ptzHandle->setToolTip(shown ? tr("收起云台面板") : tr("展开云台面板"));
    };
    updateHandle();
    connect(ptzHandle, &QToolButton::clicked, this, [this, updateHandle]() {
        const bool shown = m_ptz && m_ptz->isVisible();
        if (m_ptz) m_ptz->setVisible(!shown);
        updateHandle();
    });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, ptzHandle, updateHandle);

    auto *splitter = new QSplitter(Qt::Horizontal, body);
    splitter->setObjectName(QStringLiteral("BodySplitter"));
    splitter->setHandleWidth(1);
    splitter->addWidget(m_tree);
    splitter->addWidget(centerCol);
    splitter->addWidget(ptzHandle);
    splitter->addWidget(m_ptz);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setStretchFactor(3, 0);
    splitter->setSizes({280, 864, 16, 300});

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

void PlayerWindow::toggleMaximize()
{
    if (isMaximized()) {
        setWindowState(windowState() & ~Qt::WindowMaximized);
    } else {
        m_restoreGeom = geometry();
        showMaximized();
    }
    updateWindowButtons();
}

void PlayerWindow::updateWindowButtons()
{
    if (!m_maxBtn) {
        return;
    }
    const ThemeTokens &t = ThemeManager::instance().tokens();
    m_maxBtn->setIcon(QIcon(icons::glyph(isMaximized() ? icons::WinRestore : icons::WinMax,
                                         t.text2, 14)));
    m_maxBtn->setToolTip(isMaximized() ? tr("向下还原") : tr("最大化"));
}

void PlayerWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_sizeGrip) {
        m_sizeGrip->move(width() - m_sizeGrip->width() - 2,
                         height() - m_sizeGrip->height() - 2);
        m_sizeGrip->setVisible(!isMaximized());
        m_sizeGrip->raise();
    }
}

bool PlayerWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_topBar) {
        return QMainWindow::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) {
            break;
        }
        m_dragging = true;
        m_pressGlobal = me->globalPos();
        m_dragOffset = me->globalPos() - frameGeometry().topLeft();
        return true;
    }
    case QEvent::MouseButtonDblClick: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_dragging = false;
            toggleMaximize();
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (!m_dragging || !(me->buttons() & Qt::LeftButton)) {
            break;
        }
        const QPoint g = me->globalPos();
        if (isMaximized()) {
            // Dragging a maximized window first restores it, keeping the cursor
            // at the same relative spot along the title bar.
            if ((g - m_pressGlobal).manhattanLength() < QApplication::startDragDistance()) {
                return true;
            }
            const QRect ng = m_restoreGeom.isValid() ? m_restoreGeom
                                                     : QRect(QPoint(0, 0), size());
            const qreal rx = width() > 0 ? qreal(m_pressGlobal.x()) / width() : 0.5;
            const qreal ry = m_topBar ? qreal(m_pressGlobal.y() - frameGeometry().top())
                                            / qMax(1, m_topBar->height())
                                      : 0.5;
            setWindowState(windowState() & ~Qt::WindowMaximized);
            resize(ng.size());
            const QPoint tl(g.x() - int(ng.width() * rx),
                            g.y() - int(m_topBar ? m_topBar->height() * ry : 0));
            move(tl);
            m_dragOffset = g - tl;
            updateWindowButtons();
        } else {
            move(g - m_dragOffset);
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        m_dragging = false;
        break;
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
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
        setWindowTitle(QStringLiteral("Zero AI Player — %1").arg(name));
        statusBar()->showMessage(tr("当前通道：%1  [%2/%3]").arg(name, ch.deviceId, ch.channelId));
    } else {
        setWindowTitle(QStringLiteral("Zero AI Player"));
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

void PlayerWindow::openLocalFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        toastError(tr("文件不存在：%1").arg(path));
        return;
    }
    const QString abs = fi.absoluteFilePath();
    ChannelRef ref;
    ref.deviceId = QStringLiteral("_file");
    ref.channelId = abs;   // one tile per distinct file
    if (m_grid->contains(ref)) {
        m_grid->focusChannel(ref);
        return;
    }
    m_grid->openStream(ref, fi.fileName(), false, QUrl::fromLocalFile(abs));
    toastInfo(tr("打开文件：%1").arg(fi.fileName()));
}

void PlayerWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime || !mime->hasUrls()) {
        return;
    }
    for (const QUrl &u : mime->urls()) {
        if (u.isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }
}

void PlayerWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime || !mime->hasUrls()) {
        return;
    }
    for (const QUrl &u : mime->urls()) {
        if (u.isLocalFile()) {
            openLocalFile(u.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
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
