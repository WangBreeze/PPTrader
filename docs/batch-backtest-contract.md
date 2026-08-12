# 本地多标的批量回测契约

## 目标

H3A 提供同一内置策略对多个本地 A 股数据集的独立回测。`TradingController.runLocalBatchBacktest()` 只从当前 `localBacktestDataCatalog` 选择指定周期的数据集；后台服务再以 SQL 条件强制读取 `source=raw_tick_cache_v1`。运行路径不访问历史 Provider 或 NAS。

## 调度与资源边界

`BatchBacktestService` 拥有独立工作线程和 SQLite 只读连接。每次最多保留 20 个活动 `BacktestEngine`，单项完成后立即释放权益曲线与成交明细，只向 `BatchBacktestResultListModel` 发送轻量摘要。数据不足、SQL 读取或策略执行失败只形成该标的失败行，不中止其他标的。

批量任务与单品种回测互斥；数据库搬迁在批量任务运行时拒绝。取消标志使用跨线程原子状态，避免零间隔回测定时器饿死取消事件。所有 item、progress 和 finished 事件都携带 requestId，控制器只接受当前代次。

## QML 契约

控制器已暴露 `batchBacktestModel`、running、requestId、requested/completed/succeeded/failed、progress、statusText、error，以及启动/取消接口。模型按证券代码稳定排序，包含真实数据版本、数据集版本/日数/原始 Tick 数/缺口、归档状态（archived/archivedResultId）、实际根数、收益、回撤、胜率、Sharpe、手续费、交易数与单项耗时。

H3B.3 已在策略页新增 `BatchBacktestPanel`：配置策略/周期/根数/标的数上限，运行与取消，结果列表逐项"归档"（`archiveBatchBacktestItem`），运行历史（`batchBacktestRunHistory`）逐项"恢复失败项"（`resumeBatchBacktest`）。界面只能通过这些控制器属性访问，不能直接查询 SQLite。

## 运行持久化与恢复

每次批量启动创建不可变 `runId`，`batch_backtest_runs` 保存策略、规范化参数、周期、根数、资金/费率与计数；`batch_backtest_run_items` 保存逐项状态、错误与数据身份。明细只含代码、周期、策略和状态，不保存任何路径或凭据。应用启动把遗留 `running` 运行转为 `cancelled`（可重试）；`resumeBatchBacktest(runId)` 从当前本地 catalog 重新匹配失败/取消项并创建新运行，成功项不重跑。

## 单项按需归档

完成项完整结果（权益曲线、成交、数据集身份）通过 `itemCompleted` 回传并由控制器有界保留（32 项 LRU）。`archiveBatchBacktestItem(symbol)` 复用 7B 单事务归档写入并返回 resultId；结果被淘汰或身份缺失时明确拒绝，不重跑。

## 明确不包含

- 不共享一笔组合资金，不执行横截面调仓或组合风控；每行都是独立初始资金任务。
- 不自动批量归档权益曲线和成交明细（按需单项归档）；批量结果不共享组合资金。
- 不自动同步缺失 Tick，不以远端日 K 或实时分钟行情回退。
- 当前真实本地 catalog 只有一个可回测样本；5000+ 真标的数据装载与端到端耗时仍需 H1/H3B 验证。
