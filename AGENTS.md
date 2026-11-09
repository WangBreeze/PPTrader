# Repository Guidelines

## Project Structure & Module Organization

PPTrader is a Qt 6.5+ desktop application written in C++20 and QML. Application code lives under `src/`: `domain/` contains shared market value types, `services/` defines adapters and data-processing services, `strategy/` contains strategy implementations and dispatch, `models/` exposes data to QML, and `app/` coordinates those layers. The UI entry point is `qml/Main.qml`. Python market-data helpers are in `scripts/`, runtime seed/cache data is in `data/`, and architecture notes are in `docs/`. Treat `build/`, `build-cmake/`, and `CMakeLists.txt.user` as generated, machine-local artifacts.

## Build, Test, and Development Commands

Run commands from the repository root:

```powershell
cmake --preset vs2022-qt611
cmake --build --preset vs2022-qt611-debug --parallel
ctest --preset vs2022-qt611-debug
cmake --build --preset vs2022-qt611-debug --target all_qmllint
.\out\build\vs2022-qt611\Debug\PPTrader.exe
```

The default target is **Qt 6.11.2** on both platforms; the minimum supported version is **Qt 6.5** (Qt 6.4.3 was retired on 2026-09-24 because `Settings` moved to `QtCore` in 6.5). `cmake/PpQtToolchain.cmake` resolves the Qt prefix itself, in this order: `-DPPTRADER_QT_ROOT`, an explicitly supplied `CMAKE_PREFIX_PATH`/`Qt6_DIR`, then platform-conventional locations for `-DPPTRADER_QT_VERSION`. If the resolved Qt's major.minor differs from the requested version, configuration fails loudly — never build 6.8.3 and 6.11.2 artifacts into each other. Keep build and test configurations consistent; add a personal `CMakeUserPresets.json` (to override `PPTRADER_QT_ROOT`) instead of committing machine-specific path changes.

Qt 6.8.3 is an opt-in project toolchain that needs no global environment change:

```powershell
cmake --preset vs2022-qt683     # 可选 Qt 6.8.3
```

Linux builds use the Ninja-based `linux-qt6xx-*` presets and write executables to `<build>/bin` (a Linux executable named `PPTrader` would otherwise collide with the `qt_add_qml_module` directory of the same name):

```sh
cmake --preset linux-qt611-debug          # 默认 Qt 6.11.2
cmake --build --preset linux-qt611-debug --parallel
ctest --preset linux-qt611-debug --output-on-failure
cmake --build --preset linux-qt611-debug --target all_qmllint
./out/build/linux-qt611-debug/bin/PPTrader

cmake --preset linux-qt683-debug          # 可选 Qt 6.8.3
```

On Arch Linux the distribution Qt (prefix `/usr`) is Qt 6.11.2, which is what the global environment must point at; keep Qt 6.8.3 confined to project presets. See [Qt 多版本工具链](docs/qt-multiversion-toolchain-2026-09-24.md) and [Linux 适配报告](docs/linux-adaptation-2026-09-24.md) before claiming a Linux or multi-version result — those reports record which acceptance gates were actually run and which were not (APP-001, GPU rendering, 盘中/长稳验收).

## Coding Style & Naming Conventions

Match the existing four-space indentation and brace style. Keep all C++ types in namespace `pp`. Use PascalCase for classes and matching filenames (`TradingController.cpp`), camelCase for functions and local variables, and `m_camelCase` for members. Prefer Qt ownership, signals/slots, and `QStringLiteral` where established. Keep UI text, status messages, documentation, and necessary comments in Chinese; keep identifiers and market abbreviations such as `SMA`, `AAPL`, and `BTC-USDT` in English/original form. Preserve layer boundaries: QML communicates through `TradingController`, not service objects directly.

## QML / Qt 6.5+ Coding Rules

Qt 6.8/6.11 目标及跨版本生命周期改动必须先阅读 [Qt 6.8+ 编码与退出验收规则](docs/qt68-coding-rules.md)。项目默认 preset 已改为 **Qt 6.11.2**（Windows MSVC 2022 x64 / Linux Ninja），最低版本 **Qt 6.5**，Qt 6.8.3 以独立 preset 作为可选工具链保留。切换默认版本**不等于**缺陷自动修复：每个版本必须用各自的 preset 与独立构建目录，禁止跨版本混装运行库、平台插件或 QML 插件，`cmake/PpQtToolchain.cmake` 会在次版本不一致时直接拒绝配置。任何版本升级都不得写成“缺陷已修复”，除非该版本上有可复现的验收证据。正常无障碍必须保留，重复激活/连接需幂等，正常关闭必须验证实际 PID 回收和退出码，不能以窗口消失、强杀或功能 JSON 通过替代。Qt 6.8.0/6.11.2 源码仍有同类重复连接风险，不得假定升级自动修复 APP-001。

**不要在已持有 active focus 的项上把 `activeFocusOnTab` 或 `focusPolicy` 翻成“不可聚焦”**：Qt 6.11 会拒绝该赋值并打印 `QQuickItem: Cannot set activeFocusOnTab to false once item is the active focus item.`，项会一直留在 Tab 链里。`focusPolicy` 走同一处检查，所以翻一次会打印两条。正确写法是让该属性保持恒定、用 `enabled` 表达可用性（Qt 会自动把 disabled 项移出 Tab 链并清除其焦点），或者写成“可用性 || 该项自身 activeFocus”，等焦点离开后再退出 Tab 链。注意 `PpButton` 不能改用 `enabled: available`：那会连带禁用子 `MouseArea`，使禁用按钮的悬停说明失效。回归测试见 `tests/tst_focus_availability_qml.cpp`。

Qt 6.11.2 Windows 构建默认要求该版本的 APP-001 受控滚动条修复库；首次或清理构建目录后先按 [依赖构建配方](cmake/qt-scrollbar-fix/README.md) 生成对应 Debug/Release 库。缺库、版本/配置/哈希不符应修正依赖，不得关闭 `PPTRADER_REQUIRE_QT_SCROLLBAR_FIX` 绕过。最终打包后重验实际部署库及 `accessible_scrollbar_lifecycle` / `application_exit`，严禁修改全局 SDK 或跨 Qt 补丁版本复制 DLL。Qt 6.11 仍有 `QJsonArray`；直接使用时显式包含 `<QJsonArray>`，不要误判为 API 被移除或依赖传递包含。6.4.3 的配方与既有证据属于历史记录，源码已不支持该版本。

Never shadow inherited QML properties, especially FINAL properties, with `property`, `required property`, `property alias`, or `default property`. This project has reproduced `无法重写 FINAL 属性` at runtime and an empty root object list (`src/main.cpp` returns `-1`); do not generalize that incident to every C++ `Q_PROPERTY`, because `FINAL` is a separate attribute. Never name a custom/required property after an `Item`/`anchors`/`Rectangle`/`Text` built-in (notably `visible`, `enabled`, `opacity`, `width`, `height`, `x`, `y`, `z`, `color`, `text`, `left`, `right`, `top`, `bottom`). In `ListModel` delegates, inject roles via `required property` with a non-conflicting role name (e.g. `seriesVisible`, `leftOperand`, `rightOperand`); avoid `model.roleName` access there, which raises `ReferenceError: model is not defined` under this project's qmlcache pipeline. `qmllint` (checked on 6.4.3 at the time) did not catch the recorded case, so after any QML change launch the app and confirm the actual main window loads. See `CLAUDE.md` and `docs/qt68-coding-rules.md` for version-specific rules and the 2026-08-15 `CustomStrategyWizard.qml` case.

## Testing Guidelines

Add Qt Test coverage under `tests/` and register new executables with CTest. Name sources `tst_<component>.cpp`, for example `tests/tst_market_session.cpp`. Existing coverage checks market-session boundaries and SQLite persistence. For behavioral changes, cover normal operation, boundary cases, malformed provider data, and asynchronous signal delivery; then build, run CTest and QML lint, and manually exercise the affected UI path.

## Commit & Pull Request Guidelines

Git history is not available in this checkout, so no repository-specific message pattern can be inferred. Use short, imperative subjects with a focused prefix, such as `strategy: handle empty tick windows`. Keep commits scoped to one concern. Pull requests should explain the user-visible effect, list validation commands, link related issues, and include screenshots for QML changes. Call out schema, dependency, threading, or market-data behavior changes explicitly.

## Data & Configuration Safety

Do not commit generated databases, credentials, API tokens, or machine-specific Qt paths. Treat `data/pptrader.db` and downloaded market caches as local runtime data unless a fixture is intentionally documented.

## 真实网络验收与沙箱权限

任何依赖腾讯、TDX、远端历史 Provider 或其他外网端点的真实探针，启动前必须显式申请外网权限；禁止从 Codex 默认网络沙箱通过 `Start-Process` 启动长稳子进程。长稳前必须用完全相同的二进制、环境和 Provider 执行“1 个代码、1 个批次”预检，只有收到非零数据且无网络错误才可继续。首轮出现 `Permission denied`、`SocketAccessError` 或 0 条行情时立即停止，不得继续收集长时间无效样本。

`QNetworkReply::errorString()` 的 `Permission denied` 不能直接解释为 HTTP 403、上游限流或 IP 封禁；先在获准联网环境执行相同最小请求做单变量对照。真实验收元数据必须记录网络运行环境、预检结果、启动方式和进程 ID，但不得记录凭据。后台自动化必须确保真实探针进程继承已批准的网络权限；若无法证明继承关系，暂停并请求用户授权。详见 `docs/permission-denied-diagnosis-2026-08-28.md`。

## 代码图谱 / codebase-memory MCP 注意事项

`codebase-memory` MCP（`mcp__codebase__*` 工具）运行在 Linux 服务器 `192.168.3.7`，无法直接访问 Windows 路径 `I:\workspace\PPTrader`，但通过**共享文件夹自动同步**：Linux 侧 `/home/wanglc/PPTrader` 与 Windows 工作区约等于实时同步。**重新索引用 `repo_path="/home/wanglc/PPTrader"`**，不要用 `I:\workspace\PPTrader`（会报 `repo_path exists...`）。最新索引项目名 `PPTrader`（2026-08-16 索引）；旧项目 `home-wanglc-PPTrader` 是过时快照勿引用。图谱对 QML 隐式组件引用捕捉有限，页面切换/组件关系读 QML 源码确认；C++ 调用链/符号定义可用 `trace_path`/`search_graph`/`get_code_snippet`。
