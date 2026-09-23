# 设计 → QML 契约：共通 UI 壳与导航

> 用途：本文是「共通壳 + 导航 + 组件」的唯一真源，供设计会话与开发会话对齐。
> 状态：2026-08-16 设计定稿；2026-08-18 五项导航已实装。当前采用 5 个入口、3 个常驻页面实例。

## 0. 结论先行

1. 顶层导航从 **3 Tab**（主页面 / 指标与策略 / 全市场时刻股价）升级为 **5 项**：`主页 / 市场快照 / 指标与策略 / 回测任务 / 执行中心`。
2. 共通壳 = **TopBar（5 项导航） + 二级抽屉（仅指标与策略页） + StatusBar**，其余全是页面内容。
3. **「多窗口」不是导航项，是一个功能**（见 §1.1）：把单股图表弹出为独立窗口，可同时开多份，入口在主页（实时行情）和回测页（回测行情）。
4. 已存在 QML 组件 `PPTheme / AppTopBar / AppStatusBar / PpButton / WorkspaceTabButton` 可复用；`SecondaryDrawer / PpPanel / MetricCard / PpInput / Toggle / CheckRow / ProgressBar / ToastQueue` 是待新增目标。
5. **响应式尺寸**：Figma 设计稿是**初始参考大小**（1600×1000）；QML 实现时**水平铺满窗口、高度动态调整**（图表/多窗口同理），不把参考稿的固定像素写死。

---

## 1. 唯一导航结构（5 项）

| # | 导航项 | 旧对应 | 页面 | 说明 |
|---|---|---|---|---|
| 0 | 主页 | 主页面 | `DashboardPage` | 默认入口，无抽屉；提供实时行情多窗口入口 |
| 1 | 市场快照 | 全市场时刻股价 | `MarketSnapshotPage` | 无抽屉，右侧 318 摘要栏 |
| 2 | 指标与策略 | 指标与策略 | `StrategyResearchPage` | **有左侧 318 抽屉**，抽屉内 7 个子入口 |
| 3 | 回测任务 | 抽屉「回测研究」提升 | 路由到 `StrategyResearchPage.subPage=0` | 已实现 |
| 4 | 执行中心 | 抽屉「策略执行」提升 | 路由到 `StrategyResearchPage.subPage=5` | 已实现 |

- active 项随页面切换（`TabBar.currentIndex` / 每页激活项见 §4）。
- 「指标与策略」抽屉三组菜单（与现有 `Main.qml` subNav 一致）：
  - 研究与构建：回测研究 / 自定义策略 / 组合回测 / 多股回测 / 参数网格扫描
  - 策略执行：执行与审批（已提升为「执行中心」一级导航，抽屉入口移除）
  - 数据管理：数据导入与同步

**Figma 页面组织**（已按 5 项导航重排）：

| 页面 | 内容 |
|---|---|
| `01 主页` | Multi-chart + 多窗口示例 ×2 + 全局消息弹窗 |
| `02 市场快照` | A股全市场时刻股价（26:972） |
| `03 指标与策略` | 6 个子页（选择中心/计算规则/多指标组合/运行时插件管理/横截面轮动/二级导航骨架） |
| `04 回测任务` | 回测任务（1744:1330） |
| `05 执行中心` | 执行中心（1682:646） |

> **壳 vs 内容约定**：只有 5 个一级页需要完整壳（TopBar + 5 项导航 + 可选抽屉）。`03 指标与策略` 的 6 个子页在 QML 里是**纯内容**（壳由 `Main.qml` 的 `AppTopBar` + `subNav` 统一提供，通过抽屉切换）；Figma 里子页保留壳仅为看图上下文，不代表子页各带一份壳。

### 迁移映射（旧 3 Tab → 新 5 项）

| 旧 Tab | 新导航项 | 处理 |
|---|---|---|
| 主页面 | 主页 | 改名，`DashboardPage` 内容不变 |
| 全市场时刻股价 | 市场快照 | 改名，`MarketSnapshotPage` 内容不变 |
| 指标与策略 | 指标与策略 | 不变 |
| （抽屉·回测研究） | 回测任务 | 提升为一级导航 |
| （抽屉·执行与审批） | 执行中心 | 提升为一级导航 |

### 1.1 「多窗口」功能（非导航项）

> 实施状态（2026-09-02 08:21 修正）：实时行情子窗口已按 Figma `PPTrader / 多窗口 / 实时行情` 实现。左侧品种作用域仅为本窗口；右侧默认 1m，可动态添加/切换 5/10/15/30/60m，最多 10 张。早期固定四图方案已替代。日线不从单日分钟数据伪造；回测多窗口仍未实现。盘中长稳见 `docs/realtime-multiwindow-acceptance-2026-09-02.md`。

「多窗口」是把**单股图表弹出为独立窗口、可同时开多份**的能力，不是一级导航项。

- **入口都在「主页」左侧**（`DashboardPage` 的 Instrument Workspace）：
  - **实时行情入口**：在「实时品种」选择器**下方**（按钮 `⧉ 实时行情多窗口`）→ 弹出实时 K 线独立窗口。
  - **回测窗口入口**：在「回测品种」选择器**下方**（按钮 `⧉ 回测多窗口`）→ 弹出回测 K 线独立窗口。
  - **从哪个入口开，窗口就是哪种行情**。
- **一个窗口 = 一个品种（或一个策略 × 一个品种）× 多个周期**：窗口内图表列表显示同一标的的不同周期（1min/5min/15min/日线）同时参考；**多个品种 = 开多个窗口**。
- **回测用流式回测**：按周期速率回放 K 线——1min 周期在 1 分钟内算完该周期的策略，5min 周期在 5 分钟内算完；实时行情数据接收同理。
- 每个窗口是常驻/独立实例，不因切换导航而销毁（沿用「页面不销毁」的既有约定）。

### 1.2 图表风格（统一）

- 所有 K 线图（主页、多窗口、回测）统一采用 **TradingView 风格**，以主页图表（`2:6` Chart Workspace）为基准。
- 交互（十字线、缩放、刷选、Tooltip）沿用主页既有实现；多窗口图表只做展示/独立窗口，不重复实现一套新图表。

---

## 2. 共通壳规格（跨页一致）

### 2.1 TopBar（1600×64，`bgBar`，底边 1px `border`）

| 元素 | 坐标/尺寸 | 视觉 |
|---|---|---|
| 品牌块 | 16,15,34,34 | `cyan`、8 圆角、"PP" 12/Bold、`bgCanvas` |
| 品牌名 | 62,10,142,26 | "PPTrader" 18/Bold、`textPrimary` |
| 副标题 | 62,34,160,18 | "量化交易工作台" 10/Regular、`textMuted` |
| 导航项（5） | 从 x≈250 起、高 36 | 12/Medium `textMuted`；active = `bgSelected` + 1px `cyan` + 12/Bold，7 圆角 |
| 行情状态 | 右侧 | `PpButton`，在线 `green` |
| 工作区选择 | 右侧 | "A股 · 全市场" |
| 设置 | 最右 | "设置 ⋯" |

> 现有 `AppTopBar.qml` 只有品牌 + 3 个右按钮，需把 5 项导航加进去；导航项复用 `WorkspaceTabButton` 改造。

### 2.2 二级抽屉（318px，`bgBar`，右边 1px `border`，仅「指标与策略」显示）

| 元素 | 视觉 |
|---|---|
| 标题 "策略研究" | 15/Bold `textPrimary` |
| 副标题 "指标与策略 · 二级导航" | 10/Regular `textMuted` |
| 分组标签（研究与构建/策略执行/数据管理） | 10/Bold `textMuted` |
| 菜单项 | 12/Regular `textMuted`；active = `bgSelected` + 1px `cyan` + `cyan` 文字 |
| 底部操作提示 | 290×130 卡片 |

### 2.3 StatusBar（1600×48，`bgBar`，顶边 1px `border`）

- 左：状态点 8×8 + 数据源/覆盖率/延迟（`textMuted`）。
- 中：个股计数 + 快照时刻（`cyan`）。
- 右：CPU/MEM/渲染耗时（`textMuted`，右对齐）。
- ⚠️ CPU/MEM/16ms 是设计示例值，无遥测 API 时显示 "—" 或隐藏，禁止硬编码。

---

## 3. 共通组件清单（Figma → QML）

### 3.1 已存在（复用/改造）

| QML 文件 | 角色 | 改造点 |
|---|---|---|
| `PPTheme.qml` | 20 色 token + `fontFamily` | 无需改 |
| `AppTopBar.qml` | 顶栏 | **加 5 项导航** |
| `AppStatusBar.qml` | 底栏 | 无需改 |
| `PpButton.qml` | 按钮 | 无需改 |
| `WorkspaceTabButton.qml` | Tab 按钮 | 复用于 5 项导航 |

### 3.2 需新增

| 组件 | 对应 Figma | 关键属性 |
|---|---|---|
| `SecondaryDrawer.qml` | 318 左抽屉 | `visible` / `model` / `currentIndex` |
| `PpPanel.qml` | 面板（bgPanel + border + 圆角 10） | `title`（标题槽）/ `content` |
| `MetricCard.qml` | 指标卡（bgRaised + border + 圆角 8） | `label` / `value` / `accent` |
| `PpInput.qml` | 输入框（bgInput + border + 圆角 6） | `label` / `text` / `focus` |
| `PpSegmentButton.qml` | 分段/快捷按钮 | `checked` / 成组 |
| `Toggle.qml` | 开关（28×16，on=cyan） | `checked` |
| `CheckRow.qml` | 复选行 | `checked` / `label` |
| `ProgressBar.qml` | 进度条（bgRaised + cyan 填充） | `from` / `to` / `value` |
| `DataTable.qml` | 三栏表（持仓/委托/成交） | `model` / `columns` |
| `ToastQueue.qml` | 全局消息弹窗（顶部居中堆叠） | `enqueue(msg)` / 队列 / max 5 |
| `Toast.qml` | 单条消息弹窗 | `type` / `title` / `message` |

### 3.3 控件精确规格（实现级）

**按钮 PpButton**（圆角 6/7，文字 11/12 Medium，高 24/28/30/32/38/40 按场景取）

| 变体 | 填充 | 描边 | 文字色 | 用途 |
|---|---|---|---|---|
| primary | cyan 或 purple | 无 | `bgCanvas` | 主操作（执行回测、＋添加图表、应用并刷新） |
| danger | `bgPanel` | red 1px | red | 危险操作（停止、拒绝、卸载） |
| default | `bgRaised` | border 1px | `textPrimary` | 普通操作 |
| disabled | `bgRaised` | border | `textMuted`（45% 不透明度） | 禁用 |

**面板 PpPanel**：`bgPanel` + 1px `border` + 圆角 10（控制卡 9）；标题 13 Bold `textPrimary`；内边距 16–20。

**指标卡 MetricCard**：`bgRaised` + 1px `border` + 圆角 8；标签 9 Regular `textMuted`，数值 13 Bold `textPrimary`。

**输入框 PpInput**：`bgInput` + 1px `border` + 圆角 6（聚焦 cyan）；标签 8/9 `textMuted`，值 10/11 `textPrimary`。

**开关 Toggle**：轨道 28×16 圆角 8（on=`cyan`，off=`bgRaised`+border）；圆点 12×12 白色（on 右 / off 左）。

**复选 Checkbox**：12×12 圆角 3（选中 cyan + 白色勾，未选 `bgRaised`+border）。

**表格 DataTable**：表头 10 Medium `textMuted`；行 11 Regular `textPrimary`；斑马行 `bgRaised` 圆角 6；行高 38–43。

**状态徽标 Chip**：`bgSelected` + 1px（cyan/green/yellow）+ 文字同色，圆角 6，高 22/24/28。

**选中态（全局）**：`bgSelected` + 1px `cyan` + 文字 `cyan`；在线/成功 `bgSuccess`+`green`；警告 `yellow`。

**消息弹窗 Toast**：窗口顶部水平居中，宽 420、高 56、圆角 8、`bgRaised`+1px `border`；内部 = 左侧 8×8 类型色点 + 标题 12 Bold `textPrimary` + 内容 11 Regular `textMuted` + 右侧 `×`；类型色 success=green / info=cyan / warning=yellow / error=red；间距 8。

**弹窗队列行为**（`ToastQueue`）：
- 新消息从**顶部**进入，旧的向下挤（堆叠）；
- **最多显示 5 条**；第 6 条排队等待最底部那条淡出后补位；
- 每条淡入/淡出；**显示时长只控制淡入淡出（显隐），位置由队列里有无待显示消息决定**；
- 队列走后台，弹窗不阻塞主线程。

---

## 4. 每页 active 项 + 区块清单

### 4.1 主页（active=0）· `DashboardPage`
- 主页面双图（实时/回测）——内容不变，沿用现有 `DashboardPage.qml`，图表为 TradingView 风格（基准）。
- 左侧 Instrument Workspace 底部新增两个多窗口入口按钮：
  - 「实时品种」下方 → `⧉ 实时行情多窗口`（弹出实时 K 线独立窗口）；
  - 「回测品种」下方 → `⧉ 回测多窗口`（弹出回测 K 线独立窗口）。

### 4.2 市场快照（active=1）· `MarketSnapshotPage`
- 时间控制卡 + 主散点图卡 + 右侧摘要栏。
- 完整规格见 `docs/figma-qml-handoff.md`（节点 `26:972`）。

### 4.3 指标与策略（active=2）· `StrategyResearchPage`（抽屉 7 子入口）
| 子入口 | 对应 Figma frame | 状态 |
|---|---|---|
| 回测研究 | 选择中心 `26:56` | 已完成 |
| 自定义策略 | 多指标组合 `26:58` + 计算规则 `26:57` | 已完成 |
| 组合回测 | （组合回测面板） | 已有 QML |
| 多股回测 | 横截面轮动回测 `1394:281` | 已完成 |
| 参数网格扫描 | （参数网格面板） | 已有 QML |
| 数据导入与同步 | （导入面板） | 已有 QML |

### 4.4 回测任务（active=3）· 目标规格（当前无独立 `BacktestTaskPage.qml`）

**定位**：任务化回测的统一入口（创建 → 运行 → 结果 → 手动归档）。

**布局**：TopBar（回测任务 active）+ 左侧 280px 子导航 + 主内容区（直接复用现有面板）。

子导航 6 项（对应已跑通的能力，直接搬现有 QML 面板）：

| 子入口 | 复用面板 | 后端 |
|---|---|---|
| 单股回测 | `LocalBacktestSelector` | `BacktestEngine` |
| 多股横截面 | `CrossSectionalBacktestPanel` | `CrossSectionalBacktestService` |
| 组合回测 | `PortfolioBacktestPanel` | `PortfolioBacktestService` |
| 批量回测 | `BatchBacktestPanel` | `BatchBacktestService` |
| 参数网格扫描 | `ParameterGridPanel` | — |
| 归档结果 | `BacktestArchivePanel` | 归档链路 |

**主内容区**：配置面板（策略/标的/周期/K 线数 + 运行）+ 结果面板（权益曲线 + 指标卡 + 成交明细）+ **逐根 K 线回放条**。

**关键约定**：
- **结果深度到单根 K 线**：回放逐根推进 K 线，策略信号在该根上实时计算；暂停即停止输入下一根，图表与策略同步冻结（复用 `replayBar`/`startCustomStrategyReplay`）。
- **手动归档**：结果不自动归档，用户手动点「归档结果」；无价值的回测不归档。
- 提供「回测行情」多窗口入口（弹出回测 K 线独立窗口，可多份并行）。

### 4.5 执行中心（active=4）· 目标规格（当前无独立 `ExecutionCenterPage.qml`）

8 个区块，画布 1600×1320（原 1000 已扩展）：

| 区块 | 内容 | 后端字段 |
|---|---|---|
| ① 执行概览 | 7 个指标卡（总权益等） | — |
| ② 策略运行状态 | Strategy Panel | — |
| ③ 审批队列 | Approval Panel | — |
| ④ 执行配置 | Exec Config Panel | — |
| ⑤ 持仓/委托/成交 | 三栏表 | `positionModel` / `orderModel` / `executionModel` |
| ⑥ 风控限制 | 4 字段 | `maxOrderValue` / `maxPositionValue` / `maxDailyLoss` / `maxActiveOrders`（`configureRiskLimits`） |
| ⑦ 通知设置 | 2 开关 + URL + 5 事件 | `desktopEnabled` / `webhookEnabled`；`notifySignal` / `notifyApproval` / `notifyOrderSubmitted` / `notifyExecutionRejected` / `notifyIntentExpired`（`configureNotifications`） |
| ⑧ 模拟回放 | 底部横条 | `startCustomStrategyReplay(strategyId, symbol, periodSeconds, barCount, intervalMs)` / `stopCustomStrategyReplay`；进度 `replayedBars/totalBars` |

---

## 5. Token 与字体（引用 `PPTheme.qml`，勿重复定义）

- 色板：`bgCanvas #0B0F14` / `bgBar #10161E` / `bgPanel #121923` / `bgRaised #161F2B` / `bgInput #0E151E` / `bgSelected #102B38` / `bgSuccess #102A22` / `border #273342` / `gridY #1B2734` / `gridX #182432` / `track #344256` / `textPrimary #E7EDF4` / `textMuted #7E8DA1` / `cyan #21B6E9` / `blue #4E8CFF` / `purple #9D8CFF` / `yellow #F5C451` / `green #2AD879` / `red #FF5B62`。
- 字体：`Noto Sans SC`（Regular 400 / Medium 500 / Bold 700），字号 9/10/11/12/13/15/16/18/20/24/25。
- 圆角：面板 10 / 输入·绘图区·激活导航 7 / 普通按钮·徽标 6 / 品牌·选中卡 8 / 复选 3。
- 全部颜色走 `theme`，禁止 QML 内硬编码色值。

---

## 6. QML 实现硬约束（必读）

1. **不 shadow FINAL 属性**：自定义属性/required property 不得取名 `visible`/`color`/`text`/`width`/`height`/`left`/`right`/`top`/`bottom` 等（详见 CLAUDE.md 全表）。
2. **ListModel delegate 用 `required property` 取角色**，不访问 `model.roleName`；语义冲突时改角色名（如 `seriesVisible`/`leftOperand`/`rightOperand`）。
3. **页面只通过 `TradingController` 访问数据**，不直连 service。
4. 新增 QML 文件加入 `CMakeLists.txt` 的 `qt_add_qml_module(... QML_FILES ...)`。
5. 验收：`cmake --build` + `ctest` + `all_qmllint` 通过；改 QML 后启动确认窗口 `PPTrader · 量化交易工作台` 正常加载（qmllint 抓不到 FINAL 覆盖错误，必须实跑）。

---

## 7. 待拍板项（需主会话确认）

已解决（本会话已定稿）：
1. ~~导航 5 项 vs 3 项~~ → 定 **5 项**，Figma 已按 5 项重排页面与导航。
2. ~~「账户」去留~~ → 移除，统一 5 项（主页/市场快照/指标与策略/回测任务/执行中心）。
3. ~~「回测任务」页内容~~ → 已建（04 页，子导航 6 项 + 复用面板 + 逐根 K 线回放 + 手动归档）。
4. ~~「多窗口」功能设计稿~~ → 已建（01 页 2 个示例：实时行情 / 回测，单窗口=单标的×多周期）。
5. ~~抽屉「执行与审批」入口~~ → 移除，执行中心已提升为一级导航（05 页）。

仍待拍板：
6. **执行中心画布高度**：从 1000 扩展到 1320 是否可接受（或改为页内滚动）。
7. **QML 实装与验收**：五项路由、主页回测跳转和副屏 1600 基准已验收；1120×760 仍待可靠复验。
