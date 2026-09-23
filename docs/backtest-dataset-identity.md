# 回测数据集身份契约（H2）

最后更新：2026-08-30

## 目标与不变量

历史回测的输入是一个“可回测数据集”：某 A 股代码、某周期、来自 `raw_tick_cache_v1` 的一组派生 K 线。H2 为该数据集建立稳定的内容身份，使结果归档自含数据来源，不依赖当前数据库解释历史结果。schema v6 起，身份覆盖规范化逐笔事实内容；旧 v1 身份仍可恢复，但继续保持原来的 `count/max(id)` 语义。

不变量：

- 身份只由**事实**推导：原始日期集合、每交易日 Tick 指纹、周期、聚合算法版本。派生缓存是否新鲜不参与身份哈希。
- 身份由 SQLite 事实表组装；逐日内容摘要使用带脏标记的缓存表，事实 INSERT/UPDATE/DELETE 触发器保证失效，避免每次回测重扫全部 Tick。
- 回测运行前必须完成数据准备：过期/缺失的派生缓存按需重建，重建后仍不一致则拒绝运行。
- 结果归档保存完整身份 JSON；归档恢复后身份与当前库状态无关。
- 全过程只访问本地 SQLite，不触发 Provider/NAS；失败明确报告原因。

## 身份组成

`BacktestDatasetIdentity`（`src/domain/BacktestDatasetTypes.h`）字段：

| 字段 | 含义 |
| --- | --- |
| `instrument` / `periodSeconds` / `source` | 数据集定位；source 固定 `raw_tick_cache_v1` |
| `aggregatorVersion` | `RawTickCandleAggregator` 算法版本，当前 `1`；算法升级时 bump 并纳入哈希 |
| `fingerprintVersion` | 当前 `2`；旧归档无此字段时按 v1 读取 |
| `days` | `RawTickDayFingerprint` 列表：日期、数量、诊断用最大 ID、`rawContentSha256` 与派生缓存状态 |
| `datasetVersion` | `sha256:` 身份哈希 |
| `firstTradingDate` / `lastTradingDate` | 事实日期范围 |
| `expectedTradingDays` / `gapDayCount` | 日历期望交易日与缺口（`AShareTradingCalendar`） |
| `staleDayCount` | 事实已变但派生缓存未刷新/缺失的日数 |
| `calendarCoverageKnown` / `calendarSource` / `calendarNote` | 2024–2026 分别使用 `SSE-2023-47` / `SSE-2024-38` / `SSE-2025-45`；覆盖外年份使用 `weekday-fallback` 并明确标注 |

## 哈希规则

v2 先按 `timestamp UTC 毫秒、sourceLine、price、volumeShares、amount、direction、source` 规范化并排序，为每个交易日流式计算 SHA-256；SQLite 物理 ID、名称、导入时间不参与摘要。数据集版本由 `symbol + periodSeconds + source + aggregatorVersion + date/count/contentSha256` 计算。相同事实重导即使物理 ID 改变，版本也保持稳定；字段原位修改会改变版本并使派生缓存过期。旧归档缺少内容摘要时，继续使用 v1 的 `date/count/maximumId` 算法。

## 存储与组装

`SqliteLocalDataStore::loadBacktestDatasetIdentity(symbol, periodSeconds)` 与全目录查询共用以下事实和摘要：

1. 完整导入在原事务提交前对 `raw_trade_ticks` 流式计算 `count/max(id)/SHA-256`，摘要失败则回滚整次导入；
2. `raw_tick_content_fingerprints` 保存该逐日强身份，三个触发器在事实变化时标 dirty；单数据集查询直接读干净行，只对 dirty 行或整库无指纹的旧数据回退扫描事实；
3. `raw_tick_candle_cache_state` 保存生成缓存时的数量、内容摘要、根数和版本，并与同一次身份查询直接比较。

有事实但派生缓存行缺失、数量或内容摘要不一致的日计入 `staleDayCount`。新的完整导入在提交前已预热摘要；只有 dirty 事实和旧库首次读取需要流式扫描。内容规范化顺序有专用索引，回退重算不建临时排序树。

全量本地目录刷新只复用已存在的干净 v2 摘要，不为旧项目或 dirty 日期计算内容 SHA，也不再对事实执行全量 `GROUP BY`。只有 dirty/缺失指纹的目录日期会按 symbol/date 回退查询事实 `count/max(id)`，并保守标记缓存 stale；用户启动某个回测时，后台 `BacktestDatasetService` 才对目标数据集强制补算、校验 v2 内容身份并补建目标周期缓存。2026-08-27 的 100 标的同库复测中，目录首次冷读由 3558 ms 降至 466 ms，后续热读为 3–4 ms；详见 `docs/nas-performance-h5d-2026-08-27.md`。

## 数据库 v3 与 v6

`backtest_results` 增加 `dataset_version`、`dataset_identity_json`、`dataset_raw_tick_count`、`dataset_derived_bar_count`、`dataset_day_count`、`dataset_first_date`、`dataset_last_date`、`dataset_gap_day_count`、`dataset_stale_day_count`、`dataset_calendar_source`、`dataset_calendar_coverage_known`。迁移沿用备份→单事务→迁移历史→完整性校验流程；空身份按空字符串/0 保存，身份非空时 `dataset_version` 必须是 `sha256:`。

v6 新增 `raw_tick_content_fingerprints`、事实变更触发器，以及 `raw_tick_candle_cache_state.raw_content_sha256`。旧库自动备份并事务升级；旧归档 JSON 不回写、不重算，仍按 v1 恢复。2026-08-27 的 H5C 预热/有序索引与 H5D 目录快路当时均为向后兼容的运行时优化，没有提升 schema；2026-08-28 的批量原始 Tick 结果快照将 schema 升为 v19，样本末未平仓快照再升为 v20。v20 只保存结果事实，不改变数据集版本算法；旧结果缺少仓位列时按空仓恢复，不从当前 Tick 重算。

## 运行前准备（BacktestDatasetService）

`BacktestDatasetService` 位于独立工作线程并持有独立 SQLite 连接（可写，用于重建缓存），requestId 回传：

1. 加载身份；身份为空 → 拒绝"无原始 Tick 事实"。
2. 对 `staleDayCount` 中每个缓存缺失/过期日，用 `forEachRawTradeTick → RawTickCandleAggregator → replaceRawTickCandleCache` 重建；失败即拒绝。
3. 重建后重新加载身份，`staleDayCount` 仍 > 0 → 拒绝。
4. 填充日历覆盖，读取 `raw_tick_cache_v1` 最近 N 根；根数不足 → 拒绝"未访问网络 Provider"。

`TradingController` 的内置、自定义、本地自定义三个回测入口统一先异步准备数据集，成功后才启动 `BacktestEngine`；`m_backtestPreparing` 覆盖准备阶段，取消/错误会清理请求号。

## 边界

- 身份查询与准备不自动刷新未使用周期，也不自动同步 NAS。
- H3B.12 起，停牌日只有在 `ashare_instrument_day_status` 存在带来源/版本的明确 `suspended` 事实时才解释为无成交；未知状态仍是缺口，停牌事实与成交并存则拒绝。
- H3B.12 复用 TickData 导入批次明细：单日坏行率大于 1% 或审计状态为 `error` 时拒绝，少量坏行与旧数据缺审计只警告，不新增 schema 字段。
- H3B.14 起，权威日历已知的休市日若存在原始成交，通过 `unexpectedNonTradingDates` 结构化拒绝；不自动删除事实，需核对文件真实日期与来源。
- H3B.15 起，质量评估为拒绝时，批量服务直接使用身份中的事实计数返回，不物化 Tick/K 线、不创建引擎；拒绝原因和数据集版本仍来自同一不可变身份与内容绑定审计。
- H3B.16 起，质量映射包含停牌冲突、坏行超限、审计 error 和审计计数不一致的逐日数组；审计有效记录数与身份事实数不一致直接拒绝。
- H3B.17 起，普通批量 raw 回测把执行流与归档快照分离：按 `identity.days` 逐日读取并增量生成与全量算法一致的 `dataVersion`，完成结果以 `rawTickSnapshotDeferred` 表示只保留身份。显式归档必须按逐日内容摘要重新物化，并复核整段 `dataVersion`、条数和首末时间；任何变化都拒绝。固定留出、多折和参数敏感性仍保留现有多遍输入路径。
- H3B.18 起，显式单项归档由独立 SQLite 工作线程执行；Controller 只提交共享只读结果句柄，并以 requestId 门控完成状态。后台仍执行 H3B.17 的全部身份复验，失败不会产生部分归档；应用退出或数据库迁移会等待事务完成。
- audit v2 把标准英文表头当作元数据，不计入坏行；旧 audit v1 保留为历史，同一内容摘要优先读取较高审计版本。数字成交方向未经合同授权时仍记为坏行，不猜测映射。
- 公司行动覆盖未知只产生警告；已知事件落入未复权原始 Tick 范围时拒绝。当前尚无真实公司行动导入源，不能把该契约写成生产验收完成。
- 批量回测（H3A）目前仍使用只读连接，不自动重建缓存；stale 数据集的批量处理策略在 H3B 阶段定义。
