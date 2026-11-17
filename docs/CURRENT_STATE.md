# zero-player 现状梳理

> 生成时间：2026-11-17。范围：`F:\07.opensource\zero-player` 客户端 + 对接端 `F:\07.opensource\zero-web-server`。

## 1. 代码结构（客户端）

```
src/
├── main.cpp                     入口：Fusion 深色 palette、注册 meta type、创建 MockApi + MainWindow
├── app/
│   ├── app.pro / app.pri         qmake 工程；链接 3rdpart/qtav（预编译 engine）
│   └── MainWindow.{h,cpp}        主窗口：工具栏(分屏/刷新) + 三栏 QSplitter(目录|视频|云台) + 状态栏
├── platform/
│   ├── platform_api.h           IPlatformApi 抽象 + DeviceNode + PtzCommand
│   └── mock_platform_api.{h,cpp} Mock：内存设备树 / 假 rtsp / 云台回执
├── player/
│   ├── video_grid.{h,cpp}       分屏容器：rows×cols 管理 VideoTile，选中格、空位复用
│   └── video_tile.{h,cpp}       单格：AVPlayer + VideoOutput + 标题/状态/关闭
└── ui/
    ├── device_tree_widget.{h,cpp} 左栏目录树(QStandardItemModel) + 名称过滤
    └── ptz_widget.{h,cpp}        右栏云台：8 向按钮 + 速度滑条
```

## 2. 已有能力

| 模块 | 现状 | 评价 |
| --- | --- | --- |
| 媒体播放 | QtAV 1.13（FFmpeg 4.2.1）解码渲染，每格独立 `AVPlayer` | 可用，但无硬解/按需解码优化 |
| 分屏 | 1×1 / 2×2 / 3×3 / 4×4，点击通道自动占空位 | 基础可用；缺 1+5 等安防常用布局、拖拽、轮巡 |
| 设备目录 | 从 `IPlatformApi` 拉树，支持名称过滤 | 结构可用；但**未接真实平台**，只有 Mock |
| 云台 | 8 向 + 变倍 + 速度，`pressed→Stop` 释放式 | 逻辑可用；缺预置位、焦距/光圈、禁用态提示 |
| 登录 | **无** | 缺 |
| 平台对接 | 仅 `MockPlatformApi` | 缺 HTTP/鉴权/取流/云台透传真实实现 |
| 视觉 | 手写零散 QSS + palette，无统一设计系统 | 配色偏灰、层次弱、无品牌感 |

## 3. 与 Web 平台（zero-web-server）的对接缺口

平台为 Go + Gin，`/api/v1` 前缀，统一响应 `{code,msg,data}`，鉴权头 `access-token`。客户端当前完全没有接入，需要补齐：

1. **登录鉴权**：`GET /api/v1/user/login?username&password(md5)` → `data.accessToken`；后续请求带 `access-token`。
2. **组织目录树**：`GET /api/v1/group/tree/list?parent&hasChannel&query`（懒加载）→ 节点 `type=0` 分组 / `type=1` 通道。
3. **取流**：`GET /api/v1/play/start/{deviceId}/{channelId}` → `data.rtsp/rtmp/flv/hls...`；`GET /api/v1/play/stop/{deviceId}/{channelId}`。
4. **云台**：`GET /api/v1/front-end/ptz/{deviceId}/{channelId}?command&horizonSpeed&verticalSpeed&zoomSpeed`，方向取值 `left/right/up/down/upleft/.../zoomin/zoomout/stop`。
5. **通道身份**：节点 `deviceId`=通道国标编号，`parentDeviceId`=设备国标编号；取流/云台均需 (设备编号, 通道编号) 二元组 → 现有 `DeviceNode.id` 单字段不足以表达。

## 4. 关键约束

- 工具链：Qt 5.14.2 **msvc2017 32 位**（`D:\Qt\...`）、VS2017、FFmpeg 4.2.1 win32；`build.bat` 已就绪。
- 引擎 `3rdpart/qtav` 需先编译到 `_build`（本次已后台编译验证）。
- 平台默认端口 `18080`，本地 MySQL/Redis 已在跑，后端可本地启动做联调。

## 5. 结论

骨架（分层 + 分屏 + 云台透传 + Mock）是对的，可保留复用。待做的核心增量：**真实平台 API 实现 + 登录会话 + 按 (设备,通道) 建模 + 视觉重设计 + 安防模式布局**。
