# 插件管理 UI 与多股回测 UI（Figma 设计稿）

> 状态：设计稿已完成（2026-08-15），阶段 11A 与 11B 均已实现并验收（2026-08-20）。插件页在 Figma 信息层级上增加第三方 C++ 插件区，只显示 Controller 真实数据。
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
- **运行时插件列表**（实现为宽屏 430px）：`customStrategyExecutionCatalog` 合并已保存最新版本与运行时状态，卡片显示 name、stateName、strategyId·version·symbol、error，并对 `backtest_only` 显式禁用。生命周期对应 `enableCustomStrategyExecution` / `disableCustomStrategyExecution`。
- **第三方 C++ 插件**：扫描结果显示 manifest/SHA-256 状态；加载后由 `strategyPluginParameterSchema` 动态生成参数表单，再显式激活或卸载。目录为空时保持真实空状态。
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

## 实施顺序

1. **11A 多标的向导与横截面回测 UI（已完成）**：已替换“必须手写完整 v3 JSON”的主要交互，基于本地 catalog 选择周期一致、身份有效的数据集，生成 universe/crossIndicators/selection/position，并复用现有运行服务和结果模型。1600×1000 双栏与 1120×760 单列/滚动已在副屏复核；宽屏配置栏固定 430px 的回归断言已加入真实 QML 测试。
2. **11B 插件管理 UI（已完成）**：`StrategyPluginPanel.qml` 已接入声明式生命周期、第三方 DLL 动态参数、回放、审批和通知公开接口；1600×1000 双栏与 1120×760 单列/滚动均已副屏复核，Qt Test 锁定真实 Controller 契约形状。

两个阶段都必须补 Qt/QML 流程测试、`all_qmllint`、主程序加载、1600×1000 和 1120×760 副屏截图验收；不得访问网络或把模拟执行描述为实盘。
