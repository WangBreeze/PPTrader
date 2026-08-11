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
