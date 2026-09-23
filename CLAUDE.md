# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```sh
cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH="D:\ProgramData\Qt\6.4.3\msvc2019_64"
cmake --build build
```

Adjust `CMAKE_PREFIX_PATH` to match the local Qt install. Requires Qt 6.4+, CMake 3.24+, C++20, and Ninja.

## Architecture

PPTrader is a Qt 6 + QML + CMake quantitative trading workstation. All code lives in the `pp` namespace.

### Layer map

| Layer | Directory | Role |
|-------|-----------|------|
| Domain types | `src/domain/MarketTypes.h` | `Instrument`, `Tick`, `Candle`, `TradeSignal` — the universal value types shared across all layers. |
| Service interfaces | `src/services/` | Abstract bases (`MarketDataProvider`, `LocalDataStore`, `BrokerGateway`, `NotificationService`) that define extension points for real adapters. |
| Service impls | `src/services/` | `SimulatedMarketDataProvider` (mock tick stream), `SqliteLocalDataStore` (SQLite persistence), `BacktestEngine` (streaming historical replay), `IndicatorEngine` (SMA/ROC), `MarketSession` (trading-hours filter). |
| Strategy | `src/strategy/` | `Strategy` abstract base receiving `onTick`/`onCandle`, emitting `signalGenerated`. `TickMomentumStrategy` and `PairSpreadStrategy` are concrete examples. `StrategyEngine` dispatches ticks to strategies via `QThreadPool`. |
| Models | `src/models/` | `QAbstractListModel` subclasses (`TickListModel`, `CandleListModel`, `SignalListModel`, `StrategyListModel`) bridging data to QML. |
| Orchestration | `src/app/TradingController.h` | Single `QObject` exposed to QML as `tradingController`. Owns all services and models; wires tick → strategy → signal → storage pipelines. |
| UI | `qml/Main.qml` | Single-window dark-themed workbench: instrument list, Canvas-based real-time price chart, backtest candlestick view, signal log, strategy panel with enable/disable and parameter tuning. |

### Data flow

- **Live mode**: `MootdxMarketDataProvider` (worker thread) → `CompositeMarketDataProvider` (signal bridge) → `TradingController::handleTick()` → tick model + strategy engine → signals → signal model.
- **Backtest mode**: `BacktestEngine` → `pointReady` signal → `CandleListModel` + `SignalListModel` (pushed incrementally).
- `MarketSession::isOpen()` gates tick generation per instrument to suppress ticks outside trading hours.

### Key patterns

- Service classes use abstract base classes (interface + pure virtual) so real adapters (live market data, broker gateways) can be swapped in without touching the core.
- Strategy instances are added to `StrategyEngine`, which runs each `onTick` invocation on `QThreadPool`. Each strategy holds its own state and mutex.
- `TradingController` is the single QML context property; QML never reaches into services directly.
- Models cap rows (tick model: 500 rows) to bound memory.

## Conventions

- UI labels, status text, docs, and comments use Chinese.
- C++/QML identifiers use English.
- Ticker symbols, exchange codes, and indicator abbreviations stay in their original form (e.g., `AAPL`, `SMA`, `BTC-USDT`).
- `CandlePoint` is a struct bundling a `Candle` with optional `TradeSignal` for backtest rendering.
- Default DB path: `data/pptrader.db` under the executable directory.

## QML Coding Rules (Qt 6.4)

Qt 6.8/6.11 目标与生命周期相关工作先遵守 [Qt 6.8+ 编码与退出验收规则](docs/qt68-coding-rules.md)。公共 preset 当前仍以 Qt 6.4.3 构建，Qt 6.11.2 适配只存在于独立构建目录。连接必须幂等并限定生命周期；保留正常无障碍；真实关闭须核对 PID、退出码和分阶段耗时，不以强制清理或功能检查通过宣称退出修复。Qt 6.11 直接使用 `QJsonArray` 时显式包含 `<QJsonArray>`，不要把缺少包含误判为 API 移除。

6.4.3 Windows 构建现要求 [APP-001 受控依赖](cmake/qt-scrollbar-fix/README.md)。首次构建先生成匹配配置的修复库，缺库或哈希错误禁止关闭校验绕过。打包工具覆盖 Qt 后重新部署校验并运行滚动动作/真实退出回归；全局 SDK 保持只读，不跨版本混用二进制补丁。

### FINAL-property shadowing — read before writing QML

本项目禁止遮蔽继承属性，尤其不能用 `property`、`required property`、`property alias` 或 `default property` 覆盖 FINAL 属性。历史案例会在 QML 加载时报 `无法重写 FINAL 属性`，使 `QQmlApplicationEngine::rootObjects()` 为空、`main` 返回 `-1`；不能仅靠 stderr 是否为空判断加载成功。`FINAL` 是独立属性标记，不能概括为每一个 C++ `Q_PROPERTY` 都必然 FINAL。参见 [Qt 6.8 属性系统](https://doc.qt.io/qt-6.8/properties.html)；以下禁重名清单是工程的更严格防错规则。

Never use these names for custom or required properties (non-exhaustive):

- `Item` built-ins: `visible` `enabled` `opacity` `width` `height` `x` `y` `z` `scale` `rotation` `clip` `focus` `activeFocus` `state` `states` `transitions` `parent` `children` `data` `resources` `implicitWidth` `implicitHeight` `layer` `transform` `smooth` `antialiasing` `anchors` `baselineOffset` `transformOrigin` `containmentMask`
- `anchors` group members: `left` `right` `top` `bottom` `horizontalCenter` `verticalCenter` `baseline` `fill` `centerIn` `margins` `leftMargin` `rightMargin` `topMargin` `bottomMargin` and every `*Offset`
- `Rectangle`: `color` `radius` `border` `gradient`
- `Text`/`TextField`/`TextEdit`/`Label`: `text` `font` `color` `elide` `wrapMode` `horizontalAlignment` `verticalAlignment` `padding` `leftPadding` `rightPadding` `topPadding` `bottomPadding`

When in doubt, check the inherited C++ type's `Q_PROPERTY` declarations or the Qt 6.4 docs before naming a property.

### ListModel delegates

1. Inject model roles with `required property` (same name as the role).
2. Role names must avoid the FINAL names above. If data semantics need `visible`/`left`/`right`, rename the role (e.g. `seriesVisible`, `leftOperand`, `rightOperand`) and update every `setProperty(..., "role", ...)`, `model.get(i).role`, and any legacy-draft migration in the same change.
3. Do NOT read a ListModel role via `model.roleName` inside a delegate that also uses `required property`: with this project's Qt 6.4 + qmlcache pipeline that raises `ReferenceError: model is not defined`. Always use `required property` for role access.

### Verification

`qmllint` 6.4.3 does **not** report property-override errors (not even with `--compiler warning`), so lint cannot catch this. The error only surfaces at runtime when the QML engine loads the component. After any QML change, launch the app and confirm the main window titled `PPTrader · 量化交易工作台` appears.

### Real case (2026-08-15)

`qml/components/CustomStrategyWizard.qml` declared `required property bool visible` and `required property string left` / `right` in two `Rectangle` delegates. This broke `CustomStrategyWizard` → `CustomStrategyWorkbench` → `StrategyResearchPage` → `Main.qml` and the app exited with `-1`. Fixed by renaming to `seriesVisible` / `leftOperand` / `rightOperand`, renaming the model roles to match, and adding legacy-draft field migration in `restoreDraft()`.

## 代码图谱 / codebase-memory MCP 注意事项

- `codebase-memory` MCP（`mcp__codebase__*` 工具，代码图谱）运行在 **Linux 服务器 `192.168.3.7`**（root `/home/wanglc/`），**无法直接访问 Windows 路径 `I:\workspace\PPTrader`**。它通过**共享文件夹自动同步**访问代码：Linux 侧 `/home/wanglc/PPTrader` 与 Windows 侧 `I:\workspace\PPTrader` 自动同步（约等于实时）。
- **重新索引**：用 `index_repository(repo_path="/home/wanglc/PPTrader", ...)`，**不要**用 `I:\workspace\PPTrader`（跨文件系统会报 `repo_path exists and contains source files`）。
- 最新索引项目名 **`PPTrader`**（2026-08-16 重新索引，约 5514 节点，含二级导航/插件系统/参数扫描/日志等近期改动）。旧项目 `home-wanglc-PPTrader` 是过时快照，勿再引用。
- 图谱对 **QML 组件间引用**（QML 的隐式组件引用，无显式 import 边）捕捉有限，且索引器把 `Q_PROPERTY`/宏行标为 `parse_partial`。页面切换/组件引用关系仍需**读 QML 源码确认**（`qml/Main.qml` 的 TabBar/StackLayout/subNav + 各 Page 的 `subPage` 显隐）；C++ 调用链/符号定义可放心用图谱（`trace_path`/`search_graph`/`get_code_snippet`）。
