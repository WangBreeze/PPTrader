# 本地多标的批量回测契约

## 目标

H3A 提供同一内置策略对多个本地 A 股数据集的独立回测。`TradingController.runLocalBatchBacktest()` 只从当前 `localBacktestDataCatalog` 选择指定周期的数据集；后台服务再以 SQL 条件强制读取 `source=raw_tick_cache_v1`。运行路径不访问历史 Provider 或 NAS。

## 调度与资源边界

`BatchBacktestService` 拥有独立工作线程和 SQLite 只读连接。每次最多保留 20 个活动 `BacktestEngine`，单项完成后立即释放权益曲线与成交明细，只向 `BatchBacktestResultListModel` 发送轻量摘要。数据不足、SQL 读取或策略执行失败只形成该标的失败行，不中止其他标的。

批量任务与单品种回测互斥；数据库搬迁在批量任务运行时拒绝。取消标志使用跨线程原子状态，避免零间隔回测定时器饿死取消事件。所有 item、progress 和 finished 事件都携带 requestId，控制器只接受当前代次。

## QML 契约

控制器已暴露 `batchBacktestModel`、running、requestId、requested/completed/succeeded/failed、progress、statusText、error，以及启动/取消接口。模型按证券代码稳定排序，包含真实数据版本、实际根数、收益、回撤、胜率、Sharpe、手续费、交易数与单项耗时。

本阶段没有新增 QML 页面。后续界面只能通过这些控制器属性访问，不能直接查询 SQLite。

## 明确不包含

- 不共享一笔组合资金，不执行横截面调仓或组合风控；每行都是独立初始资金任务。
- 不批量归档权益曲线和成交明细，也不支持中断后的任务恢复。
- 不自动同步缺失 Tick，不以远端日 K 或实时分钟行情回退。
- 当前真实本地 catalog 只有一个可回测样本；5000+ 真标的数据装载与端到端耗时仍需 H1/H3B 验证。

