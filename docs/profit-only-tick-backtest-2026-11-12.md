# 原始 Tick 盈利归档与复跑报告（2026-11-12）

## 保存规则

自动归档仅接受期末净权益严格高于初始资金、总收益率严格大于 0 且数值有限的回测结果；**原始 Tick 还须至少有一笔闭合交易**。引擎的期末权益已经按执行价格、滑点和佣金计算；未平仓头寸仍按样本末价格估值，因此这一门槛避免单日纯浮盈入库，但“盈利归档”仍不等于全部收益已实现或未来可交易收益。收益为 0、亏损、无闭合交易的原始 Tick、无效数值不写入 `backtest_results`、逐笔快照、权益或成交归档。单次回测、批量自动归档、矩阵自动归档与批量手动归档均使用同一判断；非盈利结果可在当前会话查看，批次运行状态仍保存最小任务状态以支持恢复。旧归档不被删除或改写。

## 真实 Tick 复跑

数据来自原有六股隔离库；回测窗口 2026-07-20 至 2026-11-09，共 75 个共同完整交易日、六股合计 **2,057,822 条有效 Tick**。在新隔离库上执行五个已注册 Tick 策略，各策略六股，共 **30/30 项成功**。`tick_second_board` 是二板专用策略，不作为全市场通用策略收益证据。所有输入沿用严格质量门槛，六股质量状态均为 `warning`；公司行动覆盖仍未知。每项独立 100 万元初始资金、佣金率 0.0003、滑点率 0.0002、100 股交易单位与 A 股 T+1。

| 策略 | 成功 | 正收益并归档 | 未盈利不归档 | 闭合交易 | T+1 违规 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 逐笔动量 `tick_momentum` | 6 | 2 | 4 | 438 | 0 |
| 逐笔均值回归 `tick_mean_reversion` | 6 | 3 | 3 | 347 | 0 |
| 逐笔放量突破 `tick_volume_breakout` | 6 | 2 | 4 | 260 | 0 |
| 逐笔方向失衡 `tick_directional_imbalance` | 6 | 3 | 3 | 444 | 0 |
| 二板接力 `tick_second_board` | 6 | 0 | 6 | 0 | 0 |
| **合计** | **30** | **10** | **20** | **1489** | **0** |

归档库仅有 **10** 条结果，所有 `total_return_percent > 0`；非正收益结果 **0** 条。归档的逐笔快照共 **3,349,630 行**，每条盈利结果均从独立 SQLite 连接恢复完整原始 Tick、数据版本、末权益，报告 `matchesSource=true`。`integrity_check=ok`，外键违规 **0**。原始事实表仍为 **2,418,714 行**；该表含此前 90 日导入尝试，并非全部参与本轮回测。数据库从隔离源库的 **847,671,296 字节**增长到 **1,482,125,312 字节**，净增 **634,454,016 字节（605.06 MiB）**；当前每个盈利结果仍复制自己的 Tick，尚未实现共享 Tick 快照。

[逐笔动量 JSON](../out/research/profit-only-tick-2026-11-12/six-stock-75d-auto.json)、[均值回归 JSON](../out/research/profit-only-tick-2026-11-12/tick_mean_reversion.json)、[放量突破 JSON](../out/research/profit-only-tick-2026-11-12/tick_volume_breakout.json)、[方向失衡 JSON](../out/research/profit-only-tick-2026-11-12/tick_directional_imbalance.json)、[二板接力 JSON](../out/research/profit-only-tick-2026-11-12/tick_second_board.json) 保留每项结果和盈利/跳过计数；这类测试报告位于文件系统，不是回测结果数据库。最终隔离归档为 [profit-only-auto.db](../out/research/profit-only-tick-2026-11-12/profit-only-auto.db)。较早的重复测试数据库已清除，只保留最终隔离库。

## 验证与边界

编译 `PPTrader` 与真实 Tick 探针通过；`backtest_engine`、`batch_backtest`、归档 QML 和矩阵参数 QML **4/4** 通过；`all_qmllint` 通过（仅既有 `InstrumentSearchPanel.qml` 的 `var` 警告）；离屏主窗口加载并正常退出，退出码 0。首次测试因桌面 GTK 主题仍尝试连接显示服务而失败；定向 QML 测试在 `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=none` 环境下复跑通过。原有 `application_exit` 在无真实显示的环境中无法观察原生关闭，不把它算作本次通过。

单股一日预检曾对原有 `expanded.db` 调用 `--existing-db`，其初始化把该**研究隔离库**从 847,630,336 增至 847,671,296 字节；原始事实行数和归档结果数保持不变。`~/.data` CSV 从未写入。本轮数据库增长以上述初始化后的文件大小为基线。

这次验证的是 **5 策略 × 6 股 × 75 日**的盈利筛选和保存闭环。随后 5572 股单日原始 Tick、五策略 27,860 项并发计算已完成；该单日样本全部没有闭合交易，因此归档 0 条，详见[全市场 Tick 并发实测](tick-fullmarket-five-strategy-2026-11-12.md)。全市场跨日盈利归档、五个通用 Tick 策略、并发硬上限和共享 Tick 快照仍未验证。只保留盈利归档会造成历史结果的选择偏差，不能仅用归档列表计算策略胜率或收益分布；需要以冻结的完整运行报告为分母。
