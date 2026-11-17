# zero-player

跨平台安防视频客户端。媒体引擎复用 `3rdpart/qtav`（QtAV 1.13 裁剪版），
UI/播放器框架、平台对接（登录、目录、取流、云台、预置位）为本项目自研。

对接平台：**zero-web-server**（Go + Gin，`/api/v1`，统一响应 `{code,msg,data}`，鉴权头 `access-token`）。

## 功能

- **登录**：品牌双栏登录页，服务器地址 / 用户名 / 密码，密码 MD5 后提交；支持「记住密码」（本地混淆存储）与「离线演示」。
- **组织目录**：登录后按平台组织树懒加载分组与通道，支持搜索、在线/离线状态、右键操作。
- **多宫格实时预览**：1×1 / 2×2 / 3×3 / 4×4 / 1+5（一大五小）安防布局；点击通道自动占位播放，重复点击复用已有格。
- **云台控制**：8 方向 + 变倍 + 速度（经平台透传），选中格联动；非云台通道自动置灰。
- **预置位**：查询 / 调用 / 新增 / 删除。
- **会话**：登录过期（HTTP 401）自动回到登录页；退出登录停流并注销。

## 架构分层

```
UI 层      LoginDialog  PlayerWindow  DeviceTreeView  VideoGrid/VideoTile  PtzPanel        (src/ui, src/player)
            │登录           │串联          │目录              │分屏/播放             │云台/预置位
应用层      SessionManager（会话/配置持久化）  PlayerWindow（编排：目录→取流→播放→云台）
            │
平台层      IPlatformApi  ←  WebPlatformApi（HTTP/JSON/鉴权）  ｜  MockPlatformApi（离线演示）
            │  登录 / 目录(懒加载) / 取流 / 停流 / 云台 / 预置位
传输层      PlatformHttpClient（QNetworkAccessManager：baseUrl、access-token、JSON 编解码、401 处理）
媒体引擎    QtAV（AVPlayer + VideoOutput）
```

- 播放器只依赖 `zp::IPlatformApi` 抽象接口，**不关心**平台用 REST / WebSocket / 私有协议。
- 通道以 **(设备国标编号, 通道国标编号)** 二元组 `ChannelRef` 建模；取流/云台均走此二元组。
- 云台不直连设备：`PtzPanel` → `IPlatformApi::ptzControl()` → 平台透传。
- 分屏：`VideoGrid` 以「槽位」建模，支持异形布局（如 1+5）。

## 平台接口契约

见 `src/platform/platform_api.h`。核心方法：

```cpp
class IPlatformApi : public QObject {
    virtual void login(baseUrl, username, password) = 0;   // 密码 MD5 后提交
    virtual void logout() = 0;
    virtual void fetchChildren(const QString& parentId) = 0; // 目录懒加载
    virtual void startPlay(const ChannelRef&) = 0;
    virtual void stopPlay(const ChannelRef&) = 0;
    virtual void ptzControl(const ChannelRef&, const PtzCommand&) = 0;
    virtual void queryPresets/callPreset/addPreset/deletePreset(...) = 0;
signals:
    void loginSucceeded(const Session&);
    void loginFailed(const QString&);
    void childrenReady(const QString& parentId, const QList<DeviceNode>&);
    void playStarted(const ChannelRef&, const QUrl&, const QString& transport);
    void streamFailed(const ChannelRef&, const QString&);
    void presetsReady(...);
    void sessionExpired();
    ...
};
```

- `WebPlatformApi` 为真实实现，端点到 `{code,msg,data}` 的映射见 `docs/DESIGN.md` §3。
- `MockPlatformApi` 为离线演示实现（`ZERO_PLAYER_DEMO_FILE` 指向本地视频即可播放）。
- 平台默认端口 `18080`，登录 `admin` / `admin`（首次登录后请修改）。

## 构建

依赖：Qt 5.14（msvc2017，**32 位**）、FFmpeg 4.2.1 win32 dev。修改 `build.bat` 顶部路径后：

```bat
build.bat
```

脚本会：① 构建 `3rdpart/qtav` 引擎到 `3rdpart/qtav/_build`；② 构建客户端；
③ windeployqt + 拷贝 FFmpeg DLL。产物：`3rdpart/qtav/_build/bin/zero-player.exe`。

> 注意：必须用 **32 位** Qt 的 `windeployqt`，否则会把 64 位 Qt DLL 拷进 32 位程序，
> 运行时报 `0xC000007B`。`build.bat` 已固定使用 `QT_BIN`（32 位）并补拷 `Qt5OpenGL.dll`。

## 运行 / 验证

```bat
:: 正常：启动登录页 → 输入服务器地址/账号 → 进入播放页
zero-player.exe

:: 直接播放本地文件或流（便于自测，仍会先登录）
zero-player.exe D:\test.mp4
zero-player.exe rtsp://user:pass@host:554/stream

:: 离线演示：登录页勾选「离线演示」，或设置环境变量
set ZERO_PLAYER_DEMO_FILE=D:\test.mp4
```

开发调试用环境变量（便于自动化截图/联调，不填则无影响）：
`ZP_DEMO=1`（直接进离线演示）、`ZP_BASE/ZP_USER/ZP_PASS`（自动登录真实平台）、
`ZP_FILL=<N>` + `ZP_FILE=<路径>` + `ZP_LAYOUT=<0..4>`（填充 N 路演示宫格）。

## 视觉设计

统一「冷蓝指挥中心」深色风格，色板/字号/圆角定义于 `docs/DESIGN.md` §1，样式集中在
`src/resources/theme.qss` + `src/ui/theme.cpp`。

- 登录页：品牌渐变区 + 表单卡片（无边框圆角窗口）。
- 播放页：顶栏（品牌/分组/搜索/布局/用户）+ 左目录 + 中宫格 + 右控制面板 + 状态栏。

## 文档

- `docs/CURRENT_STATE.md` — 重构前现状梳理
- `docs/DESIGN.md` — 视觉设计 + 架构设计 + API 映射 + 实施步骤

## 后续可扩展

- 抓图（QtAV `VideoCapture`）、录像（`AVTranscoder`）
- ONVIF 细分云台（预置位/巡航已在平台侧，客户端可扩展面板）
- 电子放大、OSD/水印、轮巡、拖拽换格
- 多路性能：开启硬解、仅解码可见通道
