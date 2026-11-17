# zero-player 设计 V2 —— 布局模块化 · 全局配置 · 摇杆云台 · 架构选型

> 本文针对第二轮反馈：布局概念化、模块解耦、分割线、乱码、全局配置、对标主流安防客户端、
> 以及 Electron + C++ 混合渲染架构的技术选型。**先设计后实施。**

---

## 一、反馈诊断（逐条定位到代码）

| # | 你的反馈 | 现状定位 | 根因 / 结论 |
| --- | --- | --- | --- |
| 1 | 登录页 logo/动画 + 登录信息面板布局 | `LoginDialog` 一个类里把品牌区+表单区都 build 出来 | **未模块化**：品牌区、表单区应拆成独立 Widget，可独立替换/加动画 |
| 2 | 播放页 顶栏/左目录/中播放/右云台 | `PlayerWindow::buildUi()` 一次性拼装三栏 | **已分区但耦合在单类**：每区应升级为独立 Widget 类，通过信号解耦 |
| 3 | 改一个地方互不影响 | 同上，`PlayerWindow` 持有所有引用 | **缺模块边界**：需要「面板-控制器-信号」结构，改样式/布局不动逻辑 |
| 4 | 布局间淡淡分割线 | QSS `QSplitter::handle` 1px `#0B0F14`，面板另有 `border-right` | 分割线**几乎不可见**（深色背景上同色）；需统一为低对比 1px 亮线 `#232D3A`，贯穿三栏 |
| 5a | 播放窗口布局按钮乱码 | `kLayoutNames = "1×1"` + `QLatin1String` | **根因确认**：`×`=U+00D7，UTF-8 编码 `c3 97`，`QLatin1String` 按 Latin-1 逐字节解释 → 变 `Ã—`。改用 `QString::fromUtf8` 或纯 ASCII（`1x1`） |
| 5b | 配置按钮 / 全局配置（布局·样式·颜色·主题） | 无 | **缺配置系统**：需 Token 化主题 + 偏好设置面板 + QSettings 持久化 |
| 5c | 对标主流客户端 | 参考图 AEGIS AIoT | 见第二节拆解 |

---

## 二、参考图（AEGIS AIoT）拆解 —— 可借鉴的布局概念

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ [Logo/AIoT 标题]      [登录页预览][监控播放页]        ●SIP信令域 ●边缘AI算力 ●流媒体RTT   │ ← 顶栏：品牌 + 页签 + 全局状态
├──────────────┬────────────────────────────────────────────────┬────────────────────────┤
│ GB28181 目录树 │ 工具条: 1画面 4分屏 9分屏 │ 主码流4K H.265 │ AI框选 3D放大 抓图      │ 智能云台(PTZ)  │ 归位 │
│ [搜索 20位编号]│ ┌──────────────┬──────────────┐              │ ┌ 无级矢量摇杆 ─────┐  │
│ 全部│在线│球机 │ │ 01_东门 球机  │ 02_南侧 球机  │              │ │   ↑            │  │
│ │PTZ│物联    │ │  OSD: H.265   │  OSD: H.265   │              │ │ ◀ ● ▶ (拖拽)   │  │ ← 摇杆=方向+速度
│ ▾ 智慧园区    │ │  右下角: 国标号│  右下角       │              │ │   ↓            │  │
│   ▾ 周边防线  │ ├──────────────┼──────────────┤              │ └─────────────────┘  │
│     01_东门 ● │ │ 03_地库 枪机  │ 05_仓库 台机  │              │ 水平Pan 俯仰Tilt 光学Zoom│ ← 姿态数值
│     02_南侧 ● │ └──────────────┴──────────────┘              │ 基座速度 ──●── 5级      │
│   ▸ 危化品库  │                                              │ 镜头: 广角/长焦 近/远    │ ← 镜头控制
│   ▸ 高空瞭望  │                                              │ 雨刷/补光灯/除雾         │
│               │                                              │ 预置位&巡航: 调用/巡航    │ ← 预置位列表
└──────────────┴────────────────────────────────────────────────┴────────────────────────┘
```

**值得学、且我们能落地的点：**
1. **顶栏「状态带」**：不只是标题，放连接/信令/算力/RTT 等运行态 → 做成顶部状态条。
2. **左树「筛选 tab + 徽标」**：全部/在线/球机PTZ/物联感知切换 + 每节点 IoT/机型徽标 → 树节点加标签与快速过滤。
3. **视频格「OSD 信息层」**：左上通道名+PTZ徽标，右上编码/分辨率/帧率，左下时间，右下国标号，底部告警徽标 → 在 `VideoTile` 增加可配置 OSD 叠加。
4. **右栏「摇杆云台」**（最关键）：一个可拖拽的方向盘，拖多远=转多快（矢量/无级），配姿态数值与 STEP 微调、连续模式 → **把现有 8 按钮矩阵升级为 QPainter 自绘摇杆**。
5. **右栏「镜头/预置位」分组**：变倍/聚焦/雨刷/补光灯 + 预置位列表（调用/巡航） → 右栏分区滚动。
6. **工具条放「布局切换 + 码流 + 显示开关」**：布局按钮在工具条左侧，而非塞进顶栏。

---

## 三、目标架构：模块化 + 信号解耦 + Token 主题

### 3.1 UI 模块拆分（改一处不影响别处）

```
MainWindow（外壳：只负责装配与串联）
├─ TopBar           顶栏：品牌 + 当前分组 + 全局状态 + 用户菜单 + 设置按钮   ← 独立 Widget
└─ QSplitter（三栏，1px 可见分割线）
   ├─ DevicePanel     左：筛选 tab + 搜索 + 目录树（懒加载 + 徽标）           ← 独立 Widget
   ├─ VideoWorkspace  中：工具条（布局/码流/显示开关）+ VideoGrid             ← 独立 Widget
   └─ ControlPanel    右：QScrollArea 内 Tab/分组
        ├─ PtzJoystick       摇杆（自绘）
        ├─ LensPanel         变倍/聚焦/雨刷/补光灯
        └─ PresetPanel       预置位 & 巡航
```

**解耦原则：**
- 每个面板自己管理自己的控件与样式，对外只暴露 `signals`（用户意图）与 `setXxx()`（数据注入）。
- `MainWindow` 只做「信号总线」：`DevicePanel::channelActivated → api.startPlay → VideoGrid::openStream`。面板之间不互相 include。
- 样式全部走 `ObjectName` + `theme.qss`（或 Token 系统），**不在 .cpp 里写 `setStyleSheet`**（现有 `video_tile.cpp`、`video_grid.cpp` 里的散落样式要收敛）。
- 布局参数（默认栏宽、分割线颜色）集中在 `ThemeTokens`，改一处全局生效。

### 3.2 全局配置系统（主题/样式/颜色/布局 可配）

新增 `ThemeManager` + `ThemeTokens`：

```cpp
struct ThemeTokens {                 // 全部可视化参数作为数据
    QString name;                    // "深蓝指挥中心" / "浅色" / ...
    QColor bgRoot, bgPanel, bgElevated, bgInput, border, accent, ...;
    int    radiusPanel, radiusCard, fontSizeBase;
    QString fontFamily;
    // 布局偏好
    int    leftPanelWidth, rightPanelWidth;
};

class ThemeManager : public QObject {
    static ThemeManager& instance();
    ThemeTokens current() const;
    void applyTokens(const ThemeTokens&);   // 重新生成 QSS 并 app.setStyleSheet
    void loadPreset(const QString& name);   // 内置深/浅/高对比
    void loadCustom(const QString& path);   // 用户自定义 JSON（可选）
signals:
    void themeChanged();
};
```

- QSS 不再硬编码颜色，改为**模板占位**：`@accent`、`@bgRoot` 等，由 `ThemeManager` 用 `ThemeTokens` 替换后注入（或 Qt 5.14 无 `qss var`，用字符串替换生成最终 QSS）。
- 新增 **设置对话框** `SettingsDialog`（顶栏齿轮按钮打开）：外观主题下拉、主色色板、字号、布局默认栏宽、是否显示 OSD/分割线；保存到 `QSettings`，即时预览。
- 现有 `login_dialog.cpp` / `ptz_panel.cpp` 里的内联样式 → 全部迁到 `theme.qss`。

### 3.3 摇杆云台（替代 8 按钮）

`PtzJoystick : QWidget`，`paintEvent` 自绘：
- 圆形底盘 + 中心球柄；鼠标按下/拖动 → 计算相对中心的偏移向量 (dx,dy)，归一化到 `[-1,1]`。
- **矢量映射**：偏移方向→ `PtzCommand::Type`（8 向，按角度分段）；偏移幅度→ `speed`（1..10，越远越快）。拖动中按节流频率持续下发（连续模式），松开 → `Stop`。
- 中心区（死区）→ 不动作；配 STEP 微调按钮做点动。
- 右侧仍保留姿态数值占位（Pan/Tilt/Zoom，当前平台不返回则显示 `--`）。

### 3.4 分割线与留白规范

- 三栏统一 `1px solid #232D3A` 分隔线（低对比但可见），分割线不因面板背景遮盖。
- 面板内边距统一 12/14px；卡片圆角 10px；间距栅格 4/8/12/16。

---

## 四、技术架构选型结论：Electron + HWND 混合 vs 纯 Qt

你提出的 **Electron 前端 + C++ QtAV 子窗口句柄直挂（SetParent/MoveWindow）** 方案分析：

| 维度 | Electron + HWND 嵌入 | 纯 Qt Widgets（现状） | 纯 Qt QML（可选） |
| --- | --- | --- | --- |
| 视频性能 | **最高**（GPU 子窗口直渲，零拷贝） | 已满足（QtAV GL 渲染，多路已验证） | 同 Qt（QML 也能嵌 native window） |
| UI 表现力 | 最强（Web 生态、动效、Element 等） | 中（QSS 可做，动效弱） | **强**（声明式、动画、现代控件） |
| 开发复杂度 | **高**：双进程/双技术栈、坐标同步、焦点/弹窗层级、打包部署复杂 | 低（单技术栈，已跑通） | 中（需学 QML，但仍是单一 Qt 进程） |
| 视频与 UI 层级 | 需处理「网页盖不住原生窗口」、弹窗避让 | 天然一体，无此问题 | 天然一体 |
| 跨平台 | 每平台 SetParent/嵌入 API 不同，坑多 | Qt 已封装，跨平台平滑 | 同 Qt |
| 迁移成本（从现状） | **重写**：全部 C++ UI 要改成 Web | 0 | 中：UI 层改 QML，逻辑层复用 |

**结论 / 建议：**
- **现阶段不引入 Electron。** 理由：① 视频渲染 QtAV 已稳定多路播放；② 当前规模与团队下，双技术栈的复杂度（坐标同步、弹窗层级、跨平台嵌入 API 差异、打包）收益比不划算；③ HWND 嵌入的强项是「复用成熟 Web 前端」，而我们没有既有 Web 前端包袱。
- **若未来要更强的 UI 表现力/动画（如登录页品牌动画、复杂动效）**：优先演进到 **纯 Qt + QML（Qt Quick）**——同一进程、同样 QtAV 渲染、声明式 UI 动画，**无需** HWND 嵌入与 IPC，规避了 Electron 方案的全部痛点。
- **绑定演进路线**：先把 Widgets 版的**模块化 + Token 主题 + 摇杆 + 配置**做扎实（本次），UI 框架层保持可替换（面板接口不变），未来如需 QML 只换「表现层」，逻辑不动。
- 保留 HWND 嵌入作为**记录在案的备选**（若将来确需 Web 前端团队协作用 Vue 写 UI，再评估）。

> 一句话：**架构方向 = 纯 Qt 单体 + 模块化分层 + Token 主题化 UI**；用「抽象面板接口」把表现层与技术栈解耦，为将来可能的 QML/Web 演进留口子，但现在不背双栈的负担。

---

## 五、实施拆分（建议顺序，可分多轮）

| 轮次 | 内容 | 可验证点 |
| --- | --- | --- |
| R1 基础设施 | `ThemeTokens`+`ThemeManager`；QSS 模板化；修 `×`/`QLatin1` 乱码；统一分割线 | 主题可切、无乱码、分割线可见 |
| R2 UI 模块化 | 拆 `TopBar/DevicePanel/VideoWorkspace/ControlPanel`；MainWindow 变信号总线 | 面板独立可改、互不影响 |
| R3 配置面板 | `SettingsDialog`（主题/主色/字号/栏宽/OSD 开关）+ QSettings | 改完即时生效并持久化 |
| R4 摇杆云台 | `PtzJoystick` 自绘 + 矢量速度 + 连续/STEP | 拖拽控速、方向准确 |
| R5 视频 OSD | `VideoTile` 增加可配置 OSD（名/PTZ/编码/国标号/时间/告警徽标） | 对标参考图的格内信息层 |
| R6 左树增强 | 筛选 tab（全部/在线/PTZ）+ 节点徽标 | 快速过滤 |

每轮结束 `build.bat` 编译 + 截图验证，保持可运行。

---

## 六、待你确认

1. **架构**：认同「纯 Qt 单体 + 模块化 + Token 主题」，暂不引入 Electron？（QML 作为将来备选）
2. **首轮范围**：先做 R1（基础设施+修乱码）还是直接 R1+R2（含面板拆分）一起？
3. **主题**：除深蓝指挥中心，是否要内置「浅色」主题（配置面板可切）？
