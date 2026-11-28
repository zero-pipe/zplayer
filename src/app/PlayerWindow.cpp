#include "app/PlayerWindow.h"
#include "app/SessionManager.h"
#include "ui/device_tree_view.h"
#include "ui/mode_tab_bar.h"
#include "ui/ptz_panel.h"
#include "ui/toast.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/settings_dialog.h"
#include "ui/evidence_dialog.h"
#include "ui/playback_timeline.h"
#include "ui/ui_kit.h"
#include "player/video_grid.h"
#include "player/video_tile.h"
#include "analysis/analysis_service.h"
#include "analysis/ocr_detector.h"
#include "analysis/yolo_detector.h"
#include "train/dataset_store.h"
#include "train/training_tool_window.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDebug>
#include <QEvent>
#include <QColor>
#include <QFrame>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QDateEdit>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QWidget>
#include <limits>

namespace zp {

namespace {
const char *kLayoutNames[] = {"1x1", "2x2", "3x3", "4x4", "1+5"};
const VideoGrid::Layout kLayoutKinds[] = {
    VideoGrid::L1x1, VideoGrid::L2x2, VideoGrid::L3x3, VideoGrid::L4x4, VideoGrid::L1p5
};
} // namespace

void DockStripButton::paintEvent(QPaintEvent *e)
{
    QToolButton::paintEvent(e);
    if (m_count <= 0) {
        return;
    }
    QPainter p(this);
    p.setPen(ThemeManager::instance().tokens().text2);
    QFont f = font();
    f.setPixelSize(10);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, height() / 2 + 12, width(), 16),
               Qt::AlignHCenter | Qt::AlignTop, QString::number(m_count));
}

PlayerWindow::PlayerWindow(IPlatformApi *api, const Session &session, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_session(session)
{
    setObjectName(QStringLiteral("PlayerWindow"));
    // Frameless: the native title bar is dropped and replaced by the custom bar
    // built in buildUi() (brand + theme/settings/user + window controls).
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("Zeroo Player"));
    // Fit the default size to the actual screen: a hard-coded 1440x880 overflows
    // smaller laptop panels (content gets clipped / panels squeezed together).
    QRect avail = QRect(0, 0, 1440, 880);
    if (QScreen *scr = QGuiApplication::primaryScreen()) {
        avail = scr->availableGeometry();
    }
    const int defW = qBound(960, int(avail.width() * 0.96), 1440);
    const int defH = qBound(600, int(avail.height() * 0.96), 880);
    resize(defW, defH);
    // Never demand more than the screen can give, otherwise the OS clips us.
    setMinimumSize(qMin(900, avail.width()), qMin(560, avail.height()));
    move(avail.center() - QPoint(defW / 2, defH / 2));
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

    auto *settingsAct = new QAction(this);
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settingsAct, &QAction::triggered, this, &PlayerWindow::openSettings);
    addAction(settingsAct);

    // Quick toggle for AI fast-forward on the active tile (mirrors the tile's
    // own AI button). Only meaningful for local / recorded playback.
    auto *aiAct = new QAction(this);
    aiAct->setShortcut(QKeySequence(QStringLiteral("A")));
    connect(aiAct, &QAction::triggered, this, [this]() {
        VideoTile *t = m_grid ? m_grid->activeTile() : nullptr;
        if (!t) {
            return;
        }
        const bool on = !t->aiSkipEnabled();
        t->setAiSkipEnabled(on);
        toastInfo(on ? tr("AI 快放：开（无目标时自动跳过）") : tr("AI 快放：关"));
    });
    addAction(aiAct);

    // 录像回放模式（快捷键切到回放页并按当前通道查询）。客户端才是回放主战场。
    auto *playbackAct = new QAction(this);
    playbackAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(playbackAct, &QAction::triggered, this, [this]() {
        setMode(1);
        queryPlaybackDay();
    });
    addAction(playbackAct);

    buildUi();

    updateSampleBadge();
    // Dev: ZP_MODE=1 opens on the playback page (headless layout screenshots).
    if (qEnvironmentVariableIntValue("ZP_MODE") == 1) {
        QTimer::singleShot(0, this, [this]() { setMode(1); });
    }
    // Dev: ZP_SAMPLETAB=1 opens on the sample tab (headless screenshots).
    if (qEnvironmentVariableIsSet("ZP_SAMPLETAB")) {
        selectSampleTab();
    }
    // Dev: ZP_DOCK_COLLAPSED=1 starts with the tool dock collapsed.
    if (qEnvironmentVariableIsSet("ZP_DOCK_COLLAPSED")) {
        QTimer::singleShot(300, this, &PlayerWindow::collapseToolDock);
    }
    auto *sampleTimer = new QTimer(this);
    sampleTimer->setInterval(4000);
    connect(sampleTimer, &QTimer::timeout, this, &PlayerWindow::updateSampleBadge);
    sampleTimer->start();

    // Dev: ZP_TRAINTOOL=1 opens the training tool shortly after start so it
    // can be exercised headlessly. ZP_TRAINPAGE=<0..5> selects a stage.
    if (qEnvironmentVariableIsSet("ZP_TRAINTOOL")) {
        QTimer::singleShot(1500, this, [this]() {
            openTrainingTool();
            const QString page = qEnvironmentVariable("ZP_TRAINPAGE");
            if (m_trainTool && !page.isEmpty()) {
                m_trainTool->goToStage(page.toInt());
            }
        });
    }

    // Frameless windows lose the native resize border; a floating grip in the
    // bottom-right corner keeps manual resizing available.
    m_sizeGrip = new QSizeGrip(this);
    m_sizeGrip->setFixedSize(16, 16);
    m_sizeGrip->raise();

    // Per-window AI scheduler. Created before any tile exists so every tile
    // created later attaches to it; one worker/analyzer serves all tiles.
    m_analysis = new AnalysisService(this);
    m_grid->setAnalysisService(m_analysis);
    if (IFrameAnalyzer *stats = createDebugStatsAnalyzer()) {
        m_analysis->setAnalyzer(stats);   // ZP_ANALYZE=<file> — pipeline smoke test
    } else {
        // Real detector: YOLOv8n / YOLO11n via ONNX Runtime. Absent a model,
        // analysis stays idle and the UI simply shows no boxes.
        QString modelPath, classesPath, findErr;
        if (findDefaultYoloModel(&modelPath, &classesPath, &findErr)) {
            auto *det = new YoloDetector();
            QString loadErr;
            if (det->load(modelPath, classesPath, &loadErr)) {
                m_detector = det;
                m_analysis->setAnalyzer(det);
                // Tiles use the model's own class list (custom models must not
                // be interpreted as COCO ids).
                m_grid->setClassNames(det->classNames());
                qInfo().noquote() << "[ai] detector ready:" << modelPath
                                  << "backend=" << det->backendName()
                                  << "classes=" << det->classNames().size();
            } else {
                delete det;
                toastError(tr("AI 模型加载失败：%1").arg(loadErr));
            }
        } else {
            qInfo().noquote() << "[ai] detector disabled:" << findErr;
        }
    }

    // OCR (scene text) is optional: present only when the PP-OCR models ship
    // alongside the detector. Tiles enable it per-tile via their 文字 button.
    {
        QString detPath, recPath, dictPath, ocrFindErr;
        if (findDefaultOcrModels(&detPath, &recPath, &dictPath, &ocrFindErr)) {
            auto *ocrDet = new OcrDetector();
            QString loadErr;
            if (ocrDet->load(detPath, recPath, dictPath, &loadErr)) {
                m_ocr = ocrDet;
                m_grid->setOcrAnalyzer(ocrDet);
                qInfo().noquote() << "[ai] ocr ready:" << detPath
                                  << "backend=" << ocrDet->backendName();
            } else {
                delete ocrDet;
                qInfo().noquote() << "[ai] ocr load failed:" << loadErr;
            }
        } else {
            qInfo().noquote() << "[ai] ocr disabled:" << ocrFindErr;
        }
    }

    connect(m_grid, &VideoGrid::evidenceExportRequested, this,
            [this](const zp::ChannelRef &ch) {
                if (VideoTile *tile = m_grid->tileFor(ch)) {
                    openEvidenceDialog(tile);
                }
            });

    // Smart timeline scan finished: summary toast; the segments themselves are
    // painted on the tile's transport slider (click one to jump there).
    connect(m_grid, &VideoGrid::streamScanFinished, this,
            [this](const zp::ChannelRef &, int segments) {
                if (segments > 0) {
                    toastSuccess(tr("扫描完成：发现 %1 个目标时间段，点击进度条色块跳转")
                                     .arg(segments));
                } else {
                    toastInfo(tr("扫描完成：未发现目标片段"));
                }
            });

    // Control-bar toggles report their new state through the window toast.
    connect(m_grid, &VideoGrid::tileStatus, this,
            [this](const zp::ChannelRef &, const QString &text) { toastInfo(text); });

    // Dev: ZP_SETTINGS=1 opens the settings dialog shortly after start, so UI
    // can be captured headlessly (see ZP_SHOT in main.cpp).
    if (!qEnvironmentVariableIsEmpty("ZP_SETTINGS")) {
        QTimer::singleShot(500, this, &PlayerWindow::openSettings);
    }

    connect(m_api, &IPlatformApi::childrenReady, this, &PlayerWindow::onChildrenReady);
    connect(m_api, &IPlatformApi::playStarted, this, &PlayerWindow::onPlayStarted);
    connect(m_api, &IPlatformApi::streamFailed, this, &PlayerWindow::onStreamFailed);
    connect(m_api, &IPlatformApi::recordingsReady, this, &PlayerWindow::onRecordingsReady);
    connect(m_api, &IPlatformApi::playbackStarted, this, &PlayerWindow::onPlaybackStarted);
    connect(m_api, &IPlatformApi::playbackStopped, this, &PlayerWindow::onPlaybackStopped);
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
    if (m_analysis) {
        // Stop the worker (joins the thread) before the detector it may be
        // using is destroyed.
        delete m_analysis;
        m_analysis = nullptr;
    }
    delete m_detector;
    m_detector = nullptr;
    delete m_ocr;
    m_ocr = nullptr;
}

void PlayerWindow::buildUi()
{
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("RootArea"));
    setCentralWidget(root);

    auto *rootLay = new QVBoxLayout(root);
    // 通栏布局零边距、零间距：区域之间只靠 1px 分割线界定，避免夹缝。
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ---------------- Title bar (brand + actions + window controls) ----------------
    auto *topBar = new QWidget(root);
    topBar->setObjectName(QStringLiteral("TitleBar"));
    topBar->setFixedHeight(44);
    m_topBar = topBar;
    topBar->installEventFilter(this);
    auto *tb = new QHBoxLayout(topBar);
    tb->setContentsMargins(16, 0, 8, 0);
    tb->setSpacing(12);

    auto *logo = new QLabel(topBar);
    logo->setPixmap(icons::appLogo(28));
    logo->setFixedSize(28, 28);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *appName = new QLabel(QStringLiteral("Zeroo Player"), topBar);
    appName->setObjectName(QStringLiteral("AppName"));
    appName->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    tb->addWidget(logo);
    tb->addWidget(appName);

    tb->addStretch(1);

    // ---- AI 标注与训练：只保留一个入口按钮，模式切换已下沉到左栏 ----
    {
        auto *aiTab = new QToolButton(topBar);
        aiTab->setObjectName(QStringLiteral("IconBtn"));
        aiTab->setText(tr("🧠 AI 标注与训练"));
        aiTab->setCursor(Qt::PointingHandCursor);
        aiTab->setToolTip(tr("打开 AI 标注与训练工作台"));
        connect(aiTab, &QToolButton::clicked, this, &PlayerWindow::onModeAi);
        tb->addWidget(aiTab);
    }

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
    connect(settingsBtn, &QToolButton::clicked, this, &PlayerWindow::openSettings);
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

    // ===== 左：组织目录（回放模式额外显示查询条件） =====
    auto *leftCol = new QWidget(body);
    leftCol->setObjectName(QStringLiteral("LeftSidebar"));
    leftCol->setMinimumWidth(260);
    leftCol->setMaximumWidth(360);
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);

    // 一级：扁平文字 + 底部下划线 Tab（只切顶层工作模式）
    m_modeTabs = new ModeTabBar(leftCol);
    connect(m_modeTabs, &ModeTabBar::currentModeChanged,
            this, &PlayerWindow::setMode);
    leftLay->addWidget(m_modeTabs);

    // 回放查询条件（默认隐藏，进入回放模式显示）
    m_pbFilter = new QWidget(leftCol);
    m_pbFilter->setObjectName(QStringLiteral("PlaybackFilter"));
    {
        auto *pfLay = new QVBoxLayout(m_pbFilter);
        pfLay->setContentsMargins(10, 10, 10, 8);
        pfLay->setSpacing(6);
        auto *dateRow = new QHBoxLayout;
        auto *dLbl = new QLabel(tr("查询日期"), m_pbFilter);
        dLbl->setObjectName(QStringLiteral("PlaybackFilterLabel"));
        m_pbDate = new QDateEdit(QDate::currentDate(), m_pbFilter);
        m_pbDate->setCalendarPopup(true);
        m_pbDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        dateRow->addWidget(dLbl);
        dateRow->addWidget(m_pbDate, 1);
        pfLay->addLayout(dateRow);

        auto *qBtn = new QPushButton(tr("检索录像"), m_pbFilter);
        qBtn->setObjectName(QStringLiteral("PrimaryButton"));
        qBtn->setCursor(Qt::PointingHandCursor);
        connect(qBtn, &QPushButton::clicked, this, &PlayerWindow::queryPlaybackDay);
        pfLay->addWidget(qBtn);
    }
    m_pbFilter->setVisible(false);

    m_tree = new DeviceTreeView(leftCol);
    connect(m_tree, &DeviceTreeView::currentGroupChanged,
            this, &PlayerWindow::onGroupChanged);
    leftLay->addWidget(m_tree, 1);

    // 回放检索抽屉放在左栏最底部（资源树下方），实时模式隐藏。
    leftLay->addWidget(m_pbFilter);

    // ===== 中：实时预览页 =====
    auto *livePage = new QWidget(body);
    auto *centerLay = new QVBoxLayout(livePage);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    auto *layoutBar = new QWidget(livePage);
    layoutBar->setObjectName(QStringLiteral("LayoutBar"));
    layoutBar->setFixedHeight(42);
    auto *lb = new QHBoxLayout(layoutBar);
    lb->setContentsMargins(12, 0, 12, 0);
    lb->setSpacing(8);

    m_layoutGroup = new QButtonGroup(this);
    m_layoutGroup->setExclusive(true);
    // Split-layout pictograms instead of bare numbers: a 20 px grid silhouette
    // per option reads at a glance and stays legible when the bar is compact.
    static const icons::Glyph kLayoutGlyphs[] = {
        icons::Layout1, icons::Layout2x2, icons::Layout3x3,
        icons::Layout4x4, icons::Layout1p5
    };
    QList<QToolButton *> layoutBtns;
    static const char *kLayoutHints[] = {
        "单屏", "4 分屏", "9 分屏", "16 分屏", "1+5 分屏"
    };
    for (int i = 0; i < 5; ++i) {
        auto *b = new QToolButton(layoutBar);
        b->setObjectName(QStringLiteral("LayoutBtn"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(20, 20));
        // Hover shows the split count briefly, then auto-hides (handled in
        // eventFilter). A persistent tooltip felt noisy over the icons.
        b->setProperty("layoutHint", tr(kLayoutHints[i]));
        b->installEventFilter(this);
        layoutBtns.append(b);
        m_layoutGroup->addButton(b, i);
        lb->addWidget(b);
    }
    auto refreshLayoutIcons = [layoutBtns]() {
        const ThemeTokens &t = ThemeManager::instance().tokens();
        for (int i = 0; i < layoutBtns.size(); ++i) {
            QIcon ic;
            ic.addPixmap(icons::glyph(kLayoutGlyphs[i], t.text2, 20), QIcon::Normal, QIcon::Off);
            ic.addPixmap(icons::glyph(kLayoutGlyphs[i], t.accent, 20), QIcon::Normal, QIcon::On);
            layoutBtns[i]->setIcon(ic);
        }
    };
    refreshLayoutIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, refreshLayoutIcons);
    if (auto *b = m_layoutGroup->button(2)) b->setChecked(true);  // default 3x3
    connect(m_layoutGroup, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &PlayerWindow::onLayoutButtonClicked);
    lb->addStretch(1);

    m_grid = new VideoGrid(livePage);
    centerLay->addWidget(layoutBar);
    centerLay->addWidget(m_grid, 1);

    // 画布工具条：全局抓图 / 批量采集样本 / 画布全屏（设计稿 3.1）
    {
        auto *liveBottom = new QWidget(livePage);
        m_liveBottom = liveBottom;
        liveBottom->setObjectName(QStringLiteral("LayoutBar"));
        liveBottom->setFixedHeight(40);
        auto *lo = new QHBoxLayout(liveBottom);
        lo->setContentsMargins(12, 0, 12, 0);
        lo->setSpacing(8);

        auto mkAct = [&](icons::Glyph g, const QString &tip) {
            auto *b = new QToolButton(liveBottom);
            b->setObjectName(QStringLiteral("IconBtn"));
            b->setIcon(QIcon(icons::glyph(g, ThemeManager::instance().tokens().text2, 16)));
            b->setIconSize(QSize(16, 16));
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(tip);
            lo->addWidget(b);
            return b;
        };
        m_globalSnapBtn = mkAct(icons::Snapshot, tr("全局抓图"));
        connect(m_globalSnapBtn, &QToolButton::clicked, this, &PlayerWindow::onGlobalSnapshot);
        m_globalCaptureBtn = mkAct(icons::Queue, tr("批量采集样本"));
        connect(m_globalCaptureBtn, &QToolButton::clicked, this, &PlayerWindow::onGlobalCapture);

        lo->addStretch(1);
        auto *fullBtn = new QToolButton(liveBottom);
        fullBtn->setObjectName(QStringLiteral("IconBtn"));
        fullBtn->setIcon(QIcon(icons::glyph(icons::Expand, ThemeManager::instance().tokens().text2, 16)));
        fullBtn->setIconSize(QSize(16, 16));
        fullBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        fullBtn->setToolTip(tr("画布全屏"));
        fullBtn->setCursor(Qt::PointingHandCursor);
        connect(fullBtn, &QToolButton::clicked, this, [this]() {
            if (m_active.isValid()) onMaximizeRequested(m_active);
        });
        lo->addWidget(fullBtn);
        centerLay->addWidget(liveBottom);
    }

    // ===== 中：录像回放页 =====
    auto *pbPage = new QWidget(body);
    auto *pl = new QVBoxLayout(pbPage);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->setSpacing(0);
    {
        auto *pbBar = new QWidget(pbPage);
        m_pbBar = pbBar;
        pbBar->setObjectName(QStringLiteral("LayoutBar"));
        pbBar->setFixedHeight(42);
        auto *pbl = new QHBoxLayout(pbBar);
        pbl->setContentsMargins(12, 0, 12, 0);
        pbl->setSpacing(8);
        // 回放布局：与预览同样式的图标按钮；目前只保留单画面占位
        // （每路录像计划/时间独立，4 路同步待后续需要再加）。
        auto *oneBtn = new QToolButton(pbBar);
        oneBtn->setObjectName(QStringLiteral("LayoutBtn"));
        oneBtn->setCheckable(true);
        oneBtn->setChecked(true);
        oneBtn->setCursor(Qt::PointingHandCursor);
        oneBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        oneBtn->setIconSize(QSize(20, 20));
        {
            const ThemeTokens &t = ThemeManager::instance().tokens();
            QIcon ic;
            ic.addPixmap(icons::glyph(icons::Layout1, t.text2, 20), QIcon::Normal, QIcon::Off);
            ic.addPixmap(icons::glyph(icons::Layout1, t.accent, 20), QIcon::Normal, QIcon::On);
            oneBtn->setIcon(ic);
        }
        oneBtn->setToolTip(tr("单画面回放"));
        oneBtn->setProperty("layoutHint", tr("单画面"));
        oneBtn->installEventFilter(this);
        connect(oneBtn, &QToolButton::clicked, this, [this]() {
            if (m_pbGrid) m_pbGrid->setLayoutKind(VideoGrid::L1x1);
        });
        pbl->addWidget(oneBtn);
        pbl->addStretch(1);
        auto *pbFullBtn = new QToolButton(pbBar);
        pbFullBtn->setObjectName(QStringLiteral("IconBtn"));
        pbFullBtn->setIcon(QIcon(icons::glyph(icons::Expand,
                                             ThemeManager::instance().tokens().text2, 16)));
        pbFullBtn->setIconSize(QSize(16, 16));
        pbFullBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        pbFullBtn->setToolTip(tr("画布全屏"));
        pbFullBtn->setCursor(Qt::PointingHandCursor);
        connect(pbFullBtn, &QToolButton::clicked, this, [this]() {
            if (m_pbRef.isValid()) onMaximizeRequested(m_pbRef);
        });
        pbl->addWidget(pbFullBtn);
        pl->addWidget(pbBar);

        m_pbGrid = new VideoGrid(pbPage);
        m_pbGrid->setLayoutKind(VideoGrid::L1x1);
        pl->addWidget(m_pbGrid, 1);

        // 时间轴容器：控制条 + 可拖拽时间轴
        auto *tlBox = new QWidget(pbPage);
        tlBox->setObjectName(QStringLiteral("TimelineBar"));
        auto *tlLay = new QVBoxLayout(tlBox);
        tlLay->setContentsMargins(0, 0, 0, 0);
        tlLay->setSpacing(0);
        auto *ctrl = new QWidget(tlBox);
        ctrl->setObjectName(QStringLiteral("LayoutBar"));
        ctrl->setFixedHeight(40);
        auto *cl = new QHBoxLayout(ctrl);
        cl->setContentsMargins(14, 0, 14, 0);
        cl->setSpacing(8);
        auto mkBtn = [&](const QString &t) {
            auto *b = new QToolButton(ctrl);
            b->setObjectName(QStringLiteral("IconBtn"));
            b->setText(t);
            b->setCursor(Qt::PointingHandCursor);
            cl->addWidget(b);
            return b;
        };
        auto *prevBtn = mkBtn(tr("⏮ 上一段"));
        connect(prevBtn, &QToolButton::clicked, this, [this]() {
            // 从当前时刻往前找：靠近段首时跳上一段末尾，否则回到本段开头。
            int idx = -1;
            for (int i = 0; i < m_pbSegs.size(); ++i) {
                if (m_pbSegs[i].recordId == m_pbRecordId) { idx = i; break; }
            }
            if (idx > 0) {
                const RecordingSegment s = m_pbSegs.at(idx - 1);
                seekPlayback(s.startMs);
            } else if (idx == 0) {
                seekPlayback(m_pbSegs.at(0).startMs);
            }
        });
        m_pbPlayBtn = mkBtn(tr("⏸ 暂停"));
        m_pbPlayBtn->setCheckable(true);
        connect(m_pbPlayBtn, &QToolButton::clicked, this, &PlayerWindow::onPbPlayPause);
        cl->addStretch(1);
        m_pbTimeLabel = new QLabel(QStringLiteral("---- --:--:--"), ctrl);
        m_pbTimeLabel->setObjectName(QStringLiteral("TimeDisplay"));
        cl->addWidget(m_pbTimeLabel);
        cl->addStretch(1);
        auto *speed = new QComboBox(ctrl);
        speed->addItems({QStringLiteral("0.5x"), QStringLiteral("1.0x"),
                         QStringLiteral("2.0x"), QStringLiteral("4.0x"),
                         QStringLiteral("8.0x")});
        speed->setCurrentIndex(1);
        speed->setToolTip(tr("回放倍速"));
        connect(speed, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, speed](int idx) {
                    const double rate = speed->itemData(idx, Qt::UserRole)
                                            .toDouble();
                    if (VideoTile *t = m_pbGrid ? m_pbGrid->activeTile() : nullptr) {
                        if (IMediaPlayer *p = t->player()) {
                            p->setSpeed(rate > 0 ? rate : 1.0);
                        }
                    }
                });
        for (int i = 0; i < speed->count(); ++i) {
            speed->setItemData(i, speed->itemText(i).remove(QStringLiteral("x")).toDouble(),
                               Qt::UserRole);
        }
        cl->addWidget(speed);
        tlLay->addWidget(ctrl);
        m_pbTimeline = new PlaybackTimeline(tlBox);
        connect(m_pbTimeline, &PlaybackTimeline::seekRequested,
                this, &PlayerWindow::onPbSeek);
        tlLay->addWidget(m_pbTimeline);
        m_pbTimer = new QTimer(this);
        m_pbTimer->setInterval(500);
        connect(m_pbTimer, &QTimer::timeout, this, &PlayerWindow::onPbTick);
        pl->addWidget(tlBox);
    }

    // ===== 中央堆叠：实时(0) / 回放(1) =====
    auto *centerStack = new QStackedWidget(body);
    m_centerStack = centerStack;
    centerStack->addWidget(livePage);
    centerStack->addWidget(pbPage);

    m_ptz = new PtzPanel(body);
    m_ptz->setMinimumWidth(220);
    m_ptz->setMaximumWidth(360);

    // Right tool dock: the PTZ controls and the local failed-sample list share
    // one panel (tabs), instead of opening extra windows.
    m_toolDock = new QTabWidget(body);
    m_toolDock->setObjectName(QStringLiteral("ToolDock"));
    m_toolDock->setDocumentMode(true);
    m_toolDock->setMinimumWidth(280);
    m_toolDock->setMaximumWidth(380);
    m_toolDock->setMinimumHeight(42);
    m_toolDock->addTab(m_ptz, tr("云台"));

    auto *samplePage = new QWidget(m_toolDock);
    m_samplePage = samplePage;
    // 样本为空时它还不是一个真正的 Tab，必须隐藏；否则作为 QTabWidget 的
    // 裸子控件会被默认显示、叠在「云台」页上（启动乱状）。
    samplePage->hide();
    auto *sampleLay = new QVBoxLayout(samplePage);
    sampleLay->setContentsMargins(10, 10, 10, 10);
    sampleLay->setSpacing(8);

    // Header: title + live count + collapse affordance.
    auto *sampleHead = new QHBoxLayout();
    sampleHead->setSpacing(6);
    auto *sampleTitle = new QLabel(tr("样本"), samplePage);
    sampleTitle->setObjectName(QStringLiteral("CardTitle"));
    m_sampleCount = new QLabel(QString(), samplePage);
    m_sampleCount->setObjectName(QStringLiteral("PageSub"));
    auto *collapseBtn = new QToolButton(samplePage);
    collapseBtn->setObjectName(QStringLiteral("IconBtn"));
    collapseBtn->setIcon(QIcon(icons::chevron(true,
                                              ThemeManager::instance().tokens().text2, 14)));
    collapseBtn->setCursor(Qt::PointingHandCursor);
    collapseBtn->setToolTip(tr("收起样本面板"));
    connect(collapseBtn, &QToolButton::clicked, this, &PlayerWindow::collapseToolDock);
    sampleHead->addWidget(sampleTitle);
    sampleHead->addWidget(m_sampleCount);
    sampleHead->addStretch(1);
    sampleHead->addWidget(collapseBtn);
    sampleLay->addLayout(sampleHead);

    // Card list: one compact card per captured sample (see refreshSampleList).
    m_sampleScroll = new QScrollArea(samplePage);
    m_sampleScroll->setObjectName(QStringLiteral("SampleScroll"));
    m_sampleScroll->setWidgetResizable(true);
    m_sampleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sampleScroll->setFrameShape(QFrame::NoFrame);
    auto *sampleHost = new QWidget(m_sampleScroll);
    sampleHost->setObjectName(QStringLiteral("SampleListHost"));
    m_sampleLay = new QVBoxLayout(sampleHost);
    m_sampleLay->setContentsMargins(0, 0, 4, 0);
    m_sampleLay->setSpacing(8);
    m_sampleLay->addStretch(1);
    m_sampleScroll->setWidget(sampleHost);
    sampleLay->addWidget(m_sampleScroll, 1);

    auto *openBtn = new QPushButton(tr("打开标注 / 训练工具 →"), samplePage);
    openBtn->setObjectName(QStringLiteral("PrimaryButton"));
    openBtn->setCursor(Qt::PointingHandCursor);
    connect(openBtn, &QPushButton::clicked, this, &PlayerWindow::openTrainingTool);
    sampleLay->addWidget(openBtn);
    // The tab itself is added dynamically: only while the dataset is non-empty.

    // ===== 右：回放模式——录像片段列表 =====
    auto *pbRight = new QWidget(body);
    pbRight->setObjectName(QStringLiteral("RightSidebar"));
    {
        auto *prl = new QVBoxLayout(pbRight);
        prl->setContentsMargins(0, 0, 0, 0);
        prl->setSpacing(0);
        auto *pbHead = new QWidget(pbRight);
        pbHead->setObjectName(QStringLiteral("PanelHeaderBar"));
        pbHead->setFixedHeight(42);
        auto *pbHeadLay = new QHBoxLayout(pbHead);
        pbHeadLay->setContentsMargins(12, 0, 12, 0);
        m_pbSegHeader = new QLabel(tr("录像片段列表"), pbHead);
        m_pbSegHeader->setObjectName(QStringLiteral("PanelHeaderTitle"));
        pbHeadLay->addWidget(m_pbSegHeader);
        pbHeadLay->addStretch(1);
        prl->addWidget(pbHead);

        auto *pbBody = new QWidget(pbRight);
        auto *pbBodyLay = new QVBoxLayout(pbBody);
        pbBodyLay->setContentsMargins(12, 12, 12, 12);
        pbBodyLay->setSpacing(8);
        auto *segScroll = new QScrollArea(pbBody);
        segScroll->setObjectName(QStringLiteral("SampleScroll"));
        segScroll->setWidgetResizable(true);
        segScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        segScroll->setFrameShape(QFrame::NoFrame);
        auto *segHost = new QWidget(segScroll);
        segHost->setObjectName(QStringLiteral("SampleListHost"));
        m_pbSegLay = new QVBoxLayout(segHost);
        m_pbSegLay->setContentsMargins(0, 0, 4, 0);
        m_pbSegLay->setSpacing(6);
        m_pbSegLay->addStretch(1);
        segScroll->setWidget(segHost);
        pbBodyLay->addWidget(segScroll, 1);
        prl->addWidget(pbBody, 1);
    }

    // 右侧上下文堆叠：实时(0=云台+样本) / 回放(1=片段列表)
    auto *rightStack = new QStackedWidget(body);
    m_rightStack = rightStack;
    rightStack->setMinimumWidth(280);
    rightStack->setMaximumWidth(380);
    rightStack->addWidget(m_toolDock);
    rightStack->addWidget(pbRight);

    // Slim strip shown while the dock is collapsed; one click restores it.
    m_dockReopen = new DockStripButton(body);
    m_dockReopen->setObjectName(QStringLiteral("DockReopen"));
    m_dockReopen->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_dockReopen->setIcon(QIcon(icons::glyph(icons::Layers,
                                             ThemeManager::instance().tokens().text2, 16)));
    m_dockReopen->setIconSize(QSize(16, 16));
    m_dockReopen->setCursor(Qt::PointingHandCursor);
    m_dockReopen->setToolTip(tr("展开样本面板"));
    m_dockReopen->setFixedWidth(36);
    m_dockReopen->setVisible(false);
    connect(m_dockReopen, &QToolButton::clicked, this, &PlayerWindow::expandToolDock);

    auto *splitter = new QSplitter(Qt::Horizontal, body);
    splitter->setObjectName(QStringLiteral("BodySplitter"));
    splitter->setHandleWidth(1);
    splitter->addWidget(leftCol);
    splitter->addWidget(m_centerStack);
    splitter->addWidget(m_rightStack);
    splitter->addWidget(m_dockReopen);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setStretchFactor(3, 0);
    // Size the columns relative to the real width so narrow laptops don't get a
    // 1440-wide splitter squeezed into a 1100-wide window.
    {
        const int availW = qMax(720, width());
        const int left = qBound(260, int(availW * 0.205), 340);
        const int right = qBound(280, int(availW * 0.205), 340);
        const int mid = qMax(320, availW - left - right);
        splitter->setSizes({left, mid, right, 0});
    }
    m_bodySplitter = splitter;

    // Global capture switch (persisted); tiles follow it and can flip it from
    // the AI menu — the window re-broadcasts so every tile stays in sync.
    m_grid->setCaptureEnabled(QSettings().value(QStringLiteral("ai/captureSamples"),
                                                false).toBool());

    bodyLay->addWidget(splitter);
    rootLay->addWidget(body, 1);

    // ---------------- Status feedback ----------------
    // Transient messages live at the far right of the status bar; no overlay.
    m_toast = new Toast(statusBar(), this);

    // ---------------- Wiring ----------------
    connect(m_tree, &DeviceTreeView::channelActivated, this, &PlayerWindow::onChannelActivated);
    connect(m_tree, &DeviceTreeView::childrenNeeded, this, [this](const QString &parentId) {
        m_api->fetchChildren(parentId);
    });

    connect(m_grid, &VideoGrid::activeChannelChanged, this, &PlayerWindow::onActiveChannelChanged);
    connect(m_grid, &VideoGrid::tilePlaybackStateChanged, this,
            [this](const zp::ChannelRef &ch, bool) {
                if (ch == m_active) {
                    updatePtzPanel();
                }
            });
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
    // Sample collection: the switch is global (persisted) and every tile obeys.
    connect(m_grid, &VideoGrid::captureSettingChanged, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("ai/captureSamples"), on);
        m_grid->setCaptureEnabled(on);
    });
    connect(m_grid, &VideoGrid::sampleCaptured, this,
            [this](const zp::ChannelRef &, bool ok) {
                if (ok) {
                    toastSuccess(tr("已采集样本，可在右侧「样本」中查看"));
                    m_sampleShown = -1;
                    updateSampleBadge();
                } else {
                    toastInfo(tr("采集失败：需在播放状态且已有检测结果"));
                }
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

    m_toast->setPersistentText(tr("已登录：%1").arg(m_session.username));
}

void PlayerWindow::openSettings()
{
    SettingsDialog dlg(m_grid ? m_grid->videoSettings() : VideoSettings(),
                       ThemeManager::instance().presetName(), this);
    if (qEnvironmentVariable("ZP_SETTINGS") == QLatin1String("about")) {
        dlg.setPage(2);   // dev: capture the about page headlessly
    }
    connect(&dlg, &SettingsDialog::videoSettingsChanged, this,
            [this](const VideoSettings &s) {
                if (m_grid) m_grid->setVideoSettings(s);
            });
    connect(&dlg, &SettingsDialog::themeChanged, this, [this](const QString &name) {
        ThemeManager::instance().setPreset(name);
        SessionManager::instance().setTheme(name);
    });
    dlg.exec();
}

void PlayerWindow::openTrainingTool()
{
    if (!m_trainTool) {
        m_trainTool = new TrainingToolWindow(this);
        m_trainTool->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_trainTool, &TrainingToolWindow::sampleCountChanged, this,
                [this](int) {
                    m_sampleShown = -1;
                    updateSampleBadge();
                });
        connect(m_trainTool, &TrainingToolWindow::modelVersionChanged, this,
                [this](const QString &) { reloadDetector(); });
        connect(m_trainTool, &QObject::destroyed, this, [this]() {
            m_trainTool = nullptr;
            m_sampleShown = -1;   // labels may have changed while it was open
            updateSampleBadge();
        });
    }
    m_trainTool->show();
    m_trainTool->raise();
    m_trainTool->activateWindow();
}

void PlayerWindow::updateSampleBadge()
{
    const int n = DatasetStore::instance().count();
    syncSampleTab(n);
    if (m_sampleCount) {
        m_sampleCount->setText(n > 0 ? tr("（%1 条）").arg(n) : tr("（空）"));
    }
    if (m_dockReopen) {
        m_dockReopen->setCount(n);
        m_dockReopen->setToolTip(n > 0 ? tr("展开样本面板（%1 条）").arg(n)
                                       : tr("展开样本面板"));
    }
    if (n != m_sampleShown) {
        m_sampleShown = n;
        refreshSampleList();
    }
}

// Adds/removes the 样本 tab so an empty dataset leaves no dead tab behind.
void PlayerWindow::syncSampleTab(int count)
{
    if (!m_toolDock || !m_samplePage) {
        return;
    }
    const int idx = m_toolDock->indexOf(m_samplePage);
    if (count > 0 && idx < 0) {
        m_toolDock->addTab(m_samplePage, tr("样本 %1").arg(count));
    } else if (count == 0 && idx >= 0) {
        m_toolDock->removeTab(idx);
        m_samplePage->hide();   // 移除后不再由 QTabWidget 管理，需手动隐藏
    } else if (count > 0 && idx >= 0) {
        m_toolDock->setTabText(idx, tr("样本 %1").arg(count));
    }
}

void PlayerWindow::collapseToolDock()
{
    if (!m_toolDock || !m_dockReopen || m_dockCollapsed) {
        return;
    }
    m_dockCollapsed = true;
    if (m_bodySplitter) {
        const QList<int> sz = m_bodySplitter->sizes();
        if (sz.size() == 4 && sz.at(2) > 0) {
            m_dockWidth = sz.at(2);
        }
        m_bodySplitter->setSizes({sz.value(0, 280), sz.value(1, 840), 0, 36});
    }
    m_rightStack->hide();
    m_dockReopen->show();
}

void PlayerWindow::expandToolDock()
{
    if (!m_toolDock || !m_dockReopen || !m_dockCollapsed) {
        return;
    }
    m_dockCollapsed = false;
    m_dockReopen->hide();
    if (m_bodySplitter) {
        const QList<int> sz = m_bodySplitter->sizes();
        const int target = qMax(240, m_dockWidth);
        const int middle = qMax(0, sz.value(1, 840) + sz.value(3, 36) - target);
        m_bodySplitter->setSizes({sz.value(0, 280), middle, target, 0});
    }
    m_rightStack->show();
    if (m_sampleCount) {
        m_sampleCount->setText(DatasetStore::instance().count() > 0
                                   ? tr("（%1 条）").arg(DatasetStore::instance().count())
                                   : tr("（空）"));
    }
}

void PlayerWindow::refreshSampleList()
{
    if (!m_sampleLay) {
        return;
    }
    // Drop every card, keep the trailing stretch.
    while (m_sampleLay->count() > 1) {
        QLayoutItem *item = m_sampleLay->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const QVector<SampleRecord> recs = DatasetStore::instance().load();
    const int insertAt = m_sampleLay->count() - 1;   // before the stretch
    if (recs.isEmpty()) {
        auto *empty = new QLabel(
            tr("暂无样本。\n开启画面「AI 检查」菜单里的「样本采集」后，\n低置信帧会自动收进这里；也可点底栏「采集」手动保存。"),
            m_sampleLay->parentWidget());
        empty->setObjectName(QStringLiteral("SampleEmpty"));
        empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(8, 28, 8, 28);
        m_sampleLay->insertWidget(insertAt, empty);
        return;
    }

    int idx = 0;
    for (const SampleRecord &r : recs) {
        int score = 0;
        for (const AnalysisBox &b : r.boxes) {
            score = qMax(score, int(b.score * 100));
        }

        auto *card = new QFrame(m_sampleLay->parentWidget());
        card->setObjectName(QStringLiteral("SampleCard"));
        card->setProperty("done", r.done);
        card->setProperty("sampleId", r.id);
        card->setCursor(Qt::PointingHandCursor);
        card->setToolTip(tr("点击进入标注"));
        card->installEventFilter(this);

        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(10, 8, 10, 8);
        cv->setSpacing(6);

        auto *row1 = new QHBoxLayout();
        row1->setSpacing(6);
        auto *name = new uikit::ElidedLabel(card);
        name->setObjectName(QStringLiteral("SampleName"));
        name->setProperty("done", r.done);
        name->setFullText(r.channel);
        name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        row1->addWidget(name, 1);
        if (score > 0) {
            const uikit::ChipKind kind = score >= 70 ? uikit::ChipOk
                                        : score < 40 ? uikit::ChipBad
                                                     : uikit::ChipWarn;
            auto *conf = uikit::makeChip(QStringLiteral("%1%").arg(score), kind, card);
            conf->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            row1->addWidget(conf);
        }
        cv->addLayout(row1);

        auto *row2 = new QHBoxLayout();
        row2->setSpacing(6);
        auto *reason = uikit::makeChip(r.reason.isEmpty() ? tr("manual") : r.reason,
                                       r.done ? uikit::ChipMuted : uikit::ChipWarn, card);
        reason->setProperty("mono", true);
        reason->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto *status = uikit::makeChip(r.done ? tr("已标注") : tr("待标注"),
                                       r.done ? uikit::ChipInfo : uikit::ChipWarn, card);
        status->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        row2->addWidget(reason);
        row2->addStretch(1);
        row2->addWidget(status);
        cv->addLayout(row2);

        m_sampleLay->insertWidget(insertAt + idx, card);
        ++idx;
    }
}

void PlayerWindow::selectSampleTab()
{
    if (!m_toolDock || !m_samplePage) {
        return;
    }
    syncSampleTab(DatasetStore::instance().count());
    expandToolDock();
    const int idx = m_toolDock->indexOf(m_samplePage);
    if (idx >= 0) {
        m_toolDock->show();
        m_toolDock->setCurrentIndex(idx);
    }
}

void PlayerWindow::reloadDetector()
{
    if (!m_analysis) {
        return;
    }
    if (createDebugStatsAnalyzer()) {
        return;   // dev stats analyzer is in use; keep it
    }
    QString modelPath, classesPath, findErr;
    if (!findDefaultYoloModel(&modelPath, &classesPath, &findErr)) {
        toastError(tr("模型切换失败：%1").arg(findErr));
        return;
    }
    auto *det = new YoloDetector();
    QString loadErr;
    if (!det->load(modelPath, classesPath, &loadErr)) {
        delete det;
        toastError(tr("模型加载失败：%1").arg(loadErr));
        return;
    }
    IFrameAnalyzer *old = m_detector;
    m_detector = det;
    m_analysis->setAnalyzer(det);
    m_grid->setClassNames(det->classNames());
    qInfo().noquote() << "[ai] detector reloaded:" << modelPath
                      << "backend=" << det->backendName();
    const QString ver = QFileInfo(modelPath).dir().dirName();
    toastSuccess(tr("模型已切换：%1").arg(ver));
    if (old) {
        // Give any in-flight analyze() on the worker time to finish.
        QTimer::singleShot(3000, this, [old]() { delete old; });
    }
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
    // Sample card in the right dock: a click opens the training tool straight
    // on that record (mirrors the prototype's card -> workbench jump).
    if (auto *w = qobject_cast<QWidget *>(watched)) {
        if (w->objectName() == QLatin1String("SampleCard")
            && event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QString id = w->property("sampleId").toString();
                openTrainingTool();
                if (m_trainTool) {
                    m_trainTool->openRecordById(id);
                }
                return true;
            }
        }
    }

    // Layout buttons: show the split count on hover for a moment, then let it
    // auto-hide after ~1.6 s even while the pointer stays on the button.
    if (auto *btn = qobject_cast<QToolButton *>(watched)) {
        const QVariant hint = btn->property("layoutHint");
        if (hint.isValid()) {
            if (event->type() == QEvent::Enter) {
                QToolTip::showText(btn->mapToGlobal(QPoint(btn->width() / 2,
                                                           btn->height() + 6)),
                                   hint.toString(), btn, QRect(), 1600);
            } else if (event->type() == QEvent::Leave) {
                QToolTip::hideText();
            }
        }
    }

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
        // The tree always shows a synthetic "根资源组" root, so an empty
        // catalog is still a navigable directory — no error toast needed.
        m_tree->setRootNodes(nodes);
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

    // 回放模式：选中通道只记录回放对象，不自动查全部录像。
    // 查询由「检索录像」按钮 / 日期变更按需触发，避免切通道时全量预查拖慢界面。
    if (m_mode == 1) {
        m_pbChannel = ch;
        if (m_pbDate) {
            m_pbDayStart = QDateTime(m_pbDate->date(), QTime(0, 0, 0)).toMSecsSinceEpoch();
            m_pbDayEnd = m_pbDayStart + 24LL * 3600 * 1000;
            if (m_pbTimeline) m_pbTimeline->setDay(m_pbDayStart, m_pbDayEnd);
        }
        showPbEmptyState(tr("选择日期后点击「检索录像」查看该通道的录像片段"),
                         QString());
        return;
    }

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

void PlayerWindow::updatePtzPanel()
{
    // 云台 Tab 始终可见：不支持 PTZ 时面板内部显示内联说明并禁用控件，
    // 让用户明确知道原因和下一步（选支持云台的通道），而不是隐藏整个 Tab。
    Q_UNUSED(m_activePtzCapable);
    if (m_toolDock && m_ptz && m_active.isValid()) {
        const int ptzIdx = m_toolDock->indexOf(m_ptz);
        if (ptzIdx >= 0) {
            m_toolDock->setTabEnabled(ptzIdx, true);
        }
    }
}

void PlayerWindow::onActiveChannelChanged(const ChannelRef &ch, const QString &name, bool ptzCapable)
{
    m_active = ch;
    m_activePtzCapable = ptzCapable;
    m_ptz->setTarget(ch, name, ptzCapable);
    updatePtzPanel();
    if (ch.isValid()) {
        setWindowTitle(QString::fromUtf8("Zeroo Player — %1").arg(name));
        m_toast->setPersistentText(tr("当前通道：%1  [%2/%3]").arg(name, ch.deviceId, ch.channelId));
    } else {
        setWindowTitle(QStringLiteral("Zeroo Player"));
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
    if (scope == QLatin1String("playback")) {
        // 查询失败不是严重错误：内容区给可重试空状态 + 状态栏轻提示。
        if (m_pbSegHeader) m_pbSegHeader->setText(tr("录像片段"));
        showPbEmptyState(tr("录像查询失败"), tr("请稍后点击「检索录像」重试"));
        toastInfo(tr("录像查询失败，请重试"));
        return;
    }
    if (scope == QLatin1String("ptz") || scope == QLatin1String("preset")) {
        // 能力/命令类提示：非阻塞，状态栏短暂显示后自动消失。
        toastInfo(message);
        return;
    }
    toastError(message);
}

void PlayerWindow::setMode(int mode)
{
    m_mode = mode;
    // 快捷键（Ctrl+R）等外部入口切模式时，同步左栏一级 Tab 的选中态。
    if (m_modeTabs && m_modeTabs->currentIndex() != mode) {
        const QSignalBlocker blocker(m_modeTabs);
        m_modeTabs->setCurrentIndex(mode);
    }
    if (m_centerStack) m_centerStack->setCurrentIndex(mode);
    if (m_rightStack) m_rightStack->setCurrentIndex(mode);
    if (m_pbFilter) m_pbFilter->setVisible(mode == 1);

    if (mode == 0) {
        // 回到实时预览：停掉回放并清理回放瓦片
        if (m_pbRecordId > 0) {
            m_api->stopPlayback(m_pbChannel, m_pbRecordId);
        }
        m_pbPlaying = false;
        if (m_pbTimer) m_pbTimer->stop();
        if (m_pbGrid) m_pbGrid->stopAll();
        m_pbRecordId = 0;
        if (m_pbTimeline) m_pbTimeline->setActiveSegment(0);
        if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(false); m_pbPlayBtn->setText(tr("⏸ 暂停")); }
    } else {
        // 进入回放：优先用当前选中的实时通道作为回放对象
        if (m_active.isValid() && !m_active.channelId.endsWith(QStringLiteral("@playback"))) {
            m_pbChannel = m_active;
        }
        // 已有通道就自动查一次
        if (m_pbChannel.isValid()) {
            queryPlaybackDay();
        }
    }
}

void PlayerWindow::onModeAi()
{
    openTrainingTool();
}

void PlayerWindow::queryPlaybackDay()
{
    if (!m_api) {
        return;
    }
    if (!m_pbChannel.isValid()) {
        if (m_pbSegHeader) m_pbSegHeader->setText(tr("录像片段"));
        showPbEmptyState(tr("请先选择要检索的通道"), tr("在左侧目录点击一个通道后再检索"));
        toastInfo(tr("请先在左侧选择要回放的通道"));
        return;
    }
    const QDate d = m_pbDate ? m_pbDate->date() : QDate::currentDate();
    m_pbDayStart = QDateTime(d, QTime(0, 0, 0)).toMSecsSinceEpoch();
    m_pbDayEnd = m_pbDayStart + 24LL * 3600 * 1000;
    if (m_pbTimeline) {
        m_pbTimeline->setDay(m_pbDayStart, m_pbDayEnd);
        m_pbTimeline->setSegments(QList<RecordingSegment>());
        m_pbTimeline->setActiveSegment(0);
    }
    if (m_pbSegHeader) {
        m_pbSegHeader->setText(tr("查询中…"));
    }
    showPbEmptyState(tr("正在检索录像片段…"), QString());
    m_api->queryRecordings(m_pbChannel, m_pbDayStart, m_pbDayEnd);
}

void PlayerWindow::onRecordingsReady(const ChannelRef &ch,
                                     const QList<RecordingSegment> &segments)
{
    if (ch.key() != m_pbChannel.key()) {
        return;
    }
    m_pbSegs = segments;
    if (m_pbTimeline) m_pbTimeline->setSegments(segments);
    rebuildSegmentList(segments);
}

void PlayerWindow::showPbEmptyState(const QString &title, const QString &hint)
{
    if (!m_pbSegLay) {
        return;
    }
    QWidget *host = m_pbSegLay->parentWidget();
    while (m_pbSegLay->count() > 1) {
        QLayoutItem *it = m_pbSegLay->takeAt(0);
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    auto *box = new QFrame(host);
    box->setObjectName(QStringLiteral("PbEmptyState"));
    auto *vl = new QVBoxLayout(box);
    vl->setContentsMargins(16, 20, 16, 20);
    vl->setSpacing(8);

    auto *icon = new QLabel(box);
    icon->setObjectName(QStringLiteral("PbEmptyIcon"));
    icon->setText(QStringLiteral("◷"));
    icon->setAlignment(Qt::AlignCenter);
    vl->addWidget(icon);

    auto *titleLbl = new QLabel(title, box);
    titleLbl->setObjectName(QStringLiteral("PbEmptyTitle"));
    titleLbl->setAlignment(Qt::AlignCenter);
    titleLbl->setWordWrap(true);
    vl->addWidget(titleLbl);
    if (!hint.isEmpty()) {
        auto *hintLbl = new QLabel(hint, box);
        hintLbl->setObjectName(QStringLiteral("PbEmptyHint"));
        hintLbl->setAlignment(Qt::AlignCenter);
        hintLbl->setWordWrap(true);
        vl->addWidget(hintLbl);
    }
    vl->addStretch(1);
    m_pbSegLay->insertWidget(0, box);
}

void PlayerWindow::rebuildSegmentList(const QList<RecordingSegment> &segments)
{
    if (!m_pbSegLay) {
        return;
    }
    QWidget *host = m_pbSegLay->parentWidget();
    while (m_pbSegLay->count() > 1) {
        QLayoutItem *it = m_pbSegLay->takeAt(0);
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
    if (m_pbSegHeader) {
        m_pbSegHeader->setText(segments.isEmpty()
                                   ? tr("录像片段（无）")
                                   : tr("录像片段（%1）").arg(segments.size()));
    }
    if (segments.isEmpty()) {
        showPbEmptyState(tr("该日期暂无录像"), tr("请更换日期或选择其他通道后重新检索"));
        return;
    }
    QList<QPushButton *> segButtons;
    for (const RecordingSegment &s : segments) {
        auto *b = new QPushButton(host);
        b->setObjectName(QStringLiteral("SegItem"));
        b->setCheckable(true);
        b->setProperty("recordId", QVariant::fromValue(s.recordId));
        b->setText(QStringLiteral("%1 - %2    %3")
            .arg(QDateTime::fromMSecsSinceEpoch(s.startMs).toString(QStringLiteral("HH:mm:ss")),
                 QDateTime::fromMSecsSinceEpoch(s.endMs).toString(QStringLiteral("HH:mm:ss")),
                 s.container.isEmpty() ? tr("录像") : s.container.toUpper()));
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(tr("点击跳转到该片段起点"));
        const qlonglong rid = s.recordId;
        const qint64 st = s.startMs;
        connect(b, &QPushButton::clicked, this, [this, rid, st]() {
            onPbSegmentClicked(rid, st);
        });
        m_pbSegLay->insertWidget(m_pbSegLay->count() - 1, b);
        segButtons.append(b);
    }
    // 检索结果刷新后恢复当前片段的列表高亮。
    for (QPushButton *b : segButtons) {
        b->setChecked(b->property("recordId").toLongLong() == m_pbRecordId);
    }
}

void PlayerWindow::highlightSegmentInList(qlonglong recordId)
{
    if (!m_pbSegLay) return;
    for (int i = 0; i < m_pbSegLay->count(); ++i) {
        if (QPushButton *b = qobject_cast<QPushButton *>(m_pbSegLay->itemAt(i)->widget())) {
            b->setChecked(b->property("recordId").toLongLong() == recordId);
        }
    }
}

void PlayerWindow::onPbSegmentClicked(qlonglong recordId, qint64 startMs)
{
    if (recordId <= 0) {
        return;
    }
    // 列表点击等价于时间轴 seek：统一走一条路径，保证高亮/状态一致。
    seekPlayback(startMs);
}

const RecordingSegment *PlayerWindow::pbSegmentFor(qint64 absMs) const
{
    for (const RecordingSegment &s : m_pbSegs) {
        if (absMs >= s.startMs && absMs < s.endMs) {
            return &s;
        }
    }
    // 空隙里取最近的一段（左侧优先），拖动时总能给出可播放目标。
    const RecordingSegment *best = nullptr;
    qint64 bestGap = std::numeric_limits<qint64>::max();
    for (const RecordingSegment &s : m_pbSegs) {
        const qint64 gap = absMs < s.startMs ? s.startMs - absMs : absMs - s.endMs;
        if (gap < bestGap) {
            bestGap = gap;
            best = &s;
        }
    }
    return best;
}

void PlayerWindow::seekPlayback(qint64 absMs, bool fromTimeline)
{
    if (!m_pbChannel.isValid()) {
        toastInfo(tr("请先在左侧选择要回放的通道"));
        return;
    }
    if (m_pbSegs.isEmpty()) {
        if (!fromTimeline) {
            queryPlaybackDay();
            toastInfo(tr("请先检索录像，然后点击时间轴选择播放位置"));
        } else {
            toastInfo(tr("当前日期没有可播放的录像片段"));
        }
        return;
    }
    const RecordingSegment *seg = pbSegmentFor(absMs);
    if (!seg || seg->recordId <= 0) {
        toastInfo(tr("该时刻没有可播放的录像片段"));
        return;
    }
    // 目标时刻可能不在片段内（空隙）：钳到片段边界，避免服务端拒绝非法 startTime。
    const qint64 target = qBound(seg->startMs, absMs, qMax(seg->startMs, seg->endMs - 1));
    m_pbRecordId = seg->recordId;
    m_pbSegStart = target;
    m_pbSegDur = seg->durationMs > 0 ? seg->durationMs
                                     : qMax<qint64>(1, seg->endMs - seg->startMs);
    if (m_pbTimeline) {
        m_pbTimeline->setPosition(target);
        m_pbTimeline->setActiveSegment(seg->recordId);
    }
    highlightSegmentInList(seg->recordId);
    if (m_pbTimeLabel) {
        m_pbTimeLabel->setText(QDateTime::fromMSecsSinceEpoch(target)
                                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }

    // 平台没有运行中 seek：停旧会话，以目标绝对时间重新起播。
    if (m_pbRecordId > 0) {
        m_api->stopPlayback(m_pbChannel, m_pbRecordId);
    }
    if (m_pbGrid) m_pbGrid->closeStream(m_pbRef);
    m_pbPlaying = false;
    m_pbLastRealPos = -1.0;
    m_pbStallTicks = 0;
    m_pbStallNotified = false;
    if (m_pbTimer) m_pbTimer->stop();
    if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(false); m_pbPlayBtn->setText(tr("⏸ 暂停")); }
    m_api->startPlayback(m_pbChannel, seg->recordId, target);
}

void PlayerWindow::onPbSeek(qint64 absMs)
{
    seekPlayback(absMs, true);
}

void PlayerWindow::onPlaybackStarted(const ChannelRef &ch, qlonglong recordId,
                                     const QUrl &url, const QString &transport,
                                     qint64 segStartMs, qint64 segDurationMs)
{
    Q_UNUSED(transport)
    if (ch.key() != m_pbChannel.key()) {
        return;
    }
    m_pbRecordId = recordId;
    m_pbSegStart = segStartMs;
    m_pbSegDur = segDurationMs > 0 ? segDurationMs : m_pbSegDur;
    m_pbPlaying = true;
    m_pbLastRealPos = -1.0;
    m_pbStallTicks = 0;
    m_pbStallNotified = false;
    if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(false); m_pbPlayBtn->setText(tr("⏸ 暂停")); }
    if (m_pbTimer) m_pbTimer->start(500);

    m_pbRef = m_pbChannel;
    // 回放瓦片与实时瓦片互不冲突：resourceId 是 key 的主键，必须一并加后缀，
    // 否则两者 key 相同会被 VideoGrid 当成同一路。
    m_pbRef.resourceId = m_pbChannel.resourceId + QStringLiteral("@playback");
    m_pbRef.channelId = m_pbChannel.channelId + QStringLiteral("@playback");
    const QString name = m_channelNames.value(m_pbChannel.key(), m_pbChannel.channelId)
            + tr("（回放）");
    if (m_pbGrid) {
        m_pbGrid->closeStream(m_pbRef);
        m_pbGrid->openStream(m_pbRef, name, false, url);
    }
    if (m_pbTimeline) {
        m_pbTimeline->setPosition(segStartMs);
        m_pbTimeline->setActiveSegment(recordId);
    }
    if (m_pbTimeLabel) {
        m_pbTimeLabel->setText(QDateTime::fromMSecsSinceEpoch(segStartMs)
                                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }
}

void PlayerWindow::onPbPlayPause()
{
    VideoTile *tile = m_pbGrid ? m_pbGrid->activeTile() : nullptr;
    IMediaPlayer *player = tile ? tile->player() : nullptr;
    if (m_pbRecordId <= 0 || !player || !player->supportsPlaybackControl()) {
        toastInfo(tr("请先拖动时间轴选择录像位置"));
        if (m_pbPlayBtn) m_pbPlayBtn->setChecked(false);
        return;
    }
    if (player->state() == MediaState::Paused) {
        player->play();
        m_pbPlaying = true;
        m_pbLastRealPos = -1.0;
        m_pbStallTicks = 0;
        if (m_pbTimer) m_pbTimer->start(500);
        if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(false); m_pbPlayBtn->setText(tr("⏸ 暂停")); }
    } else {
        player->pause();
        m_pbPlaying = false;
        if (m_pbTimer) m_pbTimer->stop();
        if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(true); m_pbPlayBtn->setText(tr("▶ 继续")); }
        const double pos = player->position();
        if (pos >= 0.0 && m_pbTimeline) {
            m_pbTimeline->setPosition(m_pbSegStart + qint64(pos * 1000.0));
        }
    }
}

void PlayerWindow::onPbTick()
{
    if (!m_pbPlaying) {
        return;
    }
    VideoTile *tile = m_pbGrid ? m_pbGrid->activeTile() : nullptr;
    IMediaPlayer *player = tile ? tile->player() : nullptr;
    if (!player) {
        return;
    }
    const double posSec = player->position();
    if (posSec < 0.0) {
        return; // 流还未真正建立，不要把假位置写到界面上
    }
    // mpv 在流结束时 keep-open 会自己暂停：同步按钮状态，避免界面显示"暂停"实际已停。
    if (player->state() == MediaState::Paused && m_pbPlaying) {
        m_pbPlaying = false;
        if (m_pbTimer) m_pbTimer->stop();
        if (m_pbPlayBtn) { m_pbPlayBtn->setChecked(true); m_pbPlayBtn->setText(tr("▶ 继续")); }
    }
    const qint64 pos = m_pbSegStart + qint64(posSec * 1000.0);
    if (m_pbTimeline) m_pbTimeline->setPosition(pos);
    if (m_pbTimeLabel) {
        m_pbTimeLabel->setText(QDateTime::fromMSecsSinceEpoch(pos)
                                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }

    // 位置连续不动说明底层流没有前进：明确告诉用户，而不是继续画假时钟。
    if (m_pbLastRealPos >= 0.0 && posSec <= m_pbLastRealPos + 0.01) {
        if (++m_pbStallTicks >= 12 && !m_pbStallNotified) { // 约 6 秒无进展
            m_pbStallNotified = true;
            toastInfo(tr("回放画面未前进：流未推送或该时段无数据，请重新拖动时间轴或检索"));
        }
    } else {
        m_pbStallTicks = 0;
    }
    m_pbLastRealPos = posSec;
}

void PlayerWindow::onPlaybackStopped(const ChannelRef &ch, qlonglong recordId)
{
    Q_UNUSED(ch)
    Q_UNUSED(recordId)
}

void PlayerWindow::snapshotActiveTile()
{
    if (VideoTile *t = m_grid->activeTile()) {
        t->requestSnapshot();
    } else {
        toastInfo(tr("请先选择一路通道"));
    }
}

void PlayerWindow::onGroupChanged(int index)
{
    // 二级维度变化只刷新目录树，绝不触碰顶层模式（m_mode / centerStack）。
    // 当前平台只有一个 /api/v1/group/tree/list 口径，因此这里重新拉取根
    // 目录；后续平台提供两套树时，只需在此按 index 选择不同数据源。
    if (m_api) {
        m_api->fetchChildren(QString());
    }
    Q_UNUSED(index);
}

void PlayerWindow::onGlobalSnapshot()
{
    // 全局抓图：实时页抓当前焦点窗格；回放页抓回放窗格。
    VideoGrid *g = m_mode == 1 ? m_pbGrid : m_grid;
    if (VideoTile *t = g ? g->activeTile() : nullptr) {
        t->requestSnapshot();
    } else {
        toastInfo(tr("请先选择一路通道"));
    }
}

void PlayerWindow::onGlobalCapture()
{
    if (m_mode == 1) {
        toastInfo(tr("回放模式请在窗格顶部 AI 条采集样本"));
        return;
    }
    VideoTile *t = m_grid->activeTile();
    if (!t || !t->player()) {
        toastInfo(tr("请先选择一路通道"));
        return;
    }
    t->captureSample(QStringLiteral("manual"));
    m_sampleShown = -1;
    updateSampleBadge();
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
    ref.resourceId = ref.channelId; // synthetic identity so isValid()/key() work
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
    ref.resourceId = abs;  // synthetic identity so isValid()/key() are stable
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

void PlayerWindow::openEvidenceDialog(VideoTile *tile)
{
    if (!tile) {
        return;
    }
    if (tile->evidenceEvents().isEmpty()) {
        toastInfo(tr("暂无可导出的证据（先播放或扫描一段时间）"));
        return;
    }
    // Review first: the user picks which events go into the folder.
    const QString media = tile->mediaName();
    auto *dlg = new zp::EvidenceDialog(media, tile->evidenceEvents(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &zp::EvidenceDialog::exportRequested, this,
            [this, media](const QVector<zp::EvidenceEvent> &events) {
                const QString dir = QFileDialog::getExistingDirectory(
                    this, tr("选择证据导出目录"));
                if (dir.isEmpty()) {
                    return;
                }
                QString err;
                if (zp::EvidenceExporter::writeReport(media, events, dir, &err)) {
                    toastSuccess(tr("证据已导出：%1（%2 个事件）")
                                     .arg(dir)
                                     .arg(events.size()));
                } else {
                    toastError(tr("导出失败：%1").arg(err));
                }
            });
    dlg->open();
}

void PlayerWindow::fillDemo(const QUrl &url, int count)
{
    for (int i = 0; i < count; ++i) {
        ChannelRef ref;
        ref.deviceId = QStringLiteral("_demo%1").arg(i);
        ref.channelId = QStringLiteral("ch%1").arg(i);
        ref.resourceId = ref.channelId; // synthetic identity so isValid()/key() work
        m_grid->openStream(ref, tr("演示通道 %1").arg(i + 1), true, url);
    }

    // Dev hooks so the AI/OCR path can be exercised without mouse input:
    //   ZP_OCR=1          -> enable text recognition on every demo tile
    //   ZP_ROI="x,y,w,h"  -> frame that analysis region (tile-normalized 0..1)
    if (qEnvironmentVariableIsSet("ZP_OCR")) {
        for (VideoTile *t : m_grid->tiles()) {
            if (t) {
                t->setOcrEnabled(true);
            }
        }
    }
    const QString roiSpec = qEnvironmentVariable("ZP_ROI");
    if (!roiSpec.isEmpty()) {
        const QStringList parts = roiSpec.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() == 4) {
            const QRectF roi(parts[0].toDouble(), parts[1].toDouble(),
                             parts[2].toDouble(), parts[3].toDouble());
            for (VideoTile *t : m_grid->tiles()) {
                if (t) {
                    t->setRoiRect(roi);
                }
            }
        }
    }
    // ZP_ZOOM=1: zoom every demo tile to its ROI; ZP_SHARPEN=1: GPU sharpening.
    if (qEnvironmentVariableIsSet("ZP_ZOOM")) {
        for (VideoTile *t : m_grid->tiles()) {
            if (t) {
                t->setZoomEnabled(true);
            }
        }
    }
    if (qEnvironmentVariableIsSet("ZP_SHARPEN")) {
        for (VideoTile *t : m_grid->tiles()) {
            if (t) {
                t->setSharpenEnabled(true);
            }
        }
    }
    // ZP_DENOISE=1: enable rnnoise voice denoising on every demo tile.
    if (qEnvironmentVariableIsSet("ZP_DENOISE")) {
        for (VideoTile *t : m_grid->tiles()) {
            if (t) {
                t->setDenoiseEnabled(true);
            }
        }
    }
    // ZP_SCAN=1: kick off the smart-timeline scan on the first tile once the
    // media length is known (dev/headless testing).
    if (qEnvironmentVariableIsSet("ZP_SCAN")) {
        if (VideoTile *t = m_grid->tiles().value(0)) {
            QTimer::singleShot(2500, t, [t]() { t->startScan(); });
        }
    }

    // ZP_EVIDENCE=1: open the evidence review dialog a few seconds in (dev).
    if (qEnvironmentVariableIsSet("ZP_EVIDENCE")) {
        if (VideoTile *t = m_grid->tiles().value(0)) {
            QTimer::singleShot(6000, this, [this, t]() { openEvidenceDialog(t); });
        }
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
