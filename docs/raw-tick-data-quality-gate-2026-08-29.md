# 原始 Tick 回测数据质量准入（H3B.12）

> 状态更新（2026-08-30）：本文件记录 H3B.12 的门控规则；其中“复用 v23 批次导入审计”的实现已被 H3B.13 替代。当前准入只信任 schema v24 中与原始内容 SHA-256 匹配的逐日质量凭证；H3B.14 又新增权威休市日成交冲突拒绝并完成跨月真实重解析。H3B.15 将拒绝判定前置到事实物化之前；H3B.16 增加逐日修复字段，并把审计有效数/事实数不一致提升为拒绝。详见 `docs/raw-tick-day-quality-audit-2026-08-29.md`、`docs/raw-tick-quality-remediation-2026-08-29.md` 与 `docs/raw-tick-quality-remediation-manifest-2026-08-30.md`。

## 结论

截至 2026-08-29，批量原始 Tick/派生 K 线回测已在引擎启动前执行严格本地的数据质量准入。判定只读取 SQLite、权威 A 股交易日历、显式停复牌事实和 Tick 导入审计；不会联网、自动补数、从零成交推断停牌或静默复权。SQLite schema 保持 v23。

## 判定规则

| 事实 | 结果 |
| --- | --- |
| 预期交易日有原始 Tick | 继续检查 |
| 无 Tick 且有带来源/版本的明确停牌事实 | `warning`，记为已解释无成交日 |
| 无 Tick 且停复牌状态未知或明确可交易 | `reject` |
| 停牌事实与原始成交同时存在 | `reject` |
| 权威日历已知休市日存在原始成交 | `reject`，列入 `unexpectedNonTradingDates` |
| 单日坏行率大于 1% 或导入审计为 `error` | `reject` |
| 有坏行但不超过 1% | `warning` |
| 旧数据缺导入审计 | `warning`，绝不反推零坏行 |
| 公司行动覆盖未知 | `warning`，不生成复权价格 |
| 已知公司行动落入未复权 Tick 请求范围 | `reject` |

`BacktestDataQualityGate` 输出 `pass/warning/reject`、准入布尔值、缺口日期、已解释无成交日期、未知审计日期、坏行数量/比例、公司行动覆盖和中文原因。`BatchBacktestResultListModel` 公开 `dataQualityStatus`、`dataQualityEligible` 和 `dataQualityAssessment`；拒绝项不会创建 `BacktestEngine`。

H3B.15 后，拒绝项也不会再调用原始 Tick 或派生 K 线的物化查询；`actualBars` 取自已验证的不可变身份。H3B.14 同一 10 标的隔离库 5 次复测 p50/p95 为 328/482 ms，旧实现为 32,104 ms，且 10/10 拒绝原因不变。

H3B.16 后，`dataQualityAssessment` 还提供 `contradictorySuspensionDates`、`excessiveSkippedDates`、`failedAuditDates` 和 `auditCountMismatchDates`。调用方不再需要从中文消息反解析日期；审计计数不一致属于身份冲突并直接拒绝。

## 既有真实样本复核

只读复核既有 10 标的、2026-06-10～2026-08-28 隔离证据：3,030,617 条 Tick 的每个标的都有 4～9 个日历缺口，共 50 个，零缺口标的为 0。该库的停复牌事实、Tick 导入批次和导入明细均为 0 行。因此这些旧样本现在会被新门槛拒绝，不能继续作为“完整数据”回测；此前性能与规则链路数据仍是历史证据，但不再代表数据质量准入通过。

## 验证与边界

- Debug 全量构建成功。
- CTest 43/43 通过，包含交易日边界、节假日、明确停牌、事实冲突、坏行阈值、公司行动风险、SQLite 审计读取和批量执行门控。
- `all_qmllint` 通过；本阶段未修改 QML 视觉，未启动网络或访问 NAS。
- 尚缺真实公司行动数据源、NAS 同步批次的质量审计落库/补录，以及现有 10 标的缺口的授权停牌或补数证据。公司行动已完成准入契约和测试，不等于真实环境已验收。
