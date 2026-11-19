# 自定义策略原始逐笔 Tick 回测首版验收（2026-11-12）

## 本轮交付

已冻结的自定义策略 JSON 可以通过 `pptrader_custom_raw_tick_backtest` 本地命令入口，在指定 A 股和日期窗口上直接使用原始逐笔成交。入口只读现有 SQLite，先核对交易日连续性、日历覆盖、历史 Tick 质量准入和逐日内容指纹；完整输出轻量 JSON。引擎还接受外部编译策略的原始 Tick 向量或分日流式输入，均核对策略 ID、版本、参数和定义快照。`triggerClose` 模式会被拒绝；原始 Tick 只允许显式 `signal.priceMode=nextOpen` 且无横截面/多股条件的单股策略。

首版因果口径为「每笔成交是一根指标观测」：该观测的 O/H/L/C 均取该笔成交价，成交量取该笔股数；指标 `period` 计成交笔数，不计分钟、交易日或钟表秒数。当前 Tick 的指标和入出场条件形成信号，**下一笔可观察到的 Tick** 才尝试成交，仍按配置收取佣金和滑点。A 股卖出受引擎 T+1 门禁；若同日触发退出，下一交易日的第一笔可观察 Tick 才尝试卖出。信号形成时间写入成交原因，实际成交时间独立保留在交易记录。没有下一笔 Tick 时不会回填当笔价格。逐笔指标沿用现有 20 类计算器，但真实逐笔输入本轮只验证了 SMA；OHLC 相关指标在此口径下是逐笔价格窗口的派生数值，不能解释为分钟 K 或日 K 的高低价。

指标运行时的历史价格/量缓存现按最大参数所需长度有界保留，观测总数单独累计，避免长 Tick 流使缓存线性增长。原有 K 线策略仍使用原定义和快照；逐笔模式在规则快照额外固定 `rawTickPolicyVersion=1`、指标观测和下一笔执行口径，不新增 SQLite 表结构。

## 可复现的本地输入与结果

源为 `out/research/profit-only-tick-2026-11-12/profit-only-auto.db`，只读；策略定义为[固定 JSON](../out/research/custom-strategy-2026-11-12/custom-raw-sma-v1.json)，定义 SHA-256 为 `sha256:418f63e25a612373df94c0bd615d7125cddbeb1c9f4415b570ea5009d412a2e1`。窗口是 2026-07-20 至 2026-07-21，初始资金 100 万元，双边佣金率 0.0003、滑点率 0.0002、整手 100 股。策略故意用 `SMA(5)>0` 入场、`close>0` 退出，仅验证跨日执行链路，不是有选择力的交易策略。

| 股票 | 原始 Tick | 闭合交易 | 期末总收益率 | 数据版本 | 轻量输出 |
| --- | ---: | ---: | ---: | --- | --- |
| 600519 | 9,337 | 1 | +2.590450% | `sha256:27c73eed95c1746f54a48f127411e1c77c11d76979a80f296b3bacd3de0c8da8` | [JSON](../out/research/custom-strategy-2026-11-12/custom-raw-cli-result.json) |
| 000001 | 9,325 | 1 | +0.499189% | `sha256:16963b9cef6df62ce9b4286c8341058b09c3fd14dd3a30b3f5d1c49b9d5bd8cf` | [JSON](../out/research/custom-strategy-2026-11-12/custom-raw-cli-000001-result.json) |

600519 的入场信号形成于 7 月 20 日 09:30:10（北京时间），实际模拟买入在 09:30:13，700 股；同日退出信号形成，但卖出延到 7 月 21 日 09:25:03，净损益 47,372.86 元。该正收益结果经盈利门槛后写入[独立验收归档库](../out/research/custom-strategy-2026-11-12/custom-raw-profitable.db)并重新读取：1 条结果、1 笔闭合交易、9,337 条输入 Tick；文件 2,879,488 字节（2.75 MiB），`integrity_check=ok`、外键违规 0。保存完整 Tick 是现有重型结果归档的行为；命令入口输出的 JSON 只含身份、摘要和交易，不复制全套输入。原始库文件大小 1,482,125,312 字节、修改时间 14:23:06（北京时间）在本轮前后相同。验收日志见[原始逐笔测试](../out/research/custom-strategy-2026-11-12/real-raw-tick.log)。

命令示例：

```sh
./out/build/linux-qt612-debug/bin/pptrader_custom_raw_tick_backtest \
  --db out/research/profit-only-tick-2026-11-12/profit-only-auto.db \
  --definition out/research/custom-strategy-2026-11-12/custom-raw-sma-v1.json \
  --symbol 600519 --from 2026-07-20 --to 2026-07-21 \
  --output out/research/custom-strategy-2026-11-12/custom-raw-cli-result.json
```

## 验证与未完成项

合成输入测试覆盖下一笔成交、同日退出延到次日、一次性与流式结果一致、定义快照篡改拒绝、`triggerClose` 拒绝；10,000 笔输入测试覆盖指标历史缓存有界。真实两日数据完成读入、回测、盈利门槛、归档恢复。600519 的命令结果连续两次 SHA-256 相同：`c445b91ba8403fc016ed0d79990199d25462a262389686cea12af4353763a90e`；输出路径指向源库被拒绝，周末作为窗口末日被拒绝。`custom_strategy`、`custom_strategy_lifecycle`、`backtest_engine` 三项定向 CTest 通过，`PPTrader` 构建、QML lint 与离屏主窗口启动退出通过（有既有 `InstrumentSearchPanel` lint 警告和 QSettings 提示）。全量 CTest、工作台逐笔按钮、矩阵多股/多策略逐笔编排、原生桌面点击、所有 20 类指标的逐笔数值核对和全市场长序列吞吐未在本轮验收。

此处“下一笔成交价”只代表历史成交数据上的**模拟撮合参考**。没有订单簿、排队位置、可成交量约束、涨跌停和真实委托回报，不能称为可实现成交；公司行动/复权和完整费率也未覆盖。两股正收益是短窗口功能样本，不是策略盈利证据。跨日每股每天独立买卖 100 股的机械样本与逐单对账属于[另一份开发计划](rdagent-tick-mechanical-replay-plan-2026-11-12.md)，本轮工具没有实现该样本口径。
