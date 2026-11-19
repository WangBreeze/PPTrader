# PPTrader 量化交易工作台

项目文档：[开发进度](docs/development-progress.md) · [项目 Wiki](docs/project-wiki.md) · [架构设计](docs/architecture.md)

PPTrader 是一个基于 Qt 6 + QML + CMake 的量化交易工作台工程骨架。

当前版本先保证可运行，同时保持模块解耦：

- 通过 `MarketDataProvider` 抽象接入实时 tick 行情
- 根据市场交易时段过滤模拟 tick，避免休市时继续出数
- 支持真实历史 K 线缓存和确定性流式回测
- 将 tick、真实 K 线和交易信号写入本地 SQLite 数据库
- 提供模拟券商、订单状态、成交、持仓和账户核算框架
- 订单、成交、持仓和账户快照支持 SQLite 持久化与启动恢复
- 提供单笔金额、持仓市值、亏损熔断和活动委托数量风控
- 提供与 UI 解耦的策略执行意图、人工审批、自动提交和仓位计算抽象
- 策略执行配置和意图支持 SQLite 审计、恢复及未完成审批自动过期
- 提供可替换交易所规则服务和按交易日基准权益计算的亏损熔断
- 指标引擎内置 SMA、ROC 示例
- 实时 tick 动量策略在线程池中监测多标的
- 策略列表支持运行时启停
- 策略参数支持运行时调整阈值和冷却时间
- QML 工作台显示 tick、回测 K 线和信号明细
- 已预留真实券商、通知、本地存储、策略等适配接口

## 构建

默认目标是 **Qt 6.12.0**（Windows MSVC 2022 x64 / Linux Ninja），**最低版本 Qt 6.5**。项目只使用
**自己安装的 Qt 副本** `~/ProgramData/Qt/6.12.0/`（Linux `gcc_64`、Windows `msvc2022_64`），
不使用发行版 `/usr` 里的同名版本——Arch 会持续滚动到更新的次版本，项目只跟随自己验收过的版本；
也不读取 `PATH`、`LD_LIBRARY_PATH`、`QML_IMPORT_PATH`、`Qt6_DIR` 之类环境变量。`Qt 6.11.2`、`Qt 6.8.3`
以独立 preset 作为可选工具链保留，同样不需要改全局环境变量。

> Qt 6.4.3 支持已于 2026-09-24 退役：`CustomStrategyWizard` 需要的 `Settings` 自 Qt 6.5 起由
> `QtCore` 提供，旧的 `Qt.labs.settings` 已弃用。历史日期文档中的 `cmake --preset vs2022-x64` /
> `vs2022-debug` 等 6.4.3 命令已不再可用。

Windows：

```powershell
cmake --preset vs2022-qt612
cmake --build --preset vs2022-qt612-debug --parallel
ctest --preset vs2022-qt612-debug
cmake --build --preset vs2022-qt612-debug --target all_qmllint
.\out\build\vs2022-qt612\Debug\PPTrader.exe
```

Linux：

```sh
cmake --preset linux-qt612-debug
cmake --build --preset linux-qt612-debug --parallel
ctest --preset linux-qt612-debug --output-on-failure
cmake --build --preset linux-qt612-debug --target all_qmllint
./out/build/linux-qt612-debug/bin/PPTrader
```

可用的 Qt 版本 preset：

| 平台 | 默认（6.12.0） | 可选 6.11.2 | 可选 6.8.3 |
| --- | --- | --- | --- |
| Windows | `vs2022-qt612` | `vs2022-qt611` | `vs2022-qt683` |
| Linux | `linux-qt612-debug` / `-release` | `linux-qt611-*` | `linux-qt683-debug` / `-release` |

Qt 前缀解析顺序：`-DPPTRADER_QT_ROOT=<前缀>` → 按 `-DPPTRADER_QT_VERSION` 在**项目约定安装位置**
查找（Linux `$HOME/ProgramData/Qt/<版本>/gcc_64`、Windows `D:/ProgramData/Qt/<版本>/msvc2022_64` 等）；
两者都没有时直接配置失败，**不会**回退到系统 `/usr`（确需回退时显式加 `-DPPTRADER_QT_ALLOW_SYSTEM=ON`）。
解析到的 Qt 与请求版本**次版本号不一致时同样直接配置失败**，避免 QML 插件与运行库跨版本混装。
详见 [Qt 多版本工具链](docs/qt-multiversion-toolchain-2026-09-24.md)。

完全不用 preset 时按同样规则显式指定版本与前缀（版本与 `PPTRADER_QT_VERSION` 的次版本必须一致）：

```sh
cmake -S . -B build -G "Ninja" -DPPTRADER_QT_VERSION=6.12.0 \
      -DPPTRADER_QT_ROOT=$HOME/ProgramData/Qt/6.12.0/gcc_64
cmake --build build
```

非 Windows 可执行文件输出在构建目录的 `bin/` 子目录：QML 模块目录与可执行目标同名，Linux 上放在构建根目录会与 `qt_add_qml_module` 生成的同名目录冲突。

需要个人化前缀时用不入库的 `CMakeUserPresets.json` 覆盖 `PPTRADER_QT_ROOT`（参照 `AGENTS.md`）。

Python 行情桥脚本（mootdx/baostock/tushare）需要一个 Python 3.12 左右的解释器；在仓库根目录建 `.venv` 后安装 `requirements.txt`。CMake 会按平台查找 `.venv/Scripts/python.exe`（Windows）或 `.venv/bin/python`（POSIX），存在才注册 `mootdx_bridge_protocol`。Linux 端适配细节与已知边界见 [Linux 适配报告](docs/linux-adaptation-2026-09-24.md)。

## 后续模块

1. 扩展美股和加密货币真实行情适配器。
2. 扩展策略模板系统，支持更多策略类型和多股联合指标。
3. 在新版 QML 中设计策略执行审批、账户、委托、成交和持仓页面。
4. 增加真实券商适配器以及交易所规则、交易日级风控统计。
5. 增加生产级通知、审计导出和异常恢复后端。

## 开发约定

- 界面文案、状态提示、文档和必要代码注释统一使用中文。
- C++/QML 标识符保持英文，便于和 Qt、CMake、第三方 SDK 保持一致。
- 股票代码、交易所代码、指标缩写等行业通用符号按原始格式保留。
