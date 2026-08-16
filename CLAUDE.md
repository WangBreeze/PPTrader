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

### FINAL-property shadowing — read before writing QML

In Qt 6, every property exported from C++ (a `Q_PROPERTY` such as `QQuickItem::visible` or `QQuickItem::width`) is **FINAL** in QML. Declaring a same-named property on a derived QML type — via `property`, `required property`, `property alias`, or `default property` — is an override of a FINAL property and fails at runtime QML load with `无法重写 FINAL 属性` ("Cannot override FINAL property"). This silently makes `QQmlApplicationEngine::rootObjects()` empty, so `src/main.cpp` returns `-1` with no error on stderr.

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

- `codebase-memory` MCP（`mcp__codebase__*` 工具，代码图谱）运行在 **Linux 环境**（root `/home/wanglc/...`），**无法访问 Windows 工作区 `I:\workspace\PPTrader`**。`index_repository` 对 `I:\workspace\PPTrader` 会报 `repo_path exists and contains source files` 错误——这是跨文件系统隔离，不是路径写错，不要重试其他写法。
- 现有索引项目 `home-wanglc-PPTrader`（root `/home/wanglc/PPTrader`）是 MCP 服务器上一份**过时副本**，不含本工作区近期改动（二级导航、插件系统、参数扫描、日志等）。查最新代码结构/进度时，以**本地 `git log` + `docs/development-progress.md` 为准**，不要依赖图谱结论。
- 图谱对 **QML 组件间引用**（QML 的隐式组件引用，无显式 import 边）捕捉有限，且索引器把 `Q_PROPERTY`/宏行标为 `parse_partial`。页面切换/组件引用关系需**读 QML 源码确认**（`qml/Main.qml` 的 TabBar/StackLayout/subNav + 各 Page 的 `subPage` 显隐）。
