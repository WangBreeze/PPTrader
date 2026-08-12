# 真实装载-回测-归档性能基线（H3B 1 档）

最后更新：2026-08-12

## 口径

本基线验证"真实成交文件 → SQLite 事实 → 派生 K 线 → 数据集身份 → 批量回测 → 单项归档"的完整链路。数据源为本地暂存目录中 SHA-256 校验过的真实成交 CSV（`600519`，2026-08-05/06/07 三个交易日，共 12464 条原始成交），由 `pptrader_real_load_probe` 流式解析、事务导入临时 SQLite、按 60 秒派生并写入缓存，再以 SMA 交叉策略（200 根）运行批量回测并归档。

NAS 数据节点 SSH 隧道已按验收约定关闭，本档不包含网络传输段；传输/校验段复用既有 H1B 基线（1 档 1.612 秒含 manifest 与下载）。10/100 档真实装载需重新建立节点访问后执行。

## 环境

- 机器：同项目容量基线机（Intel Core i5-13600KF，20 逻辑处理器，31.84 GiB RAM，NVMe）
- Qt 6.4.3 / MSVC x64，Debug 与 Release 各跑一次
- 探针：`pptrader_real_load_probe`（不注册 CTest）

## 结果（1 标的三日真实数据）

装载明细（每文件独立计时）：

| 交易日 | 行数 | 有效成交 | 跳过 | 1min Bar | Debug 解析 | Release 解析 | Release 原始写库 | Release K 线写库 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2026-08-05 | 4423 | 4411 | 12 | 238 | 1388 ms | 356 ms | 33 ms | 2 ms |
| 2026-08-06 | 4000 | 3989 | 11 | 239 | 1250 ms | 302 ms | 30 ms | 2 ms |
| 2026-08-07 | 4069 | 4064 | 4 | 239 | 1260 ms | 310 ms | 31 ms | 2 ms |

分段汇总：

| 指标 | Debug | Release |
| --- | ---: | ---: |
| Schema 初始化 | 19 ms | 73 ms |
| 装载总耗时（解析+写库+派生） | 3960 ms | 983 ms |
| 原始成交写入 | 795 ms | 94 ms |
| 派生 K 线写入 | 51 ms | 6 ms |
| 批量回测（SMA 200 根，1 标的） | 52 ms | 25 ms |
| 单项归档（权益+成交+身份） | 10 ms | 1 ms |
| 临时 SQLite 总大小 | 3.56 MiB | 3.56 MiB |
| 失败文件 | 0 | 0 |

数据集身份（Debug 与 Release 完全一致）：

- `datasetVersion`：`sha256:23fe962fb25efe8637e6e7fc71072087df6bdef8a0cc9d53ec3a7d5491819103`
- 3 个交易日 / 12464 原始 Tick / 716 根派生 1min Bar / 0 stale

## 结论

- Release 单文件真实装载约 300–360 ms（解析主导）、三日约 0.98 秒；写库与派生占比约 10%，解析是当前瓶颈，与 H1B "解析 82.991s/100 档" 的口径一致。
- 批量回测与归档开销可忽略（25 ms / 1 ms），H3A 容量探针的结论（计算不是瓶颈）在真实装载链路上仍然成立。
- 数据集身份跨 Debug/Release 构建稳定，事实指纹与派生结果解耦。
- 100 档装载按本机 Release 单文件均值约 0.32 秒线性外推约 32 秒（不含 NAS 传输），最终以真实 10/100 档重测为准。

## 复现

```powershell
cmake --build --preset vs2022-release --target pptrader_real_load_probe --parallel
.\out\build\vs2022-x64\Release\pptrader_real_load_probe.exe `
  --staging "$env:LOCALAPPDATA\PPTrader\PPTrader\cache\nas-tick-staging" `
  --symbols 600519 --bars 200 `
  --output .\artifacts\real-load-1\report-release.json
```

原始 JSON：`artifacts/real-load-1/report.json`（Debug）与 `report-release.json`（Release）。
