# 真实装载-回测-归档性能基线（H3B 1/10/100 档）

最后更新：2026-08-12

## 口径

本基线验证"NAS manifest → 下载 → SHA-256 校验 → SQLite 事实 → 派生 K 线 → 数据集身份 → 批量回测 → 单项归档"的完整链路。数据源为 NAS 数据节点 `192.168.3.7`（经 SSH 回环隧道 `127.0.0.1:28765 → 127.0.0.1:18765`）上的真实成交仓库，交易日 `2026-08-07`。装载由 `pptrader_nas_sync_capacity` 执行（manifest/下载/解析/事务导入），派生缓存重建、批量回测与归档由 `pptrader_real_load_probe --existing-db` 执行；回测策略为 SMA 交叉（200 根），批量活动引擎上限 20。

1 档另以本地 SHA-256 暂存（`600519` 三日）验证多日身份，见文末。

## 环境

- 机器：同项目容量基线机（Intel Core i5-13600KF，20 逻辑处理器，31.84 GiB RAM，NVMe）
- Qt 6.4.3 / MSVC x64，Debug 与 Release 各跑一次
- 探针：`pptrader_real_load_probe`（不注册 CTest）

## 结果（2026-08-07，NAS 真实仓库）

同步装载分段（Release）：

| 档位 | 文件 | 原始成交 | 1min Bar | manifest | 传输 | 解析 | 原始写库 | K 线写库 | 装载总耗时 | SQLite 大小 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 4554 | 240 | 321 ms | 56 ms | 226 ms | 37 ms | 2 ms | 1249 ms | 1.54 MiB |
| 10 | 10 | 27715 | 2363 | 260 ms | 667 ms | 1383 ms | 597 ms | 94 ms | 3189 ms | 12.22 MiB |
| 100 | 100 | 300988 | 23821 | 1023 ms | 8635 ms | 21648 ms | 9638 ms | 1923 ms | 43166 ms | 83.33 MiB |

派生缓存重建 / 批量回测 / 归档（Release，同一临时库）：

| 档位 | 派生重建 | 批量回测（SMA 200 根） | 单项归档 | 成功/失败 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 10 ms | 168 ms | 1 ms | 1/0 |
| 10 | 77 ms | 206 ms | 1 ms | 10/0 |
| 100 | 1187 ms | 1013 ms | 1 ms | 100/0 |

全部档位零失败；100 档源数据约 10.49 MiB，与 H1B 基线（300988 成交 / 23821 Bar）完全一致，证明数据确定性未变。H1B 的 108.467 秒为 Debug 口径，本次 Release 装载 43.17 秒，其中传输 8.6 秒、解析 21.6 秒、原始写库 9.6 秒；解析仍为主要瓶颈。

数据集身份示例（000001，与装载阶段一致）：`sha256:16b23529763bb852b1f1328926d4e3adff57897be627785fa09ac11248be2284`，1 个交易日 / 4554 原始 Tick / 240 根 1min Bar / 0 stale。

## 1 档本地多日补充（600519 三日）

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
- 100 档真实装载 43.17 秒（含传输与写库），派生重建 1.19 秒，批量回测 1.01 秒（100 标的、20 活动引擎），归档 1 ms；全链路 0 失败。
- 网络传输与解析是装载主要耗时，计算/回测/归档开销可忽略；500/5207 档按需以相同探针重测。

## 复现

```powershell
cmake --build --preset vs2022-release --target pptrader_real_load_probe --parallel
.\out\build\vs2022-x64\Release\pptrader_real_load_probe.exe `
  --staging "$env:LOCALAPPDATA\PPTrader\PPTrader\cache\nas-tick-staging" `
  --symbols 600519 --bars 200 `
  --output .\artifacts\real-load-1\report-release.json
```

10/100 档复现（先建 SSH 隧道再执行）：

```powershell
# 隧道（远端节点监听 127.0.0.1:18765）
ssh -N -L 28765:127.0.0.1:18765 wanglc@192.168.3.7

.\out\build\vs2022-x64\Release\pptrader_nas_sync_capacity.exe `
  --endpoint http://127.0.0.1:28765 --universe .\data\astock_cache.json `
  --date 2026-08-07 --tiers 1,10,100 `
  --output .\artifacts\real-load-100\sync-capacity-release.json `
  --keep-dir .\artifacts\real-load-100

.\out\build\vs2022-x64\Release\pptrader_real_load_probe.exe `
  --existing-db .\artifacts\real-load-100\tier-100\capacity.db `
  --symbols 000001,000002,... --bars 200 `
  --output .\artifacts\real-load-100\backtest-100.json
```

原始 JSON：`artifacts/real-load-1/`（本地多日）、`artifacts/real-load-100/`（NAS 1/10/100 档）。临时验证 SQLite 不进入版本库。
