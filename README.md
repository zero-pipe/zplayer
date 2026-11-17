# zero-player

跨平台安防播放器客户端。媒体引擎复用 `3rdpart/qtav`（QtAV 1.13 裁剪版），
播放器框架（目录树、分屏、云台面板、平台对接）为本项目自研。

## 架构分层

```
UI 层     DeviceTreeWidget  VideoGrid/VideoTile  PtzWidget      (src/ui, src/player)
            │ 点击通道            │ 分屏展示            │ 云台按钮
            ▼                     ▼                     ▼
应用层     MainWindow  —— 串联 目录→取流→播放→选中→云台          (src/app)
            │
            ▼
平台层     IPlatformApi (抽象接口)  ←— 后续替换为真实 Web 平台实现  (src/platform)
            │  设备树 / 取流地址 / 云台透传
            ▼
媒体引擎   QtAV: AVPlayer + VideoOutput/GL 渲染 + FFmpeg 解码/协议   (3rdpart/qtav)
```

- 播放器只依赖 `zp::IPlatformApi` 抽象接口，**不关心** Web 平台用 REST / WebSocket / 私有协议。
  现在用 `MockPlatformApi` 提供假数据，真实接口到位后新增一个实现类即可，UI/播放器无需改动。
- 云台不直连设备：`PtzWidget` → `IPlatformApi::ptzControl()` → 平台透传。
- 分屏：`VideoGrid` 管理 rows×cols 的 `VideoTile`，每格独立 `AVPlayer` + `VideoOutput`。

## 平台接口契约（实现这个即可对接真实平台）

见 `src/platform/platform_api.h`：

```cpp
class IPlatformApi : public QObject {
public:
    virtual void fetchDeviceTree() = 0;                                  // 拉目录
    virtual void fetchStreamUrl(const QString& deviceId) = 0;            // 取播放 URL
    virtual void ptzControl(const QString& deviceId, const PtzCommand&) = 0; // 云台透传
signals:
    void deviceTreeReady(const QList<DeviceNode>& roots);   // 目录就绪
    void streamUrlReady(const QString& deviceId, const QUrl& url);       // URL 就绪
    void ptzAcknowledged(const QString& deviceId, int cmdType, int speed);
    void errorOccurred(const QString& deviceId, const QString& message);
};
```

- `DeviceNode`: 目录树节点（group 有 children；channel 为叶子，含 `online`/`ptzCapable`）。
- 实现方式建议：接口内部发 HTTP/WS 请求，成功后 `emit` 对应信号（可跨线程，用队列连接）。
- 新增实现类后，在 `main.cpp` 把 `new zp::MockPlatformApi` 换成你的实现即可。

## 构建

依赖：Qt 5.14（msvc2017，32 位）、FFmpeg 4.2.1 win32 dev。修改 `build.bat` 顶部路径后：

```bat
build.bat
```

脚本会：① 构建 `3rdpart/qtav` 引擎到 `3rdpart/qtav/_build`；② 构建客户端；
③ windeployqt + 拷贝 FFmpeg DLL。产物：`3rdpart/qtav/_build/bin/zero-player.exe`。

> 若自定义工具链，注意：客户端 qmake 需带 `-cache <engine>/.qmake.cache`
> 以复用引擎的架构(`TARGET_ARCH`)与 `QTAV_VERSION`，否则库名/路径会不匹配。

## 运行 / 验证

```bat
:: 正常：启动后左侧目录点击通道 → 中部对应分屏开始播放，选中格可云台控制
zero-player.exe

:: 直接播放本地文件或流（便于自测）
zero-player.exe D:\test.mp4
zero-player.exe rtsp://user:pass@host:554/stream

:: Mock 模式下让每个通道都返回本地样例视频
set ZERO_PLAYER_DEMO_FILE=D:\test.mp4
```

## 当前功能

- 设备目录树（平台拉取，支持搜索过滤）
- 点击通道取流播放（多路分屏；重复点击复用已有格）
- 分屏 1×1 / 2×2 / 3×3 / 4×4
- 选中格云台控制（方向/变倍/速度/停止，经平台透传；非云台/离线通道自动禁用）
- 单格关闭、状态显示（连接中/播放中/失败）
- 命令行直接打开媒体

## 后续可扩展（在现有骨架上）

- 真实 `IPlatformApi` 实现（REST/WS + 登录鉴权）
- ONVIF/厂商云台细分（预置位、巡航）——仍走平台透传
- 抓图（QtAV `VideoCapture`）、录像（`AVTranscoder`）、通道封面（`VideoFrameExtractor`）
- OSD/水印（QtAV `Filter` 或渲染器叠加）、电子放大（`setRegionOfInterest`）
- 多路性能：开启硬解、仅解码可见通道、布局模板/轮巡
