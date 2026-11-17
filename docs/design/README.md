# zero-player 一期设计稿 · 评审说明

> 浏览器直接打开 `login.html` / `player.html` 评审。右上角按钮可切换 **深/浅主题**（默认浅色，对齐 web 平台）。
> 截图：`login_light.png` / `login_dark.png` / `player_light.png` / `player_dark.png`。

## 一期目标（已收敛并实现）

1. **布局边界清晰**：顶栏 / 工作区工具条 / 左目录 / 中宫格 / 右云台 / 底状态栏，模块分区、1px 淡分割线。
2. **目录对齐 web 平台**：仅「分组 → 通道」两级、只显示名称，不做类型筛选与徽标。
3. **只做遥测云台**：摇杆（方向 + 矢量控速）+ 基座速度；镜头/预置位/巡航/AI 工具条本版不做。

## 视觉要点

- **少文字、图标自表意**：输入框图标 lead，布局切换用图标段控，工具条用图标 chip。
- **清爽配色**：主色 `#1565C0`（对齐 web），浅灰蓝背景 + 白卡片；深色为可选（顶栏齿轮菜单切换）。
- **登录页**：主视觉为「六边形 + 传感眼 + 物联节点」logo，表达智能物联引擎；服务器地址收进「服务器设置」弹层，存本地。
- **播放页**：顶栏（品牌/分组/状态/设置/用户）+ 工作区工具条（布局/搜索）+ 三栏。

## 实现落点（已落地到 Qt 代码）

- 主题：`src/ui/theme.{h,cpp}`（ThemeTokens light/dark + ThemeManager 运行时切换），`src/resources/theme.qss`（%token% 模板）。
- 登录：`src/ui/login_dialog.{h,cpp}` + `src/ui/icon_field.{h,cpp}`，logo 由 `icons::brandHero` 自绘。
- 云台：`src/ui/ptz_joystick.{h,cpp}`（自绘矢量摇杆）+ `src/ui/ptz_panel.{h,cpp}`。
- 图标：`src/ui/icons.{h,cpp}`（glyph 线性图标 + brandHero + appLogo）。
- 主题持久化：`SessionManager::theme()/setTheme()`，启动时读取。

## 字段扩展说明

布局已定型，后续要展示哪些字段（编码/帧率/国标号/告警等）只需在 `VideoTile` OSD 与 `DeviceTreeView` 节点里增删，不影响布局骨架。
