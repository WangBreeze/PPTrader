# Figma → QML 实施交接：A 股全市场时刻股价

> 交接状态：可进入开发排期；视觉规格已确认，完整数据能力存在前置缺口  
> 设计基线：2026-08-01 Figma Desktop Bridge 当前运行态  
> 实施约束：设计稿是本页面唯一视觉来源；本交接不修改 C++、QML 或 Figma 文件

## 1. Figma 节点与取证信息

| 项目 | 确认值 |
| --- | --- |
| Figma 文件名 | `PPTrader` |
| `fileKey` | `YFTuHENpOe71mzWh8006OH` |
| Figma 页面 | `03 全市场时刻股价` |
| 画板名称 | `PPTrader / A股全市场时刻股价` |
| `nodeId` | `26:972`，URL 形式为 `26-972` |
| 画板类型 | `FRAME`，裁剪内容开启 |
| 画板坐标/尺寸 | `(0, 0)`，`1600 × 1000 px` |
| 目标链接 | [打开 Figma 目标节点](https://www.figma.com/design/YFTuHENpOe71mzWh8006OH/PPTrader?node-id=26-972) |

截图已通过 Figma Desktop Bridge 的 `exportAsync` 从节点 `26:972` 导出并完成视觉核对：

- 格式：PNG。
- 原始画板：1600×1000。
- AI 预览导出：0.98×，1568×980，163,329 bytes。
- 来源：Desktop Bridge 当前运行态，不是缓存的 REST 云端预览。
- 永久截图 URL：无。Figma Console 的 REST 进程未继承 `FIGMA_ACCESS_TOKEN`，因此无法生成 30 天资源 URL；开发与验收应使用上面的节点链接重新导出。

官方 `get_design_context` 在本会话也返回 Starter MCP 调用额度已用尽。为避免猜测，本交接的精确规格来自 Desktop Bridge 深度节点树和两次节点 PNG 导出；没有使用手工重绘或截图反推尺寸。

## 2. 视觉总览

画板采用单一深色桌面工作区，从上到下分为五层：

```text
1600 × 1000
├─ 顶部导航栏             y=0,   h=64
├─ 页面标题区             y=64,  h=74
├─ 时刻控制卡             x=20,  y=138, w=1560, h=92
├─ 主内容区               y=246, h=690
│  ├─ 主图卡              x=20,   w=1226
│  ├─ 间隙                x=1246, w=16
│  └─ 右侧摘要            x=1262, w=318
└─ 底部状态栏             y=952, h=48
```

主内容距画板左右各 20 px；时刻控制卡与主内容相隔 16 px，主内容与底栏相隔 16 px。设计为固定桌面密度，但 QML 应将图表卡设为可伸缩、摘要栏保持 318 px，窗口小于 1280 px 时允许摘要栏折叠为抽屉，不应继续压缩图表坐标轴。

## 3. 颜色、字体、边框与圆角

### 3.1 色板

Figma 文件没有变量绑定，以下颜色均来自节点实际值，应在 QML 中集中为主题常量，禁止散落硬编码：

| 建议 Token | 色值 | 用途 |
| --- | --- | --- |
| `bgCanvas` | `#0B0F14` | 整页背景、品牌图标文字 |
| `bgBar` | `#10161E` | 顶栏、底栏 |
| `bgPanel` | `#121923` | 时刻控制卡、图表卡、右侧摘要 |
| `bgRaised` | `#161F2B` | 普通按钮、摘要子卡、Tooltip |
| `bgInput` | `#0E151E` | 输入框、图表绘图区、刷选轨道 |
| `bgSelected` | `#102B38` | 激活导航、选中时刻、对数模式 |
| `bgSuccess` | `#102A22` | 自动刷新状态 |
| `border` | `#273342` | 1 px 标准边框与分隔线 |
| `gridY` | `#1B2734` | 水平网格线 |
| `gridX` | `#182432` | 垂直网格线 |
| `track` | `#344256` | 时间滑轨未选区 |
| `textPrimary` | `#E7EDF4` | 标题和主要数据 |
| `textMuted` | `#7E8DA1` | 次要信息、轴标签、普通导航 |
| `cyan` | `#21B6E9` | 主强调、焦点、主板点色 |
| `blue` | `#4E8CFF` | 深市主板 |
| `purple` | `#9D8CFF` | 创业板 |
| `yellow` | `#F5C451` | 科创板、警告 |
| `green` | `#2AD879` | 北交所、在线/正常 |
| `red` | `#FF5B62` | 高价强调、错误 |

普通散点使用板块色 `62%` 不透明度；每个板块 5 个重点示例点使用 `95%` 不透明度。当前 Figma 示例共有 420 个普通/强调散点：每个板块 84 个，其中 79 个为 3.2×3.2、5 个为 5.6×5.6；另有 5 个 10×10 的选中示例点。正式实现必须按真实模型绘制全部股票，不能把 420 个静态示例点当成数据上限。

### 3.2 字体

- 全页面字体：`Noto Sans SC`。
- 字重：Regular 400、Medium 500、Bold 700。
- 字号层级：9、10、11、12、13、15、16、18、20、24 px。
- 页面标题：24/Bold；品牌：18/Bold；图表标题：16/Bold；摘要标题：15/Bold；选中价格：20/Bold；输入时刻：13/Bold。
- Figma 未包含字体文件。精确视觉验收前需确认开发机和交付环境已安装 Noto Sans SC，或由主会话协调可合法分发的字体资源；系统字体回退会改变中文字符宽度。

### 3.3 圆角和边框

| 元素 | 圆角 | 边框 |
| --- | --- | --- |
| 主图卡、右侧摘要 | 10 px | 1 px `border` |
| 时刻控制卡 | 9 px | 1 px `border` |
| 品牌块、选中股票卡、Tooltip | 8 px | 选中卡/Tooltip 为 1 px `cyan` |
| 输入框、绘图区、价格分布卡、激活导航 | 7 px | 1 px `border` 或 `cyan` |
| 普通按钮、徽标、滑轨 | 6 px | 1 px `border` |
| 复选框、刷选手柄 | 3 px | 复选框使用板块色边框 |

设计没有大面积阴影。Tooltip 使用一个偏移 `(3, 3)` 的 `#06090D` 45% 透明矩形模拟阴影，不应给所有卡片添加投影。

## 4. 布局树与精确规格

### 4.1 顶部栏

顶栏 `0,0,1600,64`，背景 `bgBar`；底部分隔线位于 `y=63`，高 1 px。

| 元素 | 坐标与尺寸 | 视觉 |
| --- | --- | --- |
| 品牌块 | `16,15,34,34` | `cyan`，8 px 圆角；“PP” 12/Bold，`bgCanvas` |
| 品牌名 | `62,10,142,26` | “PPTrader”，18/Bold，主文字 |
| 副标题 | `62,34,160,18` | “量化交易工作台”，10/Regular，次文字 |
| 多窗口 | `250,14,76,36` | 12/Medium，次文字 |
| 市场快照激活项 | `334,14,90,36` | `bgSelected` + 1 px `cyan`，7 px 圆角，12/Bold |
| 指标与策略 | `432,14,104,36` | 12/Medium，次文字 |
| 回测任务 | `544,14,82,36` | 12/Medium，次文字 |
| 执行中心 | `634,14,82,36` | 12/Medium，次文字 |
| 行情状态 | `1240,18,102,28` | 普通底、6 px 圆角；正常为 `green` |
| 工作区选择 | `1352,18,114,28` | “A股 · 全市场” |
| 设置 | `1478,18,92,28` | “设置 ⋯” |

导航中间留有弹性空白；右侧三个控件固定靠右。未接入页面路由前，非当前导航项可显示但不得伪装为已实现按钮。

### 4.2 页面标题区

| 元素 | 坐标与尺寸 | 视觉 |
| --- | --- | --- |
| 标题 | `20,76,500,34` | “A股全市场 · 时刻股价分布”，24/Bold |
| 副标题 | `20,108,560,22` | 11/Regular，显示 X/Y 轴语义和当前总数 |
| 类型徽标 | `600,82,122,28` | “横截面快照”，`bgSelected`、`cyan` 边框与文字 |

标题里的股票数必须来自模型，不得固定写 5364。

### 4.3 时刻控制区

外框 `20,138,1560,92`，背景 `bgPanel`，9 px 圆角，1 px 边框。内部上下基线分别为 `y=150` 和 `y=168/176`。

| 分组 | 位置与规格 |
| --- | --- |
| 区标题 | `40,150,140,20`，11/Bold |
| 日期 | 标签 `40,176,54,18`；输入 `92,168,148,38`，左内边距 12；箭头区宽 18 |
| 时刻 | 标签 `258,176,54,18`；输入 `310,168,132,38`，激活边框 `cyan`；值 13/Bold |
| 时区 | `452,168,112,38`，固定显示 `Asia/Shanghai` |
| 快捷区标题 | `590,150,120,20` |
| 开盘 | `590,176,94,30` |
| 10:30 选中 | `692,176,70,30`，选中态 |
| 午后 14:00 | `770,176,96,30` |
| 收盘 15:00 | `874,176,96,30` |
| 当前 | `978,176,64,30` |
| 分时定位 | 标题 `1060,150`；轨道 `1060,184,250,4`；拇指 14×14；09:30/15:00 在 `y=194` |
| 播放 | `1328,168,54,38` |
| 应用并刷新 | `1394,168,166,38`，实心 `cyan`，深色文字 |

交互语义：编辑日期、时刻、快捷按钮和滑块只改变草稿时刻；只有“应用时刻并刷新”提交查询。播放用于按固定步长推进时刻；自动刷新状态位于右侧摘要标题，不与播放混为同一开关。

### 4.4 主图卡

外框 `20,246,1226,690`，10 px 圆角。卡内左右各 20 px。

#### 标题和工具栏

- 图表标题：`40,260,330,28`，16/Bold。
- 元数据：`40,288,520,20`，10/Regular，格式为“实际快照时刻 · 数据覆盖 received / expected · 延迟”。
- 工具栏从 `x=656` 开始：范围 136×28、X 轴排序 126×28、对数 122×28、线性 86×28、全屏 68×28，间距 8 px。
- 对数模式为当前选中态：`bgSelected` + `cyan`；线性模式为普通态。

#### 绘图区和坐标轴

- 绘图区：`86,330,1128,430`，`bgInput`，7 px 圆角，1 px 边框。
- Y 轴标题：`34,304,160,18`，“股价（元 · 对数）”，10/Medium。
- 对数刻度：2000、500、100、20、5、1；水平网格 y=330/416/502/588/674/760。
- X 轴 6 条内部网格将绘图区七等分；首尾标签可同时显示序号和代码。
- X 轴标题：`506,798,300,20`，“个股序号 / 股票代码（共 N 只）”。
- X 默认按股票代码稳定排序；相同代码顺序不得因刷新跳动。
- Y 对数模式只接受 `price > 0` 的点；无效价格进入无效计数，不放在 y=0 伪造坐标。

#### 散点和交互

- 普通点 3.2 px；重点点 5.6 px；选中点 10 px。
- 板块色：主板 cyan、深市主板 blue、创业板 purple、科创板 yellow、北交所 green。
- 悬停显示十字线，Figma 示例竖线 `x=204`、横线 `y=350`，均为 1 px `cyan`。
- Tooltip：252×136，8 px 圆角，1 px `cyan`，包含名称/代码、快照时刻、价格、涨跌幅、成交量和操作提示。
- 单击固定选中项；双击请求打开对应个股窗口。若多窗口功能尚未接入，双击只能显示明确的“功能未实现”，不得静默失败。

#### 可视窗口和底部控制

- 刷选轨道：`130,828,940,12`；选中区示例 `280,826,450,16`；左右手柄 8×28。
- 当前范围文案位于 `1086,822,140,24`，右对齐。
- 缩小、全部、放大按钮分别为 72×28、82×28、72×28，起点 x=40，间距 8。
- 渲染摘要 `310,858,300,28`；板块图例从 x=660 开始，每项约 104 px。
- 操作提示位于 `40,900`；导出按钮 `1080,894,146,28`。

5364 点不能作为 5364 个独立 QML Item。推荐数据由 `QAbstractListModel` 管理，绘制由一个批处理渲染对象完成；性能基线为缩放/悬停时不创建 Delegate 风暴，目标帧耗时接近设计示例的 16 ms。纯 `Canvas` 可作为短期原型，但不建议作为最终 5000+ 点实现。

### 4.5 右侧摘要

外框 `1262,246,318,690`，10 px 圆角，内部左右边距 20 px。

| 区域 | 坐标与尺寸 | 内容 |
| --- | --- | --- |
| 标题 | `1282,260,210,26` | “市场快照摘要”，15/Bold |
| 自动刷新 | `1488,260,72,24` | 正常态 `bgSuccess` + `green` |
| 快照时刻 | `1282,292,270,20` | 10/Regular |
| 当前选中卡 | `1282,326,278,126` | 8 px 圆角、`cyan` 边框 |
| 板块筛选标题 | `1282,474` | 12/Bold；全选按钮 72×24 |
| 五个板块行 | y=508/542/576/610/644 | 行高 28；14×14 复选框、8×8 色点、名称、右对齐数量 |
| 价格分布标题 | `1282,694` | 12/Bold |
| 价格分布卡 | `1282,728,278,102` | 最高、中位、平均、最低，2×2 布局 |
| 高价股标题 | `1282,850` | 11/Bold；模式按钮 82×22 |
| Top 3 | y=878/904/930 | 排名 18、名称 126、价格 98 px |

选中卡的价格应对应所选快照时刻，不是最新实时价。板块数量、价格统计和 Top 3 必须与当前筛选集合一致；筛选改变后应原子更新主图与摘要，避免两处展示不同数据版本。

### 4.6 底部状态栏

底栏 `0,952,1600,48`，背景 `bgBar`，顶边 1 px `border`。

- 左：状态点 `16,972,8,8`；文本 `30,960,380,28`，数据源、覆盖率、延迟。
- 中：`620,960,360,28`，`cyan`，显示股票数和实际快照时刻。
- 右：`1220,960,350,28`，右对齐，CPU、内存和渲染耗时。

CPU 24%、MEM 512 MB、渲染 16 ms 是设计示例值，禁止硬编码进生产界面。没有遥测 API 时优先只显示可真实计算的渲染耗时，其余显示“—”或暂不渲染。

## 5. 可复用控件与状态

### 5.1 建议复用控件

| 控件 | 复用场景 | 基本规格 |
| --- | --- | --- |
| `PpPanel` | 时刻卡、图表卡、摘要栏 | `bgPanel`、1 px 边框、9/10 px 圆角 |
| `PpButton` | 工具栏、缩放、导出 | 默认 `bgRaised`；高 24/28/30/38 四档 |
| `PpSegmentButton` | 快捷时刻、对数/线性 | 默认/选中成组状态 |
| `PpInput` | 日期、时刻 | 高 38；焦点为 `cyan` 边框 |
| `PpStatusChip` | 行情在线、自动刷新、横截面快照 | 高 24/28；成功、信息两种现有色 |
| `BoardFilterRow` | 五类板块 | 28 px 行高，板块色复选框和数量 |
| `SnapshotStateOverlay` | 加载、空数据、低完整率、错误 | 覆盖绘图区，不改变卡片尺寸 |
| `StockTooltip` | 悬停/固定详情 | 252×136，位置受画板边界约束 |

### 5.2 状态规范

Figma 只绘制了默认、选中、焦点、在线和 Tooltip 状态；其余状态应严格复用同一色板，不引入新的视觉语言。

| 状态 | 表现与行为 |
| --- | --- |
| 加载 | 保留坐标区尺寸，`bgInput` 上显示 `bgRaised` 静态骨架和 `textMuted`“正在加载所选时刻快照…”；应用按钮进入忙碌且不可重复提交。 |
| 空数据 | 图表中心显示“该时刻没有可用 A 股快照”，副文案说明将使用最近批次还是要求重新选择；保留日期/时刻控件可用。 |
| 低完整率 | 覆盖率低于 90% 使用 `yellow` 状态点和顶部警告；可显示收到的部分点，但所有统计须标注“基于部分数据”，不得显示绿色在线成功态。 |
| 高延迟 | 批次组装超过 10 秒使用 `yellow`；显示真实延迟。现有后端会跳过横截面因子计算，价格散点是否保留由新快照模型明确。 |
| 错误 | 使用 `red` 图标/标题和 `textMuted` 详情；提供“重试”，保留用户选择的日期、时刻和筛选。不能只复用全局 `statusText` 一闪而过。 |
| 禁用 | 控件背景仍为 `bgRaised`，文字 `textMuted`，整体不透明度建议 45%；保持 Tooltip 说明禁用原因。该 45% 是实现派生态，需视觉验收确认。 |
| 选中/焦点 | `bgSelected` + 1 px `cyan` + `cyan` 文本，完全对应 Figma 的 10:30、对数模式和激活导航。 |
| 悬停 | 不改变布局；普通按钮边框从 `border` 提升为 `track`，散点显示十字线与 Tooltip。`track` 色来自现有设计，需验收确认。 |
| 自动刷新 | 开启为 `bgSuccess` + `green`；暂停时回到普通按钮；自动刷新期间仍显示最后一个完整快照，加载状态只出现在时间/元数据区域。 |

键盘和可访问性：所有 28 px 视觉按钮应通过外层不可见命中区扩展到至少 36×36；焦点不能只靠颜色，增加 1 px 外焦点环。散点必须支持键盘前后选择，Tooltip 文本通过可访问名称提供。

## 6. 图标与图片资产

**外部图标/图片资产：无。**

节点树确认：66 个 Rectangle、122 个 Text、437 个 Ellipse；IMAGE fill 为 0，VECTOR、BOOLEAN、COMPONENT、INSTANCE 均为 0，变量绑定数量为 0。

- 下拉箭头、播放、更多、勾选、缩放符号均为 Figma 文本字符：`▾`、`▶`、`⋯`、`✓`、`−`、`＋`。
- QML 应使用相同文字字符或项目已有且字形完全一致的图标组件。
- 不需要下载资源；不要手绘 SVG，也不要为这些字符新建 SVG 文件。

## 7. TradingController 与模型映射

### 7.1 当前可以复用

| 设计字段/动作 | 当前接口 | 可用程度 |
| --- | --- | --- |
| 启停行情 | `liveRunning`、`startLive()`、`stopLive()` | 可驱动“正在运行”，但不等于真实数据源健康 |
| 市场开闭状态 | `marketIsOpen`、`selectedMarketSessionText` | 单个已选标的语义，可作辅助提示 |
| 全局状态 | `statusText` | 只用于普通提示，不适合结构化错误/加载状态 |
| 当前标的 | `selectedSymbol`、`selectedInstrumentTitle`、`selectInstrument(symbol)` | 仅对 `instrumentModel` 已包含的标的可靠 |
| 标的基础字段 | `instrumentModel`：`symbol/name/market/venue` | 当前是观察列表，不是 5000+ 全市场快照模型 |
| Tick 列表 | `tickModel`：`symbol/market/time/price/volume/bid/ask` | 快照到达时最多装载 500 行，而且数值已格式化为字符串 |
| 最新横截面通知 | `crossSectionalFrameUpdated(const FactorFrame&)` | 有信号，但 `FactorFrame.values` 不是 QML 列表模型，且内容是相对强弱因子而不是完整价格快照 |
| 数据库存储概况 | `storagePath`、`persistedTickCount` | 只能显示存储状态，不能按时刻查询快照 |
| 单股窗口联动 | `selectInstrument(symbol)` | 可作为单击选中；双击打开多窗口没有专用 API |

当前 `handleMarketSnapshot()` 还存在三个与设计直接冲突的边界：

1. `tickModel` 只保留最多 500 行，设计需要 5000+ 全市场点。
2. 只在完整率不低于 90%、耗时不超过 10 秒时生成 `FactorFrame`；原始快照没有面向 QML 的稳定模型。
3. SQLite 会写入带 `batch_id` 的 Tick，但 `LocalDataStore`/Controller 没有“按日期时刻找到最近批次并加载”的读取接口。

### 7.2 必需的新数据模型

建议新增只读 `CrossSectionSnapshotListModel`，由 Controller 以 `QAbstractItemModel* crossSectionSnapshotModel` 暴露。角色至少包括：

| 角色 | 用途 |
| --- | --- |
| `displayIndex` | X 轴稳定序号 |
| `symbol`、`name`、`market`、`venue` | 轴端、Tooltip、选中卡、双击定位 |
| `board` | 主板/深市主板/创业板/科创板/北交所分类及颜色映射 |
| `timestamp` | 单股行情真实时间 |
| `price`、`previousClose` | Y 值、涨跌幅 |
| `changePercent` | Tooltip、排序/摘要 |
| `open`、`high`、`low` | 后续详情，不必全部显示在当前画面 |
| `volume`、`amount`、`bid`、`ask` | Tooltip 与导出 |
| `suspended`、`stale`、`valid`、`invalidReason` | 无效点和状态统计 |
| `selected`、`highlighted` | 10 px 选中点和 5.6 px 重点点 |

模型数值角色必须返回数值类型，不应像现有 `tickModel.price` 一样提前格式化为字符串；格式化交给 QML 展示层。

### 7.3 必需的 Controller 状态/API

建议的最小契约：

```cpp
Q_PROPERTY(QAbstractItemModel *crossSectionSnapshotModel READ ... CONSTANT)
Q_PROPERTY(int crossSectionState READ ... NOTIFY crossSectionStateChanged)
Q_PROPERTY(QString crossSectionError READ ... NOTIFY crossSectionStateChanged)
Q_PROPERTY(QString crossSectionBatchId READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(QDateTime crossSectionTimestamp READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(int crossSectionExpectedCount READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(int crossSectionReceivedCount READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(double crossSectionCompleteness READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(qint64 crossSectionLatencyMs READ ... NOTIFY crossSectionSnapshotChanged)
Q_PROPERTY(QString crossSectionSelectedSymbol READ ... NOTIFY crossSectionSelectionChanged)
Q_PROPERTY(bool crossSectionAutoRefresh READ ... WRITE ... NOTIFY ...)

Q_INVOKABLE void requestCrossSectionSnapshot(const QDateTime &marketTime);
Q_INVOKABLE void requestLatestCrossSectionSnapshot();
Q_INVOKABLE void selectCrossSectionInstrument(const QString &symbol);
Q_INVOKABLE void setCrossSectionBoardFilter(const QStringList &boards);
Q_INVOKABLE void setCrossSectionSortMode(int mode);
Q_INVOKABLE bool exportCrossSectionSnapshot(const QUrl &target);
Q_INVOKABLE void openInstrumentWindow(const QString &symbol);
```

接口命名可按开发会话调整，但语义不可省略：

- 查询必须返回“实际命中的批次时间”，不能把用户选择时刻冒充成数据时刻。
- `receivedCount/expectedCount`、完整率和延迟必须属于同一个 `batchId`。
- 历史查询应使用 Asia/Shanghai 解释用户输入，再转 UTC 查询数据库。
- 选中股票、筛选、排序和摘要更新必须带同一版本/批次，避免状态竞争。
- `liveRunning` 不能直接显示“行情在线”；需增加 Provider 健康/最后成功批次时间，或把文案改成“行情任务运行中”。
- CPU/MEM 遥测、渲染耗时、全屏、导出、多窗口均缺少完整 API；无法实现时应隐藏或显示明确禁用原因。

### 7.4 渲染建议

生产实现推荐增加 `CrossSectionScatterItem : QQuickItem`，使用一个 `QSGGeometryNode` 或每个板块一个批次节点绘制全部散点：

- 五个板块最多五个普通点批次，加重点和选中批次。
- 在 C++ 中完成对数/线性坐标映射、可视区索引和邻近点命中测试。
- QML 负责布局、控件、Tooltip 文本和状态，不为每个股票创建 Item。
- 刷选和缩放只更新可视变换；模型数据未变时不重建 5000 个对象。

如果开发会话先做纯 QML 原型，可用 Canvas 验证布局，但不得以 Canvas 原型通过“5000+ 点、16 ms 交互”最终验收。

## 8. `qml/Main.qml` 替换范围与文件拆分

当前 `qml/Main.qml` 共 1014 行，仍是旧版 1360×900 单页工作台。基于当前行号建议：

- 保留第 1–3 行 Qt import。
- 保留 `ApplicationWindow` 根概念，但将第 5–10 行窗口基准改为 1600×1000，并增加合理最小尺寸。
- 第 11–105 行的旧主题、单标的实时折线状态、函数和 Connections 不属于新页面，应迁入对应旧页面或移除，不能继续影响市场快照页。
- 第 107–152 行旧 `ToolBar` 应由 Figma 顶部栏组件替换。
- 第 154–1013 行旧两栏业务界面应替换为新的路由容器/市场快照页。
- 最终 `Main.qml` 只承担窗口、主题、顶栏、页面路由和底栏；不要把 5000 点绘制逻辑再次塞回主文件。

建议文件结构：

```text
qml/
├─ Main.qml
├─ theme/
│  └─ PPTheme.qml
├─ shell/
│  ├─ AppTopBar.qml
│  └─ AppStatusBar.qml
├─ controls/
│  ├─ PpPanel.qml
│  ├─ PpButton.qml
│  ├─ PpSegmentButton.qml
│  ├─ PpInput.qml
│  ├─ PpStatusChip.qml
│  └─ SnapshotStateOverlay.qml
└─ marketsnapshot/
   ├─ MarketSnapshotPage.qml
   ├─ SnapshotTimeControls.qml
   ├─ CrossSectionChartCard.qml
   ├─ CrossSectionScatterView.qml
   ├─ CrossSectionTooltip.qml
   ├─ VisibleRangeBrush.qml
   ├─ SnapshotSummaryPanel.qml
   └─ BoardFilterRow.qml
```

所有新增 QML 文件需加入 `CMakeLists.txt` 的 `qt_add_qml_module(PPTrader ... QML_FILES ...)`。若增加 C++ 渲染项和模型，按现有分层放入 `src/models/` 和适当 UI 适配层，QML 仍只通过 `TradingController` 访问业务数据。

## 9. 实施顺序

1. **数据契约**：增加全量快照模型、按时刻/最近批次查询、状态和错误属性。
2. **静态视觉骨架**：实现主题、顶栏、时刻卡、主图卡、摘要栏、底栏；先用最小 fixture，不写死 Figma 示例值到生产路径。
3. **散点渲染**：实现全量批处理、对数/线性、板块筛选、Tooltip、选中和刷选。
4. **交互**：日期/时刻、快捷时刻、播放、自动刷新、全屏、导出、双击多窗口。
5. **异常状态**：加载、空数据、低完整率、高延迟、查询错误和禁用。
6. **替换主框架**：完成独立页面验收后再收敛 `Main.qml`，避免边实现边破坏旧功能。

## 10. 视觉与功能验收清单

### Figma 一致性

- [ ] 在 1600×1000 下，顶栏 64、控制卡 92、主内容 690、底栏 48 px。
- [ ] 主图 1226、间隙 16、右栏 318，左右页边距均 20 px。
- [ ] 所有背景、边框、文本和五种板块色使用本文确认色值。
- [ ] 字体为 Noto Sans SC，关键字号/字重与 Figma 一致。
- [ ] 主卡圆角 10、控制卡 9、普通控件 6/7 px；没有额外卡片阴影。
- [ ] 选中态使用 `bgSelected + cyan`，在线/自动使用绿色方案。
- [ ] Tooltip 为 252×136，信息层级和边界避让正确。
- [ ] 节点 PNG 与应用截图可在 1600×1000 下叠加比对，一级区域误差不超过 1 px。

### 数据正确性

- [ ] 标题、轴标题、页脚、板块数量不硬编码 5364。
- [ ] 所有点来自同一 `batchId`，显示实际批次时间、覆盖率和延迟。
- [ ] X 默认按股票代码稳定排序；Y 对数只绘制正价格。
- [ ] 图表、板块数量、统计、Top 3 和选中卡使用相同筛选版本。
- [ ] 历史时刻查询明确返回最近命中批次，不伪造用户输入时刻。
- [ ] 低于 90% 完整率和超过 10 秒延迟均有非绿色警告。
- [ ] Figma 的 CPU/MEM/16 ms 示例值没有进入生产硬编码。

### 交互与状态

- [ ] 日期、时间、快捷按钮和滑块先改草稿，“应用并刷新”才请求数据。
- [ ] 连续点击不会发起重复查询，旧请求结果不能覆盖新请求。
- [ ] 五板块可独立显隐；全选和空选状态正确。
- [ ] 对数/线性、缩放、刷选、重置、全屏和导出行为可验证。
- [ ] 悬停显示十字线；单击固定；双击进入多窗口或显示明确未实现状态。
- [ ] 加载、空数据、低完整率、错误、禁用、选中、悬停均有独立表现。
- [ ] 主要控件支持键盘焦点和可访问名称，状态不只依赖颜色。

### 工程质量

- [ ] 不为 5000+ 个点创建独立 QML Delegate。
- [ ] 交互帧耗时在目标机器上接近 16 ms，至少无持续掉帧和 UI 阻塞。
- [ ] `cmake --build --preset vs2022-debug --parallel` 通过。
- [ ] `ctest --preset vs2022-debug --output-on-failure` 通过。
- [ ] `cmake --build --preset vs2022-debug --target all_qmllint` 不新增警告。
- [ ] 新模型覆盖正常、空数据、低完整率、历史查询、状态竞争和 5000+ 行容量测试。

## 11. 可实现程度与阻塞项

### 可直接实现

- 视觉外壳、尺寸、色板、字体层级、时刻控件、图表坐标区、右栏、底栏。
- 默认、选中、焦点、Tooltip、板块筛选和对数/线性视觉状态。
- 基于当前 `liveRunning/statusText` 的降级状态展示，但文案必须避免声称 Provider 真正在线。

### 需先补 API 才能完整实现

1. 全量 5000+ 快照 QML 模型；当前 `tickModel` 上限为 500。
2. 按日期/时刻查询 SQLite 中最近 `batch_id` 的读取链路。
3. 快照 loading/error/batch/expected/received/completeness/latency 属性。
4. 板块分类、快照统计、Top 3 和选中态的一致数据版本。
5. 多窗口打开、导出、自动刷新和可选的性能遥测 API。
6. 面向 5000+ 点的批处理渲染项或经过性能证明的替代方案。

### 需要主会话协调

- 确认历史时刻的匹配规则：严格命中、向前最近批次，还是允许时间窗口内最近批次。建议“向前最近批次，并显示实际时间”。
- 确认板块归类来源，特别是“主板”和“深市主板”的命名/拆分规则。
- 确认是否要求首版完成全屏、CSV/PNG 导出和双击打开多窗口；这些当前均无专用 Controller API。
- 确认 Noto Sans SC 的交付方式。
- 若需要永久节点 PNG 链接，需让 Figma Console MCP 进程继承 `FIGMA_ACCESS_TOKEN` 后重新导出；这不影响通过目标节点链接进行开发。

