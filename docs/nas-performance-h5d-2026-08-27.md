# NAS 回测目录刷新 H5D 优化验收（2026-08-27）

## 范围与结论

H5D 优化 `loadBacktestDatasetIdentities()` 的本地回测目录刷新。修复前，该函数即使所有逐日强指纹均已预热，仍会对 `raw_trade_ticks` 做全量 `GROUP BY`，Controller 刷新目录时因此占用主线程。修复后，正常路径只读取 `raw_tick_content_fingerprints` 与派生缓存元数据，并为每个目录标的索引读取一条名称/市场信息；只有 dirty 或缺失指纹的日期才回退查询该日事实的 `count/max(id)`，目录路径绝不计算 SHA-256。

本轮复用 H5C 保留的真实授权单交易日 100 标的 SQLite 基线，数据库约 107 MB。未连接 NAS、未访问行情 Provider、未修改开发数据库，也未测试 500/5207 标的。

## 正确性边界

- 干净且合法的 v2 指纹是目录摘要的事实来源，目录刷新不再随 Tick 行数线性扫描。
- INSERT/UPDATE/DELETE 触发器标记 dirty 后，目录必须把对应缓存判为 stale；即使原地修改未改变 `count/max(id)`，也不能误报新鲜。
- dirty/缺失指纹只在后台 `BacktestDatasetService` 准备目标数据集时重算强 SHA-256；目录刷新保持轻量、保守。
- 数据集身份、缓存版本、归档格式与 SQLite schema v18 均未改变。

## 可复现结果

Release 探针新增 `--measure-catalog`，对同一 100 标的数据库测量目录刷新、60 秒热准备和 SMA 100 根回测。修复前单次目录刷新为 **3558 ms**。修复后连续 5 次为 **466、3、3、3、4 ms**：首次冷读下降 **86.9%**，热读为 3–4 ms；按与 H5C 相同的五样本线性插值，p50/p95 为 **3/373.6 ms**。样本很少，因此同时保留原始序列，不将 p95 解释为生产 SLA。

五轮均返回 100 个目录身份；准备阶段均为 4 ms，回测累计 **500/500 成功、0 失败**，单轮回测 195–218 ms。修复前证据为 `artifacts/nas-performance-h5d-2026-08-27/before-catalog.json`，修复后证据为同目录 `after-catalog-1.json` 至 `after-catalog-5.json`。

## 验证

- 新增 Qt Test：干净指纹元数据快路；原始 Tick 原地修改后 dirty 指纹必须判 stale。
- 定向存储/身份/准备/批量回测/CSV 导入测试：5/5 通过。
- 串行 Debug 全量构建成功。
- CTest：42/42 通过，72.17 秒。
- `all_qmllint`：通过，无诊断。

本阶段没有 QML 变更，因此未重复做视觉截图。

## 剩余风险与下一步

H5D 的目录统计子项已完成；下一步继续验证准备/同步任务的取消与进程重启恢复，并在真实受控 NAS 环境覆盖 Range 中断、Token 轮换、changes、zlib、峰值内存和长稳。500/5207 真实写库继续按用户决定延期。多周期预计算、缓存配额和淘汰策略仍需先确定产品规则，不能因打开页面自动生成。
