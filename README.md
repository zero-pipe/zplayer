# zero-player

跨平台安防视频客户端（智能物联客户端引擎）。媒体引擎基于 **libmpv**（render API +
QOpenGLWidget），UI/播放器框架、平台对接（登录、目录、取流、云台、预置位）为本项目自研。

对接平台：**zero-web-server**（Go + Gin，`/api/v1`，统一响应 `{code,msg,data}`，鉴权头 `access-token`）。

## 功能

- **登录**：品牌双栏登录页，服务器地址 / 用户名 / 密码，密码 MD5 后提交；支持「记住密码」（本地混淆存储）与「离线演示」。
- **组织目录**：登录后按平台组织树懒加载分组与通道，支持搜索、在线/离线状态。
- **多宫格实时预览**：1×1 / 2×2 / 3×3 / 4×4 / 1+5（一大五小）安防布局；点击通道自动占位播放，重复点击复用已有格。
- **云台控制**：8 方向 + 变倍 + 速度（经平台透传），摇杆式交互；非云台通道自动置灰。
- **会话**：登录过期（HTTP 401）自动回到登录页；退出登录停流并注销。

## 架构分层

```text
UI 层        LoginDialog  PlayerWindow  DeviceTreeView  VideoGrid/VideoTile  PtzPanel
应用层        SessionManager（会话/配置持久化）  PlayerWindow（编排：目录→取流→播放→云台）
平台层        IPlatformApi  ←  WebPlatformApi（HTTP/JSON/鉴权）  ｜  MockPlatformApi（离线演示）
传输层        PlatformHttpClient（QNetworkAccessManager：baseUrl、access-token、JSON、401）
媒体引擎      IMediaPlayer 抽象  ←  MpvMediaPlayer（主，libmpv）｜ QtavMediaPlayer（可选，遗留）
```

目录结构：

```text
src/
  app/        应用编排与状态（PlayerWindow, SessionManager）
  platform/   平台对接抽象与实现（IPlatformApi / WebPlatformApi / MockPlatformApi / HttpClient）
  player/     播放核心
    media_player.h      引擎无关接口（IMediaPlayer / IMediaRenderer / MediaState）
    media_factory.*     按 MediaBackend 创建引擎
    video_tile.*  video_grid.*   分屏控件
    backends/           具体引擎适配（仅此处 include 引擎头文件）
      mpv_media_player.*    libmpv（主引擎）
      mpv/qthelper.hpp      libmpv 官方 Qt 辅助
      qtav_media_player.*   QtAV（遗留，默认不编译）
  ui/         可复用控件（登录页、目录树、云台、主题、图标、Toast）
  resources/  resources.qrc + theme.qss
3rdpart/
  mpv-sdk/    libmpv 预编译 SDK（x64）：include + libmpv-2.lib 入库；运行时 DLL 不入库
  qtav-sdk/   QtAV 预编译 SDK（x86 only，可选，默认不用）
```

设计要点：

- **引擎隔离**：UI 只依赖 `IMediaPlayer`，不感知底层是 libmpv / QtAV。新增引擎只需实现接口并在 `media_factory` 注册，且只有 `backends/` 下的适配器会 include 引擎头文件。
- 播放器只依赖 `zp::IPlatformApi` 抽象，不关心平台用 REST / WebSocket / 私有协议。
- 通道以 **(设备国标编号, 通道国标编号)** 二元组 `ChannelRef` 建模；取流/云台均走此二元组。
- 云台不直连设备：`PtzPanel` → `IPlatformApi::ptzControl()` → 平台透传。
- 分屏：`VideoGrid` 以「槽位」建模，支持异形布局（如 1+5）。
- 主题：light/dark 色板以 `%token%` 模板替换运行时生成 QSS（见 `ui/theme.*` 与 `resources/theme.qss`）。

## 平台接口契约

见 `src/platform/platform_api.h`。核心方法：

```cpp
class IPlatformApi : public QObject {
    virtual void login(baseUrl, username, password) = 0;      // 密码 MD5 后提交
    virtual void logout() = 0;
    virtual void fetchChildren(const QString& parentId) = 0;  // 目录懒加载
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
    void sessionExpired();
    ...
};
```

- `WebPlatformApi` 为真实实现；`MockPlatformApi` 为离线演示实现。
- 平台默认端口 `18080`，登录 `admin` / `admin`（首次登录后请修改）。

## 构建

依赖：Qt 5.14.2（**msvc2017_64**）、libmpv SDK（`3rdpart/mpv-sdk`，x64）。
工具链路径默认自动探测；若你的环境非标准，复制 `build.local.bat.example` 为
`build.local.bat`（已被 git 忽略）并填写 `VCVARS` / `QT_BIN`。

```bat
build.bat
```

脚本会：链接 mpv-sdk → qmake + nmake 构建到 `build/` → windeployqt + 拷贝 Qt/mpv 运行时 DLL 到 `out/`。
产物：`out\zero-player.exe`。

> 注意：libmpv 运行时 DLL（`libmpv-2.dll` + ffmpeg/libass 等）不入库，首次构建前需把
> `3rdpart/mpv-sdk/bin/x64/*.dll` 放到可被部署的位置（脚本会自动拷到 `out/`）。

## 运行 / 验证

```bat
:: 正常：启动登录页 → 输入服务器地址/账号 → 进入播放页
out\zero-player.exe

:: 直接播放本地文件或流（便于自测）
out\zero-player.exe D:\test.mp4
out\zero-player.exe rtsp://user:pass@host:554/stream
```

开发调试用环境变量（不填则无影响）：
`ZP_DEMO=1`（直接进离线演示）、`ZP_BASE/ZP_USER/ZP_PASS`（自动登录真实平台）、
`ZP_FILL=<N>` + `ZP_FILE=<路径>` + `ZP_LAYOUT=<0..4>`（填充 N 路演示宫格）、
`ZP_SHOT=<png>`（渲染后自截图）。

## 视觉设计

统一「冷蓝指挥中心」清爽浅色（默认）/ 深色可切换风格，色板/字号/圆角定义于
`src/ui/theme.*`，样式集中在 `src/resources/theme.qss`。

- 登录页：品牌渐变区 + 表单卡片（无边框圆角窗口）。
- 播放页：顶栏（品牌/分组/搜索/布局/用户）+ 左目录 + 中宫格 + 右控制面板 + 状态栏。

## 后续可扩展

- 引擎能力参数化暴露为 `IMediaPlayer` 设置项：字幕 / 滤镜 / 硬件解码（cuda / d3d）。
- 抓图 / 录像、电子放大、OSD/水印、轮巡、拖拽换格。
- 多路性能：硬件解码、仅解码可见通道。
