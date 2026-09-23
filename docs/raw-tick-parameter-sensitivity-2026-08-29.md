# 原始 Tick 参数敏感性

## 结论

H3B.9B 已完成严格本地的单标的原始 Tick 参数敏感性闭环，并使用真实 NAS 来源、已同步且逐文件校验的数据完成 10 标的验收。它只支持 `supportsRawTicks=true` 的内置 A 股策略，属于研究工具，不代表统计显著、收益保证、模拟交易或实盘能力。研究运行只读取本地 SQLite，不访问 Provider 或 NAS。

## 训练与最终验证

- 输入为本地 SQLite 中选定代码、起止日期内的 `raw_trade_ticks`，按 `timestamp_utc, source_line` 确定性排序。
- 至少保留 2 个训练交易日和 2 个最终验证交易日；验证日数可配置为 2～20。
- 参数轴使用注册表 Schema 规范化后生成笛卡尔积，候选上限为 64。所有候选只在训练段运行。
- 选择顺序固定为训练收益降序、最大回撤升序、规范化参数字典序。选中后只在最终留出段运行一次；每次运行使用全新的 `BacktestEngine`，不携带仓位、信号、权益或 T+1 锁存状态。

## 异步契约与界面

`RawTickParameterSensitivityService` 在独立线程和独立 SQLite 连接上完成装载、扫描与归档。`TradingController` 暴露：

- `rawTickSensitivityRunning`、`rawTickSensitivityRequestId`、`rawTickSensitivityError`；
- `rawTickSensitivityResult`、`rawTickSensitivityHistory`；
- `runRawTickParameterSensitivity(...)`、`cancelRawTickParameterSensitivity()`、`loadRawTickParameterSensitivityResult(resultId)`。

请求使用递增代次，迟到结果不能覆盖新请求。结果同时记录 SQLite 装载与参数扫描耗时；已有不可变身份重复运行仍复用原归档，但本次运行耗时会如实返回且不会改写旧档案。`ParameterGridPanel.qml` 保留原“自定义 K 线网格”，并增加独立的“原始 Tick 敏感性”模式，展示策略、代码、日期、留出日数、双参数轴、异步状态、最终验证结果、装载/扫描耗时和历史恢复。

## 身份与持久化

schema v23 新增 `raw_tick_parameter_sensitivity_results`。记录包含策略、规范化候选、训练/验证日期和事实数、两侧数据 SHA-256、候选集合 SHA-256、选中参数、数据集版本、初始资金、佣金、滑点、整手及完整身份 SHA-256。写入为单事务；相同身份复用已有归档，不同 result ID 不能覆盖已有身份。恢复只读 SQLite，不重跑、不联网，也不使用当前编辑参数重新解释旧结果。

## 验证与限制

- 确定性 6 日 fixture 覆盖两组候选、训练择优、最终验证隔离和身份稳定性。
- 临时 SQLite 覆盖异步请求、完整 round-trip、重启恢复、重复身份复用与冲突拒绝。
- 串行 Debug 全量构建成功；CTest 42/42 通过（74.98 秒）；`all_qmllint` 通过。
- 副屏 1586×993（`originX=-2453`）确认模式切换、字段与归档入口可见，无 QML 创建错误。窗口关闭后仍出现既有 APP-001 残留进程，本次只清理精确测试 PID。
- 真实 NAS 来源验收对 10 个代码、5 个交易日、50 个原始文件完成 50/50 SHA-256 一致性校验；本地隔离库共 125,774 笔 Tick。三种策略各 10 个标的、每组 9 个候选、重复 2 次，共 60/60 成功，30/30 选择和身份稳定，形成 30 个唯一归档。SQLite 装载 p50/p95 为 110.5/222 ms，扫描 p50/p95 为 1,794/3,458.2 ms，总耗时 p50/p95 为 1,937.5/3,686.7 ms。
- 真实验收后的串行 Debug 全量构建成功；CTest 42/42（76.75 秒）与 `all_qmllint` 通过。副屏恢复归档后发现并修复长摘要覆盖输入区，最终改为稳定的单行省略状态，完整内容保留在可访问名称中。

真实 5 日/10 标的验收已经证明短窗参数选择可重复并形成了耗时基线。后续 6–8 月、3,030,617 条 Tick 验收又完成 60/60 请求和 30/30 重复稳定，但有 23/30 个胜出参数相对短窗发生变化，因此短窗不能代表跨月稳定。完整跨月证据见 `docs/raw-tick-parameter-sensitivity-nas-cross-month-acceptance-2026-08-29.md`。嵌套 walk-forward 已由 H3B.9C 完成，详见 `docs/raw-tick-nested-walk-forward-2026-08-29.md`；跨年度、跨标的统计推断和多重检验仍未完成。不得用当前单次留出或单标的多折结果代替这些步骤。
