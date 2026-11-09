# PPTrader Qt 6.8 编码与退出验收规则

更新：2026-09-08。适用目标为 Qt 6.8.x；这是工程约束，不是 Qt 升级记录或缺陷已修复声明。

> **版本状态更新（2026-09-24）**：项目默认 preset 已改为 Qt 6.11.2，Qt 6.4.3 与 6.8.3 作为独立
> preset 的可选工具链保留。下文“版本与证据边界”一节记录的是 2026-09-08 当时的构建事实，作为
> 历史证据保留不改写；其中“不得只凭项目文件中的最低版本判定运行版本”“升级不等于缺陷已修复”
> 等约束仍然有效。当前多版本工具链策略见
> [Qt 多版本工具链](qt-multiversion-toolchain-2026-09-24.md)。

## 版本与证据边界

- 当前 `CMakePresets.json`、`CMakeCache.txt` 和实际测试依赖仍是 **Qt 6.4.3 / MSVC / x64**。尚未构建或运行 Qt 6.8 版 PPTrader，不得把本轮结果标成 Qt 6.8 验收。
- 6.8 升级须使用独立构建目录和本机 preset，固定补丁版本、编译器、架构、Debug/Release；记录 `QT_VERSION_STR`、`qVersion()`、实际 Qt DLL/插件路径及二进制 SHA-256。不要只凭项目文件中的最低版本判定运行版本，不混用不同 Qt 的 DLL、QML 插件与缓存。
- Qt 6.8.0 官方源码仍可见重复激活通知及 ScrollBar 动作重复连接路径：见 [QtBase 的激活入口](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/accessible/qplatformaccessibility.cpp) 和 [QtDeclarative 的 ScrollBar](https://github.com/qt/qtdeclarative/blob/v6.8.0/src/quicktemplates/qquickscrollbar.cpp)。这不等于已证明所有 6.8.x 有同一故障，也不能认定升级即可修复。源码于本日从官方仓库读取；本机仅完成 6.4.3 运行对照。

## R1：连接必须有界且可重复调用

初始化、页面显示、Provider 恢复和无障碍激活均可能重复发生，禁止假设回调只来一次。

- 优先在对象初始化时只连接一次；需要反复绑定时保存本次 `QMetaObject::Connection`，精准断开旧连接再绑定。
- 捕获 `this` 或其他对象的 lambda 必须有明确的 QObject context；`Qt::UniqueConnection` 不能给 lambda 去重。成员函数连接可以使用它，但仍要检查连接返回值和启停语义。
- 禁止用全对象 `disconnect()` 清除未知连接；它会破坏框架所需的销毁通知。已经排队的旧响应仍须用 requestId/代次/取消标志拒绝。
- 6.8 目标可在编译审计通过后启用 `QT_NO_CONTEXTLESS_CONNECT`（6.7 起提供）；不得把该宏当作所有重复连接问题的修复。

依据：[QObject 连接、断开及宏说明](https://doc.qt.io/qt-6.8/qobject.html)。以上对初始化路径的限界是 PPTrader 工程要求。

## R2：完整退出是产品行为，不是窗口消失

推荐的业务次序为：停止接单/接请求 → 标记退出并取消可取消任务 → 停止定时器、Provider 和工作线程 → 回收会引用界面的协调器 → 销毁 QML 对象/引擎 → 销毁 Controller → 进程返回。具体顺序按真实依赖关系验证，禁止机械倒置对象所有权。

- 清理入口幂等；正常关闭、菜单退出和系统关闭应汇入同一协议。`aboutToQuit` 做最后收尾，不能把所有持久化都留给 `exec()` 返回之后。[Qt 应用退出说明](https://doc.qt.io/qt-6.8/qcoreapplication.html)
- 退出不是隐藏窗口；关闭一个实时子窗口不应停止其他会话，最小化/切换页面不应触发全局退出。测试须明确“关闭子窗口”还是“退出整个工作台”。
- Controller、主题对象和模型要活到引用它们的 QML 释放之后；`QQmlApplicationEngine` 会销毁自己加载的 QML 对象，避免重复删除。[引擎所有权](https://doc.qt.io/qt-6.8/qqmlapplicationengine.html)
- 主事件循环已停止后，不能仅调用 `deleteLater()` 就声称已释放；不要在事件处理栈内直接删除正在收事件的对象。[延迟删除条件](https://doc.qt.io/qt-6.8/qobject.html#deleteLater)
- 禁止以 `QThread::terminate`、`TerminateProcess`、`_Exit`、跳过析构、泄漏整个引擎或常态关闭无障碍来达成“退出通过”。异常测试清理属于清理，不属于产品修复。

## R3：后台任务停止需要可观察的完成状态

- GUI、QML 与模型更新限所属线程；QObject 的创建、使用和销毁遵守线程归属，线程内 SQLite 连接也在所属线程关闭。不从 UI 线程删除工作线程中的对象。[线程与 QObject](https://doc.qt.io/qt-6.8/threads-qobject.html)
- `quit()` 不会中断正在运行的计算。可取消任务须在扫描/批次边界检查取消；排队任务退出后不再启动，完成回调不得复活旧页面状态。
- 停止不能被大量旧工作排在队尾而无限延迟。正在提交的不可取消事务须完整提交或回滚，不能为快退出制造半写入。
- 不把空库快退出外推为大数据退出；需覆盖本地任务运行中取消、结果正在回传和重复 stop。超过预算给出阶段与任务状态，不能继续等却显示“已退出”。

## R4：无障碍是正式兼容面

- 正常运行保留 Windows UI Automation 与键盘操作；屏蔽 `WM_GETOBJECT` 的无人值守隔离结果不能替代辅助技术兼容验收。
- 每次原生读取可能引发多次底层请求，不能把“读取一次树”当作“只激活一次”。验收分别记录读取次数和可测的底层事件数，未知则写未知。
- 检查重复激活后单次动作是否只执行一次，并检查内存、连接增长迹象及退出。滚动条、下拉菜单、预览/编辑往返都在范围内。
- 出现问题先分离：无外部读取、仅截图、无障碍读取、页面切换。Qt 私有 API 或供应商库补丁必须限定精确版本、有回归和移除条件；不得向全局 Qt 安装目录静默打补丁。

## R5：QML 属性、绑定与布局

- 不遮蔽继承属性；模型角色采用 `seriesVisible`、`leftOperand` 等业务名。`FINAL`是独立标记，不要把本项目的同名属性事故概括为“所有 C++ Q_PROPERTY 都必然 FINAL”。先核对继承类型与实际版本，保持项目的更严格禁重名约束。[Qt 属性系统](https://doc.qt.io/qt-6.8/properties.html)
- delegate 所需角色用明确的 `required property` 注入，不混用未注入的 `model`/`index`。迁移角色名称时同步草稿、模型写入与历史恢复。
- `Connections` 使用明确 `target` 和 `function onXxx(...)`；不要与旧式处理器混写，不能用 `ignoreUnknownSignals` 掩盖拼错接口。[Connections 文档](https://doc.qt.io/qt-6.8/qml-qtqml-connections.html)
- 大量动态数据及计算留在 C++/模型中。布局的直接子项使用 `Layout.*`，不同时用 anchors/固定宽高争夺同一尺寸；持久状态不依赖可能回收的 delegate。[Qt Quick 最佳实践](https://doc.qt.io/qt-6.8/qtquick-bestpractices.html)
- 首屏 footer 可见性、长内容滚动、预览返回与草稿保留要通过实际最小窗口检查；只设 `width=1120` 不能证明真实窗口就是该尺寸。用户手动改布局和测试工具干扰须单独记录。
- 核对具体控件属性的可写性，不照搬 `Item` 的赋值方式。2026-09-08 新模拟面板在 Qt 6.4.3 给 `Flow.implicitHeight` 赋值导致真实 Main 加载失败，单文件 lint 未拦截；删除该赋值并让 Flow 自算尺寸后转绿，已纳入 `paper_strategy_controller` 门禁。主题色必须取 `PPTheme` 实际字段（例如 `textMuted`，不是不存在的 `textSecondary`），并用截图检查可读性。
- **不要把 `activeFocusOnTab` / `focusPolicy` 的绑定直接跟随可用性。** Qt 6.11 在项已是 `window()->activeFocusItem()` 时拒绝置为 false 并打印 `QQuickItem: Cannot set activeFocusOnTab to false once item is the active focus item.`；`focusPolicy` 走同一处检查，所以翻一次打印两条且赋值被丢弃，项会一直留在 Tab 链。写成常量、用 `enabled` 表达不可用（Qt 会自动把 disabled 项移出 Tab 链并清除其焦点），或写成“可用性 `||` 该项自身 `activeFocus`”等焦点离开后再退出。2026-09-24 在 `PpButton` 等 5 处修复；`PpButton` 不能改用 `enabled: available`，否则子 `MouseArea` 被禁用、禁用态 tooltip 失效。回归测试 `tests/tst_focus_availability_qml.cpp`（用 `QMessageHandler` 捕获告警，不受 journald 路由影响）。

## R6：每个生命周期改动的合入门禁

以下是待执行标准，不表示本次全部通过：

1. 同一 Qt 补丁版本的 Debug/Release 分别构建与测试；QML lint 通过后仍启动编译资源中的真实 Main。
2. 隔离数据库和设置、关闭实时自动启动；不改用户草稿/几何，不触碰 NAS/开发库/交易。
3. 空窗口、真实向导操作后、重复无障碍读取后、离线任务取消后分别正常关闭。记录“关闭已接受 → 进程回收”，与启动总耗时分开。
4. 暂以 **20 秒**作为已有退出验收线；新增门禁须确认适用场景。所有正式样本均需自然回收、明确退出码 0、无遗留工作进程。NULL 退出码、仅窗口消失、测试 JSON 的 `passed=true` 均不足以通过。
5. 回归必须先在旧行为失败，再在修复后通过；同时保留行为正确性断言，不能靠缩小输入、关闭无障碍或延长超时让失败消失。
6. 记录运行版本、二进制哈希、启动环境、PID/路径/启动时间、服务停止与 QML 销毁耗时、错误及是否有调试器介入。调试器暂停期间不算纯性能基线。
7. 仅终止已核对身份的异常测试 PID；明确标注人工清理和退出码，保留证据，不称自然退出。

本规则已在 `AGENTS.md` 与 `CLAUDE.md` 引用。2026-09-08 已加入 `accessible_scrollbar_lifecycle`、`application_exit` 和 `qt_scrollbar_runtime_guard` 自动门禁，以及 Qt 6.4.3 依赖补丁的版本/配置/哈希校验；详见 [APP-001 修复与证据](app001-scrollbar-exit-fix-2026-09-08.md)。当前没有 Qt 6.8 运行环境或构建验收结果，不能将这些门禁在 6.4.3 的通过外推为 6.8 已通过。

### R7：依赖修复必须随构建交付

- 禁止只在某个开发目录手工换 DLL 后关闭问题；补丁必须有源码定位、受控生成配方、原件哈希、构建配置和测试证据。当前 6.4.3 的 [独立构建配方](../cmake/qt-scrollbar-fix/README.md)不修改全局 SDK。
- 应用及相关 GUI 测试构建前校验实际部署 DLL，缺库、错版本、Debug/Release 混用或哈希变化必须失败，不静默回落到旧库。清理生成目录后先重建受控依赖。
- 外部打包工具可能重新覆盖 Qt DLL；必须在最终部署产物上再执行校验和退出回归。构建目录通过不等于任意安装包通过。
- 升级到精确 Qt 6.8.x 时重新检查官方对应源码、移植或移除补丁，并重跑相同行为/退出用例；禁止跨版本复用二进制修复库。`UniqueConnection` 只用于可比较的成员函数连接，不把它当 lambda 的通用去重开关。

## R8：Qt 6.11.2 实测补充（2026-09-10）

- Qt 6.11 仍提供 `QJsonArray`；遇到不完整类型或找不到声明时，先为直接使用者补 `<QJsonArray>`，不得误写成 API 已移除，也不得继续依赖传递包含。
- Qt 6.11.2 官方 `qquickscrollbar.cpp` 仍未消除 APP-001 的重复成员函数连接风险。6.11.2 必须使用本版本独立生成的修复库和清单；6.4.3 DLL 禁止复制到 6.11.2。
- QTP0004 NEW 会为包含 QML 的子目录生成额外 `qmldir`。当前 PPTrader 的相对 `components` 导入与该结构存在歧义，故 6.8+ 暂设 OLD；这只是一项有弃用警告的迁移债务，不是长期关闭新策略。切换 NEW 前必须先规范模块 URI、目录与导入，并在 6.4.3/6.11.2 分别启动真实 Main。
- 仅由相邻 QML 显式导入的 JavaScript 文件应设置 `QT_QML_SKIP_QMLDIR_ENTRY`，避免同时作为模块成员登记；修改后同时跑 qmlcache 构建、`all_qmllint` 和真实 Main。
- QML/C++ 边界的结构化值在不同 Qt 补丁版本可能表现为 `QJSValue` 或 `QVariantMap/QVariantList`。生产代码应在边界规范化；测试不得只因表示类型变化就判定业务字段丢失。
- ListView 的刷新保位不能只依赖 `movementEnded`，也不能用会被大量 delegate 布局事件无限重启的零延时 Timer。位置存放在稳定父对象中，模型重排阶段阻止内部归零覆盖，真实滚轮/拖动/滚动条操作解除保护并更新位置。
- QProcess 启动 GUI 子进程时显式继承当前 Qt 的 `QLibraryInfo::BinariesPath` 与插件目录。无窗口退出测试和 Windows 原生无障碍测试分开运行；禁止对完整 CTest 全局覆盖 `QT_QPA_PLATFORM=offscreen`。

Qt 6.11.2 的实际双配置结果、运行库哈希和剩余边界见 [适配报告](qt611-adaptation-2026-09-10.md)。

## R9：跨时段验收不得依赖临时宿主长驻子进程

- Codex/IDE/CI 启动的 PowerShell、Python 或应用子进程可能受宿主 Job、会话租约或维护回收影响；不得用“提前数小时启动后休眠”承担开盘调度。
- 跨夜或跨数小时的启动由操作系统持久调度器负责。注册动作应短命，实际编排器只允许在目标窗口前最多 10 分钟启动，并写周期租约供观察者判断停滞。
- 任务必须固定精确应用路径、二进制哈希、Qt 根目录、目标交易日、证据目录和单实例策略。应用启动后再核对实际加载的 QtCore/插件版本，不能只相信 PATH。
- 文件心跳或 Codex 自动跟进只负责观察、报告和收尾，不承担真实网络任务的首次启动。真实网络权限仍按仓库规则单独显式批准，不能因使用任务计划而推定已经继承。
- 错过门禁截止后不得自动追赶或补造完整交易窗口；应保留状态、租约和错误，标为未覆盖。详细事故边界见 [2026-09-10 调度复盘](acceptance-orchestrator-postmortem-2026-09-10.md)。
