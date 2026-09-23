# 实时探针 Permission denied 根因调查（2026-08-28）

## 结论

下午 500 标的探针的 `Permission denied` 来自 Codex 默认网络沙箱，不是腾讯端点拒绝，也不是 `TencentQuoteParser` 或策略引擎缺陷。下午探针由未申请外网权限的自动化命令启动，子进程继承网络禁用。

## 最小复现与单变量对照

同一 `pptrader_live_strategy_monitor.exe`、同一缓存和同一腾讯 Provider 只请求一个 A 股代码、一个批次：

| 唯一变量 | 收到 | 错误 | 结论 |
| --- | ---: | --- | --- |
| 默认受限网络 | 0/1 | `Permission denied` | 精确复现下午症状 |
| 显式允许外网 | 1/1 | 无 | 二进制、URL、解析和腾讯响应正常 |

受限报告位于 `artifacts/permission-denied-diagnosis-2026-08-28/sandbox.json`，SHA-256 为 `48B79DFE66D1D5116A9F73370F63B2A8F62B7F4C83E510ABADD49AC4B5F0D88E`；放行报告为 `unrestricted.json`，SHA-256 为 `A2C6347209380816D665AAB215F6580E0BD75CD27901D4BD04BEA919C8BCDD11`。

用户批准外网权限后又执行一轮 500 标的受控验证：500/500 报价、610 ms、500/500 策略任务、错误 0、持久化写入 0，进一步排除代码和上游持续封禁。该轮发生在收盘后，源行情时刻保持 15:00，只能作为权限与覆盖率证据；报告 `unrestricted-500.json` 的 SHA-256 为 `E95F09AEB9A387495FEEAC19C790445710ECEA8BED136587BE54D936D0C8C009`。

## 代码路径

`TencentMarketDataProvider` 请求 `https://qt.gtimg.cn/q=...`。当 `QNetworkReply::error()` 非零时，当前实现把 `reply->errorString()` 写入批次错误；默认沙箱在发出外网请求前拒绝访问，Qt 因而返回 `Permission denied`。放行后同一路径取得 1/1 报价并完成 1/1 策略任务。

## 影响与纠正

- 原始 `afternoon.json` 是有效的“验收环境失败”证据，但不是腾讯全天可用性证据。
- 2026-08-28 只能确认上午 113 个 500/500 批次和 56,500 个策略任务零漏算；下午 Provider 验收仍待重跑。
- 不修改业务代码。正确修复点是验收编排：真实网络探针必须显式获得外网权限，并在首轮 0 报价/权限错误时快速终止。
- 没有新增产品回归测试；正确回归 seam 是“同一真实探针在受限/放行环境的差分启动”，仓库内 fake 网络测试无法证明宿主沙箱权限。
