# 自定义策略指标口径

本目录服务于“多个指标组成一个策略”的本地研究。指标不拥有资金或持仓；一次策略回测拥有自己的初始资金、持仓和费用。目录扩展不等于任意公式编程，也不代表策略收益有效。

## 版本微调与回测快照（2026-11-12）

策略管理可从任一已保存版本创建「复制微调」草稿；系统取该策略当前最高版本号加一，复制指标、入出场和风控定义，并更新草稿创建时间。编辑后仍须校验、另存为不可变版本，原版本不受影响；未保存草稿不能回测。复制操作写 `ui_click` 行为日志，记录策略 ID、源版本和草稿版本，不记录定义正文。

编译自定义策略时，运行计划固定已验证定义的规范化完整 JSON 及其 SHA-256。单次和矩阵回测把该定义快照、校验值、实际运行参数与现有风控规则一起写入结果归档的规则快照；恢复时重新核对定义内容、策略 ID、版本和校验值，发现不一致则拒绝恢复。旧归档没有定义快照时仍可读取，但不能称为已校验的完整定义。实现复用既有归档字段，未触发全库结构迁移。

**更新（2026-11-12，逐笔首版）**：引擎现可让 `nextOpen` 的单股自定义策略直接处理原始逐笔 Tick，并有本地命令入口；工作台和策略矩阵仍运行在 Tick 派生 K 线，尚未接入逐笔入口。逐笔模式每笔成交作为一条指标观测，O/H/L/C 均为成交价，`period` 计成交笔数，信号在下一笔 Tick 尝试成交；详见[逐笔引擎验收](custom-strategy-raw-tick-engine-2026-11-12.md)。策略版本和回测输入是两条独立身份：定义快照固定规则，数据集身份和数据版本固定使用的历史行情。

真实历史分钟线两股复跑、盈利归档恢复及容量见[版本快照验收报告](custom-strategy-snapshot-validation-2026-11-12.md)。

## 目录与输入

既有 10 类为 SMA、EMA、RSI、MACD、BOLLINGER、ATR、VOLUME_SMA、ROC、VWAP、PP_FLOW。2026-09-07 已新增下表 8 类，总计 18 类；构建与验收证据见 [当日扩展验收报告](custom-strategy-indicator-expansion-2026-09-07.md)。

2026-11-12 又新增 STOCH 与 SUPERTREND，当前可选 **20 类**。STOCH 的 K、D 用简单移动平均平滑，与既有 KDJ 的递归平滑不同；SUPERTREND 的 ATR 使用 Wilder 平滑，与现有简单平均 ATR 输出并非同一序列。

| 新增类别 | 参数及默认值 | 计算与首个可用位置 |
|---|---|---|
| STOCH | period=14、kPeriod=3、dPeriod=3、output=k（k/d） | 原始 K=100×(收盘−窗口最低)/(窗口最高−窗口最低)，再分别以 SMA 平滑 K、D；零振幅时原始 K=50；D 首次在第 period+kPeriod+dPeriod−2 根可用 |
| SUPERTREND | period=10、multiplier=3、output=line（line/direction） | TR 经 Wilder 平滑后形成 ATR；结合前一轨道与前收盘递推趋势线，方向上为 1、下为 −1；第 period 根开始 |

新增指标用于 DSL v2/v3 的 Bar 序列，向导以 v2 保存。每策略最多 16 个命名序列的既有限制不变：20 是可选类别数，不是必须一次添加 20 个序列。KDJ 的 K、D、J 或通道的三条输出需分别添加序列、使用不同别名。

| 新增类别 | 参数及默认值 | 计算与首个可用位置 |
|---|---|---|
| WMA | period=14 | 收盘价线性加权平均，最旧权重 1、最新权重 N；第 N 根开始 |
| BIAS | period=14 | 收盘价相对 N 根简单均线的百分比乖离；第 N 根开始 |
| CCI | period=20 | HLC3 相对自身 N 根均值的偏离，除以 0.015 倍平均绝对偏差；第 N 根开始 |
| WR | period=14 | `-100 × (窗口最高价−收盘价)/(窗口最高价−窗口最低价)`；第 N 根开始，范围 −100～0 |
| OBV | 无 | 首有效根从 0 开始；收盘较前根上涨加本根量、下跌减本根量、持平不变；不按日清零 |
| MFI | period=14 | HLC3×成交量按 HLC3 涨跌分为正负流，最近 N 次变化计算 0～100 的比率指标；第 N+1 根开始 |
| KDJ | period=9、kPeriod=3、dPeriod=3、output=k（可选 k/d/j） | N 根 RSV，K/D 从 50 起按 1/kPeriod、1/dPeriod 递归平滑，J=3K−2D；第 N 根开始 |
| DONCHIAN | period=20、output=middle（可选 upper/middle/lower） | 含当前 Bar 的 N 根最高价、最低价及二者中点；第 N 根开始 |

周期表示输入 Bar 的根数。例如 1m 上 period=14 是 14 根分钟 Bar，不是 14 天。该路径是历史 Tick 派生指定周期 Bar 后计算，并非自定义原始逐笔策略。

新增 period 范围为 2–500 的整数，KDJ 的 kPeriod/dPeriod 为 1–100 的整数。引用策略参数时也必须声明为 integer，不因默认值恰好是整数就接受 real 类型。

## 边界与可复现性

- 热身不足返回缺失值，不以 0 填充。新滚动指标按所依赖的输入检查缺失或非法价格、成交量；不引用未来 Bar。
- BIAS 均值不为有效正数时缺失；新指标不发布 NaN 或无穷大，计算中间值溢出也不能伪装成有限的 0、−100 或 100。
- 零振幅时 KDJ 的 RSV 为 50，WR 为 −50；CCI 平均绝对偏差为零时为 0。MFI 正负流均为零时为 50，仅正流为 100，仅负流为 0。这些是本项目明确的退化窗口口径。
- KDJ 使用上述递归口径，不将它与采用简单均线平滑的 STOCH 当作完全相同的算法。J 可能超出 0～100，不截断。
- OBV 坏输入或累计溢出本根缺失，断开前收盘比较；保留此前有效累计值，但不把跨缺失段的涨跌量补入。重置策略会重置全部指标状态。
- DONCHIAN **包含当前 Bar**；用当前收盘价严格大于当前上轨的条件无法实现通常所指的“突破前一通道”。目前没有通用序列位移操作，不能暗中替用户改成上一根通道。
- 序列身份包含类型、全部参数与输出选择；相同类型不同周期、不同 K/D 平滑值或不同输出不能错误合并。
- 保存的策略 JSON 包含本次具体指标参数、别名和规则；预览不替代含资金、费用和 T+1 的正式回测。

## 公式参考

WMA 的近期线性加权规则参考 [TradingView WMA](https://www.tradingview.com/support/solutions/43000594680-weighted-moving-average/)。CCI 的 HLC3、平均绝对偏差及 0.015 系数参考 [TradingView CCI](https://www.tradingview.com/support/solutions/43000502001-commodity-channel-index-cci/)。MFI 的正负典型价格资金流参考 [TradingView MFI](https://www.tradingview.com/support/solutions/43000502348-money-flow-mfi/)。通道上下轨和中点参考 [TradingView Donchian Channels](https://www.tradingview.com/support/solutions/43000502253-donchian-channels-dc/)。上面的热身、种子和退化窗口规则以 PPTrader 本文及回归测试为准，不宣称与所有第三方终端数值逐点一致。

STOCH 的两层 SMA 参考 [TradingView Stochastic](https://www.tradingview.com/support/solutions/43000502332-stochastic-stoch/)；SUPERTREND 的轨道递推参考 [TradingView Supertrend](https://www.tradingview.com/support/solutions/43000634738-supertrend/)。新增内置 STOCH 均值回归策略在超卖区 K 上穿 D 买入、超买区 K 下穿 D 卖出；阈值和周期属于本项目策略参数。
