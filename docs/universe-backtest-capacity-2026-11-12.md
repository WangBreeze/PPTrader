# 全市场 5 策略日 K 回测容量实测（2026-11-12）

> 范围更正：此报告只记录日 K 旁路实验。项目主目标是原始逐笔 Tick 回测；本文的 25,305 项、吞吐和 233.53 MiB 数据库占用均不可用于推定 Tick 的并发或归档容量。[原始 Tick 限额方案](backtest-database-and-run-limits-2026-11-12.md)另行记录。

## 范围与口径

本轮先做本地计算与归档功能，不改主窗口。自定义策略指标目录由 18 类增为 20 类，新增 STOCH（两层 SMA 平滑）和 SUPERTREND（Wilder ATR）；内置日 K 策略增为 5 个：SMA 交叉、RSI 均值回归、MACD 趋势、布林带均值回归、STOCH 均值回归。前四个为原有策略；第五个使用 K 上穿 D 且 K<30 入场，K 下穿 D 且 K>70 出场，默认 14/3/3。

独立工具 `pptrader_universe_benchmark` 调用正式 `BacktestEngine` 和 `SqliteLocalDataStore`。按股票在独立计算线程中预处理 45 根日 K 并顺序运行所选 5 策略；主线程以一个 SQLite 写入器保存完整结果，同一股票的 5 项任务共享一份不可变 K 线快照。归档保留每项权益曲线、成交和图表信号。数据源 `~/.data` 只读，隔离数据库与 JSON 报告位于 `out/research/universe-benchmark-2026-11-12/`。

固定样本为 **2026-09-01 至 2026-11-10 的 45 个交易日**。45 个日期目录均有 CSV 的代码交集为 5142 只；逐文件检查表头、日期、开高低收关系、非负成交量后，剔除 81 只原始数值冲突股票，最终 **5061 只**。交集代码表 SHA-256 为 `df608148b67ddd7fdfc4f154d28bd6cdee12e0239a1b3d9dea3db924e12934ad`。完整日期清单、81 个文件及拒绝原因见 [全量 JSON](../out/research/universe-benchmark-2026-11-12/full-16.json)。没有修改源文件。

## 并发扫描

先以同一批 500 个交集代码跑完整归档；其中 3 只因坏 OHLC 被拒，实际保存 2485 项。随后预检全市场，固定前 1000 只有效股票再次跑完整归档。两轮因文件缓存、输入集合和调度波动，不应只凭单轮最优值宣称硬件上限。

| 计算线程 | 500 代码完整归档，任务/秒 | 1000 有效股票完整归档，任务/秒 | 全量读取 CSV 并计算但不归档，任务/秒 |
|---:|---:|---:|---:|
| 1 | 882 | 1521 | 6937 |
| 2 | 1450 | **1754** | 13851 |
| 4 | 1370 | 1580 | 25030 |
| 8 | 1384 | 1443 | 34903 |
| 16 | 1476 | 1522 | 51018 |
| 24 | **1484** | 1542 | 52500 |
| 32 | 1290 | 1475 | **52939** |

本机 Ryzen 9 9955HX 为 16 核/32 线程，内存 30 GiB。**不归档模式测到 32 线程约 52,939 项/秒，24 到 32 线程已经进入平台期**；该模式仍逐只读取 CSV，预检耗时另计。完整归档的稳定最佳并发未由这两轮确定，1–2 个线程甚至可能比 16–32 快，因为 SQLite 单写入器主导总耗时。故全量归档选择 16 线程以同时验证较高计算并发与单写入器容量，而非把它称为最优配置。

## 全量结果

| 项目 | 实测 |
|---|---:|
| 策略 × 股票 | **5 × 5061 = 25,305 项** |
| 成功归档 / 失败 | **25,305 / 0** |
| 16 线程完整归档耗时 | 21.696 秒，约 1166 项/秒 |
| 预检耗时 | 1.338 秒（缓存已热） |
| 归档库初始大小 | 650,776 字节 |
| 归档库最终大小 | **244,875,264 字节（233.53 MiB）** |
| 净增长 | **244,224,488 字节（232.91 MiB），约 9.65 KiB/任务** |
| 进程峰值常驻内存 | 28.29 MiB |
| 结果 / 共享 K 线 / 权益点 / 闭合成交 | 25,305 / 227,745 / 1,138,725 / 21,668 |
| SQLite `integrity_check` | `ok` |

每个策略各有 5061 条结果；每只股票保存 45 根 K 线、每项策略保存 45 个权益点。完整归档库为 [full-16.db](../out/research/universe-benchmark-2026-11-12/full-16.db)，报告为 [full-16.json](../out/research/universe-benchmark-2026-11-12/full-16.json)。其余 `sweep-*.json`、`valid1000-*.json` 与 `compute-*.json` 保留并发扫描的逐轮原始指标。

复核得到每只股票恰有 5 条结果，每项恰有 45 个权益点，20,244 条结果引用同股共享 K 线；21,668 条成交与结果摘要求和一致，外键违规 0。容量扫描的 16 个临时数据库在保留 JSON 报告后删除，回收 **520,179,712 字节**；唯一保留的全量数据库是 `full-16.db`。

## 复现

```sh
cmake --preset linux-qt612-debug
cmake --build --preset linux-qt612-debug --target pptrader_universe_benchmark --parallel
out/build/linux-qt612-debug/bin/pptrader_universe_benchmark \
  --data-root /home/wanglc/.data \
  --end-date 2026-11-10 --days 45 --threads 16 \
  --strategies sma_crossover,rsi_mean_reversion,macd_trend,bollinger_mean_reversion,stoch_mean_reversion \
  --database out/research/universe-benchmark-2026-11-12/new-run.db \
  --report out/research/universe-benchmark-2026-11-12/new-run.json
```

新运行必须使用不存在的数据库文件；工具拒绝覆盖。只测计算时加 `--compute-only` 并省略 `--database`。可用 `--limit` 缩小股票数量；5 个内置日 K 策略可通过 `--strategies` 选择。日 K 文件内容由各归档的 `dataVersion` 固定；报告记录源目录、日期文件夹和代码交集哈希。

## 解释边界

这是**效率、并发与存储容量**测试，不是策略盈利验证。45 根日 K 对 MACD 默认 35 根预热只剩约 10 根有效决策；未做时间切分、样本外、复权、停牌与公司行动审计。当前引擎按已完成日 K 的收盘计算信号并使用该根收盘执行，不能把本轮成交或收益视为可实现的交易业绩。现有 GUI 矩阵仍有单批 200 项上限并顺序执行；本轮全市场能力通过独立工具实现，尚未接入 GUI 调度与批次管理。

Qt 6.12.0 完整构建通过；清除本机 GTK/显示环境变量后，除需本地监听套接字权限的 `nas_data_node` 外，CTest **56/56** 通过，`all_qmllint` 无错误（仅一条既有的 `InstrumentSearchPanel.qml` 告警）。指标预热规则最后调整后，`custom_strategy` 与 `custom_strategy_wizard_qml` 两项复测通过。NAS 测试在当前受限执行环境中因 `QHostAddress::LocalHost` 监听被拒而失败；未将其记为通过。
