# 插件管理 UI 与多股回测 UI（Figma 设计稿）

> 状态：设计稿已完成（2026-08-15），待按稿实现 QML。
> 依据：后端数据契约（`TradingController` 已暴露的插件生命周期/回放/审批/通知与多股回测接口）。

## Figma 位置

文件 `PPTrader`（key `YFTuHENpOe71mzWh8006OH`），页面「02 指标与策略」，两个 1600×1000 frame：

| 画面 | 节点 ID | 名称 | 位置 |
|---|---|---|---|
| A 插件管理 | `1393:17` | `PPTrader / 自定义策略 / 运行时插件管理` | (5400, 0) |
| B 多股回测 | `1394:281` | `PPTrader / 多股回测 / 横截面轮动回测` | (7200, 0) |

## 画面 A：运行时插件管理

区块与后端字段对应：

- **页头**：标题 + `pendingStrategyIntentCount` 徽章 + 通知设置入口。
- **运行时插件列表**（左 470px）：`customStrategyRuntimePlugins()` → 卡片（name、stateName 徽章、strategyId·version·symbol、error）。生命周期操作按 `state`：Active→卸载(danger)；Loaded/Unloaded→加载并激活(outline) + 卸载(ghost)。对应 `enableCustomStrategyExecution` / `disableCustomStrategyExecution`。
- **模拟回放**（右上）：`startCustomStrategyReplay(strategyId, symbol, periodSeconds, barCount, intervalMs)` 五参数输入；`customStrategyReplayRunning` 徽章 + `replayedBars/totalBars` 进度条；停止按钮。
- **审批队列**（右中）：`strategyIntentModel`（意向/时间、策略、标的、方向、价格、数量、状态、操作）+ `approveStrategyIntent` / `rejectStrategyIntent`。
- **通知设置**（右下）：7 开关（desktopEnabled/webhookEnabled/notifySignal/notifyApproval/notifyOrderSubmitted/notifyExecutionRejected/notifyIntentExpired）+ Webhook URL + `configureNotifications`。

## 画面 B：多股横截面回测

- **左侧配置面板**（430px）：Universe（explicit 6 位代码 chips）→ crossIndicators（alias + RANK/ZSCORE/MEAN 算子 + of 字段 + 升/降序）→ selection（topN + rankBy + count + ascending）→ position（equalWeight）→ entry/exit（`{cross: alias}`）→ K 线数量 + 开始/取消。
- **右侧**：状态条（`crossSectionalBacktestStatusText` / `Error` / `ResultConfiguration`）→ 权益曲线（`crossSectionalEquityCurveModel`，purple 净值线 + 收益/回撤/年化标签）→ 调仓/成交记录（`crossSectionalTradeModel`）。

对应后端：`runCrossSectionalBacktest(strategyJson, barCount)` / `cancelCrossSectionalBacktest()` / `crossSectionalBacktestRunning/StatusText/Error/ResultConfiguration` / `crossSectionalEquityCurveModel` / `crossSectionalTradeModel`。

## 视觉约束（已核实）

- 全部颜色取自 PPTheme（cyan `#21B6E9`、red `#FF5B62`、purple `#9D8CFF` 等），未发明新配色。
- PpButton：radius 6；primary=purple 填充、outline=cyan 描边、ghost=bgRaised、danger=红描边。
- 布局：1600×1000；左抽屉 318px；客户区 1282 ≥ 1120；未复用 PortfolioBacktestPanel 旧布局。

## 待实现（后续会话）

按稿实现两个 QML 组件（如 `StrategyPluginPanel.qml` / 替换或增强 `CrossSectionalBacktestPanel.qml`），
并接入上述后端接口；实现后 `all_qmllint` + 主程序加载 + 副屏截图验收。
