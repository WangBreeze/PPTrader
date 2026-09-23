# 自定义策略向导（Custom Strategy Wizard）Spec

> 状态：单标的向导已于 2026-08-19 接通步骤 1–7、本地数据门禁、校验、不可变版本保存和严格本地回测；步骤 3 参数表单、步骤 4 嵌套 AND/OR/NOT 条件编辑与步骤 7 样本窗/验证记录/逐 Bar 回放已产品化，并已有真实 QML 组件级流程回归。

## Problem Statement

量化用户希望**不写代码**，通过可视化向导创建、验证并发布受限自定义策略——组合指标、定义条件、设置仓位与风控、使用本地历史 Tick 回测，并在明确授权后进入模拟执行。真实券商执行不属于当前范围。

现状（2026-08-19）：步骤 1“策略信息”已支持 ID、名称、作者、说明；步骤 2“标的与周期”已接入 SQLite 历史 Tick 派生数据集选择，且不联网、不把数据集身份写入 DSL。当前只开放单标的 DSL v2；多标的 DSL v3 UI 尚未开放。步骤 3 按 C++ schema 动态编辑指标参数；步骤 4 独立编辑字段/序列/常量和嵌套 AND/OR/NOT 条件；步骤 5 配置仓位风控；步骤 6 明确 `backtest_only` 边界；步骤 7 可设置样本窗、运行已保存版本的严格本地回测、恢复验证记录并只读回放 Bar；步骤 8 可校验、分析和异步保存下一不可变版本。真实 QML 测试已覆盖旧草稿恢复、参数编辑、步骤门禁、嵌套条件、异步预览/保存和异步回测结果；主要缺口转为多标的 DSL v3。

## Solution

把现有 8 步骨架补全为可用向导。单标的向导默认生成 `schemaVersion 2`；多标的横截面选择生成 `schemaVersion 3`。所有 JSON 都经过 `CustomStrategyValidator` 与 `CustomStrategyCompiler`；回测只读本地 Tick 派生数据，模拟执行必须显式选择 `mock_execution` 并进入统一审批/风控，不能自动升级为真实下单。任何一步可回退修改，草稿本机自动保存。

## User Stories

1. As a 量化交易者, I want 在向导里给策略命名并选择类型/市场, so that 产出的策略带正确的元数据。
2. As a 量化交易者, I want 选择标的与周期（多市场、多标的、多周期）, so that 策略在正确的标的上运行。
3. As a 量化交易者, I want 从指标白名单组合任意数量指标并设别名/周期/输出, so that 后续条件能引用它们。（已实现）
4. As a 量化交易者, I want 拖拽排序指标, so that 指标顺序可预期。
5. As a 量化交易者, I want 用「字段 / 序列 / 常量」三态操作数定义入场条件, so that 条件能表达 close 上穿某指标等语义。（已实现）
6. As a 量化交易者, I want 定义出场条件组, so that 策略知道何时离场。（已实现）
7. As a 量化交易者, I want 用嵌套 AND/OR/NOT 组合条件, so that 能表达同时满足、任一满足和取反语义。（已实现）
8. As a 量化交易者, I want 设置仓位（固定金额 / 固定比例）, so that 每笔仓位可控。
9. As a 量化交易者, I want 设置风控（最大持仓占比、止损、止盈、最大持有 bar 数、T+1）, so that 风险被约束。
10. As a 量化交易者, I want 选择信号价（触发价收盘 / 次日开盘）, so that 成交价假设明确。
11. As a 量化交易者, I want 草稿自动保存并在下次打开恢复, so that 中断不丢工作。（已实现）
12. As a 量化交易者, I want 实时 JSON 校验并看到错误信息, so that 不合法的配置能被立即发现。
13. As a 量化交易者, I want 在向导内预览回测结果, so that 无需切 JSON 模式就能验证想法。
14. As a 量化交易者, I want 在回测验证步跑历史样本与流式回放, so that 策略质量被量化。
15. As a 量化交易者, I want 在执行步选择人工审批或自动提交, so that 实盘风险符合团队策略。
16. As a 量化交易者, I want 在发布步设置版本并发布运行, so that 策略进入运行态。
17. As a 量化交易者, I want 随时返回前面步骤修改, so that 迭代不用重来。
18. As a 新手用户, I want 每步有说明文案, so that 不熟悉量化术语也能上手。
19. As a 开发者, I want 向导产出与既有 JSON 契约严格一致, so that 编译/校验/回测流水线无需改动。

## Implementation Decisions

- **JSON 契约复用**：向导产出物即 `CustomStrategyDefinition.document`。单标的是 `schemaVersion 2`；多股横截面使用 v3 的 `universe / crossIndicators / selection`。向导不引入私有执行格式。
- **模块边界**：向导（QML `CustomStrategyWizard`）只负责「收集选择 → 拼 JSON → 展示状态」，校验、分析、保存和运行分别走 `TradingController` 的 `validateCustomStrategyJson`、`analyzeCustomStrategyJson`、`saveCustomStrategyJson`、`runSelectedLocalCustomStrategyBacktest`，不直接触达 service 或 SQLite。
- **条件模型**：`entry`/`exit` 支持比较节点及递归 `{ op: "and|or", conditions: [...] }`、`{ op: "not", condition: ... }`；操作数三态 `field`（close/open/high/low/volume）、`series`（指标别名）、`value`（常量）；运算符 gt/gte/lt/lte/eq/crossesAbove/crossesBelow。QML 编辑器使用稳定路径和扁平预序行展示，限制 3 层；C++ 校验上限仍为深度 8、节点 64。
- **指标目录**：当前向导白名单仅时序指标（SMA/EMA/RSI/MACD/BOLLINGER/ATR/VOLUME_SMA/ROC/VWAP/PP_FLOW）。C++ 侧已存在**横截面指标**（Rank/ZScore/Percentile/Mean/Median/Peer，`CompiledCrossIndicator`），但向导尚未暴露——决定：横截面指标归入第 2 步「标的与周期」多标的选择后才启用，避免与单标的时序语义混淆。
- **参数 schema**：`CustomStrategyValidator::schemaDescriptor()` 的 `indicatorSchemas` 是参数类型、默认值、范围、选项和跨字段关系的事实源；QML 只通过 `TradingController.customStrategySchema` 读取。`CustomStrategyWizardContract.js` 负责创建行、补齐旧草稿、字段级门禁和只序列化 schema 声明的参数。
- **执行范围**：默认保持 `backtest_only`；后端已支持显式 `mock_execution`，并受 `SignalExecutionPolicy`、`PositionSizer`、`StrategyExecutionCoordinator` 和审批配置约束。向导尚未完整提供该切换，真实执行仍禁止。
- **草稿持久化**：沿用 `Qt.labs.settings` 本机保存（`wizardDraft`），保留 legacy 字段迁移（`visible→seriesVisible`、`left→leftOperand`、`right→rightOperand`），旧平铺 `entry`/`exit` 迁移到 `entryCondition`/`exitCondition` 根 AND 树；不写入策略库。
- **八步状态机**：步骤 ①② 已实现单标的严格本地表单与门控；③管理指标；④独立管理入场/出场条件；⑤配置仓位风控；⑥明确执行边界；⑦⑧接通严格本地验证与不可变保存。继续沿用 `activeStep` + 侧栏进度条；运行前必须满足“已保存版本、草稿未变化、本地数据可用”三项门禁。
- **命名/术语**：严格使用领域词汇——`Instrument`（标的）、`Candle`（bar/周期）、`Tick`、`TradeSignal`（信号）、`Strategy`、`MarketSession`；不新造近义词。

## Testing Decisions

- **主 seam（理想数量：一个）**：把测试锚定在 **`CustomStrategyValidator` 的 JSON 契约**上——向导正确性的判据是「`buildJson()` 的输出恒能通过 `parseAndValidate`，且非法配置被拒绝并给出错误」。这是最高层、最稳定的 seam，覆盖向导所有步骤的合成正确性，而不关心 UI 细节。
- **集成确认 seam**：`customStrategyPreview(json, symbol, period, N)` 走「编译 → 回测」全链路，用于确认产出物不仅能校验、还能被编译运行。
- **当前覆盖**：`tst_custom_strategy.cpp` 固定 C++ 参数 schema，并从 DSL 到运行信号验证嵌套 OR/NOT；`tst_custom_strategy_wizard.cpp` 调用 QML 实际使用的 `CustomStrategyWizardContract.js`，验证元数据生成、数据集身份不进入 DSL、产物通过 `parseAndValidate`、调用方指定的不可变版本号、legacy 草稿角色/操作数/缺失参数及平铺条件迁移、稳定路径增删改、指标移除递归清理，以及样本窗日期拒绝。`tst_custom_strategy_wizard_qml.cpp` 直接加载真实 QML 组件，通过公开 QML 函数覆盖 legacy 草稿恢复、动态参数编辑、步骤 3 门禁、嵌套 OR/NOT、`raw_tick_cache_v1` 预览、v1/v2 不可变保存、保存失败和异步严格本地回测成功/失败；其控制器替身只代表 QML/应用组合根边界，设置写入临时 INI。
- **不测实现细节**：只断言「产出的 JSON 结构 + 校验/编译结果」，不断言 QML 内部 ListModel 的中间状态或控件布局。

## Out of Scope

- 指标公式本身的数学正确性（属 `CustomIndicatorRuntime`，不在向导范围）。
- 横截面多标的选股后端已经实现；本规格剩余范围仅是向导中如何安全暴露。
- 实盘下单/券商对接（`BrokerGateway` 真实适配器）。
- 策略库的云端同步/团队协作（向导只写本机草稿与本地策略库）。

## Further Notes

- 向导第 4 步使用缩进树形条件编辑器，支持同侧 AND/OR 子组及组/比较节点 NOT；根节点不可删除，子组创建时至少带两个比较节点。第 7 步已内嵌样本窗、验证记录和逐 Bar 回放，但只有匹配当前已保存版本的已完成结果可写入验证记录。
- 侧栏 8 步文案已在 `流程说明` tooltip 中完整定义（策略信息 → 标的与周期 → 指标组合 → 信号条件 → 仓位与风控 → 执行与审批 → 回测验证 → 发布与运行），是步骤拆分的事实来源。
- 提交改动时遵守本仓库 QML 规则：ListModel delegate 用 `required property`（勿用 `model.roleName`），自定义属性名避开 `visible/width/left/right/color` 等 FINAL 名（2026-08-15 已踩过一次）。
- 相关文档：`docs/custom-strategy-dsl.md`（DSL 契约）、`docs/design-strategy-execution.md`（执行层设计）。

## 待确认

- 多股 DSL v3 模式的进入条件与横截面配置 UI；当前步骤 2 明确只支持单标的本地数据集。
- 是否在未来向导中开放 `mock_execution`；当前版本固定保存为 `backtest_only`，模拟执行只能在执行中心显式启用。
- 主 seam 保持 `CustomStrategyValidator` JSON 契约；真实 QML 生成、迁移、保存与运行状态门禁测试均已落地。
- 截至 2026-08-19，QML 预览与保存均通过 `requestCustomStrategyPreview()` / `requestSaveCustomStrategyJson()` 在独立工作线程执行。每类请求公开 loading、当前/已解析 requestId、result 和 error；旧响应不会覆盖新请求。界面采用操作区局部 loading，不锁定顶层工作台。
