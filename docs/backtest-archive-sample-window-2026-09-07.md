# 回测归档请求日期范围修复（2026-09-07）

## 范围与隔离

用户确认验收“归档保存后重开，保留空白、单边及双边样本日期范围”。本项与向导最小窗口布局并行开发，构建和桌面验证串行。只使用测试临时 SQLite；不访问 NAS、网络或开发库，不改写此前验收证据。

## 原因和已完成的第一轮验证

`BacktestConfig` 的 `from/to` 是实际输入首末，`sampleFrom/sampleTo` 是用户请求边界，二者不能互相替代。之前归档保存语句和加载语句均未包含请求边界，关闭存储再重开就丢失。

公开存储接口回归 `archiveSampleWindowsSurviveReopen` 经实际保存、销毁存储对象、重新初始化、读取同一归档：

- 红测：8 行中 6 行非空请求日期失败，读取结果为无效时间；空白两行通过。总耗时 379ms，见 `artifacts/archive-sample-window-red.txt`。
- 第一轮修复后：8 行均通过，385ms，见 `artifacts/archive-sample-window-green.txt`。覆盖 Candle/Tick × 空白/仅起点/仅终点/双端，输入使用 +08:00 与毫秒，回读保持同一 UTC 时刻，且实际首末、数据哈希和规则快照不改变。

全库 schema 升级为 **v25**，新增独立的 `backtest_results.sample_from_utc/sample_to_utc`，沿用原迁移事务、迁移前备份及迁移历史机制。没有修改策略规则、数据身份或行情快照。旧证据库不得原位升级；需要用新程序检查时，应先复制至新目录再迁移副本。

兼容提醒：只支持 v24 的旧程序不能打开升级后的 v25 库；不要混用旧二进制访问升级后的工作库。本轮没有升级用户开发库或历史验收库。

## 兼容性与存储复验

SQL NULL 表示旧归档未记录请求范围；非 NULL 空字符串表示新归档明确不限制该端；有效值保存为 UTC ISO 时间。旧归档不能从实际输入范围或当前界面推测补写，也不能自动当成无边界的可重跑验证记录。

`BacktestConfig.sampleWindowKnown` 区分上述两种语义，新建配置为 true，读取旧 NULL 为 false；未知归档再次复制保存仍写 NULL，不洗成明确无界。非空损坏日期或起点晚于终点将拒绝加载，并给出“请求样本窗无效”错误。

第二轮验证已完成：

- 旧库用例实际移除两列，并把版本及元数据设为 v24；升级产生迁移前备份，回读仍为未知。实际输入首末、非空自定义规则快照、数据哈希和结果明细数量保持不变；复制旧归档仍未知，第二次打开不再产生迁移备份。红测 `artifacts/archive-sample-legacy-red.txt`（93ms）先确认了旧未知误判。
- 8 种日期组合同时覆盖 Display 和 CompleteInputSnapshot 两种公开加载模式。
- 临时库故障注入覆盖损坏起点、损坏终点和倒置双端；修复前 3 行均错误地允许恢复，见 `artifacts/archive-sample-corrupt-red.txt`（163ms）。
- 最终完整 SQLite 测试 **50/50，3178ms**，见 `artifacts/archive-sample-store-complete.txt`；不是全工程 CTest 数量。中间 47 项版本另保留在 `artifacts/archive-sample-store-final.txt`，不冒充最终结果。

## Controller 跨层验证

真实 Controller 红测 `artifacts/archive-unknown-controller-red.json` / `archive-unknown-controller-red-ctest.txt`（7.57 秒）确认：旧未知范围仍能保存成验证记录，计数错误增加，重开后为 5 条而非期望的 4 条；新归档的四种日期已准确传至 Controller 验证记录。这是独立于存储读取的消费端缺口。

已在 `saveValidationRun()` 加早期门禁：旧未知范围返回 false，中文提示要求明确范围并重新回测，不增加内存/数据库记录，不改变用户当前编辑日期。

正式 Debug 全量构建完成后，真实 Controller 目标绿测通过（6.95 秒）；固定证据 `artifacts/flow03-controller-final.json` 的 100 项检查均为 true，包括旧未知拒绝、计数不增、中文说明、四类日期传递和重开恢复 4 条。三尺寸完整 Main 宿主几何也通过，但这不替代原生鼠标操作。

最终 Debug 全量构建、CTest **49/49（113.86 秒）**和 `all_qmllint` 通过，CTest 证据为 `artifacts/flow03-final-ctest.txt`。本轮构建及 lint 的真实执行回执分别为 session 11495 / chunk bd51af（exit 0）和 chunk aa0e56（exit 0）；不引用此前 18 点的同名旧日志作为本轮证据。`LastTestsFailed.log` 为 23:09 红测遗留索引；23:26 最终 `LastTest.log` 与上述 CTest 日志均为全部通过，保留旧失败索引，不删除历史证据。

## 两个入口的最终边界

2026-09-08 补充：用户说明上一轮曾手动改变界面。本轮在电脑空闲后已补齐1120×760窗口预览/返回编辑及底部上一/下一步操作，见[原生补验](wizard-small-window-reacceptance-2026-09-08.md)。下方原生“未通过”是前一轮历史；归档入口通过保持不变，APP-001退出残留仍未通过。

归档日期入口通过：公开 SQLite 保存→销毁→重开及真实 Controller 恢复→保存验证→重开均已有结果，旧未知范围拒绝保存为可重跑验证；没有用当前编辑日期或实际输入首末代替请求日期。

小窗口原生入口本轮**未通过验收**：虽然完整 Main 的三尺寸几何和 Qt 鼠标/键盘回归通过，副屏截图实际为 1831×1103、三栏布局，与日志中的 1120×760 不符；不能把大窗口截图作为窄窗口预览/底栏操作成功。精确隔离测试 PID 40480 在原生关闭后完成 Controller shutdown（54ms），随后停在 QML 引擎销毁，已超过 60 秒；它不影响独立归档回归的结果，也不能由 CTest 中的快速退出覆盖。保留截图、阶段日志及本机符号采样，APP-001 保持开放；本轮不再启动重复实例或扩展生产退出逻辑修复。清理与完整原生记录见 [真实宿主收尾报告](flow03-host-layout-2026-09-07.md)。
