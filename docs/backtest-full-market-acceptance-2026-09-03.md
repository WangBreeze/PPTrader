# 全市场原始 Tick 回测四小时验收（2026-09-03）

## 结论与范围

Release `pptrader_real_load_probe.exe` 于 03:01:35 启动、07:01:35 自然退出；`full-market-4h.json` 已生成且 `passed=true`。`tick_momentum` 对 5,186 只 A 股的 2026-09-02 单日原始 Tick 连续完成 340 轮、1,763,240 项（每轮 5,186 项），成功 1,763,240、失败 0、T+1 违规 0。

首尾轮 SHA-256 均为 `1b0c49…8c96ce`，确定性成立。轮次 p50/p95/max 为 42.214/43.472/45.171 秒；单项 p50/p95 为 7/14 ms（结果未输出单项 max）。心跳 p95/max 为 16/91 ms。日志 170 个私有内存进度样本的 p50/p95/max 为 20.3/24.7/26.6 MiB；最终结构化记录的私有内存首/末/最大为 12.566/24.688/26.641 MiB，斜率 +0.04419 MiB/分钟。峰值仅驻留 1 个数据集、4,909 条观测，并非并行驻留 5,186 个引擎。

## 输入与写入边界

对隔离输入库的只读 `integrity_check` 为 `ok`。`raw_trade_ticks` 为 13,219,462 条，交易日均为 2026-09-02，UTC 范围 01:25:00–07:30:00；`candles` 为 1,231,000 条。`ticks`、`market_snapshots` 均为 0；`backtest_results`、`backtest_trades`、`backtest_equity_points`、`backtest_raw_ticks`、批量/组合/验证归档表均为 0，未写回归档或留下半写入。

## 验证

Release `batch_backtest_evidence` 1/1 通过；串行 Debug 构建后 CTest 47/47 通过，`all_qmllint` 通过。运行未访问 NAS 或网络。

本证据只覆盖 5,186 只的单日输入；另有 10 只 2026-08-03 至 08-28 的长周期门禁。不得外推为 5,186 只多月数据、收益有效性或 5,186 引擎并行驻留的证明。
