# Linux / 本机适配报告（2026-09-24）

> **后续变更（同日）**：项目默认工具链已改为 **Qt 6.11.2**，Qt 6.8.3 与 Qt 6.4.3 成为项目内可选
> preset，preset 命名与构建目录也随之调整。本报告下面“构建与验收命令”一节记录的是当天早些时候
> 的 Qt 6.8.3 单版本状态，作为历史证据保留；当前的版本矩阵、全局环境改动与新发现的缺陷见
> [Qt 多版本工具链](qt-multiversion-toolchain-2026-09-24.md)。

## 结论

PPTrader 已在**本机 Arch Linux + GCC 16 + Ninja + Qt 6.8.3 (gcc_64)** 完成首次跨出 Windows 的构建与全量回归。源码只做可移植性修补，没有为了跑通而关闭任何门禁（`PPTRADER_REQUIRE_QT_SCROLLBAR_FIX`、无障碍、退出验收、测试断言全部保留）。

- Debug：完整 CTest **55/55**，总耗时 15.6 秒；`all_qmllint` 目标成功。
- RelWithDebInfo：完整 CTest **55/55**，总耗时 16.3 秒；`all_qmllint` 目标成功；`--exit-probe` 在 offscreen 与真实 wayland 两个平台下退出码均为 0。
- 真实 Main：`PPTrader --exit-probe` 退出码 0，该路径要求 `QQmlApplicationEngine::rootObjects()` 非空，因此证明编译进资源的 `Main.qml` 真的加载成功。
- 真实桌面：Hyprland（Wayland）会话中窗口成功映射，标题为 `PPTrader · 量化交易工作台`；截图逐区域复核确认暗色工作台完整渲染、中文无缺字（无 tofu）、无重叠/裁切/报错文本。
- Python 行情桥：`.venv` 已建好并装齐 `mootdx 0.11.7 / pandas 2.3.3 / baostock 0.9.3 / tushare 1.4.29`，`mootdx_bridge_protocol` 已自动注册并通过。

本报告只证明当前源码与本机这套工具链的兼容性。项目公共最低版本仍是 Qt 6.4。

## 本机环境

| 项目 | 值 |
| --- | --- |
| 发行版 | Arch Linux (rolling)，内核 7.2.4-arch1-2 |
| CPU / 内存 | 32 线程，30 GiB |
| 编译器 | GCC 16.2.1 |
| CMake / Ninja | 4.4.3 / 1.13.2 |
| Qt | Qt 6.8.3 gcc_64，位于 `/home/wanglc/ProgramData/Qt/6.8.3/gcc_64` |
| 会话 | Hyprland（Wayland），`XDG_CURRENT_DESKTOP=Hyprland` |
| GPU | NVIDIA GB205M (RTX 5070 Ti Mobile) + AMD Granite Ridge iGPU |
| Python | 系统 3.14.7（过新，`mootdx 0.11.7` 一类旧包不稳）；`.venv` 用 Python 3.12.14 创建 |

### Python 行情桥

系统 Python 是 3.14.7，本机另有 `~/miniforge3/envs/dev` 的 Python 3.12.14，故 `.venv` 由后者创建：

```sh
~/miniforge3/envs/dev/bin/python -m venv --copies .venv
PIP_CACHE_DIR="$PWD/.venv/.pip-cache" .venv/bin/python -m pip install \
    -i https://pypi.tuna.tsinghua.edu.cn/simple -r requirements.txt
```

`.venv/pyvenv.cfg` 的 `home` 指向该 conda 环境，因此 **`.venv` 依赖 `dev` 这个 conda 环境存在**；若要完全独立，改用 `uv python install 3.12` 装一个独立解释器后重建即可。装好的版本：`mootdx 0.11.7`、`pandas 2.3.3`、`baostock 0.9.3`、`tushare 1.4.29`。直连 `files.pythonhosted.org` 会读超时，故走清华镜像。

## 构建与验收命令

（当天早些时候的 Qt 6.8.3 单版本写法，preset 名已随后续变更调整；当前命令见
[Qt 多版本工具链](qt-multiversion-toolchain-2026-09-24.md)。）

```sh
cmake --preset linux-qt683-debug
cmake --build --preset linux-qt683-debug --parallel
ctest --preset linux-qt683-debug --output-on-failure
cmake --build --preset linux-qt683-debug --target all_qmllint
./out/build/linux-qt683-debug/bin/PPTrader
```

当时（2026-09-24 早）的产物：Debug 主程序 130,885,920 字节，
SHA-256 `90273AC4CB701E6980FD24B96BFA394986B6868F56AAF2CD914946663CCB76A1`；
RelWithDebInfo 主程序 193,958,848 字节，
SHA-256 `86C960B51158FB9DBBCC932821AB43DC41D56D6519F7E0234B22D9046ED40225`。
这些哈希对应临时目录 `build-linux*`，该目录随后被 `out/build/*` 取代，无法再复核。

## 移植修补

### 构建系统

1. **可执行文件与 QML 模块目录同名**（`CMakeLists.txt`）。`qt_add_qml_module` 会在 `${CMAKE_CURRENT_BINARY_DIR}/PPTrader` 生成模块目录，而 Linux 上可执行目标产物也叫 `PPTrader`（Windows 带 `.exe` 不冲突），链接阶段直接失败：`/usr/bin/ld: cannot open output file PPTrader: 是一个目录`。非 Windows 平台现将 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 设为 `${CMAKE_BINARY_DIR}/bin`，与 `GNUInstallDirs` 的 `bindir` 语义一致；Windows 布局保持不变，以免影响既有 VS2022 preset 与验收脚本。
2. **Python venv 解释器路径**（`CMakeLists.txt`）。原实现硬编码 `.venv/Scripts/python.exe`，Linux 上永远不存在，`mootdx_bridge_protocol` 会被静默跳过。现按平台选择 `Scripts/python.exe` 或 `bin/python`。
3. **子进程路径由构建系统给出**（`CMakeLists.txt`）。`tst_application_exit` 需要启动被测程序，原先把 `applicationDirPath() + "/PPTrader.exe"` 写死在测试源码里。现通过 `$<TARGET_FILE:PPTrader>` 生成 `PPTRADER_APP_EXECUTABLE` 编译定义，源码保留脱离 CMake 手工编译时的回退分支。
4. **可移植 preset**（`CMakePresets.json`）。当时新增的 `linux-x64*` 系列（Ninja，Qt 由 `QT_PREFIX_PATH` 环境变量定位）**已被后续的 `linux-qt611-*` / `linux-qt683-*` 取代**：现在由 `cmake/PpQtToolchain.cmake` 按 `PPTRADER_QT_VERSION` 自行解析前缀，不再需要任何环境变量。`.gitignore` 保留 `CMakeUserPresets.json` 条目作为个人覆盖点。

### 平台语义

5. **UNC/网络共享路径判定**（`src/services/StorageProfile.cpp`）。原实现先 `QDir::cleanPath()` 再检查 `startsWith("//")`，但本机实测 `cleanPath("//nas/market/pptrader.db")` 会折叠成 `/nas/market/pptrader.db`，判定永不成立：既漏掉网络共享检查，又会把用户目标**静默改写成另一个本地路径**。现改为在归一化之前按原始输入判定，Windows 上仍是真正的 UNC 拒绝，POSIX 上也避免静默改写。

### 测试可移植性

6. **`QFile::setFileTime` 顺序缺陷**（`tests/tst_nas_data_node.cpp`）。原 helper 在 `close()` 之前设置 mtime：`QFile` 的写入带缓冲，`close()` 落盘会刷新 mtime 从而覆盖刚设置的时间。本机 Qt 6.8.3 + Linux 上已复现「`setFileTime` 返回 true，但 mtime 仍是当前时间」，导致 `pruneOlderThan` 断言拿到 `removedFiles=0`。现改为写入 → `flush()` → `close()` → 重新打开设置时间。这是所有平台都正确的顺序。
7. **请求头顺序无关的鉴权断言**（`tests/tst_nas_data_node.cpp`）。原断言在原始报文中匹配字面量 `"Authorization: Bearer test-token\r\n"`，要求 `Authorization` 恰好是最后一个请求头；请求头顺序由 `QNetworkAccessManager` 的版本与平台决定，跨平台不可靠。现改为用已有的正则解析出取值再比较，并且每个请求都重新解析（缺失即置空），既不依赖顺序也不比原实现宽松。
8. **无障碍激活断言限定平台**（`tests/TradingControllerWizardProbe.cpp`）。见下节。

### 字体回退（本机可见的渲染差异）

`PPTheme.fontFamily` 原先硬编码 `"Noto Sans SC"`，本机没有这一族（装的是 `Noto Sans CJK SC` / `思源黑体 CN`），Canvas 文本每次绘制都报 `Context2D: The font families specified are invalid`。

- `qml/components/PPTheme.qml` 改为用 `Qt.fontFamilies()` 在候选族列表里挑第一个可用者。已安装 `Noto Sans SC` 的机器仍优先选中它，**视觉规格不变**。Qt 6 的 `QFont::setFamily` 只是 `setFamilies(QStringList(family))`，不拆分逗号列表，所以不能写成逗号分隔字符串。
- 新增 `canvasFontFamily`：Canvas 的 `ctx.font` 走 Qt 自己的 CSS 风格解析器，会把**未加引号**的族名按空格拆成多个候选族（`qExtractFontFamilies` / `qSetFontFamilyFromTokens`），所以 `"10px Noto Sans SC"` 会被拆成 `Noto`/`Sans`/`SC` 三个都不存在的族。这是本机之前 6 条警告的另一半原因，**与装没装 Noto Sans SC 无关**，属于既有缺陷。含空格的族名必须整体加引号。
- `ScatterPlot.qml` 里硬编码的 `'Noto Sans SC'` 改为引用主题；`LiveSnapshotRangeChart.qml`、`BacktestEquityChart.qml` 改用 `canvasFontFamily`。

修复后本机告警从 6 条降为 0，解析结果为 `Noto Sans CJK SC`，对 16 个中文探针字符的 `QFontMetrics::inFont` 全部命中（缺字 0）。

## 为什么无障碍断言在 Linux 上必须改

`tests/TradingControllerWizardProbe.cpp` 原来无条件要求 `QAccessible::isActive()`。Qt 6.8.3 源码（`qtbase` v6.8.3 `src/gui/accessible/qaccessible.cpp`）里：

```cpp
bool QAccessible::isActive()
{
    if (QPlatformAccessibility *pfAccessibility = platformAccessibility())
        return pfAccessibility->isActive();
    return false;
}

void QAccessible::setActive(bool active)
{
    for (int i = 0; i < qAccessibleActivationObservers()->size() ;++i)
        qAccessibleActivationObservers()->at(i)->accessibilityActiveChanged(active);
}
```

即 Qt 6.8 起 `setActive()` **不再写任何全局标志**，只通知激活观察者；`isActive()` 直接返回平台插件的无障碍实现状态。Windows 的 `windows` 平台插件内置原生 UIA，所以激活后为 true；本机这套 Qt 6.8.3 安装**不含任何 `plugins/accessible/` 桥接插件**，`offscreen` 与 `wayland` 实测 `setActive(true)` 后 `isActive()` 均为 false。

因此该断言编码的是 Windows-UIA 事实，不是跨平台不变式。现按平台分支：

- Windows：保留 `require(QAccessible::isActive(), "退出测试保持原生无障碍激活")` 门禁。
- 其他平台：用 `phase("accessibility-bridge=active|unavailable")` **记录实测状态**，不把平台缺桥接记成产品回退；其后的 10000 次重复激活、关闭时序、PID 回收与退出码照常验收。

未删除 `tst_accessible_scrollbar_lifecycle`，它本就在非 Windows 上 `QSKIP`。

## 验证证据

| 门禁 | 结果 |
| --- | --- |
| Debug 全量 CTest | **55/55**，15.6 秒 |
| RelWithDebInfo 全量 CTest | **55/55**，16.3 秒 |
| `all_qmllint`（Debug / Release） | 均通过（退出码 0） |
| `PPTrader --exit-probe`（offscreen / wayland） | 均退出码 0，root objects 非空 |
| 真实 Main（Hyprland/Wayland） | 窗口映射，标题 `PPTrader · 量化交易工作台`，截图逐区域复核通过 |
| `Context2D` 字体告警 | 6 → 0 |
| `mootdx_bridge_protocol` | 通过（`.venv/bin/python` 已注册） |

原始失败与判定依据：`application_exit`、`sqlite_local_data_store`、`nas_data_node` 三项失败均已定位到**平台语义/测试假设**，不是产品缺陷；`QDir::cleanPath`、`QFile::setFileTime`、`QAccessible` 三项结论都用最小独立 probe 在本机复现过，见上文章节。

## 边界与未完成

1. **GUI 验证是软件渲染**。本 agent 的 Linux 沙箱里 `/dev` 是最小 tmpfs，**没有 `/dev/dri`、没有 `/dev/nvidia*`**（仅 `core fd full null ptmx pts random shm stderr stdin stdout tty urandom zero`），所以 Mesa 建不出 DRI screen，Qt 落到 `llvmpipe` 软件渲染（`qt.rhi.general: OpenGL VENDOR: Mesa RENDERER: llvmpipe`）。这属于沙箱边界，**不代表正常桌面会话**；请从本机自己的终端再启动一次确认 GPU 加速与帧率。窗口映射、QML 加载与退出验收不受影响。
2. 同一沙箱下强制 `QSG_RHI_BACKEND=vulkan` 会在只有 llvmpipe 物理设备时崩溃（退出码 139），且 `vulkaninfo` 未安装。不要在无 GPU 设备的环境里指定该后端。
3. Qt 安装自带的 `xcb` 平台插件在本机加载失败（缺系统库），本次全部验证走 `wayland` 与 `offscreen`，未处理 X11/XWayland 路径。
4. `Qt.labs.settings` 的弃用告警保留。仓库最低版本仍是 Qt 6.4，而 `QtCore.Settings` 从 6.5 才有，强行迁移会破坏最低版本支持，需先设计兼容方案并用两套 Qt 实测。
5. APP-001 滚动条受控库仅覆盖 Windows Qt 6.4.3/6.11.2；本机 Linux 构建按其默认值关闭该选项并打印未启用告警，**不得据此宣称 APP-001 已修复**，Linux 上也没有做相应回归。
6. 未访问 NAS、外网行情、真实账户，也未做盘中或长稳验收；Python 依赖安装走的是清华镜像，仅为本机可用性。
7. 截图复核另记录到几处**既有外观问题**（与平台无关，本次未改，涉及 Figma 规格，需设计确认后再动）：空状态占位文字与禁用按钮对比度过低、主页“1m”周期下拉被绘制成浅色填充与暗色主题不一致、状态栏 `0 只 ·` 后有多余分隔符、左上卡片下半部留白偏大。截图时状态栏显示 `无可用实时源 · stopped`，因为验证时用 `PPTRADER_AUTOSTART_LIVE=0` 关闭了实时自启动，属于预期状态。
