# APP-001：重复无障碍激活导致的退出延迟修复

日期：2026-09-08。范围：Qt 6.4.3 Windows / PPTrader真实向导正常退出；不是Qt6.8升级，也不代表真实行情或策略收益验收。

## 已取得的因果证据

1. 原始库中，单个QML滚动条经过原生Windows无障碍激活，再重复通知10次，一次increase动作将position从0.2推进到0.75，预期只有0.25。最小动作回归在约0.29秒内失败。
2. 同一未修复库下，真实Main向导经历10,000次重复激活，关闭后仍超过20秒未退出。测试PID21984被所属QProcess按失败路径清理，不能记作自然退出。
3. 保持应用二进制及测试输入不变，只替换应用生成目录中的同版本受控QuickTemplates2库：单次动作恢复一个步长；真实向导关闭后的等待降至594ms，正常进程退出码0。该首轮父测试日志在进程结束后读取PID，显示0；它不是应用PID，后续已将测试改为启动时保存PID。

证据：`artifacts/app001-exit-2026-09-08/scrollbar-native-red.xml`、`scrollbar-native-green.xml`、`wizard-exit-red.xml`、`wizard-exit-green.xml`。首轮green为修改测试矩阵前的同一测试二进制，因此可以和red直接对照；更完整矩阵、离线取消和原生界面复验在后续段记录。

## 根因与修复位置

Qt的Windows无障碍入口重复转发active=true；`QQuickScrollBar::accessibilityActiveChanged(true)` 每次重复连接increase/decrease动作。原生界面的可访问树读取会产生多次底层请求，连接数量随激活累积。销毁时Qt Core Debug连接链表校验放大了清理耗时。

修复只给这两个成员函数连接增加`Qt::UniqueConnection`，保留false分支的精准断开。未关闭辅助技术、未强制结束正式成功样本、未跳过析构、未修改策略或行情算法。最小动作行为和复杂向导退出均有红→绿证据，排除了“只是后台服务停止慢”作为本轮主因。

## 依赖与防复发

- 全局Qt安装与源码保持只读。已备份应用目录原始Debug DLL为 `artifacts/app001-exit-2026-09-08/Qt6QuickTemplates2d.original.dll`。
- [独立构建配方](../cmake/qt-scrollbar-fix/README.md)校验已审计原始源文件SHA-256，复制到独立out目录，只改两个connect；生成DLL及补丁身份/配置/哈希清单。
- [应用构建保护](../cmake/QtScrollBarRuntime.cmake)默认保护Qt6.4.3 Windows构建。运行库缺失、版本/配置/哈希不符会使构建失败，而不是悄悄退回旧Qt库。外部打包覆盖DLL后仍须重新校验与验收。
- Qt6.8.0源码此前仍见同类连接形态，但不能据此推断所有6.8.x都未修复；升级必须在准确补丁版本重跑回归。本次不将6.4.3 DLL装入6.8环境。
- [Qt6.8编码与退出规则](qt68-coding-rules.md)继续要求连接幂等、正常无障碍兼容、取消协议、真实进程退出及退出码验收。

## Debug 自动门禁与原生界面复验

下表为首轮完整矩阵；关闭耗时是父测试观察到 `native-closed` 后等待进程结束的时间（轮询分辨率50ms），与启动/重复激活耗时分开。每个成功样本均正常退出、退出码0，未强制终止。

| 场景 | PID | 关闭后等待 | QML引擎析构 | 结果 |
| --- | --- | --- | --- | --- |
| 普通关闭 | 23756 | 653ms | 154ms | 通过 |
| 重复激活10,000次 | 46748 | 581ms | 151ms | 通过 |
| 10,000次激活＋真实离线任务运行中取消 | 19028 | 551ms | 159ms | 通过，进度0.00833333时确认取消 |

取消样本使用隔离库的120个合成原始Tick及对应1m派生输入，真实 `TradingController`→`sma_crossover`→异步执行。先观察运行进度介于0和1，再排队取消，要求状态“已取消”、无错误、停止后正常关闭；不是尚未启动就取消，也不是大数据长稳/收益验收。排队回调带局部QObject context，异常离开作用域后不会访问已释放的局部变量。

`accessible_scrollbar_lifecycle`覆盖0/10/1,000次重复激活、increase/decrease和false→true再次激活，单次动作步长均正确。非Windows明确跳过该原生入口，不伪造跨平台兼容。`qt_scrollbar_runtime_guard`在独立合成库目录覆盖缺库、缺清单、错误版本/配置/补丁身份/哈希拒绝、正确复制及幂等重复部署，不修改实际应用库。

原生Computer Use复验：PID25464，真实Main的1120×760窗口（截图边界1121×761，wizard宽726）。5次纯无障碍树读取之后，上一步4→3、查看预览→返回编辑，保留4指标/3条件，最后Alt+F4。03:09:10.431接受关闭，03:09:11.486前父进程观察到退出，约1055ms；Controller停止47ms、QML析构178ms，退出码0、stderr为0字节，窗口列表和精确PID均确认已消失。未关闭无障碍、未附加调试器。

该界面样本没有预览输入，信号预览按钮提示先选择本地历史Tick派生数据，组合预览显示空态；不把它算作实际信号或策略收益通过。纯树读取5次不等于原生激活5次，真实底层事件次数未知。截图保留在本会话工具输出，未声称另存了图片文件。

机器证据：`scrollbar-matrix-debug.xml`、`exit-matrix-debug.xml`、`computer-use-fixed.json`、`.json.log`、`.stdout.log`、`.stderr.log`；人工观察及父进程回执整理为 `computer-use-fixed-observation.json`，均位于本报告证据目录。

## 构建和全量回归

1. 独立Qt库Debug和RelWithDebInfo构建均退出0；后者提供Release系列DLL。应用Release及两个退出目标构建退出0。
2. Release定向CTest：`accessible_scrollbar_lifecycle`、`application_exit`、`qt_scrollbar_runtime_guard`、`trading_controller_wizard` **4/4通过，21.23秒**。这是本轮实际运行，不引用旧Release结果。
3. 最终Debug全量构建退出0；完整CTest **52/52通过，139.32秒**；`all_qmllint`退出0。证据：`build-debug-full.log`、`ctest-debug-full.log/.xml`、`qmllint-debug.log`；另一次相同lint的标准输出留存于 `qmllint-debug-captured.log`，同样退出0。
4. Release额外XML矩阵：普通/10,000次激活/离线运行中取消后关闭分别 **403/235/343ms**，均自然退出码0；PID分别23756/38864/46900。这个Release的23756是03:20:44新建的进程，与03:03的Debug同号PID是系统复用，不能混为同一个实例。机器明细见 `scrollbar-matrix-release.xml`、`exit-matrix-release.xml`。父测试PID45300的直接子进程23756经路径/启动时间核验，实际加载 `out/build/vs2022-x64/Release/Qt6QuickTemplates2.dll`，路径回执见 `release-runtime-observation.json`。

本机原Qt SDK由MSVC2019构建，PPTrader与本次独立依赖使用VS2022工具链、x64；两种配置均实际加载并执行回归，不能将“文件名相同”当作兼容证明。全局SDK的Debug DLL与原始ScrollBar源码SHA-256核对未变。部署库和应用的SHA-256如下：

| 文件 | SHA-256 |
| --- | --- |
| 全局原始Qt Debug DLL | `8886cd6ba7e12470ad59c1171c9c158148d0f9851ce453ef81763ad5ce8a6b00` |
| 全局原始ScrollBar源码 | `17797d8bcfc797ec5bf45d16fa64ce917a5459c5ac89ec411c8be8b665925b3e` |
| 应用局部修复Debug DLL | `2f539572ff84c83197961f6386e9920a49884159b6ab33f043518641dad1e0ba` |
| 应用局部修复Release DLL | `a1117a4e2602c73f0b4632e369482347eadd957b7a410cff85e06504ee348f1a` |
| 最终Debug PPTrader.exe | `70d55a3474881ceef4ae0627bee3a24e6f40104e6a6ce077c0d2132a0d4fd035` |
| Release PPTrader.exe | `a4946d99cb74a287faab2158402867d446deac13d9b000810a3a5028f8c0ba57` |

首轮红/绿应用与测试二进制相同；随后新增取消/矩阵测试和最终完整构建使应用测试入口二进制发生变化，不能把后者哈希冒充首轮对照哈希。修复库未变。原Debug/Release应用局部库备份分别为 `Qt6QuickTemplates2d.original.dll` / `Qt6QuickTemplates2.release-original.dll`（证据目录），全局安装未覆盖。

收尾再次核验工作区Debug/Release PPTrader进程数为0。成功样本没有超时清理；本轮唯一明确失败清理是原始库红测PID21984，已在前文标明。依赖修复、Debug全量及Release定向验收已完成，APP-001本次已复现原因关闭，保留下列适用边界。

## 范围与保留问题

- 本补丁解决有红→绿证据的ScrollBar重复动作连接及本次向导慢退出；早前本地查询积压退出是另一已修复问题，旧失败记录不删除，不将未知历史永久残留都强行归为同一个根因。
- APP-003持续UIA客户端的COM包装对象/长时间内存增长没有在本轮重新做长稳测量，继续独立开放，不因退出变快就宣称已修复。
- Qt仍为6.4.3。Qt6.8规则是未来升级和编码约束，尚无6.8二进制运行验收。
- 本轮不访问NAS、行情网络、用户数据库或下单接口，不改变业务策略/行情算法。默认构建保护仅涵盖本工程构建产物，外部安装包必须单独校验实际库。
