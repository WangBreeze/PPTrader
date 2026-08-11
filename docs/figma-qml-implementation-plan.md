# Figma → QML 实施蓝图：A 股全市场时刻股价

> 视觉真源：[PPTrader / A股全市场时刻股价](https://www.figma.com/design/YFTuHENpOe71mzWh8006OH/PPTrader?node-id=26-972)  
> `fileKey=YFTuHENpOe71mzWh8006OH`，`nodeId=26:972`，画板 `1600×1000`  
> 详细像素取证见 [figma-qml-handoff.md](./figma-qml-handoff.md)。本蓝图以当前源码为准，只规划、不修改 Figma、QML 或 C++。

## 1. 先决结论

`figma-qml-handoff.md` 第 7 节提出的核心快照契约已在当前仓库落地，实施时不要重复创建第二套模型：

- 已有 `tradingController.marketSnapshotModel`，可承载 5000+ 行并按 `symbol` 稳定排序。
- 已有按时刻和最新批次查询：`requestMarketSnapshot()`、`requestMarketSnapshotAt()`、`requestLatestMarketSnapshot()`。
- 已有请求/命中时刻、批次、完整率、批次延迟、陈旧度、健康状态和错误属性。
- 查询语义已确定为“请求时刻之前最近的已完成批次”；无时区字符串按 `Asia/Shanghai` 解析。

异步加载态、请求代次保护和结构化 Provider 健康已在后续阶段完成。仍缺少统一后端筛选与聚合、跨页面快照选中、导出、多窗口和系统遥测。

## 2. QML 组件树

```text
Main.qml
├─ PPTheme                         全局视觉 Token
├─ AppTopBar                       固定顶部导航，h=64
├─ PageRouter / StackLayout        页面切换
│  └─ MarketSnapshotPage           本页视图状态与版本协调
│     ├─ SnapshotPageHeader        标题、轴语义、横截面徽标
│     ├─ SnapshotTimeControls      日期/时刻草稿、快捷时刻、播放、应用
│     └─ RowLayout                 gap=16
│        ├─ CrossSectionChartCard  图表标题、工具栏、坐标区和底部控制
│        │  ├─ ChartHeader
│        │  ├─ ChartToolbar
│        │  ├─ CrossSectionPlot
│        │  │  ├─ ChartAxesGrid
│        │  │  ├─ CrossSectionScatterItem / Canvas
│        │  │  └─ ScatterInteractionOverlay
│        │  │     ├─ Crosshair
│        │  │     └─ StockTooltip
│        │  ├─ VisibleRangeBrush
│        │  └─ ChartLegendFooter
│        └─ SnapshotSummaryPanel
│           ├─ SelectedStockCard
│           ├─ BoardFilterList
│           ├─ PriceStatsGrid
│           └─ TopStocksList
├─ SnapshotStateOverlay            loading/empty/stale/incomplete/error
└─ AppStatusBar                    固定底部状态，h=48
```

职责边界：

- `Main.qml`：只负责窗口、主题、公共顶/底栏和路由，不保存 5000 点数据。
- `MarketSnapshotPage`：保存草稿时刻、请求序号、板块过滤、Y 轴模式、可视范围、悬停与固定选择等页面状态。
- `TradingController`：负责数据库查询和单一快照版本；QML 不拼接不同 `batchId`。
- `CrossSectionScatterItem`：只做批量坐标变换、绘制和命中；轴文字、Tooltip、十字线继续用少量 QML Item。
- `SnapshotSummaryPanel`：消费与图表同一过滤版本的聚合结果，不自行再次遍历一个不同模型副本。

## 3. 尺寸与视觉实施表

### 3.1 一级布局

| 组件 | 1600×1000 精确值 | 伸缩规则 |
| --- | --- | --- |
| `AppTopBar` | `x=0,y=0,w=1600,h=64` | 宽度跟窗口；高度固定 64 |
| `SnapshotPageHeader` | `y=64,h=74` | 左右边距 20 |
| `SnapshotTimeControls` | `x=20,y=138,w=1560,h=92` | 宽度填充；高度固定 92 |
| 主内容 | `x=20,y=246,w=1560,h=690` | 上下间距 16；高度填充至底栏前 |
| `CrossSectionChartCard` | `w=1226,h=690` | `Layout.fillWidth=true`；建议最小宽 900 |
| 主图/摘要间距 | `16` | 固定 |
| `SnapshotSummaryPanel` | `w=318,h=690` | 宽度固定 318；窗口 <1280 时改抽屉 |
| `AppStatusBar` | `x=0,y=952,w=1600,h=48` | 宽度跟窗口；高度固定 48 |

不要使用 `ApplicationWindow.header` 产生额外内容坐标偏移；顶栏应真实占据设计的 `y=0..64`。

### 3.2 主题 Token

| 类别 | 值 |
| --- | --- |
| 背景 | 画布 `#0B0F14`；顶/底栏 `#10161E`；面板 `#121923`；抬升控件 `#161F2B`；输入/绘图区 `#0E151E` |
| 状态底色 | 选中 `#102B38`；正常 `#102A22` |
| 边线 | 标准 `#273342`；Y 网格 `#1B2734`；X 网格 `#182432`；滑轨 `#344256` |
| 文本 | 主 `#E7EDF4`；次 `#7E8DA1` |
| 强调 | cyan `#21B6E9`；blue `#4E8CFF`；purple `#9D8CFF`；yellow `#F5C451`；green `#2AD879`；red `#FF5B62` |
| 字体 | `Noto Sans SC`；Regular 400、Medium 500、Bold 700 |
| 字号 | 9/10/11/12/13/15/16/18/20/24 px |
| 圆角 | 主卡 10；时刻卡 9；Tooltip/选中卡 8；输入/绘图区 7；按钮 6；复选框/手柄 3 px |
| 标准边框 | 1 px；普通卡片不加阴影 |

### 3.3 组件级规格

| 组件 | 尺寸/间距 | 字体与颜色 | 交互状态 |
| --- | --- | --- | --- |
| `AppTopBar` | 品牌左 16；导航 y=14/h=36；右侧状态 y=18/h=28 | 品牌 18/Bold；导航 12/Medium；激活项 cyan | 默认、激活、禁用；未实现导航不可伪装可点击 |
| `SnapshotPageHeader` | 标题 `20,76,500,34`；副标题 y=108；徽标 `122×28` | 标题 24/Bold；副标题 11/Regular | 股票数来自 `marketSnapshotModel.count` |
| `SnapshotTimeControls` | 内边距 20；日期 `148×38`；时刻 `132×38`；快捷按钮高 30；应用 `166×38` | 标签 10/Medium；区标题 11/Bold；时刻 13/Bold/cyan | 草稿、焦点、选中、loading、disabled；仅“应用”提交 |
| `ChartHeader` | 标题 x=40/y=260；工具栏 136/126/122/86/68×28，gap=8 | 标题 16/Bold；元数据 10/Regular | 范围、X 排序、对数/线性、全屏 |
| `CrossSectionPlot` | 目标 `1128×430`；卡内左 66、右 32；圆角 7 | 轴标题 10/Medium；刻度 9/Regular | hover、固定选中、缩放、刷选、空/陈旧覆盖层 |
| 散点 | 普通 3.2；重点 5.6；选中 10 px | 五板块色；普通 62%、重点 95% | 网格在点层下；选中点和十字线在普通点上 |
| `StockTooltip` | `252×136`，圆角 8，cyan 1 px | 标题 12/Bold；价格 16/Bold；其余 9/10 | 四边自动翻转，不得被 Plot `clip` 截断 |
| `VisibleRangeBrush` | 轨道 `940×12`；选区高 16；手柄 `8×28` | 选区/手柄 cyan | 拖动、缩小、放大、全部重置；保持全市场 X 稳定 |
| `SnapshotSummaryPanel` | 宽 318；内边距 20；选中卡 `278×126`；统计卡 `278×102` | 标题 15/Bold；选中价 20/Bold | 自动刷新、五板块显隐、全选、Top 3 |
| `AppStatusBar` | 高 48；左/中/右三段 | 10 px；健康 green、警告 yellow、错误 red | 严格区分批次延迟与快照陈旧度 |

状态派生只复用上述色板：hover 将边框由 `#273342` 提升为 `#344256`；disabled 使用 `textMuted` 且整体约 45% 不透明度。两者不在 Figma 独立画板中，需在视觉验收时确认。

## 4. 数据契约与页面状态

### 4.1 已有契约：直接接入

`marketSnapshotModel` 已提供以下角色，QML 不再使用上限 500 且已字符串格式化的旧 `tickModel`：

```text
displayIndex, symbol, name, market, venue, board, marketBucket,
price, previousClose, changePercent, open, high, low,
volume, amount, turnover, bid, ask, snapshotTime,
suspended, stale, valid, health, invalidReason
```

现有 Controller 对齐关系：

| 页面字段 | 当前属性/API |
| --- | --- |
| 数据模型 | `marketSnapshotModel`；另有 `count/get(row)/indexOfSymbol(symbol)` |
| 请求草稿提交 | `requestMarketSnapshotAt("yyyy-MM-dd HH:mm:ss")` |
| QDateTime 请求 | `requestMarketSnapshot(QDateTime)` |
| 当前/最新按钮 | `requestLatestMarketSnapshot()` |
| 用户请求时刻 | `requestedSnapshotTime` |
| 实际命中时刻 | `resolvedSnapshotTime`，所有展示必须以它为准 |
| 版本 | `snapshotBatchId` |
| 覆盖 | `snapshotReceivedCount/snapshotExpectedCount/snapshotCompleteness` |
| 批次采集耗时 | `snapshotLatencyMilliseconds` |
| 请求时刻与快照的距离 | `snapshotStalenessMilliseconds` |
| 健康 | `snapshotHealth`、`snapshotHealthText`、`snapshotStale` |
| 说明 | `snapshotError`；Incomplete/Stale 时它是警告说明，不一定表示查询失败 |
| 刷新通知 | `marketSnapshotChanged` |

`snapshotHealth` 枚举：`0 Empty`、`1 Healthy`、`2 Incomplete`、`3 Stale`、`4 Error`。完整率阈值为 90%，陈旧阈值为 10 秒。

注意：查询返回 `true` 只表示找到了批次；仍可能是 Incomplete 或 Stale。UI 必须读取 `snapshotHealth`，不能以返回值决定绿色状态。模型中的 `valid` 只排除价格无效和停牌，不排除陈旧；Stale 状态应保留点并显示警告，而不是清空图表。

### 4.2 各组件消费字段

| 组件 | 必需字段/状态 |
| --- | --- |
| `SnapshotPageHeader` | `marketSnapshotModel.count`、当前 Y 模式 |
| `SnapshotTimeControls` | 页面局部 `draftDate/draftTime`、`requestedSnapshotTime`、查询 busy |
| `ChartMeta` | `resolvedSnapshotTime`、received/expected、latency、batchId |
| `CrossSectionScatterItem` | displayIndex、symbol、board、price、valid、stale；视图过滤/范围/Y 模式 |
| `StockTooltip` | symbol/name/price/changePercent/volume/snapshotTime、页面 hover/pinned symbol |
| `SelectedStockCard` | 同 Tooltip；必须取快照价而非最新 Tick |
| `BoardFilterList` | board 和按同一过滤版本计算的数量 |
| `PriceStatsGrid` | 过滤后有效价格的 min/median/mean/max |
| `TopStocksList` | 同一过滤版本按 price 降序前三 |
| `AppStatusBar` | health、received/expected、completeness、latency、staleness、resolved time |

### 4.3 仍待建设/明确的契约

| 缺口 | 首版处理 | 推荐后续契约 |
| --- | --- | --- |
| 查询 loading/busy | 已由独立 SQLite 查询线程执行，并暴露 `snapshotLoading/requestId/resolvedRequestId` | 后端筛选聚合完成后继续评估 5000 行模型重置耗时 |
| 自动刷新 | QML Timer 可调用 `requestLatestMarketSnapshot()`；Provider 健康已结构化 | 增加最后成功批次历史和退避策略；`liveRunning` 仍只代表任务状态 |
| 选中股票 | 页面局部保存 row/symbol，`indexOfSymbol()` 查找 | 若需跨页面共享，增加 snapshot selection API；不要用仅支持观察列表的 `selectInstrument()` |
| 板块筛选与统计 | 原型可在数据加载后一次缓存计算 | 生产增加派生视图/聚合对象，统一返回筛选版本、板块计数、统计与 Top 3 |
| X 排序 | 当前模型已按 symbol 稳定排序；首版固定“股票代码” | 有多种排序后再增加明确 sort mode；未实现时控件不显示下拉能力 |
| 全屏 | QML 页面状态可实现 | 无需业务 Controller API |
| 导出 | PNG 可评估 `grabToImage()`；CSV 尚无契约 | 增加带 batchId 的导出服务和结果/错误 |
| 双击多窗口 | 显示禁用原因 | 增加 `openInstrumentWindow(symbol)` |
| CPU/MEM | 显示“—”或隐藏 | 有真实遥测后再展示；严禁硬编码 Figma 示例值 |

### 4.4 状态优先级

```text
Error > Loading(无旧数据) > Empty > Incomplete/Stale > Healthy
```

- **Loading**：初次加载用静态骨架；有旧快照时保留旧图，仅在元数据区显示刷新进度。
- **Empty**：保留时间控件，Plot 中央显示“请求时刻之前没有已完成快照”。
- **Incomplete**：yellow 警告，仍可显示部分点；摘要全部标记“基于部分数据”。
- **Stale**：yellow 警告，显示 `snapshotStalenessMilliseconds`；不把陈旧但有效的点删除。
- **Error**：red 状态、`snapshotError`、重试；保留草稿时刻和筛选。
- **Healthy**：green 状态；只有此状态可显示“快照正常”。

加载期间禁用重复提交；请求失败不能清空用户草稿。每次异步请求携带单调 `requestId`，旧结果不会覆盖新结果；用户查询历史时刻后，实时批次落库也不会强制切回“最新”。

## 5. 5000+ 散点渲染方案

### 5.1 方案 A：Canvas 原型

适合第一阶段验证像素布局，不作为最终性能结论：

1. 模型 `modelReset` 后只调用一次 `get(row)`，缓存成紧凑 JS 数组；禁止每次 `onPaint` 对 5000 行反复构造 QVariantMap。
2. 点/网格使用静态 Canvas；十字线、Tooltip 使用单独 Overlay，鼠标移动不重画点层。
3. 只有模型、板块过滤、Y 模式或可视范围改变才重画点层。
4. 用颜色分批循环绘制；过滤后仍保留全市场 `displayIndex` 作为 X，避免股票横向跳动。
5. 测试缩放和快速 hover 的纹理上传成本；若 p95 帧耗时超过 16.7 ms，停止扩展 Canvas 方案。

风险：JS 遍历、QVariantMap 构造、Canvas 纹理上传和高 DPI 重绘会在高频交互时放大；不能以静态 5000 点一次绘制成功代替交互性能验收。

### 5.2 方案 B：自定义 `QQuickItem`（生产推荐）

实现 `CrossSectionScatterItem`，在 `updatePaintNode()` 中用 Scene Graph 批处理：

- 五个板块最多五个普通点几何节点，重点和选中分别为上层节点。
- 每个圆点用两个三角形组成的 quad，并由片元着色器裁成圆；不要依赖不同图形后端不一致的硬件 point size。
- 5000 点仅约 30k 顶点，模型/过滤/排序/缩放时重建几何；hover 只更新十字线和 Tooltip。
- QAbstractItemModel 属于 GUI 线程。模型信号到达时在 GUI 线程复制紧凑 `PointRecord`，Scene Graph 只读取同步后的副本，不能在渲染线程直接遍历模型。
- 监听 `modelReset/rowsInserted/rowsRemoved/dataChanged`，模型切换时安全断开旧连接。
- 点径按 QML 逻辑像素定义，在高 DPI 和 D3D/OpenGL 后端分别验证 3.2/5.6/10 px。

### 5.3 命中策略

当前 X 按唯一股票代码稳定排序，可用 `O(log N + k)` 命中：

1. 将鼠标 x 反变换为 `displayIndex`。
2. 在可见且按屏幕 x 排序的行数组中二分定位。
3. 只检查命中半径覆盖的左右候选，再计算屏幕欧氏距离；选择距离最近者。
4. 被板块过滤、价格无效或不在可视窗口的点不参与命中。
5. hover 只更新页面局部状态；单击才固定选择，双击才调用多窗口 API。

全市场视图下 X 间距可能小于 1 px，因此不能只检查一个最近索引。若未来支持任意二维排序，再升级为均匀网格索引；5000 点无需一开始引入四叉树。

## 6. 实施顺序与交付边界

1. 建立 `PPTheme` 和基础控件，完成 1600×1000 静态骨架。
2. 接入现有 `marketSnapshotModel` 与快照健康属性，完成真实数据只读页。
3. 用 Canvas 完成布局、轴、筛选、Tooltip 和摘要的功能原型。
4. 增加统一过滤/聚合数据层，保证图点、数量、统计和 Top 3 同版本。
5. 以自定义 `QQuickItem` 替换 Canvas 点层并完成 5000+ 性能验收。
6. 补齐异步查询、自动刷新、多窗口和导出；最后再替换旧 `Main.qml` 主框架。

当前任务不创建上述组件，也不修改 `qml/`、C++ 或 Figma。

## 7. 视觉验收清单

### 几何与字体

- [ ] 1600×1000 下顶栏 64、标题区 74、时刻卡 92、主内容 690、底栏 48 px。
- [ ] 左右边距 20；主图 1226、间距 16、摘要 318；一级区域误差不超过 1 px。
- [ ] Plot 为 1128×430；Tooltip 为 252×136；摘要选中卡为 278×126。
- [ ] Noto Sans SC 可用；页面/图表/摘要标题分别为 24/16/15 Bold。
- [ ] 主卡 10、时刻卡 9、Tooltip 8、输入/Plot 7、按钮 6 px 圆角。
- [ ] 1 px 边框和网格在 100%/125%/150% 缩放下没有模糊半像素。

### 颜色与层级

- [ ] 背景、面板、文本、边框和五板块色逐项匹配第 3.2 节。
- [ ] 普通/重点/选中点为 3.2/5.6/10 px；普通 62%、重点 95%。
- [ ] 网格在散点下，十字线/选中点/Tooltip 在散点上。
- [ ] Tooltip 靠四边自动翻转，且不被 Plot 裁剪。
- [ ] Healthy 才使用绿色；Incomplete/Stale 为 yellow，Error 为 red。

### 数据和状态

- [ ] 页面只使用 `marketSnapshotModel`，不回退到最多 500 行的 `tickModel`。
- [ ] 标题、元数据、图点、板块数量、统计、Top 3 均属于同一 `snapshotBatchId` 和过滤版本。
- [ ] 显示 `resolvedSnapshotTime`，不拿用户请求时刻冒充实际数据时刻。
- [ ] `snapshotLatencyMilliseconds` 与 `snapshotStalenessMilliseconds` 文案不混用。
- [ ] Empty、Incomplete、Stale、Error 独立呈现；Stale 点不因 `valid=true` 被误标健康。
- [ ] 对数模式不绘制 `price<=0`，无效数量可追溯。
- [ ] Figma 示例的 5364、CPU 24%、MEM 512 MB、16 ms 均未硬编码。

### 交互与性能

- [ ] 日期/时刻/快捷按钮/滑块只改草稿，“应用并刷新”才提交。
- [ ] 连续提交无重复动作；异步化后旧请求结果不能覆盖新请求。
- [ ] 板块显隐不改变未过滤股票的 X 位置。
- [ ] hover 显示十字线和 Tooltip；单击固定；双击仅触发明确的多窗口动作。
- [ ] 不为每个股票创建独立 QML Item/Delegate。
- [ ] 5364 点、1600×1000、目标机器下 hover/缩放 p95 帧耗时以 16.7 ms 为目标，无持续 UI 阻塞。
- [ ] 28 px 视觉按钮具有至少 36×36 命中区，键盘焦点和状态不只依赖颜色。
