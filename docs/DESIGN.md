# zero-player 设计与重构方案

> 目标：把当前「Mock 骨架」重构为**登录 → 组织目录 → 安防多宫格 → 云台**的完整安防播放器，并对登录页与播放页做统一的视觉重设计。

---

## 一、视觉设计系统（Design Tokens）

统一的「深色指挥中心」风格：低饱和深蓝灰底 + 单一冷色主强调色 + 语义化状态色。所有控件通过 `theme.qss` + `QPalette` 统一驱动，杜绝零散硬编码样式。

### 1. 色板

| Token | 值 | 用途 |
| --- | --- | --- |
| `--bg-root` | `#0B0F14` | 窗口最底背景 |
| `--bg-panel` | `#121821` | 面板（树/云台/侧栏） |
| `--bg-elevated` | `#18202B` | 卡片、悬浮、工具栏 |
| `--bg-input` | `#0E141B` | 输入框底 |
| `--border` | `#232D3A` | 常规分隔线/描边 |
| `--border-strong` | `#2E3A49` | 悬浮/聚焦描边 |
| `--text-primary` | `#E6EDF3` | 主文字 |
| `--text-secondary` | `#9AA7B6` | 次要文字 |
| `--text-muted` | `#5F6B7A` | 占位/禁用 |
| `--accent` | `#2F81F7` | 主色（选中、按钮、焦点） |
| `--accent-2` | `#22B8CF` | 渐变副色/强调 |
| `--success` | `#2ECC71` | 在线/播放中 |
| `--warning` | `#E3B341` | 连接中/告警 |
| `--danger` | `#F85149` | 离线/失败 |

> 选中态：`--accent` 描边 + `rgba(47,129,247,.18)` 柔光；在线点：`--success` 8px 圆点。

### 2. 字体与尺寸

- 字体：`"Microsoft YaHei UI", "Segoe UI", sans-serif`；数字用等宽 `"Cascadia Mono", Consolas`。
- 字号：标题 18/15，正文 13，辅助 12，微型 11。
- 圆角：面板 10px、卡片 8px、按钮 8px、输入 8px、tag 6px。
- 间距栅格：4 / 8 / 12 / 16 / 24。
- 顶栏高 56；左栏默认 280（可拖 240–420）；右栏默认 300（可拖 260–360）。

### 3. 登录页布局（品牌双栏卡片，无边框圆角窗口）

```
┌───────────────────────────────┬────────────────────────┐
│  品牌区 (渐变 + 网格纹理 + 光晕) │  表单区 (bg-panel)      │
│  ▣ ZeroPlayer                  │   登录到平台            │
│  跨平台安防视频客户端            │   服务器地址 [........]  │
│                                │   用户名    [........]  │
│  · 组织目录 · 多宫格            │   密码      [........]👁 │
│  · 云台控制 · 实时预览           │   □ 记住密码  □ 离线演示 │
│                                │   [   登  录   ]         │
│  v2.x                          │   (错误提示行)           │
└───────────────────────────────┴────────────────────────┘
```

- 左品牌区：`#0B0F14 → #0E2233` 对角渐变 + 细网格/光晕装饰（QPainter 或半透明贴图），产品名 + 一句话定位 + 能力要点。
- 右表单区：`--bg-panel`，输入框聚焦时 `--accent` 描边；登录按钮主色实心，hover 提亮，loading 态转圈禁用。
- 「离线演示」勾选后跳过网络，使用内置 Mock 数据（便于无后端时演示）。

### 4. 播放页布局（安防模式）

```
┌──────────────────────────────────────────────────────────────────────┐
│ 顶栏: ▣ ZeroPlayer  │ 当前分组: 根资源组  │ [搜索] │ 布局 1 4 9 16 1+5 │  👤admin ▾  ⏻ │
├───────────────┬──────────────────────────────────────────┬───────────┤
│ 组织目录        │            视频宫格 (VideoGrid)            │ 控制面板   │
│ [🔍 搜索通道]   │  ┌────────┬────────┬────────┐            │ [云台][预置位]│
│ ▾ 根资源组      │  │ 通道A  ●│ 通道B  ●│ 通道C  ●│            │           │
│   ▾ 园区A      │  │        │        │        │            │   ◤ ▲ ◥   │
│     ● 大门-球机 │  ├────────┼────────┼────────┤            │   ◀ 停 ▶   │
│     ● 停车-枪机 │  │ 通道D   │ 通道E   │        │            │   ◤ ▼ ◥   │
│   ▸ 园区B      │  └────────┴────────┴────────┘            │ 变倍 +  −  │
│               │                                           │ 速度 ───●─ │
│               │                                           │ 焦距 光圈   │
├───────────────┴──────────────────────────────────────────┴───────────┤
│ 状态栏: ● 已连接 18080 │ 在线 6/8 │ 布局 3×3 │ 选中: 大门-球机 H.264 25fps │
└──────────────────────────────────────────────────────────────────────┘
```

- **顶栏**：品牌、当前分组名、全局搜索、布局分段控件、用户菜单（用户名/切换账号/退出登录）。
- **左栏 组织目录**：搜索 + 树（分组用文件夹图标、通道用摄像头图标 + 在线圆点）；双击/单击通道即播放；右键菜单（播放 / 全屏 / 关闭）。
- **中栏 视频宫格**：`VideoTile` 圆角卡片，选中态主色描边 + 柔光；悬停显示抓图/全屏/关闭；状态覆盖层（连接中 spinner / 播放中 / 失败可重试）。
- **右栏 控制面板**：Tab 切换「云台 / 预置位」。
  - 云台：3×3 方向盘（8 向 + 中心停），变倍 +/−，速度滑条（1–10），焦距/光圈，禁用态整体置灰并提示原因。
  - 预置位：列表 + 调用 / 新增 / 删除。
- **状态栏**：连接状态、在线统计、当前布局、选中通道编码信息。

### 5. 交互与反馈

- 统一 toast（右下角浮层，3s 自动消失，成功/失败/信息三态）。
- 登录失败行内红字；会话过期（HTTP 401）自动回登录页并提示。
- 空闲/无通道时宫格显示引导插画与「从左侧目录选择通道开始预览」。

---

## 二、架构设计与重构

### 1. 分层（保留现有分层，强化平台层）

```
UI          LoginDialog  PlayerWindow  DeviceTreeView  VideoGrid/VideoTile  PtzPanel/PresetPanel
             │登录            │串联           │目录              │分屏/播放             │云台/预置位
应用层       SessionManager（会话/令牌持久化）  MainController（编排：选中→取流→播放→云台）
             │
平台层       IPlatformApi   ←  WebPlatformApi（HTTP/JSON/鉴权）  ｜  MockPlatformApi（离线演示）
             │  登录 / 目录(懒加载) / 取流 / 停流 / 云台 / 预置位
传输层       PlatformHttpClient（QNetworkAccessManager：baseUrl、access-token、JSON 编解码、401 处理）
媒体引擎     QtAV（AVPlayer + VideoOutput）
```

### 2. 平台层接口调整（关键变更）

现 `DeviceNode` 只有单 `id`，无法表达 **(设备编号, 通道编号)** 二元组；取流/云台都需要二者。改为：

```cpp
struct ChannelRef {               // 一路可播放/可控制通道
    QString deviceId;             // 设备(父)国标编号 = 节点 parentDeviceId
    QString channelId;            // 通道国标编号     = 节点 deviceId
    bool isValid() const { return !deviceId.isEmpty() && !channelId.isEmpty(); }
};

struct DeviceNode {
    QString id;                   // 唯一标识（treeId）
    QString name;
    QString type;                 // "group" | "channel"
    bool online = true;
    bool ptzCapable = false;
    ChannelRef ref;               // 仅 channel 节点有效
    QList<DeviceNode> children;
};

class IPlatformApi : public QObject {
    // 登录/会话
    virtual void login(const QString& baseUrl, const QString& user, const QString& pwdMd5) = 0;
    virtual void logout() = 0;
    // 目录（懒加载：按 parentId 拉直接子节点）
    virtual void fetchChildren(const QString& parentId) = 0;   // 根传空串
    // 取流 / 停流
    virtual void startPlay(const ChannelRef& ch) = 0;
    virtual void stopPlay(const ChannelRef& ch) = 0;
    // 云台
    virtual void ptzControl(const ChannelRef& ch, const zp::PtzCommand& cmd) = 0;
    // 预置位
    virtual void queryPresets(const ChannelRef& ch) = 0;
    virtual void callPreset(const ChannelRef& ch, int presetId) = 0;
    virtual void addPreset(const ChannelRef& ch, int presetId) = 0;
    virtual void deletePreset(const ChannelRef& ch, int presetId) = 0;
signals:
    void loginSucceeded(const Session& session);
    void loginFailed(const QString& message);
    void childrenReady(const QString& parentId, const QList<DeviceNode>& nodes);
    void playStarted(const ChannelRef& ch, const QUrl& url, const QString& transport);
    void playStopped(const ChannelRef& ch);
    void streamFailed(const ChannelRef& ch, const QString& message);
    void ptzAcknowledged(const ChannelRef& ch, int cmdType, int speed);
    void presetsReady(const ChannelRef& ch, const QList<PresetInfo>& presets);
    void sessionExpired();
    void errorOccurred(const QString& scope, const QString& message);
};
```

- `MockPlatformApi` 同步实现新接口（离线演示）：内存树、样例视频（`ZERO_PLAYER_DEMO_FILE`）、云台回执。
- `WebPlatformApi` 用 `PlatformHttpClient` 调真实接口（见第三节）。

### 3. 目录加载策略

真实平台目录是**按 parent 懒加载**。客户端策略：
- 根：`parent=""` 拉一级节点；
- 分组节点在 UI 展开时按需 `fetchChildren(group.id)`（保持轻量，支持大组织）；
- 树控件用 `QAbstractItemModel` 的自定义懒加载模型（`canFetchMore/fetchMore`）或在展开时插入占位再替换。

> 保留「全局搜索」：搜索时走平台 `query` 参数（`group/tree/query` + `common-channel/list`），或客户端对已加载节点过滤。MVP 先做客户端过滤 + 平台 `query` 模糊搜索接口二选一，默认客户端过滤。

### 4. 取流与生命周期

- 选中通道 → `startPlay(ref)` → 平台返回多协议地址；客户端按 **rtsp → rtmp → flv → hls** 优先级选（QtAV/FFmpeg 对 rtsp、rtmp、hls 支持最好）。
- 每格记录 `ChannelRef` 与其 stream；关闭格 / 换布局 / 退出时 `stopPlay(ref)` 释放平台侧流。
- 播放失败（超时/错误）覆盖层显示重试按钮，重试重新 `startPlay`。

### 5. 会话与配置持久化（QSettings）

保存：`server.baseUrl`、`username`、`rememberPassword`（**不存明文密码**，或按用户要求用轻量混淆存储）、`lastLayout`、窗口几何、`offlineDemo`。

### 6. 云台命令映射

UI → 平台 `command`：
`Up→up, Down→down, Left→left, Right→right, UpLeft→upleft, UpRight→upright, DownLeft→downleft, DownRight→downright, ZoomIn→zoomin, ZoomOut→zoomout, Stop→stop`。
速度滑条 1–10 → `horizonSpeed=verticalSpeed=speed*25`（0–250），`zoomSpeed=speed`（变倍用低范围）。

---

## 三、平台 API 对接映射（zero-web-server）

统一响应 `{ "code":0, "msg":"success", "data":... }`，`code!=0` 为失败；鉴权头 `access-token: <jwt>`。

| 能力 | Method & Path | 关键参数 | 取用字段 |
| --- | --- | --- | --- |
| 登录 | `GET /api/v1/user/login` | `username`, `password`=md5(明文) | `data.accessToken`, `data.username` |
| 注销 | `GET /api/v1/user/logout` | header token | — |
| 用户信息 | `POST /api/v1/user/userInfo` | header token | `data.username`, `data.role` |
| 组织树 | `GET /api/v1/group/tree/list` | `parent`(分组 id), `hasChannel=true`, `query` | 节点见下 |
| 取流 | `GET /api/v1/play/start/{deviceId}/{channelId}` | 路径参数 | `data.rtsp/rtmp/flv/hls/ws_flv/...` |
| 停流 | `GET /api/v1/play/stop/{deviceId}/{channelId}` | 路径参数 | — |
| 云台 | `GET /api/v1/front-end/ptz/{deviceId}/{channelId}` | `command`,`horizonSpeed`,`verticalSpeed`,`zoomSpeed` | — |
| 预置位查询 | `GET /api/v1/front-end/preset/query/{deviceId}/{channelId}` | | 预置位列表 |
| 预置位调用 | `GET /api/v1/front-end/preset/call/{deviceId}/{channelId}?presetId=` | | — |
| 预置位新增/删除 | `GET /api/v1/front-end/preset/{add|delete}/{deviceId}/{channelId}?presetId=` | | — |

**树节点字段**（`group/tree/list` 返回数组）：
`id`(分组数字 id)、`deviceId`(分组国标编号 / 通道为通道国标编号)、`parentDeviceId`(通道所属设备国标编号)、`name`、`type`(0=分组,1=通道)、`status`(`ON`/`OFF`)、`isLeaf`/`leaf`。

> 通道 → `ChannelRef{ deviceId = node.parentDeviceId, channelId = node.deviceId }`。
> 分组懒加载：以 `parent = node.id`（分组数字 id）继续拉子节点，`hasChannel=true` 合并通道叶子。

---

## 四、文件规划（增量 / 重构）

```
src/
├── main.cpp                    应用装配：主题加载、Api 选择(Mock/Web)、首启登录
├── app/
│   ├── PlayerWindow.{h,cpp}    播放主窗口（替代 MainWindow，新布局）
│   └── SessionManager.{h,cpp}  会话/令牌/配置持久化
├── platform/
│   ├── platform_api.h          重构：ChannelRef、新接口、Session、PresetInfo
│   ├── platform_http_client.{h,cpp}  QNetworkAccessManager 封装
│   ├── web_platform_api.{h,cpp}     真实平台实现（新增，核心）
│   └── mock_platform_api.{h,cpp}    同步新接口（离线演示）
├── ui/
│   ├── theme.{h,cpp}           主题加载（加载 resources/theme.qss + palette）
│   ├── login_dialog.{h,cpp}    品牌双栏登录
│   ├── device_tree_view.{h,cpp} 目录树（懒加载 + 图标 + 在线态 + 右键菜单）
│   ├── ptz_panel.{h,cpp}       云台面板（8 向 + 变倍 + 速度 + 焦距/光圈）
│   ├── preset_panel.{h,cpp}    预置位面板
│   ├── toast.{h,cpp}           浮层提示
│   └── icons.{h,cpp}           图标（SVG/绘制）
├── player/
│   ├── video_grid.{h,cpp}      宫格：布局(1×1/2×2/3×3/4×4/1+5)、占位、选中
│   └── video_tile.{h,cpp}      单格：状态覆盖层、悬停工具、抓图/全屏/关闭
└── resources/
    ├── theme.qss               统一样式表
    └── resources.qrc           图标/纹理/字体
```

## 五、实施步骤（分阶段，可逐步验证）

1. **基础设施**：重构 `platform_api.h`；新增 `PlatformHttpClient`、`SessionManager`、`theme` 与 `resources/theme.qss`；`MockPlatformApi` 适配新接口。**编译通过 + Mock 跑通新布局**。
2. **登录**：`LoginDialog` + `WebPlatformApi::login` 真实接口 + 会话持久化 + 离线演示开关。**接本地后端联调登录成功**。
3. **目录**：`DeviceTreeView` 懒加载接 `group/tree/list`；通道→ChannelRef。**树可展开、可搜索**。
4. **取流播放**：`WebPlatformApi::startPlay/stopPlay`；`VideoTile` 状态机与协议回退；宫格 1+5。**局域网 RTSP 播放**。
5. **云台 + 预置位**：`PtzPanel`/`PresetPanel` 接 `front-end/ptz`、`preset/*`。**云台联动选中通道**。
6. **收尾**：toast、会话过期处理、退出停流、README/文档更新；全量回归编译与本地运行验证。

## 六、风险与取舍

- **无设备/媒体节点**：本地后端能否取到真实 RTSP 取决于 ZMS 与设备；无设备时用「离线演示 + 样例视频」验证播放链路。
- **QtAV 协议**：优先 rtsp/rtmp；若平台只给 flv/hls，QtAV+FFmpeg 一般可播；webrtc 不支持（不作为默认）。
- **大目录性能**：采用懒加载；必要时虚拟化/分页（后续）。
- **密码存储**：默认不落明文；「记住密码」用轻量可逆混淆仅本地便利，文档注明。

---

## 七、待确认事项

1. 视觉主色偏好：冷蓝（默认 `#2F81F7`）/ 青绿 / 其他？
2. 是否需要「记住密码」（涉及本地存储方式）？
3. 优先支持哪些宫格布局：1×1/2×2/3×3/4×4 + 1+5 是否满足？
