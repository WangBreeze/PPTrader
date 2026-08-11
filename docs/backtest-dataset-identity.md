# 回测数据集身份契约（H2）

最后更新：2026-08-12

## 目标与不变量

历史回测的输入是一个"可回测数据集"：某 A 股代码、某周期、来自 `raw_tick_cache_v1` 的一组派生 K 线。H2 为该数据集建立**不可变身份**，使结果归档自含数据来源，不依赖当前数据库解释历史结果。

不变量：

- 身份只由**事实**推导：原始日期集合、每交易日 Tick 指纹、周期、聚合算法版本。派生缓存是否新鲜不参与身份哈希。
- 身份由 SQLite 事实表实时组装，**不落身份缓存表**，避免与事实失去同步。
- 回测运行前必须完成数据准备：过期/缺失的派生缓存按需重建，重建后仍不一致则拒绝运行。
- 结果归档保存完整身份 JSON；归档恢复后身份与当前库状态无关。
- 全过程只访问本地 SQLite，不触发 Provider/NAS；失败明确报告原因。

## 身份组成

`BacktestDatasetIdentity`（`src/domain/BacktestDatasetTypes.h`）字段：

| 字段 | 含义 |
| --- | --- |
| `instrument` / `periodSeconds` / `source` | 数据集定位；source 固定 `raw_tick_cache_v1` |
| `aggregatorVersion` | `RawTickCandleAggregator` 算法版本，当前 `1`；算法升级时 bump 并纳入哈希 |
| `days` | `RawTickDayFingerprint` 列表：`tradingDate`、`rawCount`、`rawMaximumId`、`barCount`、`cacheVersion` |
| `datasetVersion` | `sha256:` 身份哈希 |
| `firstTradingDate` / `lastTradingDate` | 事实日期范围 |
| `expectedTradingDays` / `gapDayCount` | 日历期望交易日与缺口（`AShareTradingCalendar`） |
| `staleDayCount` | 事实已变但派生缓存未刷新/缺失的日数 |
| `calendarCoverageKnown` / `calendarSource` / `calendarNote` | 2026 权威 `SSE-2025-45`；其他年份 `weekday-fallback` 并明确标注 |

## 哈希规则

`computeBacktestDatasetVersion(identity)`：对 `symbol + periodSeconds + source + aggregatorVersion` 追加按交易日升序的每行 `date/count/maximumId`，SHA-256 后加 `sha256:` 前缀。`barCount/cacheVersion` 是派生结果，不进入事实身份；缓存过期表现为 `staleDayCount > 0` 而版本不变。

## 存储与组装

`SqliteLocalDataStore::loadBacktestDatasetIdentity(symbol, periodSeconds)` / `loadBacktestDatasetIdentities()` 用两条 GROUP BY 聚合 SQL 组装：

1. `raw_trade_ticks` 按 `symbol + trading_date` 的 `count/max(id)`；
2. `raw_tick_candle_cache_state` 按 `symbol + period_seconds + trading_date` 的 `raw_count/raw_max_id/bar_count/cache_version`。

只有缓存状态中实际存在的 `(symbol, period)` 才产生身份；有事实但缓存行缺失或指纹不一致的日计入 `staleDayCount`。

## 数据库 v3

`backtest_results` 增加 `dataset_version`、`dataset_identity_json`、`dataset_raw_tick_count`、`dataset_derived_bar_count`、`dataset_day_count`、`dataset_first_date`、`dataset_last_date`、`dataset_gap_day_count`、`dataset_stale_day_count`、`dataset_calendar_source`、`dataset_calendar_coverage_known`。迁移沿用备份→单事务→迁移历史→完整性校验流程；空身份按空字符串/0 保存，身份非空时 `dataset_version` 必须是 `sha256:`。

## 运行前准备（BacktestDatasetService）

`BacktestDatasetService` 位于独立工作线程并持有独立 SQLite 连接（可写，用于重建缓存），requestId 回传：

1. 加载身份；身份为空 → 拒绝"无原始 Tick 事实"。
2. 对 `staleDayCount` 中每个缓存缺失/过期日，用 `forEachRawTradeTick → RawTickCandleAggregator → replaceRawTickCandleCache` 重建；失败即拒绝。
3. 重建后重新加载身份，`staleDayCount` 仍 > 0 → 拒绝。
4. 填充日历覆盖，读取 `raw_tick_cache_v1` 最近 N 根；根数不足 → 拒绝"未访问网络 Provider"。

`TradingController` 的内置、自定义、本地自定义三个回测入口统一先异步准备数据集，成功后才启动 `BacktestEngine`；`m_backtestPreparing` 覆盖准备阶段，取消/错误会清理请求号。

## 边界

- 身份查询与准备不自动刷新未使用周期，也不自动同步 NAS。
- 停牌日会按交易日历计为缺口；区分停牌与真实缺数据需要未来接入停牌事实。
- 坏行质量门槛沿用 TickData 导入诊断（8A），不新增事实表字段。
- 批量回测（H3A）目前仍使用只读连接，不自动重建缓存；stale 数据集的批量处理策略在 H3B 阶段定义。
