# 自定义策略 DSL v1

## 定位与安全边界

自定义策略使用声明式 JSON，不执行用户提供的 C++、Python、JavaScript、文件、网络、循环或动态模块。v1 定义必须使用 `custom_` 前缀、声明 `market: "A股"` 和 `executionScope: "backtest_only"`。已保存版本可通过控制器进入异步历史回测，但不能进入模拟/实盘执行。

## 最小定义

```json
{
  "schemaVersion": 1,
  "strategyId": "custom_sma_filter",
  "version": 1,
  "name": "自定义均线过滤",
  "description": "快线上穿慢线时进入，反向交叉时退出",
  "author": "本地用户",
  "market": "A股",
  "executionScope": "backtest_only",
  "createdAt": "2026-08-08T00:00:00.000Z",
  "position": {"type":"fixedPercent","percent":0.5},
  "risk": {"stopLossPercent":5,"takeProfitPercent":10,"maximumHoldingBars":20,"tPlusOne":true},
  "parameters": [
    {"key":"fastPeriod","displayName":"快速周期","type":"integer","defaultValue":5,"minimumValue":2,"maximumValue":120,"step":1}
  ],
  "entry": {"op":"gt","left":{"field":"close"},"right":{"indicator":"SMA","period":{"parameter":"fastPeriod"}}},
  "exit": {"op":"lt","left":{"field":"close"},"right":{"indicator":"SMA","period":{"parameter":"fastPeriod"}}}
}
```

## 表达式白名单

- 布尔操作：`and`、`or`、`not`。
- 比较操作：`gt`、`gte`、`lt`、`lte`、`eq`、`crossesAbove`、`crossesBelow`。
- 行情字段：`open`、`high`、`low`、`close`、`volume`。
- 指标：`SMA`、`EMA`、`RSI`、`MACD`、`BOLLINGER`、`ATR`、`VOLUME_SMA`。
- 操作数：有限常量、参数引用、行情字段或白名单指标。表达式最多 8 层、64 个节点；单个 `and/or` 最多 16 个子条件。

未知字段、未知指标、未来字段、非有限数值、越界周期、重复参数和白名单外操作符全部拒绝。参数最多 32 项，仅支持 `integer` 和 `real`。

## 仓位与风险规则

- `position` 可省略；当前仅支持 `fixedPercent`，`percent` 范围为 `0.01–1.0`，默认 `1.0`。
- `stopLossPercent` 范围为 `0–100`，`takeProfitPercent` 范围为 `0–1000`；`0` 表示关闭。
- `maximumHoldingBars` 是 `0–5000` 的整数，按买入后的有效 Bar 计数；`0` 表示关闭。
- A 股始终强制 `tPlusOne=true`。买入交易日内触发退出时只锁存首次原因；即使价格随后恢复，也会在下一交易日首个有效 Bar 执行退出，避免信号状态与可卖状态分叉。

编译结果中的 `ruleSnapshot` 固定保存仓位比例、止损、止盈、最大持仓 Bar 和 T+1。旧定义不包含 `position/risk` 时使用兼容默认值，不改写已保存 JSON。

## 版本规则

SQLite 以 `(strategy_id, version)` 为主键。同版本同内容重复导入视为幂等成功；同版本内容不同必须拒绝并要求增加版本号。保存后可通过控制器查询或导出规范化 JSON。`runCustomStrategyBacktest()` 必须显式提供版本（`0` 表示当前最新版本），并把策略版本、规范化参数、实际 K 线 SHA-256 数据版本和规则快照一并写入回测结果。

## 指标与求值语义

- SMA、成交量均线和布林带使用连续窗口；任一窗口值缺失时结果缺失。布林带使用总体标准差。
- EMA 以首个完整周期的 SMA 为种子，之后使用 `2 / (period + 1)` 递推；缺失值会重置种子。
- RSI 使用最近 `period` 个涨跌的简单平均；没有下跌时为 100。ATR 使用真实波幅的连续简单平均。
- MACD 的快慢线使用上述 EMA，信号线以首个完整信号周期的 MACD 均值为种子。
- 普通比较遇到缺失值返回 Unknown；`not Unknown` 仍为 Unknown。`and/or` 使用三值逻辑。
- 上穿/下穿要求相邻两个有效 Bar；缺失值会清空前值，禁止跨数据缺口产生伪交叉。
- 最小预热根数取所有指标的最大值；包含交叉条件时额外增加一根用于确认前值。

`TradingController.analyzeCustomStrategyJson()` 返回规范化参数、规则快照、指标/条件数量和预热根数。成功结果为 `backtest_ready`、`executable=true`；这里的“可执行”仅指受控历史回测。任务开始前，控制器从不可变版本编译计划；引擎再次核对实例声明的 ID、版本、参数和规则快照，任一不一致都会拒绝运行。
