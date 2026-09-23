# PPTrader 可见应用级原始 Tick 回测四小时长稳验收

## 结论

本轮在限定范围内通过：同一 Release `PPTrader.exe` 通过正常 QML 窗口、`TradingController` 和 `BatchBacktestService` 连续运行 4 小时，应用无重启、无失败、无 watchdog、无 stderr，完整结果摘要哈希逐轮一致。SQLite 原始事实和普通回测归档未被修改；批量运行表仅产生与完成轮次严格对应的终态审计记录。

该结论只覆盖 10 个独立真实本地数据集、`tick_momentum`、固定日期窗口和可见 PPTrader，不代表 500/5207 个独立数据集、NAS I/O、归档高压、策略收益有效性或生产 SLA。

## 输入与运行边界

- 时间：2026-09-01 21:36:29 至 2026-09-02 01:36:31（北京时间），应用报告 14,401.867 秒。
- 数据：NAS 来源已显式同步形成的本地隔离 SQLite 副本；长稳期间未访问 NAS、网络或 Provider。
- 范围：2026-07-23～2026-08-28，10 个标的，各 27 个观测交易日，每轮 723,900 条原始 Tick。
- 策略：`tick_momentum`；`lookbackTicks=20`、`thresholdPercent=0.2`、`cooldownSeconds=30`、`minimumHoldSeconds=60`。
- 交易假设：初始资金 1,000,000，佣金 0.03%，滑点 0.02%，整手 100。
- 质量状态：10 项均允许执行，但公司行动覆盖未知，因此保持 `warning`，不能解释为完成复权一致性验收。
- 可见性：唯一 PPTrader 窗口位于副屏，“回测任务 → 批量回测”真实显示轮次和 10 项进度；截图为 `artifacts/backtest-app-longrun-2026-09-01/formal/visible-batch-backtest-start.jpg`。

## 结果与性能

| 指标 | 结果 |
| --- | ---: |
| 完成轮次 / 标的任务 | 4,797 / 47,970 |
| 累计 Tick 观察 | 3,472,548,300 |
| 成功 / 失败 | 47,970 / 0 |
| 每轮输入范围 | 723,900～723,900 |
| 轮次 p50 / p95 / max | 2.805 / 3.912 / 7.456 秒 |
| watchdog（120 秒） | 0 |
| 完整结果摘要确定性 | 通过，首末均为 `sha256:27767b…22ae` |

本轮没有持久化每个标的的独立耗时序列，因此不能给出真实的单项 p50/p95/max；将轮次耗时简单除以 10 会混淆排队、加载和执行，故不作伪造。应用证据也明确标记 `tPlusOneViolationsAvailable=false`，本轮不单独声称 T+1 违规为零；T+1 规则仍由既有引擎测试和此前后端验收约束。

进程监控从应用启动后约 99 秒开始，采集 476 个样本、覆盖 14,313 秒，最大间隔 31.469 秒且非提前退出。私有内存 p50/p95/max 为 263.254/268.383/269.422 MiB，线性斜率 +0.03763 MiB/分钟；工作集 p50/p95/max 为 232.020/234.320/236.117 MiB，斜率 -0.43394 MiB/分钟。句柄最大 959，线程最大 98。当前规模未出现失控增长，但正私有内存斜率仍应在更长或更大档复测。

## SQLite 结束核对

只读结束快照为 `artifacts/backtest-app-longrun-2026-09-01/formal/post-run-database-snapshot.json`：

- `PRAGMA integrity_check=ok`，`user_version=24`，退出后 WAL 为 0。
- `raw_trade_ticks`、`ticks`、`candles`、`market_snapshots` 的计数、ID/数值聚合及时间范围与运行前基线逐项一致。
- `backtest_results` 及 Candle、权益、原始 Tick、成交子表和 `portfolio_backtest_results` 均保持 0。
- `batch_backtest_runs=4797`，全部 `completed`；`batch_backtest_run_items=47970`，全部 `succeeded`；无 `ready/running`、无错误、无孤儿项。
- 数据库因批量运行审计记录增长 18.965 MiB；初始与结束 SHA-256 不同属于预期元数据写入，不代表行情事实或回测归档被改写。

## 自动验证

- 运行前同一源码完成 Debug 全量构建、CTest 46/46（79.30 秒）和 `all_qmllint`。
- 结束后运行回测定向 CTest 10/10（18.32 秒）：引擎、批量、质量门禁、数据身份、数据服务、归档 QML、横截面、组合、组合存储及长稳证据测试均通过。
- `scripts/capture_backtest_postrun_snapshot.py` 以 SQLite 只读连接生成可复查结束快照；任一基线、归档或终态检查失败都会返回非零状态。

## 后续

下一档先补齐应用证据中的逐标的耗时序列和 T+1 计数，再做用户显式归档后的写入/重启恢复长稳。100 个独立真实数据集可作为下一容量档；500/5207 档继续按既定决定延期。质量侧仍需授权公司行动/复权和停复牌事实，不能用本轮稳定性结论替代数据可信度治理。
