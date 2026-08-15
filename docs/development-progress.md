# PPTrader 开发进度

最后更新：2026-08-15

## 当前阶段

阶段 9C、10B、10C、10D v1、10E、10F、历史回测 H3A、NAS H1A–H1C、H2 回测数据集身份（H2.1–H2.3）、H3B 批量结果按需归档/中断恢复（H3B.1–H3B.3）与 H3C 组合资金回测（H3C.1–H3C.4）已完成。历史 Tick 回测具备单品种闭环、多品种轻量结果矩阵、归档型数据集身份、批量逐项归档与失败项恢复，以及共享资金池的组合定期再平衡和观测交易日并集估值；NAS 支持用户明确代码清单的受控 manifest、并发下载/串行事务导入、跨进程计划恢复与显式暂存清理。真实装载基线已验收到 100 只；500 与全市场档按用户决定延期。

状态口径（2026-08-14）：上述“完成”指代码与对应阶段自动/受控验收完成，不等于生产上线。10F 仍缺四小时以上长稳；NAS Range/Token/changes/zlib 仍缺真实节点故障与吞吐复验；H3C 只在共同交易日固定权重调仓，逐日估值覆盖首个共同交易日后的观测日期并集，不包含所有腿同时缺失的未知日期。完整矩阵与代码审查限制见 `docs/documentation-status-2026-08-14.md`。

## 已完成

| 模块 | 当前能力 |
| --- | --- |
| 工程环境 | Qt 6.4.3、VS2022 x64、CMake Presets 和 VS Code 构建链路 |
| 行情 | 模拟实时 Tick、mootdx A 股历史 K 线桥接、TickData 原始成交逐行导入、任意周期 K 线缓存、市场交易时段判断 |
| 策略与回测 | SMA、RSI、MACD、布林带与受限 JSON DSL；严格 Tick 本地回测、交易记录、权益曲线和结果归档；回测数据集内容身份（逐日规范化 Tick SHA-256） |
| 横截面计算 | A 股全市场快照、完整率、因子接口、相对强弱排名/Z-Score/分位数；5000 标的容量基线 |
| 交易账户 | 委托、成交、持仓、现金、冻结资金、手续费、盈亏核算和启动恢复 |
| 模拟撮合 | 市价/限价、部分成交、DAY/GTC/IOC/FOK、流动性参与率、到期自动撤单 |
| 风控与规则 | 单笔金额、持仓市值、活动委托数、交易日亏损限制；交易时段、整手、价格步长、停牌和 A 股涨跌停检查 |
| 策略执行 | UI 无关的信号意图、人工审批/自动执行、仓位计算、冷却期、审批过期、每日订单次数和金额预算 |
| 持久化 | 行情观测 Tick、原始成交 Tick、派生 K 线、信号、订单、成交、持仓、账户快照、策略配置、执行意图与完整回测归档；旧库增量迁移 |
| 新版界面 | 三页常驻主工作台；主页双选择器与真实交易日/本地历史双 K 线；策略页支持内置/自定义模式及严格本地回测；全市场页支持 Scene Graph 批量散点 |

## 本次完成

- 完成阶段 3.2 多股回测对接：`TradingController` 暴露 `crossSectionalBacktest*` 契约（running/status/error/结果配置/权益曲线/成交模型 + `runCrossSectionalBacktest`/`cancelCrossSectionalBacktest`），新增 `CrossSectionalBacktestPanel` QML 面板（DSL v3 JSON 输入、运行/取消、权益曲线与调仓明细），复用 `PPTheme`/`PpButton`/`BacktestEquityChart` 视觉语言并集成到策略页。`all_qmllint` 通过，主程序加载无 QML 错误。

- 完成阶段 3.1 多股回测服务：`CrossSectionalBacktestService` 在独立线程编译 DSL v3、逐腿校验数据集身份并读取 `raw_tick_cache_v1` K 线、对齐成帧后运行多股引擎，requestId 门控与取消语义同组合回测服务。`tst_cross_sectional_backtest` 新增服务生命周期与缺数据拒绝用例（10/10）。

- 完成阶段 2.5 多股回测引擎：`CrossSectionalBacktestEngine::run()` 逐帧推进——每帧调 `CrossSectionalCustomStrategy` 产出目标持仓，用 `PortfolioExecutor` 先卖后买调仓，前向填充估值并标 stale，产出 `PortfolioResult`（权益曲线/成交/摘要）。`tst_cross_sectional_backtest` 新增端到端选股回测用例（9/9）。

- 完成阶段 2.4 多股策略：`CrossSectionalCustomStrategy` 每帧求值 DSL v3——每条腿一个 `CustomIndicatorRuntime` 更新单标的指标，`CrossSectionalIndicatorRuntime` 计算横截面指标（含 PEER 引用），按 `selection.topN` 产出等权目标持仓。`tst_cross_sectional_backtest` 新增横截面选股用例（8/8）。

- 完成阶段 2.3 横截面指标运行时：`CrossSectionalIndicatorRuntime` 提供 `rank`（1-based，同值按 symbol 稳定排序）、`zscore`（总体标准差）、`percentile`（最高值 1.0）、`mean`/`median` 纯函数，口径与 `AShareRelativeStrengthIndicator` 一致。新增 `tst_cross_sectional_backtest` 覆盖排名并列/升序、总体标准差 Z-Score、分位数、均值中位数与空输入（7/7）。

- 完成阶段 2.2 DSL v3：新增 `schemaVersion=3` 顶层 `universe`（explicit 代码列表）、`crossIndicators`（RANK/ZSCORE/PERCENTILE/MEAN/MEDIAN/PEER，`of` 支持 field 或单标的指标，PEER 引用指定标的字段）与 `selection`（topN 按横截面指标排序）；条件新增 `{"cross":"alias"}` 横截面操作数；`position.type` 支持 `equalWeight`。`CompiledCustomStrategy` 携带 `universeSymbols/crossIndicators/selection`，`CompiledCustomOperand` 新增 `Cross` 类型。`tst_custom_strategy` 新增 v3 编译与非法语法用例（17/17）。

- 完成多股自定义回测阶段 1（单只闭环加固）：抽取 `PortfolioExecutor`（`equity()`/`positions()`/`rebalance()`，封装先卖后买、整手、费用、滑点、T+1 与缺报价 stale），`PortfolioBacktestEngine` 改用后结果字节级不变（`tst_portfolio_backtest` 回归通过）。新增 `AlignedBarSeries` 对齐帧序列：按 `startTime` 对齐成帧、周期不一致拒绝、缺失标的标 `absent`、`replayBar()` 只读逐帧回放。

- 完成阶段 2.1 多股对齐前向填充：`AlignedBarSeries::forwardFillClose()` 返回该帧（含）之前最近 bar 的 close，缺 bar 帧用更早 bar 前向填充并标 `stale`，首 bar 之前或未知标的返回空；预计算逐帧最近 bar 索引表使查询 O(1)。`tst_aligned_bar_series` 12/12。

- 完成 H3C.4 缺日逐日估值：`PortfolioBacktestEngine` 从首个共同交易日起遍历各腿观测日期并集，缺报价持仓沿用最近收盘并标记 `priceStale`；缺腿日只输出权益、不成交、不推进共同交易日调仓时钟。归档持仓 JSON 向后兼容保存 stale 标记，控制器向 QML 暴露 `staleValuationPointCount` 与 `valuationCalendar=observed_union`，组合面板明确显示缺日报价估值点数。Qt Test 覆盖缺日权益、调仓时钟和归档恢复。

- 完成自定义策略向导预览的来源隔离：`TradingController.customStrategyPreview()` 改由 `CustomStrategyPreviewService::buildLocalTickPreview()` 读取，SQL 固定限定 `source=raw_tick_cache_v1`，不会混入同品种、同周期的 Provider 研究 K 线，也不会回退网络。只有远端 K 线时返回明确不可用错误。`tst_custom_strategy` 新增多来源与仅远端数据用例。

- 完成 NAS 节点摘要缓存：`nas_data_node.py` 按 `(mtime_ns, size)` 缓存文件 SHA-256，`/v1/catalog` 与 `/v1/manifest` 复用缓存避免对同一文件重复哈希；`/v1/health` 新增 `digestCacheEntries`/`digestCacheHits` 统计（客户端只读 version，向后兼容）。Python 节点本地端到端校验：连续两次 catalog 后命中 1 次、摘要一致。

- 完成自定义策略 DSL v2 信号输出配置：新增可选顶层 `signal`（仅 v2），`priceMode` 支持 `triggerClose`（默认，v1 行为不变）与 `nextOpen`（触发后下一根 K 线开盘价成交）。`CustomBacktestStrategy` 增加挂起入场状态，`nextOpen` 在下一 Bar 开盘价发出买入信号并以该开盘价为持仓参考价；`tst_custom_strategy` 新增两模式成交价对比与 v1 拒绝用例（14/14）。对应设计稿 26:58 的“信号输出：价格 = 下一根开盘价”。

- 完成 NAS 传输压缩（zlib，零新依赖）：客户端全新下载发送 `Accept-Encoding: zlib`，节点 `nas_data_node.py` 返回 4 字节原始长度 + `zlib.compress` 体并带 `X-PPTrader-Encoding: zlib`；客户端 `qUncompress` 解压后仍按 catalog `size`/`sha256` 整体校验并原子提交，`Range` 续传路径保持不压缩。`tst_nas_data_node` 新增压缩下载→解压→校验→缓存复用与关闭压缩回退用例（11/11）；Python 节点本地端到端校验通过（144000 字节 CSV → 564 字节，大小/摘要/内容全对）。真实虚拟机吞吐复验与 pyarrow 二进制解析、WebSocket 变更通知、分块续传仍为后续项。协议与路线图已同步。

- 完成自定义策略 DSL v2 与向导第 3 步（指标组合）：`schemaVersion=2` 新增顶层 `series` 命名序列/别名引用（也支持操作数内联 `alias`，前向引用允许）与三个指标 `ROC`（百分比变动）、`VWAP`（Asia/Shanghai 会话累计）、`PP_FLOW`（价量趋势 PVT）；v1 文档语义保持不变并拒绝 v2 语法。新增只读预览服务 `CustomStrategyPreviewService` 与 `TradingController.customStrategyPreview()`（仅重放本地 `raw_tick_cache_v1` K 线，返回指标序列 + B/S 信号，不写库、不访问网络）。QML 新增 `CustomStrategyWizard`：八步向导壳（第 3 步完整实现）、已选指标卡片（别名/周期/显示/移除/添加白名单）、入场/出场条件构建器（序列/字段/常量、七种比较符）、右侧组合预览（Canvas K 线 + 指标线叠加 + B/S 标记 + 命中详情 + 执行摘要）、前端草稿暂存（Qt.labs.settings），并与现有 JSON 工作台双模式并存（向导生成 DSL v2 JSON → 校验/保存/回测）。设计稿依据 Figma 节点 26:58。

- 完成 H4.3 节点变更摘要与显式轮询：节点新增 `GET /v1/changes?since=<毫秒>`（只扫描根目录下一层日期目录 mtime，不递归、不下载、不触发回测），返回目录总数、最新时间与变更目录；客户端 `requestChanges`（含 401 重试）与控制器 `requestNasNodeChanges()`，NAS 面板新增"检查节点更新"按钮与变更展示。Python 真实服务验证 since 前后语义，`tst_nas_data_node` 新增 changes 解析用例（10/10）。Parquet/Zstd 压缩传输评估已写入 `nas-data-node-protocol.md`（预计冷缓存传输 8.6s→2-3s，解析仍为瓶颈，列为后续项）。

- 完成 H4.2 NAS Token 轮换：客户端支持 `PPTRADER_NAS_TOKEN_FILE` 热重载（每次请求前按 mtime 重读，环境 Token 兼容兜底）与 401 自动重试一次；节点端 `PPTRADER_NAS_TOKEN_FILE` 每行一个 Token、鉴权前重建集合（撤销的旧 Token 立即失效，环境 Token 始终有效）。`tst_nas_data_node` 新增热重载与 401 自愈用例（9/9），Python 节点脚本 Token 重建逻辑验证通过。

- 完成 H4.1b NAS 下载 Range 断点续传：客户端改为写 `.part` 暂存文件，中断/网络失败保留部分字节，重试时携带 `Range: bytes=N-` 续传（服务器不支持时自动从头并清理）；完成后按 catalog 大小与 SHA-256 整体校验并原子改名提交，缓存命中语义不变。FakeNasServer 支持 206/Content-Range 与中断模拟，`tst_nas_data_node` 新增中断→续传→完整校验用例（8/8 通过）。

- 完成 H4.1 并发下载与多活动同步队列：`NasTickSyncQueue` 支持一次取一批 Ready 项并多活动推进（下载并行、导入串行），`TradingController` NAS 同步按 `PPTRADER_NAS_DOWNLOAD_CONCURRENCY`（默认 4）并行下载、独立 requestId 门控、串行事务导入；真实 100 档热缓存复验 21.58s → 18.60s（约 14%），冷缓存预期收益更高。`nas_sync_capacity` 探针支持 `--concurrency`。

- 完成 H3C 组合资金回测（H3C.1–H3C.3）：新增 `PortfolioTypes`/`PortfolioBacktestEngine`（共享资金池、等权/固定权重、共同交易日收盘调仓、先卖后买、T+1、整手/费用/滑点、停牌估值、单腿权重上限与活跃腿数）；`PortfolioBacktestService` 逐腿校验 H2 数据集身份后异步运行；SQLite v5 组合归档三表单事务保存与恢复；策略页新增组合回测面板（腿解析、周期/调仓/根数配置、权益曲线、调仓成交、归档加载）。真实 100 档库 10 腿等权组合复验通过（10 笔建仓、佣金 299、归档成功）。

- 完成 H3B 批量结果按需归档与中断恢复：批量每项运行前加载数据集身份（身份缺失/缓存过期按失败隔离，不自动重建），完整结果有界保留（32 项 LRU）并经 `archiveBatchBacktestItem(symbol)` 复用回测归档事务按需落库；新增 `batch_backtest_runs`/`batch_backtest_run_items` 持久化（全库 v4），应用启动把遗留 running 转为可重试 cancelled，`resumeBatchBacktest(runId)` 只重跑失败/取消项且不重复成功项。
- 完成 H3B.3 批量回测 QML 面板：策略页新增 `BatchBacktestPanel`，提供策略/周期/根数/标的数上限配置、运行与取消、进度与结果列表（数据集短哈希、收益、错误）、逐项"归档"按钮，以及运行历史（runId/状态/成功失败计数）与"恢复失败项"入口；同步更新 `batch-backtest-contract.md`、`project-wiki.md` 与路线图 H3B 状态。

- 完成 H2.3 数据集身份 QML 展示：`backtestResultConfiguration` 携带完整数据集身份；自定义策略结果身份区展示 `datasetVersion`、日期范围、交易日数、原始 Tick 数、缺口与日历来源；归档面板与本地回测目录显示同一身份摘要；`LocalHistoryDataset` 与 `BacktestArchiveListModel` 增加数据集摘要字段/角色，目录刷新时由聚合身份填充。副屏验收截图留待后续补录。
- 文档同步：新增 `docs/backtest-dataset-identity.md` 契约，更新 `project-wiki.md`（v3、数据结构、归档说明）、`development-progress.md` 与 `历史Tick回测与NAS数据路线图.md` H2 状态。

- 完成 H2 回测数据集身份 H2.1：新增 `BacktestDatasetIdentity` 与 `RawTickDayFingerprint` 领域类型。身份由原始日期集合、每交易日 Tick 指纹（count/max id）、周期和聚合算法版本组合，经 `computeBacktestDatasetVersion()` 生成 `sha256:` 数据集版本；派生缓存是否新鲜不参与身份哈希，只反映在 `staleDayCount`。`SqliteLocalDataStore` 以两条 GROUP BY 聚合 SQL 实时组装身份（不落身份缓存表），`AShareTradingCalendar::applyDatasetCoverage()` 统计期望交易日、缺口并标注 `SSE-2025-45`/`weekday-fallback` 来源。
- 完成 H2.2 运行前数据准备与归档身份：新增独立线程 `BacktestDatasetService`，运行回测前加载身份、按需重建过期派生缓存（复用现有 `RawTickCandleAggregator` 派生链）、填充日历覆盖并读取 K 线；身份为空或重建后仍不一致直接拒绝，全程不访问 Provider/NAS。内置、自定义与本地自定义三个回测入口统一为异步准备→身份校验→启动引擎，旧结果不能覆盖新请求。数据库全库升级为 v3：`backtest_results` 增加 11 个 `dataset_*` 列，归档单事务保存完整身份 JSON，恢复时原样还原，历史结果不依赖当前库解释。
- 测试与验证：新增 `tst_backtest_dataset_service`（空库拒绝/过期缓存重建/根数不足），扩展 `tst_backtest_dataset_identity`（JSON round-trip）与 `tst_sqlite_local_data_store`（归档身份持久化）；2026-08-12 全量串行 Debug 构建通过，CTest 20/20，`all_qmllint` 通过。修复 Qt 空 `QString()` 绑定 SQLite `NOT NULL` 列被当作 NULL 的真实问题。

- 完成 H1A `NasTickSyncQueue`：当前 catalog 按交易日排序去重，逐日串行复用现有下载、SHA-256、流式解析和 SQLite 事务导入。单日失败继续后续日期；取消保留成功项，“重试未完成”只重试失败或取消项。队列状态、汇总、进度、当前日期和恢复能力均通过 `TradingController` 暴露。
- 策略页 NAS 工作区提供“同步选中 / 同步全部 / 取消 / 重试未完成”，状态文字显示当前代码、日期、字节和总体进度。H1C 已将计划原子持久化；重启恢复不联网，中断项可显式重试，成功项不重复。缓存统计和 30 天前暂存清理都由用户点击触发，不删除 SQLite 事实，也不自动扫描或同步全市场。
- 真实节点受控验证：节点有 45 个原始日期目录，最近样本日含 5247 个 CSV；指定样本代码的查询范围返回 39 个交易日。本次同步三个交易日，分别写入 4411、3989、4064 条原始成交并形成 238、239、239 根 1 分钟 K 线；重复运行计数稳定。临时 SSH 隧道已关闭，详细口径见 `docs/nas-multiday-sync.md`。
- 副屏 1600×1000 显示“NAS 多日同步完成：3 个交易日”，1120×760 下工作区保持可滚动且无横向溢出。证据位于 `artifacts/nas-multiday-h1a/nas-three-day-complete-1600x1000-secondary.png` 和 `nas-three-day-complete-1120x760-secondary.png`。
- 完成 H1B 受控多代码 manifest 与真实临时库容量基线。应用有效沪深 A 股口径下，1/10/100 只分别完成 4554/27715/300988 条 Tick 和 240/2363/23821 根 1 分钟 Bar，耗时 1.612/9.903/108.467 秒，零文件失败。只读全量 manifest 为 5186/5207，缺失 21；500 与全市场真实写库按用户决定延期，旧的错误筛选结果已从 `artifacts/nas-manifest-h1b/sync-capacity.json` 替换。
- 完成 H1C：`NasSyncStateStore` 以 16 MiB、12000 项上限原子持久化同步计划，启动只读本地状态；运行中项恢复为可重试取消项，成功项保持完成。`NasTickCacheManager` 仅统计/清理受管日期代码 CSV，显式 30 天保留期操作跳过符号链接和无关文件，不触及 SQLite；QML 展示 SHA-256 复用、计划恢复和缓存统计。
- 2026-08-09 H1C 串行 Debug 全量构建通过，CTest 18/18、`all_qmllint` 和 Python 节点脚本语法检查通过；临时目录测试覆盖状态 round-trip、损坏 JSON 拒绝、恢复重试、缓存统计和安全清理。运行日志为空，敏感信息扫描无命中。本轮副屏自动启动因执行环境 GUI 审批额度限制未能重新截图；此前 H1B 的 1600×1000 与 1120×760 截图仍保留，但不冒充 H1C 新增缓存行的视觉验收。

- 完成 H3A `BatchBacktestService`：独立工作线程使用 SQLite 只读连接，按数据集逐个读取 `raw_tick_cache_v1`，最多维持 20 个活动 `BacktestEngine`；单项完成即释放曲线和成交明细，只保留轻量摘要。某标的数据不足或执行失败只产生失败行，不中止整批，也不会访问 Provider/NAS。
- 新增 `BatchBacktestResultListModel` 和 `TradingController` 稳定契约：model、running、requestId、请求/完成/成功/失败数、progress、status/error，以及启动/取消入口。批量任务与单品种回测互斥，数据库搬迁会拒绝活动批量任务；原子取消标志避免零间隔引擎饿死取消事件。
- 新增临时 SQLite Qt Test，覆盖 12 个成功标的加一个缺失标的、数据版本、稳定模型角色/排序、忙状态拒绝和取消。H3A 没有修改 QML，也没有实现组合资金、批量明细归档或中断恢复；契约见 `docs/batch-backtest-contract.md`。
- 2026-08-09 串行 Debug 全量构建成功，CTest 18/18 与 `all_qmllint` 通过；运行日志无 QML 错误。H1A 已在副屏完成 1600×1000 和 1120×760 验收，仓库扫描未发现临时 SSH 验证所用的局域网地址、用户名、密码或远端路径。

- 新增非默认 CTest 的 `pptrader_backtest_capacity` Release 容量探针，直接复用 `BacktestEngine`、策略注册表和真实 `raw_tick_cache_v1` 数据形状；输出完成/失败、总耗时、标的与 Bar 吞吐、任务 p50/p95、事件循环心跳和活动引擎数。
- 当前产品接口仍是一次单品种回测，但本机计算容量已经覆盖业务规模：5207×239 根、20 个有界活动引擎用时 2.710 秒，0 失败，峰值工作集 15.66 MiB，心跳最大 22 ms。单活动引擎为 7.629 秒；64 活动引擎仅快 118 ms，但心跳最大增至 41 ms，因此不采用。
- 将压力提升到 50,000×239 根仍在 25.236 秒完成，0 失败，峰值工作集 16.88 MiB。该探针流式释放结果且只读取一次真实模板，不能代替真实 5207 份数据装载、归档、组合资金或横截面回测；完整口径见 `docs/backtest-capacity-baseline.md`。

- `LocalHistoryDataset` 新增 `rawTickDerived/backtestEligible` 稳定判定。SQLite catalog 按品种、周期、source、adjustment 分组，避免远端 K 与 Tick 派生 K 合并成模糊数据集；`localHistoryDataCatalog` 继续展示全部本地研究数据，`localBacktestDataCatalog` 只暴露 A 股 `raw_tick_cache_v1`。
- 内置 `runStrategyBacktest()`、`runSelectedBacktest()` 与自定义策略入口统一按选中品种、周期、根数直接读取 SQLite。数据缺失或根数不足时立即拒绝，并明确说明未访问网络 Provider；打开页面、切换选择和运行回测都不会触发 NAS/历史下载。
- 策略页的内置与自定义模式统一使用“回测品种 · 历史 Tick”选择器；远端 K 线工作区明确标为研究缓存。Dashboard 初始化时同步选中数据集周期，避免日线旧默认值导致真实分钟数据空图；结果摘要补齐胜率、Sharpe 和手续费真值。
- 真实虚拟机节点已部署为 `pptrader-nas-node.service` systemd 用户服务，脚本只监听远端回环地址，仓库根目录和端口仅保存在目标机环境文件；原有端口上的其他服务未被修改。通过 SSH 本地端口转发验证 health、单代码 5 个交易日 catalog 和摘要。通用 unit 模板位于 `deploy/systemd/pptrader-nas-node.service`。
- 使用现有真实 600519 单日事实验证：4064 条 Tick、239 根 1 分钟和 49 根 5 分钟派生 K 线。策略页以 200 根 1 分钟数据完成 SMA 回测：最终权益 99965.44、总收益 -0.03%、最大回撤 0.74%、5 笔成交、胜率 40%、Sharpe -0.622、手续费 2745.34，并进入现有结果归档链。
- 针对性 Qt Test 覆盖 Tick 数据可选、远端 K 拒绝、选择隔离和自定义策略数据边界；副屏 1600×1000 与 1120×760 无溢出。证据位于 `artifacts/tick-backtest-route/`。

- 新增 `TencentMinuteBarService`、`RealtimeMinuteSeriesResult` 与独立 requestId gate，只为主页当前选中的沪深品种异步请求腾讯 1 分钟 OHLCV。新请求会取消旧网络请求，旧响应不能覆盖后选品种。
- Controller 新增独立 `realtimeMinuteCandleModel`、loading/error/source/lastBarTime/stale/requestId 状态。实时任务运行时每 60 秒刷新，停止任务即取消；手动刷新仍是明确操作。该服务没有 `LocalDataStore` 依赖，不写 `raw_trade_ticks`、`candles`、历史 catalog 或回测数据身份。
- 解析器验证 OHLCV、成交量手转股、上海时区转 UTC、按最新交易日过滤和时间去重。休市保留最近交易日并显示陈旧，不将下载时间冒充行情时间。
- 主页上图已替换为“实时 1 分钟 K 线”，周期固定 1min；下图继续使用独立下载/同步到 SQLite 的历史 Tick 派生 K 线。副屏真实运行显示 600519 的 241 根腾讯 1 分钟 K 线，1600×1000 与 1120×760 布局通过。

- 阶段 10F.1 新增原生 C++ `TencentMarketDataProvider` 与 `TencentQuoteParser`。腾讯公开行情以 60 秒周期、80 只/批顺序拉取沪深全市场，作为 TDX 主备之后、自动发现之前的异构候选；`PPTRADER_REALTIME_PROVIDER=tencent` 可用于单源诊断，北交所仍不映射。
- Provider 能力新增原生分钟 K、最小周期、轮询周期和候选级 watchdog。腾讯候选使用 150 秒静默阈值，避免 1 分钟正常轮询被 30 秒全局 watchdog 误切换。报价成交量从手转换为股，行情时间按上海时区转 UTC。
- 休市返回的数据使用腾讯真实报价时间作为快照解析时间，不能用网络下载完成时间伪装当前行情；同一报价时间不会每分钟重复落库。Provider 连接健康与快照 stale 独立展示。
- 受控探测 5317 个缓存代码收到 5226 个有效报价，完整率 98.29%，67 批总耗时 18.65 秒、批次 p95 269.1 ms。应用单源副屏验收完成实际沪深股票池 5207/5207、完整率 100%、评分 100，1600×1000 与 1120×760 无溢出。腾讯原生 1 分钟 OHLCV 已接入主页选中品种实时图，但只保存在内存模型中，不写历史 K 线缓存。
- 2026-08-09 串行 Debug 全量构建成功，CTest 17/17、`all_qmllint` 与三个 Python 行情/探测脚本语法检查通过；运行日志无 QML 错误，敏感信息扫描无命中。早期错误时间语义产生的 3 个腾讯验证批次及关联观测已从开发数据库事务清理，其他数据未删除。

- 阶段 10F v1 新增 `RealtimeProviderCapability` 与 `RealtimeProviderRouter`，以单活方式管理公共 TDX 固定主源、固定备用源和自动发现兜底源。Router 与 Provider 位于同一工作线程，QML/Controller 只接收统一 Tick、快照、健康与路由事件。
- Error、连续 3 个低于 90% 完整率的批次或 30 秒无状态/Tick/快照会熔断并切换；高于等于 90% 的不完整批次保持 Degraded 服务。熔断冷却后自动重试，备用接管后不主动抢回主源。旧 Provider 的迟到 Tick/快照不能进入当前链路。
- Controller 新增活动 Provider、能力列表、0–100 评分、备用状态、切换次数和路由说明；顶部按钮、全市场摘要与公共状态栏使用这些真值。公共 TDX 仍明确为轮询快照，未冒充 WebSocket 或原始逐笔。
- 故障注入发现并修复两项真实问题：长批次内 Tick 未刷新 watchdog 会误熔断；连续高完整率 Degraded 批次不应按严重失败切换。自动测试覆盖静默切换、Tick 心跳、90% 水位线、全候选冷却恢复和旧源事件门控。
- 备用端点 200 只、2 轮探测均为 100%，源耗时 229–277 ms。副屏全市场故障注入中，不可达主源切换到备用源并得到 5156/5207 只、99.0% 快照，状态为 Degraded、评分 70；65 秒运行未再误熔断。证据位于 `artifacts/realtime-router-10f/`。
- 2026-08-09 串行 Debug 全量构建成功，CTest 17/17、`all_qmllint` 和 Python 桥接脚本语法检查均通过；仓库扫描未发现已知局域网主机、用户名、密码或远端数据路径。

- 阶段 10E 新增 `HistoricalProviderCapability` 与 `HistoricalDataProviderRouter`。Provider 明确声明市场、沪深 venue、周期、历史起点、复权、单次上限、限频、授权和配置状态；路由优先 Tushare Pro，其次 BaoStock，最后 mootdx，并在失败时自动回退。北交所本阶段不加入能力路由。
- 新增 BaoStock/Tushare JSON Lines 桥并锁定 Python 依赖。Tushare Token 只从 `PPTRADER_TUSHARE_TOKEN` 读取，未配置时不尝试联网；BaoStock 无凭据。每个候选请求限时 45 秒，新请求会清理旧路由状态。
- 显式下载支持不复权/前复权/后复权，缓存复用同时核对复权口径。来源数据统一成交量为股，并逐根校验 OHLCV；成功后以单事务写 `candles` 与新增 `historical_dataset_versions`，记录 source、adjustment、downloadedAt、根数、范围和 `sha256:` dataVersion。数据库全库 schema 由 v1 向后兼容升级为 v2。
- 真实网络验证中 BaoStock 两次在登录阶段被上游套接字拒绝，已有缓存保持不变；同机 mootdx 在 1.73 秒内成功返回 600519 的 5 根日线。实际应用随后完成 300750 的 500 根日线回退下载与 SQLite 入库，范围为 2024-07-17 至 2026-08-07；再次请求命中缓存且未访问 Provider。
- 复权缓存隔离、Provider 优先级/回退、沪深 venue、失败保护、旧请求门控、SQLite v2 迁移与身份 round-trip 已纳入 Qt Test。2026-08-09 串行 Debug 构建成功，CTest 16/16、`all_qmllint` 通过。副屏证据位于 `artifacts/historical-provider-10e/`；运行日志无 QML 错误。

- 阶段 10D 新增只读 `scripts/nas_data_node.py`：提供 v1 health、受控单代码/最多 367 日 catalog 和单日 Tick CSV 下载；支持 SHA-256、ETag、字节范围和可选 HTTPS/Bearer Token。节点不递归扫描仓库，不返回服务端绝对路径；非回环监听必须启用 Token。
- 新增 `NasDataNodeClient` 与 `NasDataNodeTypes`。客户端拒绝带 userinfo 的 URL和非 HTTPS 的非回环端点，异步校验最大 2 MiB catalog；下载写入 `QSaveFile`，大小与 SHA-256 同时通过才确认。同摘要的本地暂存文件直接复用，取消和 requestId 门控保证旧结果不覆盖新请求。
- `TradingController` 和策略页新增显式 NAS 同步契约与工作区：端点仅存 QSettings，Token 只从运行环境读取；用户必须先查询指定代码/日期范围，再选择单日执行下载与导入。通过校验的文件复用现有 `TickDataCsvImportService`，事务写入原始事实并刷新 catalog、派生周期与主页 K 线；页面打开、目录浏览和本地选择均不自动联网。
- 临时 HTTP 节点测试覆盖鉴权、catalog、下载摘要、缓存复用和端点安全策略；真实虚拟机通过 SSH 本地端口转发完成 health、目录和实际文件摘要校验。协议和部署边界记录于 `docs/nas-data-node-protocol.md`。Parquet/Zstd、完整断点续传状态机、WebSocket 变更通知和 NAS 原生 HTTPS 服务化仍是 10D 后续项。
- 真实 PPTrader UI 完成一次单日端到端同步：catalog 返回 1 个交易日，文件大小 157929 字节且 SHA-256 匹配；流式读取 4069 行、接受并事务写入 4064 条原始成交、按交易时段跳过 4 条盘后记录，刷新 239 根 60 秒 K 线。副屏 1600×1000 与 1120×760 复核通过，证据位于 `artifacts/nas-data-node-10d/nas-catalog-real.png`、`nas-sync-import-real.png` 和 `nas-sync-minimum-1120x760.png`。
- 2026-08-09 串行 `cmake --build --preset vs2022-debug --parallel 1` 成功，CTest 15/15 全部通过，`all_qmllint` 通过；仓库敏感信息扫描未发现真实主机、用户名、密码或远端数据路径。临时 SSH 隧道和回环服务在验收后已停止。

- 阶段 10C 新增 `StorageProfile`：应用从 `QSettings` 读取本地数据库绝对路径，未配置时使用程序目录 `data/pptrader.db`。UNC/SMB/NFS 路径被明确拒绝为 SQLite 打开目标；局域网/NAS 数据必须走 10D 服务接口，而不是共享 WAL 文件。
- 新增 `DatabaseRelocationService` 和 Controller 异步 `relocateStorage()` 契约。搬迁前拒绝正在运行的行情、回测、历史下载、CSV 导入、批次导入、查询或未排空写队列；随后保存账户状态，停止并关闭全部 SQLite 连接，在工作线程复制到目标临时文件，验证完整性和 database UUID 后原子确认并切换。源库始终保留，不执行不可恢复删除；目标已存在时拒绝覆盖。
- `SqliteLocalDataStore` 支持显式 checkpoint/close/reopen；`AsyncTickWriter`、快照查询和两条 K 线查询服务改为停止数据库连接但保留工作线程，因此可在搬迁后重连。Controller 暴露 database UUID、schema version、搬迁中和错误状态，成功后持久化新路径并刷新 catalog/归档/导入历史。
- 临时 SQLite 测试覆盖验证复制、UUID/事实数据保持、源库保留、目标覆盖拒绝、网络共享拒绝，以及后台 Tick Writer 在 source→target 间 stop/start。串行 CTest 14/14 与 `all_qmllint` 通过；新版本已在副屏恢复真实 v1 运行库。尚未启用设置页文件选择器，也未实现 NAS 认证或远端同步。

- 阶段 10B 建立全库 schema v1：`PRAGMA user_version` 是数据库整体版本，`app_database_metadata` 保存稳定 database UUID、schema/application 版本与创建/迁移时间，`schema_migration_history` 保存不可重复的迁移名称、应用时间和校验标识；原 `schema_versions` 继续仅表示子模块版本。
- 旧库升级前使用 SQLite `VACUUM INTO` 生成同目录一致性备份；随后在一个事务中运行现有兼容建表/补列、写入全库身份和迁移历史，并执行 `integrity_check`、`foreign_key_check` 与必需业务表检查。任一步失败即回滚，不留下半套 schema；高于程序支持版本的数据库会明确拒绝打开。
- 测试验证旧数据保留、备份仍为升级前 `user_version=0` 且 `integrity_check=ok`、数据库 UUID 重启稳定、第二次打开不重复备份，以及人为破坏元数据表时事务回滚并保留旧表。真实运行库已从 v0 升级为 v1，备份为 `out/build/vs2022-x64/Debug/data/pptrader.db.pre-v0-20260809-003223-039.bak`；迁移后 600519 的真实双 K 线仍可恢复。
- 10B 完成后的串行 Debug 全量构建、CTest 14/14 和 `all_qmllint` 通过；迁移后副屏证据为 `artifacts/chart-interaction-10a/database-v1-real-data-secondary.png`。迁移备份保留期与用户删除入口尚未实现，进入 10C 后处理配置与搬迁生命周期，仍不会自动删除备份。

- 阶段 9C 新增 `RawTradeIncrementalIngestService`：只接受同一代码、交易日和真实来源的增量批次，要求稳定 `sourceLine`/成交序号并先整体校验；TDX 轮询快照未接入该入口，不能被误当成逐笔成交。
- `SqliteLocalDataStore::appendRawTradeTicksIncremental()` 使用单事务和现有唯一键幂等追加，公开接收、写入、重复、受影响周期和缓存错误。新成交仅改变对应代码/交易日的事实指纹；服务主动重建此前实际使用过的周期，未使用周期仍按查询时生成。
- 临时 SQLite 测试覆盖重复重放、局部 60/300 秒缓存刷新、刷新后零重建命中，以及混合代码批次在写入前整体拒绝。真实盘中 Provider 尚未接入；后续由 10F 的 QMT/其他真实成交源或 10D NAS 数据节点调用统一入口。
- 阶段 10A 首版完成 C++ 可测试视口：以指针为锚点缩放、按 Bar 平移、边界归一化和可见价格范围；`LocalCandleSeriesItem` 仅为当前可见窗口生成 Scene Graph 几何。QML 支持滚轮、拖拽、十字线、价格/时间标签、OHLCV、单击锁定、Esc 取消、双击/Home/右键复位及键盘缩放平移。
- 2026-08-09 串行 Debug 全量构建、CTest 14/14 与 `all_qmllint` 通过。真实 600519 的 239 根 1 分钟与 49 根 5 分钟数据在副屏完成滚轮缩放和拖拽复核，截图为 `artifacts/chart-interaction-10a/dashboard-kline-zoom-1600x1000-secondary.png` 与 `dashboard-kline-pan-1600x1000-secondary.png`。

- 完成阶段 9B 真实接入验证：远端 600519、2026-08-07 文件共 4069 行，识别 4064 条有效成交，4 条盘后记录按 A 股时段规则跳过；事务写入 4064 条 `raw_trade_ticks`，生成 239 根 1 分钟 Candle。
- 扩展逐笔解析器兼容实际数据源的中文六列表头“成交时间、成交价格、价格变动、成交量、成交金额、性质”，并支持买盘/卖盘/中性盘；原五列 B/S/M 格式保持兼容。中文格式的成交量仍按手乘 100 转为股，价格变动列只作为源字段存在，不参与 OHLC 计算。
- 集合竞价桶由“仅精确 09:25:00”修正为 `[09:25:00, 09:30:00)`，覆盖真实文件中的秒级撮合时间；午休、15:00 之后和其他盘外记录仍不进入事实表或 K 线。
- 主页实测上图显示 239 根 60 秒真实交易日 K 线，下图由同批原始 Tick 按需重建为 49 根 300 秒 K 线（含集合竞价桶）。截图为 `artifacts/raw-tick-kline-9a/real-ssh-kline-dashboard-1600x1000.png`，导入审计截图为 `real-ssh-import-result.png`。
- 本次 SSH 仅用于运行期受控传输，临时 CSV 在成功入库和截图后已删除；应用仍不保存 SSH 配置或凭据，也没有自动扫描远端目录。2026-08-09 CTest 14/14 与 `all_qmllint` 通过。

- 完成阶段 9A：新增 `raw_trade_ticks` 事实表。逐笔 CSV 以 2048 条为一批从 `QFile::readLine()` 流式写入，同一代码/交易日的替换在一个 SQLite 事务中完成；解析失败或取消会回滚并保留此前完整交易日。原有 `ticks` 继续表示 Provider/快照行情观测，不能再被误当成逐笔成交事实。
- 新增 `RawTickCandleAggregator` 和 `raw_tick_candle_cache_state`。查询接受 1–86400 秒周期；小于 4 小时按集合竞价、上午、下午独立分桶，4 小时及以上归为单交易日桶，任何周期都不跨交易日。缓存记录原始行数、最大 ID 与算法版本，任一变化都会只重建对应代码/日期/周期的 `raw_tick_cache_v1` Candle。
- `LocalCandleSeriesQueryService` 会在独立 SQLite 连接中先检查并生成缺失/过期缓存，再执行最新页优先查询和 OHLCV 保真显示聚合。新增“仅最近原始交易日”语义供主页实时图使用；通用本地历史查询可输入任意秒周期。
- `TradingController` 新增独立 `liveDayCandleSeriesModel` 状态族，以及 `requestLiveDayCandleSeries()`、`requestLocalCandleSeriesPeriod()`。主页上图从最新原始交易日读取真实 K 线，下图从本地历史数据读取真实 K 线；原权益曲线和回测成交仍保留在“指标与策略”页。
- 新增 `CandleSeriesChart`，网格仅使用一个 Canvas，全部蜡烛继续由 `LocalCandleSeriesItem` 的 Scene Graph 批量几何绘制。主页两个周期输入均明确限制为 1–86400 秒，无数据、加载和错误状态不绘制伪造曲线。
- 5min 有表头文件仍作为外部 Candle 导入：由于无法从 OHLCV 反推出逐笔成交，它不会写入 `raw_trade_ticks`，也不能派生其他周期。单文件导入面板新增“原始 Tick 入库”计数，避免把写入 Bar 数误认为事实记录数。
- 临时 SQLite 测试覆盖 90 秒任意周期、缓存命中、原始日替换后的缓存失效、事务失败回滚和原始 Tick 数量。2026-08-09 串行 Debug 构建、CTest 14/14、`all_qmllint` 均通过。
- 副屏 `Left=-2500, Top=40` 复核客户区 1600×1000 与 1120×760：双 K 线卡片、周期控件和空状态无溢出。开发运行库尚未重新导入原始 Tick，因此截图诚实显示空图；证据位于 `artifacts/raw-tick-kline-9a/`。

- 完成阶段 8D：`SqliteLocalDataStore` 新增本地 K 线计数与分页查询。第 1 页固定为最新一页，每页内部仍按时间升序返回；查询只接受 catalog 中实际存在的 1min/5min A 股数据集，空数据和越界页有独立状态。
- 新增 `LocalCandleSeriesQueryService` 与独立工作线程。分页结果超过显示预算时按连续桶聚合：开盘取首根、收盘取末根、最高/最低取极值、成交量及 B/S/M 分向量求和，并公开 `sourceCount/bucketSize/downsampled`；桶不会跨上海交易日或上午/下午会话，没有采用丢点抽样。
- `TradingController` 暴露 `localCandleSeriesModel`、loading、当前/已解析 requestId、品种/周期、页码、总根数、原始根数、显示点数、桶大小、时间范围、状态和错误，以及 `requestLocalCandleSeries()`。`LocalCandleSeriesRequestGate` 拒绝过时完成事件。
- 新增 `LocalCandleSeriesItem`。GUI 线程从模型复制紧凑点缓存，Scene Graph 同步阶段只读该缓存；影线、上涨实体、下跌实体分为三个 `QSGGeometryNode`。800 点预算为 1,600 个影线顶点和 4,800 个实体顶点，不包含 GPU 完整帧时间承诺。
- 新增 `LocalMinuteSeriesPanel`，可选 SQLite 已有 1min/5min 数据集、分页大小和显示预算，提供最新/更早页、悬停 OHLCV、加载/空/错误/降采样状态。键盘可访问控件已接入；图表明确不伪造逐根键盘选择。
- `local_candle_series` Qt Test 使用临时 SQLite 覆盖最新页/更早页、OHLCV 聚合、空与非法请求、乱序响应门控、模型角色、命中和几何顶点预算。2026-08-08 串行 Debug 构建、CTest 14/14 和 `all_qmllint` 通过。
- 副屏 `Left=-2500, Top=40` 运行复核覆盖客户区 1600×1000 与 1120×760，窗口无横向溢出或 QML 运行错误。开发运行库当前没有 1min/5min catalog，因此截图如实显示空状态，没有写入或伪造样本；证据位于 `artifacts/local-minute-series-8d/`。
- 完成阶段 8C：新增 `AShareTradingCalendar`。2026 年元旦、春节、清明、劳动、端午、中秋和国庆休市范围来自上交所公告〔2025〕45号；周末与节假日分别统计。日历同时供 TickData Planner 和 `MarketSession` 使用，休市日不会启动模拟行情或通过交易时段检查。
- 日历覆盖状态不伪造：2026 年标记来源 `SSE-2025-45`；其他年份的周一至周五仍可按兼容策略生成计划，但每项和摘要都标记 `weekday-fallback`。后续需要按年度维护权威公告。
- 计划阶段通过 `candleCountsByTradingDate()` 按 Asia/Shanghai 统计 SQLite 当前周期数据，暴露 existing/expected、empty/partial/complete 和 `willOverwrite`。默认跳过达到 1min 240 根或 5min 48 根的日期；用户可关闭跳过，此时已有数据日期明确显示将执行幂等 UPSERT。
- 新增 `tickdata_import_run_items`，事务化保存每个日期的状态、文件大小、覆盖预检、读取/有效/跳过/写入数量、质量和错误，但不保存根目录或文件路径。父摘要新增跳过完整、覆盖日期、排除休市日、回退日期和日历来源；旧库只增列迁移。
- 历史摘要支持按六位代码和任务状态参数化筛选。用户重新确认根目录后，可从 SQLite 恢复所选任务的逐日计划：成功日期保留，失败、取消、缺失及崩溃时 Running 日期按当前确定路径重新检查；旧版无逐日明细的摘要明确拒绝恢复。
- `TickDataBatchImportPanel` 新增“完整日跳过”、覆盖统计、逐日预检、历史筛选和“载入失败日期”。所有查询只访问 SQLite；恢复只重建确定路径，不枚举目录、不解析 CSV，真正读取仍需用户再次点击开始/继续。
- 测试新增 2026 官方休市/开市边界、未覆盖年份回退、完整/部分覆盖、成功日期恢复、逐日明细事务 round-trip 和历史筛选。TickData 测试共 15 项通过；全量 CTest 13/13 通过。
- 完成阶段 8B：新增 `TickDataBatchPlanner`，仅按用户确认的代码、格式和最多 366 个自然日生成预期路径；周末从计划中排除，空文件和缺失文件标为 Missing。生成计划只读取 `QFileInfo`，不会解析 CSV、递归枚举目录或扫描全量 TickData。
- 批次执行复用阶段 8A 的 `TickDataCsvImporter`，保持单线程、逐文件顺序处理。每个成功交易日独立调用 `upsertCandles()` 事务写入；解析/写库失败记录在该计划项并继续下一日，结束后统一刷新本地历史数据目录。
- 支持运行中取消与显式继续：取消检查发生在逐行读取边界；继续时保留已成功项，只重新检查并执行失败、取消或原缺失的预期文件。requestId/运行状态隔离单文件和批次任务，避免旧完成事件覆盖新任务。
- 新增数据质量审计：报告跳过记录、时间逆序、连续竞价参考 Bar 缺失和 `healthy/warning/error`。完整交易日参考值为 1min 240 根、5min 48 根，不含 09:25 独立集合竞价 Bar；该值只作质量提示，停牌、半日市等场景不会被伪造成解析失败。
- 新增 `tickdata_import_runs` 表，保存 run ID、代码、日期区间、格式/周期/量单位、成功/失败/缺失/取消数、记录与 Bar 统计、质量和状态。表中不保存根目录、文件路径、主机、用户名或凭据；启动时恢复最近运行摘要供界面查看。
- 新增 `TickDataBatchImportPanel`：提供日期区间、显式计划、开始串行导入、取消、继续失败项、逐日状态、质量提示和最近运行摘要。QML 只调用 `TradingController`，目录/股票/单文件入口在批次运行期间互斥。
- Qt Test 使用 `QTemporaryDir` 覆盖周末排除、计划不解析、缺失文件恢复、成功项保留、好/坏/好三日继续、SQLite catalog 与摘要 round-trip、时间逆序/缺 Bar 质量提示和流式取消。测试不访问网络、开发机数据库或真实 TickData。
- 2026-08-08 串行 Debug 构建、CTest 13/13 和 `all_qmllint` 零警告通过。副屏视觉复核使用自动行情关闭的空闲工作台，未启动计划、未打开任何真实 CSV；截图为 `artifacts/tickdata-import-8b/batch-plan-1600x1000-secondary.png` 和 `batch-plan-1120x760-secondary.png`。
- 完成阶段 8A：新增 `TickDataCsvImporter` 与后台 `TickDataCsvImportService`，只接受用户显式选择的本地/已挂载绝对目录，按 `YYYYMMDD/代码.csv` 或 `YYYYMMDD+5min/代码.csv` 定位单个文件。没有实现远端同步、SSH/SFTP 或任何凭据配置。
- 逐笔格式固定为无表头 `HH:mm:ss,price,volume_lots,amount,side`。时间按 `Asia/Shanghai` 解释后转 UTC，成交量从手乘 100 转为股；B/S/M 使用独立 `RawTradeDirection`，聚合后分别保存 `buyVolume/sellVolume/neutralVolume`，不复用 `TradeSignal`。
- 5min 格式要求精确表头 `time,open,close,high,low,volume`。成交量单位默认 Unknown，必须显式选择“股（原值）”或“手（×100）”才能导入；系统不会依据数值猜测或擅自乘 100。
- 聚合支持 60/300 秒。09:25 集合竞价单独成 Bar；连续竞价使用 09:30–11:30、13:00–15:00 两个独立锚点，午休不跨 Bar；11:30 和 15:00 归入各自时段最后一个 Bar，15:05 及其他非交易时段记录跳过并给出路径、行号和原因。
- `TradingController` 暴露目录、品种、日期、格式、周期、量单位、requestId、running、读取/有效/跳过/写入数量、时间范围、source、结构化诊断和错误状态。只有显式“导入单代码单交易日”会打开文件；取消或解析失败不写 SQLite。
- `candles` 以向后兼容列保存买/卖/中性方向量和 `directional_volume_known`。既有唯一键继续保证重复导入幂等；UPSERT 使用单事务，成功后刷新 `localBacktestDataCatalog`，失败保持原 catalog。
- 新增 `tickdata_csv_import` Qt Test，全部使用 `QTemporaryDir`，覆盖两种 CSV、时区、手/股单位、B/S/M、OHLC、09:25/午休/11:30/15:00/15:05、结构化错误、SQLite 幂等与 catalog，以及 100,000 行合成文件逐行处理。
- 2026-08-08 串行 Debug 构建、CTest 13/13 和 `all_qmllint` 通过；大合成文件测试读取 100,000 行并生成 120 根 1min Bar。副屏 `Left=-2500, Top=40` 完成 1600×1000 与最小客户区 1120×760 验收，截图为 `artifacts/tickdata-import-8a/tickdata-import-1600x1000-secondary.png` 和 `tickdata-import-1120x760-secondary.png`；运行时保持自动行情关闭，未读取真实 CSV。
- 完成阶段 7B：新增版本化 `backtest_results`、`backtest_equity_points`、`backtest_trades` 和 `schema_versions`。结果父记录、全部权益点与成交明细在一个 SQLite 事务中写入；子项失败会回滚父记录，不留下半个归档。
- 归档保存覆盖 result/task ID、完成时间、策略 ID/版本、规范化参数、规则快照、symbol/venue/周期、请求/实际 Bar 数、起止时间、资金/费率/整手、`sha256:` 数据版本、完整摘要、权益点与成交字段。结构不含 Provider 或券商凭据。
- `BacktestEngine` 在过滤并排序真实输入后记录 `actualBars`。`TradingController` 仅在成功完成且归档身份校验通过后调用写库；写入失败时保留当前内存结果，但明确发布“本次结果未保存”，不会冒充已归档。
- 新增 `BacktestArchiveListModel` 和控制器属性/命令：启动恢复摘要列表，按 result ID 只读加载 SQLite 详情，并用独立展示副本更新 `backtestResultConfiguration`、权益曲线和成交模型。历史配置不再引用当前引擎或编辑状态。
- 新增 `BacktestArchivePanel`。策略页可键盘选择、查看 result ID、请求/实际根数和 dataVersion，再显式加载归档；加载后显示“历史快照，与当前状态可能不同”。主页复用同一真实模型并标识“已加载归档”。
- 临时 SQLite 测试覆盖内置/自定义完整 round-trip、重启恢复、稳定倒序、模型角色与非法行、强制子表失败事务回滚、恢复不访问历史 Provider，以及当前配置变更不篡改历史身份。
- 2026-08-08 Debug 串行构建和 CTest 12/12 通过；`all_qmllint` 在修正归档委托焦点背景类型后通过。使用既有 600519 本地 500 根缓存完成 SMA 任务（17 笔，仅用于链路验收），随后重启确认只恢复 1 条摘要，再显式加载权益与成交；全过程未访问 Provider。副屏截图为 `artifacts/backtest-archive-7b/archive-loaded-result-1600x1000-secondary.png`、`archive-loaded-result-1120x760-secondary.png` 和 `archive-loaded-dashboard-1600x1000-secondary.png`，窗口坐标均为 `Left=-2500, Top=40`，最小窗口实际外框 1136×799。
- 当前不提供归档删除、保留期清理、批量导出、多结果对比或云同步；本地归档默认持续保留，由用户自行备份运行数据库。
- 完成阶段 7A：新增 `LocalHistoryDownloadPanel`，提供独立 A 股下载目标、周期、2–5000 根目标数量、当前缓存、数据缺口、实际来源、入库数量、可用范围、错误和请求版本。只有“下载并保存本地数据”按钮会启动获取。
- `TradingController` 新增独立 historyDownload 状态族和 `selectHistoryDownloadInstrument()`/`downloadAndStoreHistory()`；下载目标不修改主页实时品种、主页本地回测品种或指标与策略页研究品种，下载状态也不复用回测 running/progress/error。
- 显式下载使用独立的 `KlineDataFetcher + HistoricalDataService` 实例，避免历史预览或回测请求互相取消；仍复用现有 mootdx 历史协议，没有引入第二种数据源。
- 缓存已经满足目标根数时返回“缓存复用”且本次入库为 0，不调用 Provider；不足时只在显式按钮触发后请求 Provider。返回数据逐根校验 symbol、周期、时间、有限 OHLCV、正价格、高低关系和非负成交量，仅有效记录进入事务 UPSERT。
- Provider 失败不会删除或覆盖已有有效缓存；成功后刷新 `localHistoryCatalog`，主页面 `LocalBacktestSelector` 和自定义策略严格本地入口立即看到新数据。两类选择与严格本地回测继续只读 SQLite。
- `HistoricalDataService` 的活动 requestId 会淘汰过时响应；控制器另行公开递增 `historyDownloadRequestVersion`，旧结果不会覆盖新请求状态。界面只显示进行中状态，不伪造百分比进度。
- 临时 SQLite + fake Provider 测试覆盖显式请求、有效数据过滤、入库与 catalog、完整缓存复用、失败保护、选择不触网和两个真实在途请求的乱序响应门控；测试不访问网络。
- 2026-08-08 Debug 串行构建、CTest 12/12 和 `all_qmllint` 通过。副屏 `Left=-2500, Top=40` 完成 1600×1000 与最小客户区 1120×760 截图；三段下载工作区无横向溢出，窄窗口通过现有纵向滚动访问。
- 阶段范围不含全市场批量下载、复权/公司行动、真实分钟级实时图或 Figma 修改。
- 完成自定义策略 QML 阶段 6A。`StrategyResearchPage` 新增“内置策略/自定义策略”模式切换；原内置策略目录、参数配置、历史加载、权益曲线和成交记录保持原有行为与常驻结果。
- 新增 `CustomStrategyWorkbench`：展示 `customStrategyModel` 中的策略 ID、版本、latest、`backtest_ready`、executable 和 `backtest_only` 真值；选中版本会通过 `exportCustomStrategyJson()` 加载原始定义。
- JSON 编辑器只处理声明式文本，显式调用控制器校验、分析和保存接口。字段/路径错误直接显示；编辑后的内容必须重新校验，且规范定义必须与已保存版本一致，才能运行。已存在版本不会伪装成“新版本”再次保存。
- 新增 `runSelectedLocalCustomStrategyBacktest()` 与 `customStrategyLocalRunAvailability()`。该入口只读取阶段 5A 选中的 SQLite 数据集，不调用 `HistoricalDataService`、`fetchHistory` 或 Provider；缺失数据、根数不足、预热不足、定义无效或任务运行中均返回明确原因。
- 严格本地入口仍复用阶段 4 的 `CustomStrategyCompiler`、不可变 strategyId/version、规范化参数、规则快照和 `BacktestEngine` SHA-256 `dataVersion`，没有削弱结果身份校验。
- 结果区可折叠并复制 strategyId、version、规范化参数和 dataVersion；`backtestResultMatchesCustomStrategy()` 对比当前选择，版本不一致时显示“结果属于其他策略版本”。
- 新增 `CustomStrategyWorkspacePolicy` 及 Qt Test，使用 `QTemporaryDir` SQLite fixture 覆盖无效/非 executable、未选本地数据、根数不足、有效本地数据、模型刷新后精确版本定位和结果版本匹配；不读取开发机数据库。
- 2026-08-08 串行 Debug 构建、CTest 12/12 与 `all_qmllint` 通过。副屏 `Left=-2500, Top=40` 完成 1600×1000 和最小客户区 1120×760 验收；窄窗口顶部卡片换行并通过纵向滚动访问 JSON 工作区，无横向越界。
- 本阶段未修改 Figma，也未实现自由代码策略、LLM 生成、网络请求或自定义策略模拟/实盘执行。
- 完成阶段 5B：`DashboardPage` 重构为左侧实时/本地回测双选择器与右侧上下双图表的桌面工作台；1600×1000 与最小客户区 1120×760 均保持核心图表可见，且不改变三个顶层常驻 Tab。
- 新增 `LiveInstrumentSelector`。它只搜索阶段 5A 已加载的 A 股股票池并调用 `selectLiveInstrument()`；切换不会修改指标与策略页的 `selectedInstrument` 或本地回测选择。
- 新增 `LiveSnapshotRangeChart`，使用 `marketSnapshotModel` 中选中股票的真实 open/high/low/price 绘制最新快照价位范围和当前点，并展示实际涨跌、成交量、Provider/陈旧/错误状态。该图不是分时图或 K 线；项目尚无选中实时品种的逐笔/分时序列时，不绘制伪造曲线。
- 新增 `LocalBacktestSelector`，只消费 `localBacktestDataCatalog`，显示实际周期、K 线根数和首末时间；刷新清单和选择均不触发网络请求，缺失或根数不足直接展示控制器错误。
- 主页回测区复用真实 `equityCurveModel` 与任务结果身份，空结果显示明确状态；有结果时展示真实总收益、最大回撤、交易数和权益曲线，不伪造历史 K 线或回测指标。
- 双选择器、刷新操作、搜索结果和图表状态已补充中文 Accessible 名称、键盘焦点和 Enter/Space 激活。QML lint 兼容 Qt 6.4，不使用较新版本才提供的 `visualFocus`。
- 2026-08-08 串行 Debug 构建、CTest 12/12 和 `all_qmllint` 通过。副屏 `DISPLAY1` 验收窗口位于 `Left=-2500, Top=40`；1600×1000 与最小窗口实际外框 1136×799 截图均无控件越界，双图表保持可见。
- Figma Starter MCP 调用额度已耗尽，本阶段未重试也未修改 Figma；视觉实现依据已确认的 node `2:2` 信息层级、项目设计交接文档和现有 `PPTheme`。
- 完成主页双品种选择底层契约阶段 5A。`selectedLiveSymbol/selectedLiveInstrumentTitle` 与 `selectedBacktestSymbol/selectedBacktestInstrumentTitle` 使用独立状态和独立通知信号，不再借用 legacy `selectedInstrument`。
- 新增 `WorkspaceInstrumentSelectionState`，同时保存研究、实时和本地回测三类选择。实时选择只接受已加载 A 股股票池；本地回测选择只接受 SQLite 清单中指定周期且达到最小根数的数据，失败不会调用 `HistoricalDataService`。
- `LocalDataStore` 新增 `localHistoryCatalog()`；SQLite 最初按实际 `candles` 表建立清单，当前已进一步按代码、周期、source 和 adjustment 隔离，返回名称、市场、venue、周期、来源、K 线数与首末时间。空库和选中缓存消失均有明确不可用状态。
- 控制器新增 `localBacktestDataCatalog`、`selectedBacktestDataAvailable`、周期、秒数、K 线数、首末时间和选择错误属性，以及 `selectLiveInstrument()`、`selectLocalBacktestInstrument()`、`refreshLocalBacktestDataCatalog()`。历史缓存完成写入后会刷新清单，但刷新本身不触发网络。
- 新增 `workspace_instrument_selection` Qt Test；SQLite fixture 测试覆盖多代码/多周期真实聚合和 unknown 来源排除，选择测试覆盖三类状态隔离、股票池外实时品种、缺失周期、根数不足及缓存消失。本阶段未修改 QML 布局或 Figma。
- 完成自定义策略回测接入：新增 `runCustomStrategyBacktest()`，按 `strategyId + version` 从启动恢复的不可变定义集中查找，编译后复制计划，再发起已有异步历史数据请求；运行中导入新版本不会改变当前任务。
- `BacktestEngine` 支持受控外部 `BacktestStrategy` 实例，但只接受 `custom_` ID，且实例声明的 ID、版本、规范化参数和规则快照必须与 `BacktestConfig` 完全一致；不一致直接拒绝。
- 新增确定性 `dataVersion`：引擎对实际参与回测的排序后 K 线生成 `sha256:` 摘要，并与策略版本、参数、规则一起进入 `BacktestResult.config` 和 `backtestResultConfiguration`。
- 自定义策略模型、Schema 与分析状态升级为 `backtest_ready`、`executable=true`；该状态仅表示可用于历史回测，`executionScope=backtest_only` 仍阻止模拟/实盘执行。
- 回测引擎测试新增 50% 仓位外部策略、版本/规则快照、相同数据相同结果/摘要、数据变更摘要变化和非法身份拒绝覆盖。
- 完成自定义策略 DSL 第三阶段：新增固定仓位比例、止损、止盈、最大持仓 Bar 和强制 A 股 T+1；旧定义缺省时保持 100% 仓位、风险阈值关闭和 T+1 开启。
- 仓位与风控规则在编译时固化为 `ruleSnapshot`；`analyzeCustomStrategyJson()` 暴露该快照，`BacktestConfig` 预留 `strategyVersion` 与 `ruleSnapshot`，避免结果只记录策略 ID。
- `CustomBacktestStrategy` 记录买入参考价、交易日和持仓 Bar。当日触发退出只锁存首次原因，下一交易日首个有效 Bar 才卖出，价格恢复不会清除待执行退出。
- `BacktestStrategy::positionFraction()` 为内置策略提供兼容默认值，`BacktestEngine` 按策略仓位比例计算可用预算；现有内置策略结果保持全仓默认语义。
- `custom_strategy` 测试新增非法规则、默认/显式快照、50% 仓位、止损 T+1 延迟、止盈、最大持仓和重置覆盖。公共自定义策略回测入口仍未注册。
- 完成自定义策略 DSL 第二阶段：`CustomStrategyCompiler` 将已验证 JSON 编译为固定枚举、索引化操作数/条件和去重指标计划；运行参数先按 Schema 补默认值并再次校验，不在逐 Bar 路径解析 JSON。
- 新增增量 `CustomIndicatorRuntime`，覆盖 SMA、EMA、RSI、MACD、BOLLINGER、ATR 和成交量均线。预热根数、EMA/MACD 种子、连续窗口、缺失值重置和布林带总体标准差语义均已固定。
- 新增 `CustomBacktestStrategy` 三值条件求值器。普通比较遇到缺失为 Unknown，布尔操作保留 Unknown，上穿/下穿只比较相邻有效 Bar且不跨缺口；`reset()` 可得到相同信号序列。
- 控制器新增 `analyzeCustomStrategyJson()`，只返回 compilable、规范化参数、预热根数和计划规模；当前明确返回 `compiled_not_registered`、`executable=false`，尚未开放公共回测按钮。
- `custom_strategy` 测试新增七类指标数值/预热/缺失值、全白名单编译、MACD 快慢周期、跨缺口交叉和重置确定性覆盖。
- 新增自定义策略 DSL v1 领域定义与 `CustomStrategyValidator`。定义只能使用声明式 JSON、A 股市场和 `backtest_only` 作用域；未知指标、未来字段、非法操作符、非有限/越界参数、过深或过大的表达式均被拒绝。
- 首批白名单包含 SMA、EMA、RSI、MACD、BOLLINGER、ATR、成交量均线，布尔/比较条件、行情字段、常量和参数引用；本阶段仅为 `schema_only`，不会创建可执行策略或进入模拟/实盘订单链路。
- SQLite 新增向后兼容的 `custom_strategies` 表，以 `(strategy_id, version)` 为主键。同版本同内容导入幂等，同版本不同内容拒绝覆盖，新版本并存且按 ID/版本稳定恢复。
- 新增 `CustomStrategyListModel` 与控制器契约：`customStrategyModel`、Schema、数量、错误、JSON 校验/保存、版本详情和规范化导出；QML 仍只经 `TradingController`，本阶段未猜测缺失的正式编辑器视觉。
- 新增 `custom_strategy` Qt Test，覆盖合法 Schema、未知指标、未来字段、自动交易作用域、参数越界、非法 JSON、版本不可变、SQLite 重启恢复和模型角色。
- `TradingController` 新增稳定的回测配置状态：周期、K 线数量、初始资金、佣金、滑点和当前策略参数均以 Q_PROPERTY 暴露；`setBacktestConfiguration()`、`setSelectedBacktestParameter()` 和恢复默认接口统一执行边界及交叉参数校验，`runSelectedBacktest()` 不再使用写死配置。
- 结果身份新增 `backtestResultConfiguration`，将已完成任务的 taskId、策略、规范化参数、品种、周期、请求根数、资金、费率和整手快照绑定在一起，避免后续编辑覆盖历史结果说明。
- `EquityCurveListModel` 与 `BacktestTradeListModel` 新增 QML 稳定 `count/get()` 契约和计数信号；策略页已接入可编辑参数区、配置错误提示、单 Canvas 权益曲线及真实成交明细，不为每个曲线点创建 QML Item。
- Qt Test 新增结果模型角色、边界行与 `countChanged` 覆盖。2026-08-08 串行 Debug 构建、CTest 10/10 和 `all_qmllint` 均通过；程序在副屏 `DISPLAY1`（窗口 `Left=-2500, Top=40`）完成真实缓存回测和截图验收。
- 新增独立 `BacktestStrategy` 与 `BacktestStrategyRegistry`，`BacktestEngine` 不再固定构造 SMA；策略目录、参数 Schema、默认值、数值范围、交叉参数校验、预热根数和工厂创建由统一注册表提供。
- 首批四项内置回测策略已实现：SMA 双均线、RSI 均值回归、MACD 趋势和布林带均值回归。全部保持 A 股长仓语义，通过相同撮合、费率、滑点和整手配置进入现有回测引擎。
- `BacktestConfig` 新增 `taskId`、`strategyId` 和规范化参数快照；控制器暴露策略目录、当前选中策略、任务 ID、完成任务 ID、结果策略和结果品种，并新增 `runStrategyBacktest()`/`runSelectedBacktest()`。
- 历史数据链路新增递增 requestId。Provider、`KlineDataFetcher`、`HistoricalDataService` 和控制器均携带代次；新请求会淘汰旧响应，旧品种 K 线不能覆盖当前预览或启动错误回测。
- 策略页接入四项回测目录、鼠标/键盘选择、参数编辑、任务 ID、权益曲线、成交明细和真实结果状态；正式视觉仍以准备好的新版 QML 设计稿为唯一真源，当前只建立可替换的功能组件和数据接线。
- 新增注册表、默认参数、参数边界、未知策略、预热不足、四策略确定性运行及历史请求乱序淘汰测试。既有交易/审批测试移除固定 2026-08-03 的脆弱时间依赖。
- `Main.qml` 重构为窗口、主题、顶层 `TabBar`、常驻 `StackLayout` 和公共状态栏；启动默认进入“主页面”，主页面、指标与策略、全市场时刻股价切换时不销毁页面实例。
- 新增 `DashboardPage`，只使用控制器真值展示 Provider/市场/股票池/快照、模拟账户、当前品种、最近回测和待审批策略；尚无来源的最近访问和最新价格显示 `--`。
- 新增 `StrategyResearchPage`，接入统一 A 股搜索、当前品种、历史 K 线加载、策略目录、可校验参数、任意品种回测启动及完整结果模型；页面仍保持可由新版设计稿整体替换的组件边界。
- `TradingController` 新增共享 `stockSearchText` 和 `openInstrumentResearch()` 导航契约。全局搜索结果或散点单击先选择真实证券，再发出 `instrumentResearchRequested`，由 `Main.qml` 统一跳转策略页，页面之间不直接引用。
- 顶层 Tab 支持鼠标、Tab/Shift+Tab、左右方向键和 Enter/Space，具有可访问页签语义和焦点态；1600、1279、1120 px 下标题、Tab 和右侧状态操作均不溢出。
- 真实 5076 点运行验证中，散点选择 `301515 港通医疗` 后自动进入策略页；关闭主板后有效点数为 3442，切换到其他页再返回仍保持筛选，证明全市场页状态常驻。
- 建立 5207 点性能与可观测性基线。`CrossSectionScatterItem` 新增可开关的模型准备、Scene Graph CPU 更新、采样次数、可见点数和顶点数指标；Debug 默认开启、Release 默认关闭，可由 `PPTRADER_PERF_METRICS` 覆盖，关闭后不启动计时器且旧代次采样不会回填。
- `MarketSnapshotListModel` 为点云提供只读快照快速路径，保留任意 `QAbstractItemModel` 的角色读取回退；Debug 下模型装载/排序/点缓存准备由原约 114 ms p95 降至 55.581 ms，Scene Graph 几何重建 p95 为 8.189 ms。
- 新增 `cross_section_performance` Qt Test，覆盖 5207 点摘要/连续过滤、线性/对数坐标、1000 次 hover 命中、板块/轴/尺寸连续变化、指标开关/旧采样淘汰、顶点数量和多 DPI 逻辑边界。
- 快照契约测试新增 3×5207 持续写库与异步查询压力场景：GUI 派发低于 1 ms、事件循环持续响应、三批写入完成，旧 requestId 不会覆盖最终请求。
- 完成 100%/125%/150% DPI 运行截图和 20 轮缩放/筛选/轴切换；未改变 Figma 布局，仅将底栏“渲染”明确为“SG CPU”，避免把 CPU 几何构建误报为 GPU 整帧耗时。详细口径见 `docs/performance-baseline.md`。
- 完成 Figma P1 视觉收口：1600×1000 下时刻轨道、播放/应用按钮恢复设计坐标，应用按钮宽度固定为 166 px；图表工具栏恢复五段设计宽度，底部五板块图例从画板 x=660 起均匀排列。
- 时刻卡新增 92/148 px 双行响应式布局；页面在 1360 px 以下切换单实例摘要抽屉，摘要内容可滚动且关闭后可恢复。已验证 1600、1599、1360、1359、1279 和 1120 px，不重叠、不越界。
- 通用按钮新增独立外焦点环、Enter/Space 激活和可读禁用原因；板块筛选支持 Tab、Space/Return 及可访问 checked 状态。散点图提供可聚焦入口和可访问说明，但不伪造尚未实现的逐点键盘选择。
- 自动刷新只表示每 5 秒查询已完成快照，成功色不代表 Provider 健康；低完整率会同时标记摘要、价格统计和 Top 3，底栏继续显示控制器给出的 Provider/完整率/陈旧状态。
- 修复 `CrossSectionScatterItem` 在窗口尺寸变化时未失效几何的问题；`geometryChange()` 现在只在尺寸变化时重建批量几何，保持现有五个 `QSGGeometryNode` 和 5000+ 点 Scene Graph 架构。
- 新增 `CrossSectionScatterItem`。GUI 线程从 `marketSnapshotModel` 一次构建紧凑点缓存，渲染同步阶段不访问 `QAbstractItemModel`；每个板块只创建一个 `QSGGeometryNode`，所有股票共享批量几何和材质，不创建逐股票 QML Item。
- 散点采用八边形三角批次模拟 2 px 圆点，使用 Qt 内置 `QSGFlatColorMaterial`，避免图形后端相关的点大小和自定义着色器兼容问题。模型重置、尺寸、板块掩码或对数/线性切换才重建几何；hover、选中和十字线不会重建点云。
- 将统计、Top 3、坐标变换和最近点命中下沉到可独立测试的 `CrossSectionPlotData`；过滤仍保留全市场 `displayIndex`，不会让横坐标跳动。北交所板块位和颜色保留，本阶段不新增北交数据源。
- `ScatterPlot.qml` 现在只保留静态网格 Canvas、提示框和鼠标交互，已删除 JavaScript 5000 行复制、排序和逐点 Canvas 绘制。
- 新增 `cross_section_plot_data` Qt Test，覆盖板块位、北交预留位、过滤统计、坐标、命中测试和 5207 点容量。
- 新增 `MarketSnapshotListModel`，以数值角色向新版 QML 暴露 5000+ 标的的代码、名称、板块、价格、昨收、涨跌幅、成交量/额、快照时间和逐行健康状态；默认按代码稳定排序，并提供 `get()`、`indexOfSymbol()`。
- 新增全市场时刻查询服务：无时区输入统一按 `Asia/Shanghai` 解释，查询严格选择请求时刻之前最近一个已完成批次，并分别返回用户请求时刻和实际命中时刻。
- 固定最大允许陈旧度为 10 秒；控制器结构化暴露批次号、预期/收到数量、完整率、批次延迟、陈旧毫秒数、健康枚举和错误，不再依赖瞬时全局状态文本。
- SQLite 新增向后兼容的 `market_snapshots` 元数据表。异步写入先保存 Tick 明细，再登记完成批次；崩溃留下的无元数据 Tick 不会被历史查询误认为完整快照。
- `TradingController` 新增 `marketSnapshotModel`、`requestMarketSnapshot()`、`requestMarketSnapshotAt()` 和 `requestLatestMarketSnapshot()`；新版 QML 只通过这些控制器契约读取业务数据。
- 整体替换旧 `qml/Main.qml`，按 Figma 交接文档实现顶部栏、页面标题、时刻控制、主图卡、市场摘要和底部状态栏，并在小于 1280 px 时将摘要切换为覆盖式抽屉。
- 新增单 Canvas 散点组件：模型重置时一次缓存 5000+ 行，绘制按 16 ms 节流；板块过滤保持全市场 X 序号稳定，图表、统计、Top 3 和选中详情共享同一缓存及过滤版本。
- 接入 Empty、Loading、Incomplete、Stale、Error 状态；实际时刻、完整率、批次耗时、陈旧度和错误均来自控制器。CPU/MEM、导出、全屏、播放和双击多窗口没有伪造，统一显示 `--` 或明确禁用。
- 新增独立 `AShareUniverseLoader`。实时 Provider 启动时读取全量缓存，不再使用两只观察列表；观察列表仍只负责选择、策略和交易，避免为 5000+ 股票创建策略实例。缓存加载会去重、清除名称 NUL、校验代码与市场标签并规范为上交所/深交所/北交所，同时暴露源数量、接受数量、拒绝数量、缓存路径和错误。
- `MarketDataProvider` 新增结构化 Stopped/Starting/Healthy/Degraded/Error 状态，以及 expected、received、batchId、更新时间和错误。桥接器的正常 status 不再冒充错误；协议 begin/end 数量会与 C++ 实际组装数量交叉校验，缺失或分块错误保持降级状态。
- 全市场批次继续走既有 `snapshot_begin → quote → snapshot_end` 协议、`MarketSnapshotAssembler`、横截面引擎和 `AsyncTickWriter`。Writer 不可用时改为单事务同步保存完整快照及元数据；队列丢批和写入失败会降低 Provider 健康度。批次落库完成后自动异步查询最新已完成快照。
- 新增专用 `AsyncMarketSnapshotQueryService`：工作线程创建独立 SQLite 连接，QML 查询不再阻塞 GUI。每次请求递增 `snapshotRequestId`，只有当前代次可以更新模型、健康和 loading；无效输入同样淘汰旧请求，并暴露 `snapshotLoading`、当前和已解析请求 ID。
- QML 仅把原本页面内的同步 busy 改绑 `TradingController.snapshotLoading`，状态栏改用真实 Provider 健康；视觉结构和 Figma 尺寸保持不变。
- 新增 `market_snapshot_contract` Qt Test，覆盖上海时区解析、完成时间边界、向前最近批次、10 秒陈旧度、空/不完整快照、板块回退、数值角色和 5001 行容量；原异步 SQLite 测试补充批次恢复断言。
- 稳定 `StrategyIntentListModel` 的 QML 契约：恢复数据按创建时间倒序排列、按 `intentId` 去重，更新时发出完整角色集合，并补充 `count`、`pendingCount`、`get()`、`indexOfIntent()` 和整数枚举角色。
- `TradingController` 启动恢复后以协调器快照重建意图模型，保证模型与持久化状态一致；空 ID、未知 ID 和非待审批状态均安全返回 `false`。
- 策略配置查询现在返回完整订单策略字段，并新增稳定排序的批量查询与订单类型、卖出方式、限价偏移、整手数量更新接口。
- 配置保存改为“SQLite 写入成功后再更新内存”，写入失败会返回 `false`，避免界面显示已保存但重启后丢失。
- 扩展策略执行 Qt Test，覆盖角色一致性、非法行、去重恢复、稳定排序、计数信号和配置持久化失败；SQLite schema 保持兼容且本阶段未新增表或列。
- mootdx 实时桥新增 `snapshot_begin → quote → snapshot_end` 协议，5000 个代码通过 UTF-8 临时文件传递；真实压测后将默认分块从 80 调整为 40，兼顾完整率和延迟。
- 仓库内 `.venv` 已安装 `mootdx 0.11.7` 和 `pandas 2.3.3`，VS Code 终端及调试配置的 `PPTRADER_PYTHON` 已指向该隔离环境。
- 实时桥会验证服务器确实能返回行情，优先使用已验证端点并在失败时执行 mootdx 服务器发现，避免“连接成功但返回空数据”的失效节点。
- 压测工具支持固定服务器、连续批次、轮询间隔、超时和逐批统计；股票缓存已从 TDX 刷新为 5317 条并修正沪深市场标签。
- `MarketDataProvider` 现在可以同时输出逐笔 Tick 和完整 `MarketSnapshot`；异常退出或缺少结束事件时会封口已有的部分快照。
- 控制器只对完整率不低于 90%、组装耗时不超过 10 秒的 A 股快照执行横截面指标；批次 Tick 使用一次模型重置，避免逐行刷新 UI 模型。
- 新增独立后台 Tick 写入线程、有界 3 批队列、SQLite WAL/5 秒忙等待和单批事务；`batch_id + symbol` 唯一约束保证重放幂等。
- Tick 表新增批次号、昨收、开高低和成交额字段，并为旧数据库提供增量迁移。
- 新增 `MarketSnapshot`、`CrossSectionalQuote`、`FactorFrame` 等与 UI 无关的横截面领域模型。
- 新增全市场快照组装器，可按股票池过滤、批次去重、统计完整率并稳定排序。
- 新增仅支持 A 股的横截面指标接口和相对强弱示例，输出原始收益、Z-Score、排名和分位数。
- 将实时策略从“每个 Tick 广播给所有策略”改为按 symbol 建索引路由，避免全市场场景下的平方级任务增长。
- mootdx Tick 开始接收昨收、开高低、成交额字段，为横截面因子提供基础输入。
- 扩展 `OrderRequest` 的有效期类型和过期时间，并向订单模型暴露 `timeInForce`、`expiresAt`。
- 模拟券商按 Tick 成交量限制撮合，可跨多个 Tick 部分成交；实现 IOC、FOK 和过期撤单。
- 策略执行配置支持订单超时、每日下单次数和每日累计下单金额预算。
- SQLite 保存并恢复订单有效期、策略预算及策略意图中的完整订单参数。
- 增加订单生命周期、策略预算和持久化回归测试。

本阶段还完成了应用层接线：`StrategyEngine` 的信号事件现在携带策略 ID；`TradingController` 在 SQLite 初始化后恢复策略配置和未完成意图，再创建实时策略；信号通过 `StrategyExecutionCoordinator` 进入统一的审批、预算、风控和订单提交链路。默认策略执行模式仍为 Disabled，必须由后续配置显式开启。

控制器已提供 `strategyIntentModel`、`pendingStrategyIntentCount`、`approveStrategyIntent` 和 `rejectStrategyIntent`，新版 QML 可以直接构建审批列表、详情和操作工作流。

策略配置接口包括 `configureStrategyExecution`、`configureStrategyOrderPolicy`、`strategyExecutionConfiguration` 和 `strategyExecutionConfigurations`，覆盖执行模式、订单类型、TIF、固定金额、最大仓位、限价偏移、整手、审批/订单超时及每日预算，并对枚举、负数和非有限数值做边界校验。

## 验证状态

2026-08-09 回测容量基线完成。`pptrader_backtest_capacity` Release 探针分别运行 100、5207 和 50,000 个任务，并比较活动上限 1/20/64；全部零失败，原始 JSON 与汇总报告位于 `artifacts/backtest-capacity/`。最终串行 Debug 构建通过，CTest 17/17、`all_qmllint` 通过，敏感信息扫描无命中。探针不加入默认 CTest，也不修改 QML，因此本阶段无需新增视觉截图。

2026-08-09 历史 Tick 严格回测与 NAS 守护基线完成。最终构建使用按 source 限定的 SQL 再次执行 600519、1 分钟、200 根 SMA 回测，结果与前次一致，证明没有混入远端 K。串行 Debug 构建通过，CTest 17/17、`all_qmllint` 通过；新增测试覆盖同品种同周期多来源 catalog 拆分和只读取 `raw_tick_cache_v1`。副屏证据为 `artifacts/tick-backtest-route/strategy-tick-backtest-result-1600x1000-secondary.png`、`strategy-tick-catalog-1600x1000-secondary.png` 和 `strategy-tick-catalog-1120x760-secondary.png`；运行日志为空，敏感信息扫描无命中。

2026-08-09 阶段 9B 使用真实远端单文件完成 SSH 受控传输、中文格式解析、SQLite 事务入库、60/300 秒缓存生成和副屏显示。临时文件已删除，运行库保留 4064 条原始事实；全量 CTest 14/14、`all_qmllint` 通过。未执行远端目录全量扫描或批量导入。

2026-08-09 阶段 9A 串行 Debug 全量构建通过，CTest 14/14 通过，`all_qmllint` 通过。任意周期与缓存失效测试使用临时 SQLite；流式事务测试使用临时 CSV，未访问网络或用户数据。副屏截图为 `artifacts/raw-tick-kline-9a/dashboard-1600x1000-secondary.png` 与 `dashboard-1120x760-secondary.png`。

2026-08-08 阶段 8C 串行 Debug 构建通过，CTest 13/13 通过，`tickdata_csv_import` 内部 15 项通过，`all_qmllint` 零警告通过。测试仅使用临时 CSV/SQLite，不访问网络或真实 TickData。副屏使用自动行情关闭状态检查 1600×1000 与 1120×760；未确认目录、未生成计划、未读取文件。截图为 `artifacts/tickdata-import-8c/batch-plan-1600x1000-secondary.png` 和 `batch-plan-1120x760-secondary.png`。

2026-08-08 阶段 8B 最终串行 Debug 构建通过，CTest 13/13 通过，`all_qmllint` 零警告通过。批次计划、继续/取消、质量审计和运行摘要持久化均由临时文件与临时 SQLite 验证；测试未访问网络或开发机数据。副屏截图外框分别为 1616×1039 和 1136×799，对应客户区 1600×1000 与 1120×760，窗口均为 `Left=-2500, Top=40`。

2026-08-08 阶段 8A 最终串行 Debug 构建通过，CTest 13/13 通过，`all_qmllint` 零警告通过。新增测试目标用临时目录和临时 SQLite 验证流式解析、聚合、诊断、幂等及 catalog，全程未访问网络或开发机 TickData 文件。副屏截图外框分别为 1616×1039 和 1136×799，对应客户区 1600×1000 与 1120×760，窗口均为 `Left=-2500, Top=40`。

2026-08-08 主页双品种契约阶段 5A 串行 Debug 构建、CTest 12/12 和 `all_qmllint` 均通过。测试数据库均使用 `QTemporaryDir`，未读取开发机 `pptrader.db`。本阶段没有 QML/Figma 视觉改动，因此按约定不做截图验收。

2026-08-08 自定义策略异步回测接入完成：身份一致性加固后的串行 Debug 构建、CTest 11/11 和 `all_qmllint` 均通过。测试验证外部策略仓位、版本/参数/规则身份、数据顺序无关摘要、输入变化检测和错误快照拒绝。最终二进制在副屏 `DISPLAY1` 以 `Left=-2500, Top=40`、1600×1000 启动，SQLite 恢复和主工作台无视觉回归；截图为 `artifacts/custom-strategy/async-backtest-secondary-startup.png`。

2026-08-08 自定义策略 DSL 第三阶段串行 Debug 构建通过，CTest 11/11 和 `all_qmllint` 通过；规则层测试覆盖仓位比例、规则边界、止损/止盈、最大持仓和 T+1 锁存。最终二进制在副屏 `DISPLAY1` 以 `Left=-2500, Top=40`、1600×1000 启动，应用窗口截图成功且主工作台无视觉回归；截图为 `artifacts/custom-strategy/rules-secondary-startup.png`。

2026-08-08 自定义策略 DSL 第二阶段串行 Debug 构建通过，CTest 11/11 通过，`all_qmllint` 通过。新增测试覆盖七类指标、预热、缺失值、三值逻辑、MACD 快慢周期、相邻交叉和重置确定性。最终二进制在副屏 `DISPLAY1` 以 `Left=-2500, Top=40` 启动并切换策略页，无 QML 或现有数据回归；截图为 `artifacts/custom-strategy/compiler-secondary-startup.png`。

2026-08-08 自定义策略 DSL 第一阶段串行 Debug 构建通过，CTest 11/11 通过，`all_qmllint` 通过。`custom_strategy` 测试验证 Schema 白名单、安全拒绝、不可变版本、SQLite 重启恢复和 QML 模型角色。最终程序在副屏 `DISPLAY1` 以 `Left=-2500, Top=40` 启动并切换策略页，真实运行库迁移后原页面和 5076 点快照状态无回归；截图为 `artifacts/custom-strategy/secondary-startup.png`。

2026-08-08 本阶段最终串行 Debug 构建通过，CTest 10/10 通过，`all_qmllint` 通过。PPTrader 在副屏 `DISPLAY1`（2560×1440，桌面坐标 X=-2560）以 1600×1000 启动，自动化验收确认窗口坐标 `Left=-2500, Top=40` 后才切换页面和操作。使用本地 600519 缓存运行 SMA 默认参数任务 1，完成 17 笔交易，并成功显示深色参数编辑区、单 Canvas 权益曲线和同任务成交列表；截图为 `artifacts/strategy-research/final-secondary-details.png`。收益结果只用于链路验证，不构成投资或策略评价。

2026-08-08 串行 Debug 构建通过，CTest 10/10 通过，`all_qmllint` 通过。使用本地真实 600519 历史数据选择 MACD 默认参数完成任务 1，页面显示 21 笔成交及收益/回撤摘要；该结果仅验证数据、策略、任务和 QML 链路，不构成策略评价。截图位于 `artifacts/strategy-research/`。

2026-08-02 三页主工作台在 1600×1000、1279×1000、1120×1000 下完成截图检查，真实历史快照显示 5076/5207 点。方向键逐页切换、搜索 Enter 跳转、散点选股跳转和板块筛选跨页恢复均通过；证据位于 `artifacts/workspace-tabs/`。最终串行 Debug 构建通过，CTest 10/10 通过，`all_qmllint` 通过。

2026-08-02 性能基准在 Qt 6.4.3/MSVC Debug 下完成：摘要/过滤 p95 3.449 ms、线性/对数全量坐标 p95 0.547 ms、每批 1000 次 hover p95 7.566 ms、模型装载及点缓存 p95 55.581 ms、Scene Graph CPU 几何重建 p95 8.189 ms。DPI 和连续交互证据位于 `artifacts/performance-dpi/`；150% 受显示器工作区高度限制，未取得完整 1600×1000 逻辑画板，限制已记录。

最终串行验证：`cmake --build --preset vs2022-debug --parallel 1` 通过，CTest 10/10 通过，`all_qmllint` 通过。最终真实 5076 点截图为 `artifacts/performance-dpi/final-100.png`，底栏明确显示 `SG CPU 1.9 ms`，该单次运行值不作为 GPU 或长期 p95 指标。

2026-08-02 P1 收尾使用真实历史批次启动：页面显示 5076/5207 点、完整率 97.48%、批次耗时 1879 ms，Provider 为 Stopped、快照为 Stale，界面未将自动刷新误报为行情在线。截图与键盘验证保存在 `artifacts/p1-audit/`：包含 1600×1000、1599、1360、1359、1279、1120 px，摘要抽屉、禁用原因、焦点顺序、板块 Space 切换和分时滑块 5 分钟步进。散点尺寸变化后均重新落在 Plot 内。

2026-08-02 使用 VS2022 Debug 构建成功，`ctest --preset vs2022-debug --output-on-failure` 的 9 个测试目标全部通过，`all_qmllint` 零警告通过。实际窗口恢复 5076 点历史快照后，Scene Graph 几何重建单次观测约 1.8 ms，点云、板块颜色和摘要一致；截图保存在 `artifacts/scenegraph-ui.png`。该数值是本机单次几何构建耗时，不等同于长期 p95 帧时间。

2026-08-01 的快照契约测试新增股票池映射与规模、Provider 失败状态、请求代次乱序淘汰、GUI 事件循环响应和独立 SQLite 线程回读覆盖。

同日完成真实工作台端到端单批验收：旧缓存源 5317 条，经市场/代码一致性校验接受 5207 条沪深股票、拒绝 110 条指数/债券候选；固定服务器 `202.108.253.139:80` 收到 5076/5207 条，完整率 97.48%，采集耗时 1879 ms。`market_snapshots.received_count` 与对应 `ticks` 行数均为 5076，异步回读后 QML 显示 5076 个散点。截图保存在 `artifacts/market-snapshot-live.png`。独立 5000 只桥接探测收到 4876 条，完整率 97.52%，采集耗时 1786 ms，无超时、解析错误或桥接错误。

刷新脚本已支持 TDX 市场代码 2 和北交代码映射，但本次服务器返回的北交股票列表为空，因此当前正式缓存仍没有北交所记录；界面和 Scene Graph 数据结构保留北交筛选且显示 0，不伪造数据。原 Canvas 的真实 5076 点首次绘制约 133 ms，现已由批量 Scene Graph 点层替换。

同日已完成真实 TDX 网络端到端压测。服务器 `202.108.253.139:80`、5000 只股票、40 只/请求时，单批收到 4772 条，完整率 95.44%，服务端采集约 1.80 秒、进程墙钟约 2.37 秒。按 3 秒间隔连续运行 5 批，每批均为 4772 条，采集耗时 1787–1795 毫秒，无超时、解析错误或完整率衰减。100 只/请求时完整率降至 75.22%，因此不采用。该结果是单机单服务器样本，不代表所有网络和交易时段。

工程已为 MSVC 启用 `/FS`，避免多编译进程争用 `vc143.pdb`；资源紧张时仍可使用：

```powershell
cmake --build --preset vs2022-debug --parallel 1
```

新版 QML 已清理旧界面的重复 ID，并通过 QML lint。构建后的 `'pwsh.exe' 不是内部或外部命令` 来自开发机全局 vcpkg MSBuild AppLocal：它先尝试 PowerShell 7，找不到后自动回退到系统 Windows PowerShell 并成功复制依赖；不是 PPTrader 脚本，也不影响构建结果。

## 未实现与后续优先级

2026-08-14 完成 H2 内容身份加固：SQLite 全库升级为 v6，新增逐日规范化 Tick SHA-256 缓存与事实 INSERT/UPDATE/DELETE 脏标记触发器，派生 K 线缓存保存对应内容摘要。v2 身份可识别行数和最大 ID 不变的原位字段修改，相同事实重导保持版本稳定；旧 v1 归档 JSON 继续按 `count/max-id` 解析。首次读取旧库未计算日期需要流式扫描一次，后续复用摘要缓存。

1. **继续修可信度边界**：H2 内容身份、向导预览限源和 H3C 观测日并集估值已完成；下一步补齐 NAS 并发乱序/取消迟到响应测试。组合估值仍不得越过“所有腿同时缺失时无日期事实”的边界。
2. **完成真实稳定性验收**：TDX/腾讯至少四小时长稳、失败分块重试、缺口补拉、限流和 p50/p95；NAS Range/Token/changes/zlib 在真实节点上的故障、轮换、冷缓存吞吐与重启恢复。需要 SLA 时再评估授权 QMT/专业源。
3. **继续自定义策略产品闭环**：完成向导其余步骤、固定金额/30% 上限风控、样本窗与逐 Bar 回放、最多 10 条验证记录、显式模拟执行审批和通知。自由代码、LLM 生成即执行和真实下单不在当前范围。
4. **补齐数据工程**：500/5207 真实写库仍延期；后续包括全市场批量按需同步、公司行动/复权一致性、跨年度权威交易日历、坏行质量门槛、原始逐笔回放，以及按实测决定 Parquet/Zstd/pyarrow。实时分钟不得补写历史 Tick 事实。
5. **完善组合与研究**：权威日历覆盖全腿同时缺失日、策略信号驱动调仓、组合级风险/熔断、组合对比与横截面因子研究；当前固定权重组合不等于完整组合管理系统。
6. **持久化生命周期与交付**：回测归档删除、保留期、版本引用保护、同输入确定性对照、多结果比较、导出、备份清理与存储设置页仍未实现。
7. **后续产品路线**：真实券商/对账与账户交易页面、多资产（美股/外汇/加密货币）、北交所完整源、LLM 辅助、全屏/多窗口均未完成，必须按独立阶段验收。

## 维护规则

每个阶段结束时更新“本次完成”“验证状态”和“未实现”；功能状态只以已构建、已测试的代码为准。架构、流程或表结构发生变化时，同时更新 `docs/project-wiki.md`。

2026-08-14 H3C.4 完成后实际串行 Debug 构建成功，CTest 22/22 通过（40.14 秒），`all_qmllint` 目标无诊断。此前已移除 `CustomStrategyWizard.qml` 未使用导入并完成向导预览严格限源。本轮未启动应用、联网或访问 NAS；构建环境的 `pwsh.exe` 提示来自全局 vcpkg AppLocal，系统 Windows PowerShell 回退成功。
