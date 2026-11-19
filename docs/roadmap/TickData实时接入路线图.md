# TickData 实时接入路线图

> 状态：**接口层与接入层已完成（2026-11-09）**；性能优化未开始，按用户要求"先只做接口"。
> 基线变更（2026-11-09）：**Qt 6.11.2 安装失败，项目默认版本改为 Qt 6.12.0**。
> 6.12.0 已能配置与构建，但**尚未跑完整验收**（CTest / qmllint），见 §2。
> 契约：[TickData 对外接口契约 v1](../tickdata-live-api-2026-11-09.md)

## 1. 已完成什么

| 层 | 交付物 | 状态 |
|---|---|---|
| 服务端 | `scripts/nas_data_node.py` 新增 `GET /v1/live/<symbol>`（游标增量） | ✅ |
| 契约 | 5 个端点 + JSON 字段 + 增量语义 + 错误码 | ✅ 实测 |
| C++ SDK | `ITickDataLiveFeed` / `LiveTickBatch` / `HttpTickDataLiveFeed` | ✅ 编译通过 |
| CLI | `TickData/tools/tickdata_query.py`（零依赖） | ✅ 退出码 0/1/2 |
| 接入 | `TickDataLiveProvider`（实现 `MarketDataProvider`） | ✅ 端到端收到 tick |
| 接线 | `TradingController` 互斥源 + 4 个环境变量 | ✅ 默认关闭零影响 |

**实测证据**：`PPTRADER_TICKDATA_LIVE=1` 时 `[TickData] 本轮 5207 个标的，新增 8757346 条 tick`；
不设环境变量时 TickData 输出 **0 行**。

## 2. Qt 基线已切到 6.12.0（6.11 安装失败）

**背景**：本机 Arch 发行版 Qt 已升到 6.12.0，6.11.2 安装失败，项目无法再用 6.11.2 配置
（`cmake/PpQtToolchain.cmake` 的次版本守卫会直接拒绝）。因此 2026-11-09 起：

| 项 | 变更 |
|---|---|
| `PPTRADER_QT_VERSION` 默认值 | `6.11.2` → **`6.12.0`** |
| 新增 preset | `vs2022-qt612`、`linux-qt612-debug`、`linux-qt612-release`（含对应 build/test preset） |
| 保留为可选 | `qt611`（6.11.2）、`qt683`（6.8.3），各自 preset 与构建目录不变 |
| 文档 | `AGENTS.md` / `CLAUDE.md` / 本路线图 / wiki 已同步 |

**已完成**：`cmake --preset linux-qt612-debug` 配置通过（版本守卫 6.12 == 6.12）、
构建通过（PPTrader 二进制 157 MB，含 TickData 实时源）。

**已实测（2026-11-09，Qt 6.12.0 固定安装副本）**：

| 项 | 结果 |
|---|---|
| `ctest --preset linux-qt612-debug` | **56/56 通过**（⚠️ 见下方 headless 坑） |
| `all_qmllint` | **通过**，仅 1 条既有警告（`InstrumentSearchPanel.qml:35` 的 `var` 提升） |
| 主窗口加载（offscreen 跑 12 秒） | **未提前退出**，QML 错误 **0** |
| `PPTRADER_TICKDATA_LIVE=1` tick 投递 | **正常**（首轮 883 万条，随后每轮增量） |
| 默认关闭（不设环境变量） | TickData 输出 **0 行** |

**⚠️ headless 环境的坑（重要）**：直接在无 GL 上下文的 shell 里跑全量 CTest，
`paper_strategy_controller` 会**稳定失败**（0.9 秒内退出），stderr 是：

```
MESA-EGL: warning: failed to get driver name for fd -1
MESA: error: ZINK: failed to choose pdev
MESA-EGL: warning: egl: failed to create dri2 screen
```

该测试用 PPTrader 本体跑 `--paper-panel-probe`，会创建真实窗口 ⇒ 需要可用显示/GL。
**加 `QT_QPA_PLATFORM=offscreen` 即 56/56**。这不是代码问题，别误判成回归。

**仍未覆盖（不得宣称"6.12 已验证"）**：

- APP-001 受控滚动条（Windows）
- GPU 渲染路径
- 盘中实时长稳
- 6.11.2 时代的布局/退出/长稳结论**不能套用到 6.12.0**

> `AGENTS.md` 的规矩仍在：**任何版本升级都不得写成"缺陷已修复"，除非该版本上有可复现的验收证据。**
> 6.11.2 时代的 APP-001 / 布局 / 长稳结论**不能套用到 6.12.0**。

## 3. 性能优化（按优先级）

### P0 · 收窄实时订阅范围（最高性价比）

**问题**（已实测）：全市场 5,207 标的 × 每轮 1 请求 = 1,700 req/s，把节点打满；
首轮每标的返回 2,000 条 → 一次灌 875 万条 tick 进 `handleTick`。

**方案**：实时源只订阅「当前选中标的 + 自选表」，而不是 `buildMarketUniverse()` 全市场。
- `TickDataLiveProvider::start()` 里按 `setMaxSymbols()` 截断（默认 0=不限，接线处显式传）；
- 或在 `TradingController::startLive()` 里给实时源单独构造标的清单。

**验收**：≤200 标的时，每轮新增 ≤ 几千条、节点 CPU 不饱和、`pollCompleted` 的
`newTicks` 稳定且游标持续推进。

### P1 · 节点批量接口（减少请求数）

```
GET  /v1/live?symbols=600519,000001,...   （建议 ≤200 个/次）
→ {"schemaVersion":1, "batches":[{...单个标的的 live 响应...}]}
```
把「标的数」次请求压成 1 次。需要在节点侧做并发读取与总字节上限保护。

### P2 · 增量定位（减少单次扫描）

现状每次请求整文件 `O(行数)` 过滤。可按「游标对应的字节偏移」记录，
下次增量从偏移处继续读；需处理文件被覆盖重写的失效检测（比较 `mtimeMs`/`bytes`）。
契约里已返回 `bytes`/`mtimeMs`，正是为此预留。

### P3 · 由轮询改推送

SSE（`GET /v1/live/<symbol>/stream`，`text/event-stream`）或 WebSocket。
服务端在采集器每轮写盘后推送增量；调用方不再定时拉取。
需要处理断线重连与游标续传（同样是 `nextAfter` 语义）。

### P4 · 背压与去重

- 消费端（`handleTick`）处理不过来时丢「最旧未消费」而不是无界堆积；
- 按 `batchId`（现为 `tickdata:<date>:<time>`）做幂等去重，防止重连后重复投递。

## 4. 与现有 NAS 同步路径的关系

现在有**两条**读 TickData 数据的路：

| 路径 | 用途 | 特征 |
|---|---|---|
| `/v1/ticks/<symbol>/<date>` + `TickDataCsvImporter` | **历史/回测**：整日 CSV → 事务写库 → 派生 K 线 | 批量、可按日补、有 sha256 校验 |
| `/v1/live/<symbol>` + `TickDataLiveProvider` | **实时/当日至收盘**：游标增量 → `handleTick` | 小批量、低延迟、无落库 |

**不要混用**：历史路径要「完整性」（sha256 + 对账），实时路径要「新鲜度」。
同一天的数据，先用实时路径看盘、收盘后用历史路径入库，两条路互不干扰。

## 5. 明确不做（本期边界）

- 不改 TickData 采集器（`nq` 已失效，北交所走东财；与本次接入无关）
- 不做多标的并发压测 / SLA 承诺
- 不做鉴权轮换、HTTPS、跨机访问（仍是 loopback 明文）
- 不做实时数据落库（落库走历史路径）

## 6. 风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| 发行版 `/usr` 的 Qt 被隐式选中 | Arch 会把它滚到新次版本，产物与验收基线错位 | 工具链已改为只认项目安装副本，`/usr` 仅在显式 `-DPPTRADER_QT_ALLOW_SYSTEM=ON` 时兜底，并清掉不一致的 `Qt6_DIR` 缓存 |
| 无 GL 上下文的环境跑 CTest | `paper_strategy_controller` 因 EGL/ZINK 失败，误判成回归 | 跑测试前设 `QT_QPA_PLATFORM=offscreen`（实测 55/56 ↔ 56/56 的分界就在这里） |
| 全市场订阅 | 节点打满、内存暴涨 | P0 先做；默认关闭，需显式开环境变量 |
| 节点与采集器争抢 I/O | 盘中采集变慢 | 实时源只读；后续可给节点加读缓存 |
| 上游再变（腾讯/东财） | 北交所断供 | TickData 侧已有降级链与监控，接入层不做补偿 |
| 重复投递 | 策略重复下单 | 互斥源 + `batchId` 去重（P4） |

## 7. 验收清单

### 已完成（2026-11-09，Qt 6.12.0 固定安装副本 `~/ProgramData/Qt/6.12.0/gcc_64`）

- [x] `cmake --preset linux-qt612-debug` 通过（`Qt6_DIR` 落在项目安装副本下，`PPTRADER_QT_ALLOW_SYSTEM=OFF`）
- [x] `ctest --preset linux-qt612-debug` **56/56**（需 `QT_QPA_PLATFORM=offscreen`）
- [x] `all_qmllint` 通过（仅 `InstrumentSearchPanel.qml` 一条既有 `var` 提升告警）
- [x] 主窗口 offscreen 加载 12 秒未提前退出，QML 错误 0
- [x] `PPTRADER_TICKDATA_LIVE=1` 复验：收到 tick；默认关闭时 **0 行输出**
- [x] 删除 6.11.2 的构建产物 `out/build/linux-qt611-debug`（1.9 GB；6.11.2 在本机已装不回来）
- [x] wiki / `AGENTS.md` / `CLAUDE.md` / 本路线图只写 6.12 的证据，不把 6.11 的结论冒充为 6.12 的

### 未完成

- [ ] **P0** 收窄实时订阅范围并给出实测数据（见 §3）
- [ ] APP-001 受控滚动条（Windows 专属，Linux 无法覆盖）
- [ ] GPU 渲染路径
- [ ] 盘中实时长稳
