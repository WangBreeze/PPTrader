# PPTrader 架构设计

## 目标

PPTrader 面向 A 股、美股和加密货币市场，目标是构建一个高性能、界面流畅、模块解耦的 Qt 6/QML 桌面量化交易工作台。架构上将行情、存储、指标、策略、券商、通知、回测和界面展示拆分，方便逐步接入真实服务。

## 模块边界

- `domain`：通用领域类型，例如标的、tick、K 线和交易信号。
- `services/MarketDataProvider`：实时行情源接口，真实适配器需要把供应商数据统一转换为 `Tick`。
- `services/MarketSession`：各市场交易时段判断，避免休市时继续生成模拟 tick。
- `services/BacktestEngine`：历史数据播放和策略评估流。
- `services/IndicatorEngine`：可复用指标计算模块，多股联合指标后续放在这里扩展。
- `strategy`：策略接口，可接收 tick 和 K 线并产生交易信号。
- `strategy/StrategyEngine`：有界线程池调度层，用于并发执行多策略、多标的监测。
- `strategy/StrategyExecutionCoordinator`：把策略信号转换为执行意图，编排人工审批或自动提交。
- `strategy/PositionSizer`：仓位计算边界；当前提供固定金额与最大持仓比例实现。
- `strategy/SignalExecutionPolicy`：策略级启用状态、执行模式和冷却规则边界。
- `strategy/TickMomentumStrategy`：实时 tick 动量监测策略示例，用于验证多标的策略执行链路。
- `models/StrategyListModel`：策略运行状态模型，供界面启停策略。
- 策略参数：当前 tick 动量策略支持运行时调整触发阈值和冷却时间。
- `services/BrokerGateway`：券商登录、订单和成交事件边界；当前提供模拟券商实现。
- `services/TradingAccountService`：资金冻结、持仓成本、盯市和盈亏核算。
- `services/PreTradeRiskService`：在订单进入券商前执行金额、持仓、亏损和活动委托限制。
- `services/ExchangeRuleService`：市场整手、价格步长、停牌、时段和涨跌停规则边界。
- `services/StrategyExecutionStore`：策略执行配置和意图审计的持久化边界。
- `services/NotificationService`：监测到信号后的消息通知边界。
- `services/LocalDataStore`：tick、K 线和信号审计记录的本地存储边界。
- `models`：Qt 列表模型，隔离 UI 展示和业务引擎。
- `app/TradingController`：暴露给 QML 的应用编排门面。
- `qml`：界面层，负责实时 tick、回测 K 线和信号明细展示。

## 运行流程

实时行情流程：

1. 界面调用 `TradingController::startLive()`。
2. `MarketDataProvider` 发布标准化 tick。
3. `TradingController` 将 tick 追加到 `TickListModel`。
4. 策略订阅 tick，产生 `TradeSignal`。
5. 通知服务和券商网关消费确认后的信号。

回测流程：

1. 界面调用 `TradingController::runBacktest()`。
2. `BacktestEngine` 流式输出 K 线。
3. 指标计算产生买入或卖出信号。
4. `CandleListModel` 和 `SignalListModel` 增量更新，QML 动态渲染图表。

模拟交易流程：

1. 界面通过 `TradingController` 登录模拟券商并提交标准订单。
2. `SimulatedBrokerGateway` 根据最新 bid/ask 撮合并发布订单、成交事件。
3. `TradingAccountService` 冻结或释放资金/持仓，更新成本和盈亏。
4. 订单、成交、持仓模型及账户属性通知 QML 刷新。

策略执行流程（抽象层）：

1. 策略信号进入 `StrategyExecutionCoordinator`，生成独立执行意图。
2. 执行规则检查启用状态、模式、信号有效性、冷却期和可卖持仓。
3. `PositionSizer` 根据账户、现有持仓和策略配置计算委托数量。
4. 人工模式等待上层批准或拒绝；自动模式通过 `OrderSubmissionPort` 提交。
5. 订单提交后仍由账户服务的统一交易前风控进行最终校验。

策略执行配置和意图通过独立存储端口持久化。应用恢复时，历史已提交/拒绝意图保留，尚未完成的创建、审批和批准状态统一转为过期，避免重启后误下单。

## 线程计划

- UI 和 Qt Model 保持在主线程。
- 行情适配器运行在 IO 工作线程，发出不可变标准事件。
- 策略运行在有界 `QThreadPool` 中，每个策略实例维护自己的状态；当前已接入 tick 动量策略示例。
- 本地存储第一阶段通过 SQLite 落库，后续再拆成独立写入队列。
- 券商网关使用各自的异步连接对象，通过 Qt 队列信号回到应用层。

## 存储计划

采用本地追加优先设计：

- 原始 tick 按市场、标的、日期分区存储。
- K 线表按标的、周期、开始时间建立键。
- 策略运行元数据和信号审计记录独立保存。
- 券商、账户、订单数据与研究数据隔离。
- 订单采用 UPSERT，成交以成交编号幂等写入；账户与持仓在同一事务中保存。

第一阶段已经接入 SQLite，数据库默认位于程序目录下的 `data/pptrader.db`。tick 数据量很大时，可以增加 Parquet 或 DuckDB 导入导出，同时保留 SQLite 作为元数据和应用状态存储。

## 当前状态

已实现：

- Qt 6/QML/CMake 应用工程
- 模拟和 mootdx/通达信 A 股实时、历史行情链路
- 市场交易时段过滤
- SQLite 历史 K 线 UPSERT 缓存和确定性流式回测
- SMA 和 ROC 指标示例
- 策略、券商、通知扩展接口
- SQLite 本地数据存储实现
- 有界策略线程池骨架
- 实时 tick 动量策略
- 策略运行时启停
- 策略阈值和冷却时间运行时配置
- tick、K 线、交易信号模型
- 模拟券商登录、订单路由、限价撮合和撤单
- 资金冻结、成交、持仓成本、盯市及盈亏核算
- 订单、成交和持仓 QML 模型
- 交易前风控限制及 QML 配置接口
- 与 QML 解耦的策略执行意图、人工审批/拒绝及自动提交抽象
- 订单、成交、持仓和账户快照的 SQLite 持久化与启动恢复
- 策略执行配置/意图 SQLite 持久化和安全过期恢复
- 默认交易所规则检查和交易日级盈亏熔断

尚未实现：

- 批量写入队列和历史数据查询界面
- 真实券商适配器与生产级风控
- 策略模板系统和多股联合指标
- 多股联合指标
- 生产级通知后端
