# A 股停复牌事实导入

## 能力边界

阶段 12E/12F 提供用户显式触发的本地 CSV 导入，不抓取网页、不调用未承诺稳定性的交易所接口，也不从缺失 K 线推断停牌。原始事实应来自上交所或深交所公开信息，经人工或外部受控流程转换后再导入。上交所公开数据目录列有停复牌信息；来源版本必须填写公告号、导出日期或数据版本。上证所信息网络的正式统计数据服务包含股票和可转债停复牌查询，但需要业务申请，当前项目未取得授权或接入凭据。

参考入口：

- [上交所公开数据目录](https://big5.sse.com.cn/site/cht/www.sse.com.cn/market/publicdata/)
- [上交所停复牌信息](https://big5.sse.com.cn/site/cht/www.sse.com.cn/disclosure/dealinstruc/suspension/)
- [深交所停复牌公告](https://www.szse.cn/disclosure/notice/temp/index.html)
- [上证所信息网络统计数据服务](https://www.sseinfo.com/services/data/szjr/)

## CSV 契约

文件必须为 UTF-8，表头和顺序固定：

```csv
symbol,name,venue,trading_date,state,reason
600519,贵州茅台,SSE,2026-08-20,suspended,"临时停牌公告"
601318,中国平安,上交所,2026-08-21,tradable,恢复交易
```

- `symbol`：6 位沪深 A 股代码；北交所不在当前范围。
- `venue`：`SSE`/`上交所` 或 `SZSE`/`深交所`，必须与代码和本次声明来源一致。
- `trading_date`：ISO 日期；周末和已知公告休市日拒绝导入。
- `state`：只允许 `suspended`/`停牌` 或 `tradable`/`可交易`。
- `reason`：可为空；含逗号时按标准 CSV 引号转义。

## 执行与审计

`AShareInstrumentDayImportService` 在后台逐行解析，单行上限 1 MiB。任何非法行、重复“代码+交易日”或来源冲突都会使整份文件失败。操作分两步：先“预览差异”，只读 SQLite 并返回新增/不变/修订数量；再“确认事务导入”，重新校验同一文件的 SHA-256。预览后文件、来源或版本变化会拒绝确认。

schema v11 的 `ashare_instrument_day_import_runs` 保存批次摘要和修订原因，`ashare_instrument_day_import_items` 保存该批次每条事实的不可变快照、变化类型和前序批次 ID；`ashare_instrument_day_status` 仅作为当前有效投影。审计批次、逐条明细和当前投影在同一事务写入，失败保留原数据。更改既有事实必须填写修订原因；相同 `source + sourceVersion + SHA-256` 幂等复用已有批次。旧 v10 批次没有可回填的逐条明细，仍保留摘要，不伪造历史记录。数据库不保存目录、凭据或网络地址。

## 已知限制

当前没有交易所网页/API 自动适配器、定时增量任务或生产数据验收。修订历史已可保留，但尚未实现批次撤回、回滚当前投影或授权数据服务客户端。2024–2026 市场日历有公告覆盖；其他年份工作日仍标为 `weekday-fallback`，导入事实不能把该市场日历降级状态升级为权威。未导入的标的日继续为 `unknown`。
