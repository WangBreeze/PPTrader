# Qt 多版本工具链与默认版本迁移（2026-09-24）

## 结论

PPTrader 的**默认工具链已改为 Qt 6.11.2**（Windows MSVC 2022 x64 / Linux Ninja），同时保留
**Qt 6.8.3** 与遗留 **Qt 6.4.3** 作为项目内可选工具链。三者互不干扰：Qt 版本与前缀由项目自己
解析，不再依赖全局环境变量；解析到的次版本与请求不符时**直接配置失败**。

本机（Arch Linux）的全局环境已改为指向发行版 Qt 6.11.2，Qt 6.8.3 只存在于项目 preset 中。

Windows 侧 preset 已按同样的矩阵提供，但**本轮只在 Linux 上实际构建与验收**；Windows 未运行，
不得据此宣称 Windows 结果。

## 全局环境：系统级用 Qt 6.11.2，6.8.3 退出全局

改动文件：`~/.bashrc`（机器本地，不在仓库内）。原来整块把 Qt 6.8.3 注入全局：

```bash
export QTDIR=$HOME/ProgramData/Qt/6.8.3/gcc_64
export Qt6_DIR=$QTDIR
export QT_DIR=$QTDIR
export LD_LIBRARY_PATH=$QT_DIR/lib:$LD_LIBRARY_PATH
export QML2_IMPORT_PATH=$QT_DIR/qml
export QML_IMPORT_PATH=$QT_DIR/qml
export PATH=$QT_DIR/bin/:$PATH
```

改为只声明系统级 Qt 6.11.2，并主动清掉父进程可能带进来的 6.8.3 条目：

```bash
export QTDIR=/usr
export Qt6_DIR=$QTDIR
export QT_DIR=$QTDIR
# PATH / LD_LIBRARY_PATH / QML_IMPORT_PATH 不再导出 6.8.3
```

为什么连 `QML_IMPORT_PATH` 也去掉，而不是改成 `/usr/lib/qt6/qml`：

- 系统 Qt 自带 `QT_INSTALL_QML`，不需要 `QML_IMPORT_PATH` 就能解析自己的 QML 模块。
- 这个变量是版本化的。若全局固定成某一版本的 qml 目录，另一版本 Qt 的程序会优先在那里找到
  不同次版本的 QML 插件与 `qmldir`，属于典型的跨版本混装。
- 同理不导出 `LD_LIBRARY_PATH`：系统 Qt 就在默认库搜索路径 `/usr/lib`，而 `LD_LIBRARY_PATH`
  优先级高于可执行文件的 `RUNPATH`，一旦残留就会让 6.11.2 程序加载 6.8.3 的运行库。

**这个冲突是真实发生过的**，不是理论风险：在本次改动生效前的构建里，用系统 Qt 6.11.2 配置项目
直接失败，原因就是环境里残留的 `LD_LIBRARY_PATH` 让 6.11.2 的 `qmlimportscanner` 加载了 6.8.3 的
`libQt6Core`：

```text
/usr/lib/qt6/qmlimportscanner: /home/wanglc/ProgramData/Qt/6.8.3/gcc_64/lib/libQt6Core.so.6:
version `Qt_6.11' not found (required by /usr/lib/qt6/qmlimportscanner)
```

改动后重新配置即通过。注意：**已经运行的进程不会自动获得新环境**，需要新开终端（或重启会话）
才会生效。

## 项目侧：由项目自己解析并锁定 Qt

新增 `cmake/PpQtToolchain.cmake`，在 `find_package(Qt6 ...)` 之前 include，并在之后调用
`pptrader_verify_qt_version()`。解析顺序：

1. `-DPPTRADER_QT_ROOT=<前缀>`
2. 调用方显式给出的 `-DCMAKE_PREFIX_PATH` / `-DQt6_DIR`
3. 按 `-DPPTRADER_QT_VERSION`（默认 `6.11.2`）在平台约定位置查找

平台约定位置：

| 平台 | 候选（按顺序） |
| --- | --- |
| Windows | `D:/ProgramData/Qt/<版本>/msvc2022_64`、`D:/ProgramData/Qt/<版本>/msvc2019_64`、`C:/Qt/<版本>/msvc2022_64`、`C:/Qt/<版本>/msvc2019_64`、`%USERPROFILE%/ProgramData/Qt/<版本>/msvc2022_64` |
| Linux | `$HOME/ProgramData/Qt/<版本>/gcc_64`、`/opt/Qt/<版本>/gcc_64`、`/usr` |

`msvc2022_64` 与 `msvc2019_64` 都列出，是因为 Qt 6.8 起安装器改过 MSVC 套件名。`/usr` 放在最后，
只有当它的 Qt 版本与请求版本完全一致时才会被选中，不会抢走项目指定的 6.8.3。

版本校验规则：与 `PPTRADER_QT_VERSION` 完全一致 → 通过；**次版本号不同 → `FATAL_ERROR`**；
仅补丁号不同 → 警告并提示按实际版本重新取证（APP-001 一类结论不能跨补丁版本套用）。

因此下面两条命令在同一台机器上互不影响，且都不需要改全局环境变量：

```sh
cmake --preset linux-qt611-debug   # 解析到 /usr        → Qt 6.11.2
cmake --preset linux-qt683-debug   # 解析到 ~/ProgramData/Qt/6.8.3/gcc_64
```

## Preset 矩阵

| 平台 | 默认 6.11.2 | 可选 6.8.3 | 遗留 6.4.3 |
| --- | --- | --- | --- |
| Windows | `vs2022-qt611` | `vs2022-qt683` | `vs2022-qt643`（`vs2022-x64` 为历史别名） |
| Linux | `linux-qt611-debug` / `linux-qt611-release` | `linux-qt683-debug` / `linux-qt683-release` | 未新增，按需用 `-DPPTRADER_QT_VERSION=6.4.3` |

`vs2022-x64` / `vs2022-debug` / `vs2022-release` 保留为 Qt 6.4.3 的历史别名，因为大量既有日期文档
与脚本引用它们；`windows-qt611` 用 `vs2022-qt611` 命名是为了让构建目录与既有 Qt 6.11.2 适配目录
`out/build/vs2022-qt611` 保持一致。

Windows 上 Qt 6.11.2 默认仍要求 APP-001 受控滚动条运行库（`PPTRADER_REQUIRE_QT_SCROLLBAR_FIX`
的既有默认值），Linux 上该选项按原逻辑关闭并打印未启用告警。

## 迁移中发现并修复的缺陷

### 1. “最新一条”排序不确定（产品代码，已修）

`src/services/SqliteLocalDataStore.cpp` 有 7 处形如

```sql
order by imported_at_utc desc, dataset_id desc
```

的“取最新记录”查询。并列时间戳用**随机 id** 兜底，语义上是错的：同一毫秒内导入的两条记录
时间戳完全相同，按 UUID 排序得到的是任意顺序，`history.first()` 会随机变成旧记录。

已把 7 处的兜底键统一从随机 id 改为 `rowid`（插入顺序）：

| 行 | 表 | 原兜底键 |
| --- | --- | --- |
| 2699 | `ashare_instrument_day_import_runs` | `run_id` |
| 2941 / 2967 | `ashare_industry_datasets` | `dataset_id` |
| 3482 | `factor_candidate_evaluations` | `evaluation_id` |
| 4286 | `tickdata_import_runs` | `run_id` |
| 5302 | `portfolio_backtest_results` | `result_id` |
| 6110 | `backtest_results` | `result_id` |

时间戳仍是主排序键，只有并列时才改由插入顺序决定，因此不会改变原有“越新越靠前”的语义。

证据：修复前 `tst_ashare_industry_import` 单测重复运行 **Qt 6.8.3 通过 5/8、Qt 6.11.2 通过 7/8**；
修复后两套 Qt 各跑 12 次 **12/12 通过**。这个缺陷与 Qt 版本无关，是既有问题，只是它会让任何
“全绿”结论不可信，所以在多版本验收前必须先修掉。

### 2. 两套构建目录的测试不能并发跑（已知约束）

同时运行 `linux-qt611-debug` 与 `linux-qt683-debug` 的 CTest 会出现失败，且**失败用例每次不同**
（实测出现过 `paper_strategy_controller`、`custom_strategy_wizard_qml`、`ashare_industry_import`）。
分别串行运行则稳定全绿。已确认不是某一个 Qt 版本的问题。本轮未定位到确切的共享资源，但结论明确：
**多版本验收必须串行执行**，本文所有数字都来自串行运行。

### 3. 发行版 Qt 的诊断日志默认不进 stderr（本机环境，重要）

在本机用发行版 Qt 6.11.2 运行时，Qt/QML 的诊断输出**不出现在 stderr**，原因是它被路由到了
systemd journal。实测同一份程序：

| 运行方式 | Qt 日志行数 |
| --- | --- |
| Qt 6.11.2（系统 Qt），默认 | **0**（stderr 只有 7 行 MESA/bash 噪声） |
| Qt 6.11.2，`QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="*=true"` | 163,545 |
| Qt 6.8.3（Qt 安装器版），默认 | 正常输出：GUI 运行即出现 1 条 QML 弃用告警 |
| Qt 6.8.3，`QT_LOGGING_RULES="*=true"` | 87,056 |

原因是发行版 Qt 被编译/配置为把日志交给 systemd journal（本机进程也确实跑在 `systemd-run` scope 下），
而 Qt 安装器版没有这一路径，所以只有前者受影响。

这直接影响验收可信度：**不加 `QT_FORCE_STDERR_LOGGING=1`，Linux 上看到的“零告警”是假象**。
本轮所有告警计数都在该变量下重新取证。产品代码本轮不改这一行为（是否强制 stderr 属于用户/发行
决策），但 Linux 上的告警类验收必须显式加上它。

### 4. `activeFocusOnTab` 在已聚焦项上被翻转（产品代码，已修）

在实际桌面会话中运行 Qt 6.11.2 构建会打印两条

```text
QQuickItem: Cannot set activeFocusOnTab to false once item is the active focus item.
```

Qt 6.11.2 源码（`qtdeclarative` v6.11.2 `src/quick/items/qquickitem.cpp`）中 `setActiveFocusOnTab`
在 `this == window()->activeFocusItem()` 且新值为 false 时**直接告警并丢弃赋值**。实测确认
`focusPolicy` 走同一处检查，所以**同一个按钮翻一次会打印两条**、且两个属性都不会生效，该按钮会
永远留在 Tab 链里。

根因是组件把该属性直接绑到了可用性上。全仓库共 5 处非字面量绑定（其余 43 处是常量 `true`/`false`）：
`PpButton.qml`（`activeFocusOnTab: available` + `focusPolicy: available ? …`）、
`LocalMinuteSeriesPanel.qml`、`LocalBacktestSelector.qml`（`enabled`）、
`CustomStrategyWorkbench.qml`（`count > 0`）、`TickSeriesChart.qml`（`visible`）。
`PpButton` 有 118 个动态 `available` 绑定，其中大量会因数据刷新自行翻转，因此这条告警在任何一次
真实交互里都可能出现。

修法是保留原语义但不在持有焦点时翻转：`activeFocusOnTab: <可用性> || <该项>.activeFocus`
（`focusPolicy` 同理），焦点离开后再退出 Tab 链。**没有**改用 `enabled: available`：实测
`PpButton` 的 `MouseArea` 与悬浮提示是 `Control` 的直接子项，禁用 `Control` 会连带禁用
`MouseArea`，使“禁用态仍可读 tooltip”这一组件要求失效。

另实测：`enabled` 置 false 时 Qt 会自动清除 `activeFocus`（无告警），且 **disabled 项会被 Tab 链
跳过**（`first -> last`，即使 `activeFocusOnTab` 仍为 true）——这正是推荐用 `enabled` 表达可用性
的依据。

回归测试 `tests/tst_focus_availability_qml.cpp` 用真实 `PpButton` 覆盖该路径，并通过
`QMessageHandler` 捕获告警（因此不受 journald 路由影响）。已验证该测试**有牙齿**：把 `PpButton`
改回旧绑定后，测试恰好复现出同样两条告警并失败。

### 5. `Qt.labs.settings` 弃用告警 + 退役 Qt 6.4.3（已修）

`CustomStrategyWizard.qml` 使用了 `Qt.labs.settings`，在 6.8+ 每次加载都会打印弃用告警。
`Settings` 自 Qt 6.5 起由 `QtCore` 提供，已实测在 6.8.3 与 6.11.2 上都可用（`import QtCore` +
`Settings {}`），因此按用户决定迁移并把**项目最低版本提升到 Qt 6.5**。

随之退役 Qt 6.4.3：`find_package(Qt6 6.5 …)`、`qt_standard_project_setup(REQUIRES 6.5)`，
并从 `CMakePresets.json` 移除 `vs2022-qt643` 及其历史别名 `vs2022-x64` / `vs2022-debug` /
`vs2022-release`。**历史日期文档里的这些命令已不再可用**，这是有意的：源码已不支持 6.4.3，
保留同名 preset 只会让旧文档“看起来还能跑”。APP-001 的 6.4.3 配方与既有证据作为历史记录保留。

## 验收证据（Linux，串行执行）

| 门禁 | Qt 6.11.2（系统 `/usr`） | Qt 6.8.3（项目内 `~/ProgramData/Qt/6.8.3/gcc_64`） |
| --- | --- | --- |
| 配置时版本确认 | `已确认 Qt 6.11.2（/usr/lib/cmake/Qt6）` | `已确认 Qt 6.8.3（…/6.8.3/gcc_64/lib/cmake/Qt6）` |
| 构建（Debug） | 成功 | 成功 |
| 完整 CTest | **56/56**（新增焦点回归后；此前 55/55 为 19.4 s / 19.1 s） | **56/56**（此前 55/55 为 15.8 s / 15.8 s） |
| `all_qmllint` | 通过 | 通过 |
| `--exit-probe` | 退出码 0 | 退出码 0 |
| 真实窗口（Hyprland/Wayland） | 映射成功，标题 `PPTrader · 量化交易工作台` | 同左 |
| `Context2D` 字体告警 | 0 | 0 |
| `Qt.labs.settings` 弃用告警 | 0（已迁移到 `QtCore.Settings`） | 0 |
| `activeFocusOnTab` 告警 | 0 | 0 |
| 任一 QML/Qt 告警（`QT_FORCE_STDERR_LOGGING=1`） | **0** | **0** |

链接与运行库隔离（`ldd` 实测）：

| 二进制 | 大小 | 解析到的 `libQt6Core.so.6` | `RUNPATH` |
| --- | ---: | --- | --- |
| `out/build/linux-qt611-debug/bin/PPTrader` | 148,159,088 | `/usr/lib/libQt6Core.so.6` | 空 |
| `out/build/linux-qt683-debug/bin/PPTrader` | 130,891,928 | `…/ProgramData/Qt/6.8.3/gcc_64/lib/libQt6Core.so.6` | `…/6.8.3/gcc_64/lib` |

SHA-256（焦点修复与 Settings 迁移之后重算）：6.11.2 为
`2571B77308EFD5FC1E90C0957B994A77D14332BAE61140385ABAD5501E377036`，6.8.3 为
`83AACC46BF383990704E98D5433D116283FB78C97B467783BD20B6BD08591CD6`。

### GUI 渲染验证（两套 Qt 各一次）

两套构建都在真实 Hyprland/Wayland 会话中启动并截取窗口区域（各 1282×1493）。证据文件在
[`artifacts/qt-multiversion-2026-09-24/`](../artifacts/qt-multiversion-2026-09-24/)：
`gui-qt611.png`、`gui-qt683.png`、`guiwarn-*.log`、修复前后的 CTest 日志，以及汇总
`evidence.json`（含全部哈希与测量值）。

- 视觉复核（独立视觉模型逐区域检查，含 2–5 倍放大）：完整暗色工作台，顶栏 + 5 个页签 + 4 张卡片
  + 底部状态栏；中文全部正常显示、无 tofu/缺字方块；无文字重叠、裁切、越界或错误文本；实时 K 线
  真的画出了红绿蜡烛、均线与成交量。
- 跨版本一致性：两张截图逐像素比较 **AE ≈ 62.8 像素 / 1,914,026 像素（约 0.0033%）**，
  **RMSE 0.0857%**。差异量级属于抗锯齿级别的噪声，可以认为两套 Qt 渲染一致。
- 复核中发现的**既有外观问题**（与 Qt 版本无关，两套一致，本轮不改，涉及图表布局与主题对比度）：
  成交量子图左上角“12.1万”前的首字符被最左侧绿色量柱压住不可辨；“设置 ⋯”“回测多窗口”等控件对比度
  偏低；状态栏中间 `0 只 ·` 分隔符后为空。历史 K 线卡片整片留白属于该卡片自身声明的空状态，不是渲染失败。

## 边界与未完成

1. **Windows 未验证**。`vs2022-qt611` / `vs2022-qt683` preset 已提供并复用既有 Qt 6.11.2 Windows
   适配结论（`docs/qt611-adaptation-2026-09-10.md`），但本轮没有 Windows 机器可跑，不得引用为
   本轮结果。
2. **APP-001 未在 Linux 上验收**。受控滚动条运行库只覆盖 Windows Qt 6.4.3/6.11.2；Linux 构建按其
   默认值关闭该选项。切换默认版本**不代表** APP-001 已修复。
3. **GUI 验证是软件渲染**。本 agent 沙箱的 `/dev` 没有 `/dev/dri`，Mesa 落到 `llvmpipe`。请在
   本机自己的终端再启动一次确认 GPU 加速与帧率。
4. **并发约束**：两套构建目录的 CTest 不能同时跑，原因未定位。
5. **测试计数已更新为 56**：其中 55 项为原有覆盖，第 56 项是本次新增的
   `focus_availability_qml` 焦点回归。两套 Qt 串行运行均 56/56。
6. 未访问 NAS、外网行情、真实账户；未做盘中或长稳验收。Qt 6.4.3 本轮未构建（已退役）。
7. `QT_FORCE_STDERR_LOGGING` 只用于取证，未写进产品代码；若要 Linux 上默认可见日志，需另行决定。
