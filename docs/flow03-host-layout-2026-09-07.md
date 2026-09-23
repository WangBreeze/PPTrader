# FLOW-03 宿主布局收尾（2026-09-07）

2026-09-08 更新：用户确认上轮曾手动调整窗口，不能以本报告的尺寸矛盾推定程序自行改变布局。电脑空闲后的同一构建已完成指定小窗口预览、返回编辑和上一/下一步原生操作；退出残留仍复现，详见[小窗口补验](wizard-small-window-reacceptance-2026-09-08.md)。本报告保留原始失败和清理事实，不改写旧截图。

## 范围与基线

本轮承接[向导修复记录](wizard-flow-fixes-2026-09-07.md)，只处理实际 Main → StrategyResearchPage → CustomStrategyWorkbench → Wizard 的布局与状态隔离。归档请求日期由主线单独处理，不在这里修改 SQLite、归档类型或 NAS preparation。

此前 1120×760 副屏截图已经确认：610px 固定卡片使底栏落在可见视口外；底层 JSON RowLayout 仍可见，其最小宽度超过宿主，向导右缘透出 JSON 内容。单独给 Wizard 分配 724px 的几何测试无法发现外层问题。

本轮测试改为真实 TradingController 和完整 Main：三种窗口尺寸 1120×760、1280×1000、1600×1000，检查 footer 的场景坐标不超过页面视口，JSON 层在向导模式不可见，返回 JSON/再次进入向导后保留草稿及步骤。仅测试构建提供固定仓库路径的 `--source-qml`，用于在同一二进制上验证源码 QML 的红/绿；默认 CTest 仍加载编译资源。

## 退出诊断口径修正

旧可见向导探针使用局部事件循环，先销毁 QML 引擎、再调用 Controller shutdown；生产 main 则先在 aboutToQuit 停止服务，事件循环返回后才销毁引擎。因此此前 11–35 秒只证明旧探针下存在销毁延迟，不能据此解释生产 APP-001 或排除生产 worker 顺序问题。

本轮只校正测试入口，使用应用事件循环及与 main 一致的隐藏→排队 quit→aboutToQuit shutdown→引擎销毁次序；生产生命周期不变。后续采样使用本机 Qt PDB，不下载符号、不访问网络或 NAS。保留以前所有失败/中断证据，不把调试器干预算作自然退出。

所有运行使用独立临时库、INI 设置和副屏，不修改用户草稿、既有窗口布局或原验收库。构建与桌面验收串行，结果在下方追加，不提前宣称通过。

## 宿主红绿与最小修复

`trading_controller_wizard` 宿主红测在 11.16 秒内失败，精确指向 1120×760 footer 超过页面视口，以及三种尺寸中 JSON 层仍可见。其余归档/回放契约通过，见 `artifacts/flow03-host-red.json`、`flow03-host-red-ctest.txt`。

- 自定义策略卡片只在向导模式按页面剩余高度收缩，上限保留 610px；进入向导将外层滚动位置回到顶部。内部条件和预览仍由自身滚动区承载。
- 向导模式隐藏 JSON RowLayout 和旧模式标题，限制向导内容在工作台边界内；退出时恢复原 JSON 层，两个常驻实例及编辑内容不销毁。
- 没有修改 Main 的导航、窗口几何存储、策略算法或 NAS 接入。

同一二进制显式加载仓库源码 QML 的可见副屏对照通过（退出码 0，`artifacts/flow03-host-source-final.json`），覆盖三尺寸的 footer/右缘边界与模式切换保留状态。最初两次源码运行使用 Hidden 启动，窗口未呈现、布局尚未完成，得到 width=-36；此结果属于启动方式造成的无效几何，不是业务布局根因。改为可见启动后通过，并撤掉试探性的额外 preferredWidth。新探针增加 isExposed 和每尺寸可见/正宽门禁，防止负宽使边界检查假通过。

## 本轮最终自动验证

- `cmake --build --preset vs2022-debug --parallel 1`：完整 Debug 构建退出 0。工具会话 11495，最终回执 chunk `bd51af`；没有另存本轮构建日志，不引用其他时段旧日志。
- `ctest --preset vs2022-debug --output-on-failure --output-log artifacts/flow03-final-ctest.txt`：**49/49，113.86 秒**。其中真实 Controller/Main 6.95 秒、SQLite 3.21 秒、Wizard QML 14.69 秒。
- `cmake --build --preset vs2022-debug --target all_qmllint --parallel 1`：退出 0，工具回执 chunk `aa0e56`，无新增 QML 警告。
- `artifacts/flow03-controller-final.json`：100 项检查均通过，包括四类日期从新归档传入验证记录、旧未知日期拒绝保存且计数不变、SQL 故障反馈、重开恢复 4 条和实际 Main 三尺寸布局。探针阶段日志为追加文件，只有末段对应这次最终 CTest；未进行桌面可访问树交互的该段 shutdown 8ms、引擎销毁 178ms。

上述结果不替代以下真实原生验收，也不覆盖历史红测索引。归档存储由协调主线补齐 schema v25，本任务只增加 Controller 消费端门禁；见 [完整归档日期语义](backtest-archive-sample-window-2026-09-07.md)。旧未知范围必须明确选择并重新回测，不能用当前编辑日期冒充历史请求范围。

## 副屏原生验收：未通过，不扩大修复

最终编译资源以 `--wizard-contract-probe … --visible --visual-width 1120` 启动，独立测试 PID **40480**，启动于本地 23:27:23。唯一隔离窗口的桌面返回标识为 **33622258**，标题为“PPTrader · 隔离向导验收”。准备阶段日志是逻辑宽 1120、向导宽 726，但随后桌面捕获为 **1831×1103**、`originX=-2561`，画面已是三栏展开。这个尺寸与启动记录矛盾，不能标成 1120×760 通过，也不能在缺少同时刻几何证据时强行按 DPI 换算。

实际截图：[尺寸不匹配证据](../artifacts/flow03-native-size-mismatch.png)。大画面中 footer 可见且无 JSON 透出，但原生拖拽没有确认改变尺寸，预览切换、滚动和最小窗口底部按钮实点仍未通过。原生系统菜单能够打开，关闭使用 Alt+F4；没有修改用户窗口设置、没有把失败截图缩放伪装成目标尺寸。

UTC 阶段日志 `artifacts/flow03-native-final-1120.json.log`：15:30:56.073 `native-closed`；15:30:56.537–.591 Controller shutdown **54ms**；15:30:56.604 `native-engine-destruct-begin`。23:32:35 本地检查仍无 destroyed，PID 尚在、MainWindowHandle=0、累计 CPU 141.66 秒。正确生命周期下仍出现原生关闭残留，不能用上面的 CTest 178ms 替代。

仅对本轮 PID 使用本机 Qt PDB、非侵入 `cdb -pv` 采一次暂停栈，未下载符号：`artifacts/flow03-native-1120-local-symbols.txt`。主线程在 `QObjectPrivate::ConnectionData::removeConnection → QObject::~QObject → QQuickScrollBar / QQuickScrollBarAttached → QQuickFlickable → QQuickStackLayout → QQmlApplicationEngine::cleanUp`。这是可靠的当时调用位置，不足以证明连接数量、UIA 或某个 Qt 缺陷就是最终根因；暂停采样也影响墙钟时间。没有根据单栈改生产生命周期。

按协调范围停止新增 GUI/探针改动，不并行启动其他实例。FLOW-03 状态为“代码与真实宿主自动回归通过，原生最小窗口操作未完成”；APP-001 继续开放。进程最终处置单独补记，任何强制释放都不计自然退出通过。

最终处置：原生关闭后约 **249 秒**仍未完成析构。获得精确清理授权后，再次核对 PID 40480、程序绝对路径、启动时间 `2026-09-07T23:27:23.1189907+08:00`、MainWindowHandle=0，于 **23:35:05** 仅手动终止此测试进程并确认消失。启动会话返回目标退出码 **-1**，不是正常退出；没有完整可见探针 JSON 成功报告。阶段日志、符号栈、截图和隔离目录保留，未清理其他进程或用户资料。此后不再启动 GUI 或修改代码。

## 修改范围与交接检查

本轮生产改动限于 `qml/components/CustomStrategyWorkbench.qml`、`qml/components/StrategyResearchPage.qml` 的宿主布局，以及 `src/app/TradingController.cpp` 的旧未知日期保存保护。测试改动为 `tests/TradingControllerWizardProbe.cpp` 及 `CMakeLists.txt` 的固定测试源码路径；既有测试入口头文件、main 接线和 Wizard 组件鼠标测试来自前轮，不重复视为本轮新增业务能力。SQLite/domain 的 v25 改动由协调主线负责。

文档更新本报告、`development-progress.md`、`known-issues.md`、`wizard-flow-fixes-2026-09-07.md`；其余归档报告、Wiki、路线图由协调主线同步。最后定向 `git diff --check` 返回 0；四份文档本地 Markdown 链接检查缺失 0。只读进程检查 PPTrader/MSBuild/link/cdb 均为 0，构建与桌面资源已释放，等待审核。待办是原生最小尺寸实点及 APP-001 后续有证据的定位，不扩大 NAS/回测业务范围。
