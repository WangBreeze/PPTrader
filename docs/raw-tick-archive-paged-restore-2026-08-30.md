# H3B.24 原始 Tick 归档轻量恢复与分页读取

## 结论

历史原始 Tick 回测归档的默认恢复不再把完整 `backtest_raw_ticks` 装入 `BacktestResult::rawTicks`。`TradingController` 读取结果身份、摘要、有界权益曲线、成交和图表信号，并用 `rawTickSnapshotAvailable/rawTickSnapshotLoaded` 明确区分“完整输入已归档”和“当前对象已完整载入”。因此用户加载历史结果时可直接恢复可视结果，不会再仅为恢复完整输入事实而让 `rawTicks` 内存随 Tick 总数增长；成交和信号明细仍沿用既有读取口径。

## C++ 契约

- `loadBacktestArchive(resultId, Display)` 是默认路径。它对 raw 子表执行一次 `count/min/max` 完整性检查，但不物化 Tick 容器。
- `loadBacktestRawTickPage(resultId, offset, pageSize)` 按 `sequence_no` 稳定顺序读取，单页限制为 1～4096 条，并返回总数与 `hasMore`。
- `CompleteInputSnapshot` 仅供明确需要重放或核验的 C++ 调用方使用；它仍可能占用与完整 Tick 数量成比例的内存，不是界面默认行为。
- 分页窗口按父结果不可变 `actualBars` 校验；正常入口应先完成默认轻量恢复的一次整段完整性检查。

schema 保持 v24。没有修改 QML、Provider、NAS 或网络边界。本阶段结束时分页 API 仍位于存储层；该后续缺口已在 H3B.25 由独立工作线程、requestId 门控和 QML 逐笔浏览替代，见 `docs/raw-tick-archive-async-browser-2026-08-30.md`。

## 可重复验证

临时 SQLite fixture 覆盖 12 条原始 Tick：默认恢复的 `rawTicks` 为 0、完整快照可用且未载入；5/5/2 三页保持源行 1～12 的稳定顺序；超过 4096 条的页面请求明确拒绝。显式完整模式仍恢复全部事实，既有异步流式归档和身份失败回滚测试继续通过。

本阶段只形成受控契约，不声称真实跨月读取延迟或 MiB 基线。最终 Debug 串行全量构建成功，CTest 43/43（76.35 秒）、`all_qmllint` 和隔离临时数据库的副屏原生关闭探针均通过（PID 40452，退出码 0）。
