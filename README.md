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

```powershell
cmake -S . -B build -G "Ninja"
cmake --build build
```

如果 Qt 不在 CMake 默认搜索路径里，可以指定 `CMAKE_PREFIX_PATH`：

```powershell
cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH="C:\Qt\6.4.3\msvc2019_64"
cmake --build build
```

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
