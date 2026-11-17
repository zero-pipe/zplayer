#include "app/MainWindow.h"
#include "ui/device_tree_widget.h"
#include "ui/ptz_widget.h"
#include "player/video_grid.h"

#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QWidget>

#include <functional>

namespace zp {

MainWindow::MainWindow(IPlatformApi *api, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
{
    setWindowTitle(tr("zero-player 安防客户端"));
    resize(1440, 860);
    buildUi();

    connect(m_api, &IPlatformApi::deviceTreeReady, this, &MainWindow::onDeviceTreeReady);
    connect(m_api, &IPlatformApi::streamUrlReady, this, &MainWindow::onStreamUrlReady);
    connect(m_api, &IPlatformApi::ptzAcknowledged, this, &MainWindow::onPtzAcknowledged);
    connect(m_api, &IPlatformApi::errorOccurred, this, &MainWindow::onError);

    connect(m_tree, &DeviceTreeWidget::deviceActivated, this, &MainWindow::onDeviceActivated);
    connect(m_grid, &VideoGrid::activeDeviceChanged, this, &MainWindow::onActiveDeviceChanged);
    connect(m_ptz, &PtzWidget::commandIssued, this, &MainWindow::onPtzCommand);

    toast(tr("正在加载设备目录…"));
    m_api->fetchDeviceTree();
}

MainWindow::~MainWindow() = default;

void MainWindow::openDirect(const QUrl &url)
{
    if (!url.isValid()) {
        return;
    }
    const QString id = QStringLiteral("_direct");
    const QString name = url.fileName().isEmpty() ? url.toString() : url.fileName();
    m_names.insert(id, name);
    m_ptzCapable.insert(id, false);
    m_grid->openStream(id, name, false, url);
    toast(tr("直接播放: %1").arg(name));
}

void MainWindow::buildUi()
{
    auto *toolbar = addToolBar(tr("主工具栏"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    toolbar->addWidget(new QLabel(tr(" 分屏 "), this));
    m_layoutBox = new QComboBox(this);
    m_layoutBox->addItem(tr("1 × 1"));
    m_layoutBox->addItem(tr("2 × 2"));
    m_layoutBox->addItem(tr("3 × 3"));
    m_layoutBox->addItem(tr("4 × 4"));
    m_layoutBox->setCurrentIndex(1);
    toolbar->addWidget(m_layoutBox);
    connect(m_layoutBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onLayoutChanged);

    toolbar->addSeparator();
    auto *refresh = toolbar->addAction(tr("刷新目录"));
    connect(refresh, &QAction::triggered, this, [this]() {
        toast(tr("正在刷新设备目录…"));
        m_api->fetchDeviceTree();
    });

    m_tree = new DeviceTreeWidget(this);
    m_tree->setMinimumWidth(240);
    m_tree->setMaximumWidth(420);

    m_grid = new VideoGrid(this);
    m_grid->setGridSize(2, 2);

    m_ptz = new PtzWidget(this);
    m_ptz->setMinimumWidth(200);
    m_ptz->setMaximumWidth(260);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_tree);
    splitter->addWidget(m_grid);
    splitter->addWidget(m_ptz);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    setCentralWidget(splitter);

    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::onDeviceTreeReady(const QList<DeviceNode> &roots)
{
    m_names.clear();
    m_ptzCapable.clear();

    std::function<void(const QList<DeviceNode> &)> walk = [&](const QList<DeviceNode> &nodes) {
        for (const DeviceNode &n : nodes) {
            if (n.isLeaf()) {
                m_names.insert(n.id, n.name);
                m_ptzCapable.insert(n.id, n.ptzCapable);
            } else {
                walk(n.children);
            }
        }
    };
    walk(roots);

    m_tree->setDeviceTree(roots);
    toast(tr("设备目录已加载"));
}

void MainWindow::onDeviceActivated(const QString &deviceId, const QString &name, bool ptzCapable)
{
    m_names.insert(deviceId, name);
    m_ptzCapable.insert(deviceId, ptzCapable);
    toast(tr("获取流地址: %1").arg(name));
    m_api->fetchStreamUrl(deviceId);
}

void MainWindow::onStreamUrlReady(const QString &deviceId, const QUrl &url)
{
    const QString name = m_names.value(deviceId, deviceId);
    const bool ptz = m_ptzCapable.value(deviceId, false);
    m_grid->openStream(deviceId, name, ptz, url);
    toast(tr("开始播放: %1  (%2)").arg(name, url.toString()));
}

void MainWindow::onActiveDeviceChanged(const QString &deviceId, bool ptzCapable)
{
    m_activeDeviceId = deviceId;
    m_ptz->setTarget(deviceId, ptzCapable);
    if (!deviceId.isEmpty()) {
        setWindowTitle(tr("zero-player 安防客户端 — %1").arg(m_names.value(deviceId, deviceId)));
    } else {
        setWindowTitle(tr("zero-player 安防客户端"));
    }
}

void MainWindow::onPtzCommand(const QString &deviceId, const zp::PtzCommand &cmd)
{
    if (deviceId.isEmpty()) {
        return;
    }
    m_api->ptzControl(deviceId, cmd);
    toast(tr("云台指令已发送: %1  speed=%2").arg(deviceId).arg(cmd.speed));
}

void MainWindow::onPtzAcknowledged(const QString &deviceId, int cmdType, int speed)
{
    Q_UNUSED(cmdType)
    toast(tr("云台指令已透传: %1 (speed=%2)").arg(deviceId).arg(speed));
}

void MainWindow::onError(const QString &deviceId, const QString &message)
{
    toast(tr("错误 [%1]: %2").arg(deviceId.isEmpty() ? tr("平台") : deviceId, message));
}

void MainWindow::onLayoutChanged(int index)
{
    static const int sizes[4][2] = {{1, 1}, {2, 2}, {3, 3}, {4, 4}};
    if (index < 0 || index >= 4) {
        return;
    }
    m_grid->setGridSize(sizes[index][0], sizes[index][1]);
    toast(tr("分屏布局: %1 × %2").arg(sizes[index][0]).arg(sizes[index][1]));
}

void MainWindow::toast(const QString &text)
{
    statusBar()->showMessage(text, 5000);
}

} // namespace zp
