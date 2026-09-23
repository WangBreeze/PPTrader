# Qt 6.11.2 适配与验收（2026-09-10）

## 结论

PPTrader 已在独立的 Qt 6.11.2 / MSVC 2022 x64 构建目录完成 Debug 全量回归和 Release 关键路径验收。结果只证明当前源码与本机 Qt 6.11.2 SDK 的兼容性；项目公共最低版本仍是 Qt 6.4，旧 Qt 6.4.3 Release 与 2026-09-10 盘中编排器未被替换。

- Debug：完整 CTest **54/54**，总耗时 168.33 秒；`all_qmllint` 目标成功。
- Release：应用构建成功；`accessible_scrollbar_lifecycle`、`application_exit`、`qt_scrollbar_runtime_guard`、`paper_strategy_controller` **4/4**。
- 真实 Main：Debug/Release 的契约探针都加载编译进资源的 `Main.qml`，不是只检查单个 QML 文件。
- 退出：普通关闭、重复无障碍激活、离线回测取消后关闭均自然回收，退出码 0；Release 三场景合计 8.02 秒。
- APP-001：Qt 6.11.2 官方 `qquickscrollbar.cpp` 仍需本项目的成员函数 `Qt::UniqueConnection` 修复；没有假定升级自动解决，也没有禁用无障碍。

## 兼容修复

### C++ / Qt Core

Qt 6.11 仍然提供 `QJsonArray`。本次问题不是类型被删除，而是部分源文件依赖其他头文件的传递包含。已在实际构造、遍历或返回 `QJsonArray` 的编译单元显式包含 `<QJsonArray>`，避免不同 Qt 版本调整包含关系后出现不完整类型错误。

旧版单次留出法参数敏感度归档允许没有外层折记录；新版嵌套 walk-forward 必须具有精确折数。本次恢复逻辑按 `analysisMode` 区分两类格式，并兼容旧归档中只在汇总字段保留的胜负次数，避免升级后的完整数据库测试误拒绝合法旧记录。

### QML / QML 模块

- 修复 `CustomStrategyWizard.qml` 对词法 `seriesModel` 的错误限定访问，并在 QML 边界将 `QDateTime` 规范化为 JavaScript `Date`。
- 两个只由相邻 QML 显式导入的 JavaScript 契约文件设置 `QT_QML_SKIP_QMLDIR_ENTRY`，不再作为模块级脚本重复登记。
- Qt 6.8 引入的 QTP0004 在 NEW 行为下会给子目录生成额外 `qmldir`；当前源码仍保留 `Main.qml` 对 `components` 的相对导入，因此暂时显式设为 OLD，保证 Qt 6.4/6.11 同一源码树可构建。配置时的弃用警告是已知迁移债务，后续应扁平化/规范化 QML 模块后切换为 NEW，不能靠隐藏警告关闭。
- 成交列表不再依赖 Qt 6.11 下不稳定的滚动结束通知。页面根对象按运行 ID 保存位置；ListView 模型重排时同步抵抗内部 `contentY=0`，用户下一次滚轮、拖动或滚动条操作时解除刷新保护。真实 Controller/Main 探针覆盖 100 条显示窗口、超过 100 笔成交、实际滚轮以及连续运行数据刷新。
- 删除多窗口创建后冗余的 `show()`；悬停状态使用 `containsMouse`，避免 Qt 6.11 的属性诊断。

### 测试编排

Qt 6.11 的测试子进程不能假定从 CTest/QProcess 自动继承正确的 Qt 运行库搜索路径。`application_exit` 现在显式把当前 `QLibraryInfo::BinariesPath` 放到子进程 PATH，并设置对应插件目录；测试使用 offscreen 平台隔离桌面状态。Windows 原生平台的无障碍滚动行为由独立 `accessible_scrollbar_lifecycle` 覆盖，不能给整套 CTest 全局强制 offscreen。

向导契约探针同时接受 Qt 6.11 暴露的 `QJSValue` 和直接 `QVariantMap`，避免测试把正确的 `workflowReplayData` 误判为空。QML 向导测试通过真实控件选择非法右操作数，并等待 Flickable 稳定后再检查可见性，减少平台布局时序造成的假失败。

## APP-001 受控运行库

QtDeclarative 6.11.2 源文件 SHA-256：

`64b4c06235f32c52857b570862289dc3ef95aeaa138563d1f3de70dc30e5340d`

| 配置 | 文件 | 大小 | SHA-256 |
| --- | --- | ---: | --- |
| Debug | `Qt6QuickTemplates2d.dll` | 6,173,184 | `22DB66288A6E9E9B95AFEC711B533C2851DB7448ADF5AF9DCF948C4F4CCAE703` |
| RelWithDebInfo（供 Release 系列） | `Qt6QuickTemplates2.dll` | 2,042,368 | `1E0B97BC3509361245178B36F5F5032A3787799CB469CDD1D506064CA242E4E5` |

两者文件版本均为 6.11.2.0，清单记录精确 Qt 版本、补丁身份、配置、源文件哈希和运行库哈希。应用构建会再次校验并复制，缺失或跨版本/跨配置混用时直接失败。生成配方见 `cmake/qt-scrollbar-fix/README.md`。

隔离 Qt 6.11 Release 应用：

- 路径：`out/build/vs2022-qt611/Release/PPTrader.exe`
- 大小：10,309,120 字节
- SHA-256：`F28FB2C2D93215A7A86E21F32B92D3180E62206CB53DA3F247E5ADB70DC08F5D`

## 尚未完成与边界

1. QTP0004 OLD 只是跨版本过渡措施；QML 模块目录现代化尚未完成。
2. 当前公共 preset 仍指向 Qt 6.4.3；未将仓库最低版本或默认开发环境改为 Qt 6.11。
3. 本轮没有访问 NAS、外网、真实账户或实时事实表，不代表盘中行情、策略盈利、真实下单或安装包验收。
4. 构建目录不是可分发安装包；正式打包后仍须重新核对实际 DLL 哈希并重跑无障碍和退出测试。
5. `Qt.labs.settings` 的跨最低版本迁移未在本轮强行实施；在最低版本仍为 6.4 时，必须先设计兼容方案并用两套 Qt 实际验证，不能只为消除新版本提示破坏旧版本。

## 2026-09-13 调度修复后复验

盘中验收调度改造后重新执行 Qt 6.11.2 Release 全量构建，应用 SHA-256 仍为 `F28FB2C2D93215A7A86E21F32B92D3180E62206CB53DA3F247E5ADB70DC08F5D`。启动、退出、滚动条依赖、模拟策略控制器、交易时段、窗口布局、行情快照、Router 和实时图会话 9/9 通过，`all_qmllint` 通过。Windows PowerShell 5.1 调度回归同时验证了 Qt 6.11.2 固定与实际运行前的版本门禁；本次未联网、未启动盘中任务。
