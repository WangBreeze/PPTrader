# Repository Guidelines

## Project Structure & Module Organization

PPTrader is a Qt 6.4+ desktop application written in C++20 and QML. Application code lives under `src/`: `domain/` contains shared market value types, `services/` defines adapters and data-processing services, `strategy/` contains strategy implementations and dispatch, `models/` exposes data to QML, and `app/` coordinates those layers. The UI entry point is `qml/Main.qml`. Python market-data helpers are in `scripts/`, runtime seed/cache data is in `data/`, and architecture notes are in `docs/`. Treat `build/`, `build-cmake/`, and `CMakeLists.txt.user` as generated, machine-local artifacts.

## Build, Test, and Development Commands

Run commands from the repository root:

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-debug --parallel
ctest --preset vs2022-debug
cmake --build --preset vs2022-debug --target all_qmllint
.\out\build\vs2022-x64\Debug\PPTrader.exe
```

The checked-in preset targets the repository's VS2022 x64 and Qt 6.4.3 installation paths. These commands configure, build, test, lint QML, and launch the Windows executable. Keep build and test configurations consistent; add a personal `CMakeUserPresets.json` instead of committing machine-specific path changes.

## Coding Style & Naming Conventions

Match the existing four-space indentation and brace style. Keep all C++ types in namespace `pp`. Use PascalCase for classes and matching filenames (`TradingController.cpp`), camelCase for functions and local variables, and `m_camelCase` for members. Prefer Qt ownership, signals/slots, and `QStringLiteral` where established. Keep UI text, status messages, documentation, and necessary comments in Chinese; keep identifiers and market abbreviations such as `SMA`, `AAPL`, and `BTC-USDT` in English/original form. Preserve layer boundaries: QML communicates through `TradingController`, not service objects directly.

## QML / Qt 6.4 Coding Rules

C++-exported properties (`Q_PROPERTY`, e.g. `visible`, `width`, `color`) are FINAL in QML: declaring a same-named property — `property`, `required property`, `property alias`, or `default property` — fails at runtime QML load with `无法重写 FINAL 属性` and makes `src/main.cpp` return `-1` with no stderr. Never name a custom/required property after an `Item`/`anchors`/`Rectangle`/`Text` built-in (notably `visible`, `enabled`, `opacity`, `width`, `height`, `x`, `y`, `z`, `color`, `text`, `left`, `right`, `top`, `bottom`). In `ListModel` delegates, inject roles via `required property` with a non-conflicting role name (e.g. `seriesVisible`, `leftOperand`, `rightOperand`); avoid `model.roleName` access there, which raises `ReferenceError: model is not defined` under this project's Qt 6.4 + qmlcache pipeline. `qmllint` 6.4.3 does not catch this, so after any QML change launch the app and confirm the `PPTrader · 量化交易工作台` window loads. See `CLAUDE.md` for the full rule set and the 2026-08-15 `CustomStrategyWizard.qml` case.

## Testing Guidelines

Add Qt Test coverage under `tests/` and register new executables with CTest. Name sources `tst_<component>.cpp`, for example `tests/tst_market_session.cpp`. Existing coverage checks market-session boundaries and SQLite persistence. For behavioral changes, cover normal operation, boundary cases, malformed provider data, and asynchronous signal delivery; then build, run CTest and QML lint, and manually exercise the affected UI path.

## Commit & Pull Request Guidelines

Git history is not available in this checkout, so no repository-specific message pattern can be inferred. Use short, imperative subjects with a focused prefix, such as `strategy: handle empty tick windows`. Keep commits scoped to one concern. Pull requests should explain the user-visible effect, list validation commands, link related issues, and include screenshots for QML changes. Call out schema, dependency, threading, or market-data behavior changes explicitly.

## Data & Configuration Safety

Do not commit generated databases, credentials, API tokens, or machine-specific Qt paths. Treat `data/pptrader.db` and downloaded market caches as local runtime data unless a fixture is intentionally documented.
