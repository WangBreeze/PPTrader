# H3B.26 原始 Tick 源文件只读质量适配器

## 目标与边界

`RawTickSourceQualityAdapter` 在同步或导入前检查用户显式指定的单个源文件，并生成机器可读修复报告。它不会扫描父目录、连接 NAS/网络、修改源 CSV、写 SQLite 或自动把未知值猜成合法数据。本阶段建立的是源端修复前的确定性预检，不代表 H3B.14 的跨月样本已经修好或重新准入。

适配器使用 `QFile::readLine()` 流式读取，同时计算完整文件 SHA-256。报告包含识别格式、总行数、有效/坏行/倒序数、B/S/M 分布、原始与规范化成交量、诊断路径/行号/原因，以及 `ready`、`clean_reexport`、`specify_volume_unit`、`provide_direction_mapping_version`、`missing_source_file` 等动作码。详细诊断有界保存，默认 200 条、最大 10,000 条；`diagnosticTotalCount` 和 `diagnosticsTruncated` 保留完整计数与截断事实。

## 显式数据合同

- 成交量必须声明为 `lots` 或 `shares`；未知单位直接拒绝。只有 `lots` 会按 1 手 = 100 股规范化。
- `B/S/M` 与“买盘/卖盘/中性盘”是内置受控值。数字或供应商私有编码必须同时提供显式映射和非空 `directionMappingVersion`，否则不能准入。
- 坏行率超过 1% 必须清洁重导；不超过 1% 或存在时间倒序时输出 `warning`，由用户复核后再决定是否进入后续导入。
- 报告只描述本次文件内容；交易日缺口、休市日冲突、停复牌和公司行动仍由既有质量闸门判断。

## 命令行探针

请求文件只列出需要检查的文件，不支持目录自动发现：

```json
{
  "files": [{
    "path": "X:\\authorized-data\\20260807\\600519.csv",
    "symbol": "600519",
    "name": "贵州茅台",
    "venue": "上交所",
    "tradingDate": "2026-08-07",
    "volumeUnit": "lots",
    "directionMappingVersion": "vendor-contract-v1",
    "directionMappings": {"1": "B", "2": "S", "4": "M"},
    "maximumDiagnostics": 200
  }]
}
```

```powershell
.\out\build\vs2022-x64\Debug\pptrader_raw_tick_source_quality_probe.exe `
  --request .\quality-request.json --output .\quality-report.json
```

退出码 `0` 表示所有文件可规范化，`2` 表示报告已生成但至少一项拒绝，`1` 表示请求或报告文件无效。输出使用 `QSaveFile` 原子提交；报告不包含凭据，但会包含请求中的源文件路径，应按本地数据证据管理。

## 验证与下一步

Qt Test 覆盖标准格式、源文件字节/哈希不变、未知单位、带版本数字方向映射、批量动作聚合、缺失文件，以及 50,000 条有效记录加 5 条坏行时诊断上限为 2 的流式检查。实际跨月源文件尚未在本阶段读取；下一步必须由授权数据合同确认量单位和方向编码，再对 H3B.16 的 16 个清洁重导文件运行探针。只有内容 SHA-256 发生变化后才应显式重新同步，并重新执行交易日、停复牌、公司行动和审计准入。

2026-08-30 工程验收：Debug 串行全量构建通过；CTest 45/45 通过（80.71 秒），其中 `raw_tick_source_quality_adapter` 为 2.13 秒；`all_qmllint` 通过。命令行探针目标已纳入全量构建，缺失请求文件返回调用错误。未修改 QML，因此未启动程序或做视觉验收。

> 真实验收补充：H3B.27 用16个 NAS 目标文件反向验证适配层。用户随后确认项目 A 股时段延长至15:30；共享 `HistoricalTickSessionRules` 现使适配器、`TickDataCsvImporter` 和历史 K 线派生统一接受09:25～11:30、13:00～15:30。重跑结果为3/16 ready、13/16 reject；三项变化只来自29条15:00～15:30成交，其余问题不变。详见 `docs/raw-tick-source-quality-acceptance-2026-08-30.md`。
