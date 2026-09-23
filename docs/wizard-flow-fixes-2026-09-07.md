# 向导保存、回放与嵌入布局修复（2026-09-07）

## 范围与证据边界

本轮在[双指标原生验收](custom-strategy-signal-lifecycle-2026-09-07.md)之后处理 FLOW-08、FLOW-09、FLOW-03；原验收报告、数据库和截图保持不变。只使用新临时 SQLite 与独立 INI 设置，不访问网络、NAS 或开发数据库，不修改用户草稿和窗口几何。

采用先复现再修复的诊断循环：存储层测试真实 SQL，集成探针实例化真实 TradingController 与资源中的 Wizard，布局测试仅将不相关业务依赖替换为 Stub。合成数据不作为收益、真实市场或 NAS 验收证据。

## 已定位原因与最小修复

- **FLOW-08**：无效样本日期使用空 `QString()` 绑定为 SQL NULL，违反验证表 `sample_from_utc/sample_to_utc NOT NULL`。改为非 NULL 空串，与读取层“空串表示未限定”的约定一致；有效时间统一转 UTC。保存错误保留失败 `QSqlQuery::lastError()`，不再读取无关连接错误。Controller 仅落库成功后增加内存计数并通知；公开返回 bool，QML 不再无条件显示成功。
- **FLOW-09**：真实 `replayBacktestBar()` 返回 `timestamp`，Wizard 判断/显示 `startTime`，首根因此误报无数据。只修正消费者字段，保持现有 Controller 契约和越界空对象。
- **FLOW-03**：紧凑模式依据向导实际宽度（小于 1360px），隐藏固定导航侧栏，编辑区占满，预览切换显示；底部状态与操作分行。宽模式保留原布局，切换预览不重建草稿或改变当前步骤。

未改变 SQLite schema、NAS preparation、数据源、策略计算或实时仅内存边界。

## 可重复验证

红测证据：`artifacts/flow08-query-error-red.txt` 中空/仅起点/仅终点三种窗口触发非空约束，双端有效通过；`artifacts/flow08-09-real-controller-red.json` 复现 UI 与库计数不同、有效 Bar 被拒绝、写入失败仍增加记录；`artifacts/flow03-layout-red.txt` 复现 724/884/1280px 嵌入内容区不足，1600px 通过。

存储测试 `validationRunSampleWindowsRoundTrip` 覆盖四种日期窗口的写入、关闭、重开和字段值。`trading_controller_wizard` 使用测试构建内的 `--wizard-contract-probe`：真实加载四份合成归档、保存/独立读取验证记录、首根/末根/正负越界回放、临时触发器注入写入失败、Controller 重开。此入口仅 `BUILD_TESTING` 编译，不启用行情。

重要区别：现有历史归档没有序列化 `sampleFrom/sampleTo`；真实 Controller 探针证明归档结果保存验证及恢复计数，不证明四种样本窗能通过历史归档保持。四种日期窗口的精确往返由存储层直接测试，归档样本窗身份缺口待单独处理，不以当前配置推测补写。

定向 CTest 三目标通过（27.01 秒）：真实 Controller 3.03 秒、SQLite 3.17 秒、Wizard QML 19.03 秒。布局测试覆盖 724/884/1280/1600px，检查条件编辑区至少 500px、底部按钮不越界、紧凑预览切换可恢复。

## APP-001 与未验收内容

本轮不把向导缺陷修复当作永久退出残留修复。此前本地查询取消解决的 19.457 秒积压等待与 357ms 修复结果仍有效，但未解释所有偶发残留；详见[退出专项](app001-local-query-shutdown-2026-09-07.md)。真实 NAS 操作后的残留、异常存储硬退出上界及完整辅助技术兼容仍待单独验证。

## 最终自动验证与恢复验收

本节记录第一次恢复验收，不代表后续最终状态。真实宿主 footer/JSON 层收尾与退出探针次序修正见 [FLOW-03 宿主报告](flow03-host-layout-2026-09-07.md)；归档请求日期和旧未知范围保护见 [归档日期报告](backtest-archive-sample-window-2026-09-07.md)。以下失败和计数保留为当时事实。

中断前串行 Debug 全量构建成功，全量 CTest **49/49（145.20 秒）**、`all_qmllint` 通过，日志为 `artifacts/flow-fixes-ctest.txt`。恢复后仅增强 `embeddedWizardDoesNotClip`：不再直接设置预览状态，而是对实际按钮发送 Qt 鼠标点击并用 Enter 返回，三个紧凑宽度均保持步骤和布局。定向构建及完整 `custom_strategy_wizard_qml` 目标 **1/1（16.12 秒）**通过，见 `artifacts/flow-resume-mouse-ctest.txt`。恢复后未改生产代码/QML，也未再重跑全量 CTest，不能把上轮全量成绩称为新增测试后的全量重跑。

副屏原生截图：

- `I:/workspace/PPTrader/artifacts/flow-resume-1280-edit.png`：1280×1000，向导实际宽度 886px，中央条件不再被左右栏挤压，底部按钮在首屏内。
- `I:/workspace/PPTrader/artifacts/flow-resume-1120-edit.png`：1120×760，向导实际宽度 726px，中央条件可见；外层原有 610px 卡片使底部按钮落在首屏下方。存在页面滚动条，但本轮未完成原生滚动到 footer 的操作验收，不称最小窗口全流程通过。

两次截图中的右缘仍可见底层 JSON 工作台少量内容；完整容器裁剪与最小高度布局仍需收尾。桌面工具点击预览只明确观察到 hover，后续按键曾遇到窗口句柄/输入几何错误，并与 90 秒验收窗口自动关闭重叠；不能归因为按钮业务故障，也不能称原生预览切换通过。真实鼠标/键盘事件已经由 Qt Test 覆盖。未在本轮重新点击原证据库的“保存当前验证”。

## 退出复验及采样限制

- `scripts/pptrader-exit-probe.ps1 -TimeoutSeconds 30 -Repeat 5`：正常主程序的原生关闭路径 **5/5**，退出码均 0，启动到 PID 回收 3,077–3,300ms；见 `artifacts/flow-resume-native-exit.txt`。不是纯关闭耗时，也不是向导交互后通过的证据。
- 原生向导隔离实例 PID 31808、18568：自动关闭后均自行退出（码 0），`native-closed→native-engine-destroyed` 分别 **20,342ms、34,890ms**；随后 Controller shutdown 仅 41ms、29ms。没有强制终止这两个实例。
- PID 40280、40428：通过桌面工具 Alt+F4，在 90 秒自动关闭之前观察到 native-closed，均最终自行退出（码 0）。引擎销毁阶段分别 **11,692ms、15,640ms**，Controller shutdown 分别 **19ms、41ms**。对应 `flow-stack-safe-1280.json.log`、`flow-stack-final-1280.json.log`。
- 非侵入、不挂起的 CDB 快照保存在 `artifacts/flow-stack-final-1280-cdb.txt`。由于该次符号路径没有包含 Qt 安装目录 PDB，且运行态快照不是稳定暂停栈，导出符号的大偏移不能当作真实函数归因。只据阶段日志定位到 QML 引擎销毁，尚未证明 SQLite、UIA 或某个 Qt 函数是根因；本机 Qt PDB 已确认存在，后续可进行有符号的受控采样。
- 采样失败也保留：PID 45080 的首次 CDB 命令将后半段当作符号路径，未取得栈，进程以 **0xC0000354** 结束，属于调试器干预的无效退出样本，绝不计为自然退出或修复；之后 `-pd` 选项被本机调试器拒绝，亦未取得栈。原日志不删除、不改写。

本轮所有测试进程已释放，未影响其他应用实例。APP-001 仍开放：已确认“没有长时间进程残留的普通探针通过”不足以替代“复杂向导销毁无延迟”。诊断技能要求以可复现证据再改生命周期，本轮不因无符号快照而改 SQLite 或共享 NAS 模块。
