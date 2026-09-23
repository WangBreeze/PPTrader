# NAS 数据节点协议

## 定位与边界

NAS 数据节点是 PPTrader 与局域网原始 Tick 仓库之间的只读传输边界。SQLite 运行库始终位于本机；远端文件只有在用户显式查询并同步后，才进入本地暂存区并通过现有 TickData 事务导入链写入 `raw_trade_ticks`、刷新派生 K 线。打开页面、浏览本地 catalog 或选择回测品种都不会联网。

当前节点由 `scripts/nas_data_node.py` 提供，基于 Python 标准库，可直接服务既有的 `YYYYMMDD/代码.csv` 分区。它不会递归扫描根目录：每次 catalog 请求只检查用户给出的单代码和最多 367 个日期。

## HTTP v1 契约

- `GET /v1/health`：返回服务状态、协议版本与摘要缓存统计（`digestCacheEntries`/`digestCacheHits`）。节点按 `(mtime_ns, size)` 缓存文件 SHA-256，manifest/catalog 复用不重复哈希；客户端只读 `version`，额外字段向后兼容。
- `GET /v1/catalog?symbol=600519&start=20260801&end=20260807`：返回确定日期范围内实际存在的文件清单。
- `POST /v1/manifest`：接收显式代码数组与最多 31 天日期范围，返回受控多代码文件清单；最多 6000 个代码、12000 个条目。
- `GET /v1/ticks/600519/20260807`：下载单代码单交易日文件，支持 `Range`、`ETag` 和 `Accept-Ranges`。

catalog 的 `schemaVersion` 当前为 `1`。每个条目包含 `symbol`、`tradingDate`、`format`、`size`、`sha256`、不可变 `version` 和相对 `downloadPath`。客户端限制单代码 catalog 为 2 MiB、manifest 为 16 MiB，并校验代码、日期、规范相对路径、大小、版本与 SHA-256；下载写入 `.part`，只有完整大小和摘要一致才提交为目标 CSV。本地已有同摘要暂存文件时直接复用。

## 安全模型

数据节点默认只监听回环地址。非回环监听必须配置 Bearer Token；Token 仅从运行环境读取，不写入 QSettings、SQLite、日志或文档。PPTrader 拒绝带 userinfo 的 URL，并要求非回环端点使用 HTTPS；开发期若通过 SSH 本地端口转发访问回环端点，可保持节点不暴露到局域网。

推荐生产部署使用 NAS 反向代理终止 TLS、系统服务管理进程，并把访问令牌交给操作系统凭据注入。服务日志不得记录 Authorization、查询参数、仓库绝对路径或文件内容。

### Token 轮换（H4.2）

- 客户端：`PPTRADER_NAS_TOKEN`（环境变量）保持兼容；新增 `PPTRADER_NAS_TOKEN_FILE`（每次请求前按文件修改时间热重载，内容 trim 后整体作为 Token）。收到 401 时自动重新加载 Token 并重试一次，仍失败才报错。
- 节点端：`PPTRADER_NAS_TOKEN` 环境 Token 始终有效；新增 `PPTRADER_NAS_TOKEN_FILE`，文件每行一个有效 Token，**每次鉴权前重建集合**（文件更新后旧 Token 立即失效，环境 Token 保留）。运维可在重叠期把新旧 Token 同时写入文件，再移除旧 Token，实现不停机轮换。
- Token 文件路径只来自运行环境，文件内容、路径与轮换状态不写入 SQLite、日志或文档。

### 变更摘要（H4.3）

`GET /v1/changes?since=<epoch 毫秒>`：只扫描根目录下一层日期目录的修改时间，不递归文件，不下载、不触发回测。返回 `schemaVersion`、`totalDirectories`、`latestDirectoryAt` 与 `changedDirectories`（按日期升序）。客户端 `requestNasNodeChanges()` 显式轮询，QML"检查节点更新"按钮展示目录总数、最近时间与变更数量；它只提示变化，不自动同步。

### 压缩传输（已实现，zlib）

为保持节点 Python 标准库与 Qt 客户端零新依赖，先落地 zlib 压缩（而非 Zstd）：客户端在**全新完整下载**时发送 `Accept-Encoding: zlib`；节点把原始 CSV 用 `zlib.compress` 压缩，并按 4 字节大端原始长度 + zlib 流返回，响应头带 `X-PPTrader-Encoding: zlib`、`X-Original-Size` 与 `X-Content-SHA256`。客户端收到后 `qUncompress` 解压，仍按 catalog 的 `size` 与 `sha256` 整体校验后原子提交，缓存命中语义不变。带 `Range` 的续传请求不压缩，走原 206 路径。

验证：`tst_nas_data_node` 新增压缩下载→解压→校验→缓存复用，以及关闭压缩后的兼容回退用例（11/11）；Python 节点本地端到端校验通过（144000 字节 CSV 压缩到 564 字节，大小/摘要/内容全部一致）。真实生产吞吐与耗时留待虚拟机复验。已知取舍：压缩下载若在网络层中断不会写入 `.part`，重试会重新压缩下载，不复用字节续传；局域网场景可接受。二进制批量解析（pyarrow）与 WebSocket 变更通知仍为后续项。

## systemd 用户服务部署

仓库提供 `deploy/systemd/pptrader-nas-node.service` 通用模板。虚拟机基线已经把节点脚本安装到用户本地目录，并启用该用户服务；节点只监听 `127.0.0.1`，实际仓库根目录和端口仅写入目标机的 `%h/.config/pptrader/nas-node.env`，不进入源码或文档。`loginctl enable-linger` 已用于保证没有交互登录时用户服务仍可恢复。

目标机环境文件格式如下，值必须按部署环境填写：

```ini
PPTRADER_NAS_ROOT=<tick-root>
PPTRADER_NAS_PORT=<loopback-port>
```

常用运维命令：

```bash
systemctl --user daemon-reload
systemctl --user enable --now pptrader-nas-node.service
systemctl --user status pptrader-nas-node.service
curl http://127.0.0.1:<loopback-port>/v1/health
```

Windows 客户端开发期通过 SSH 本地端口转发把远端回环服务映射到本机回环地址，再在 PPTrader 中显式配置该本机 URL。不要让节点直接监听局域网明文 HTTP，也不要把密码写入 unit、脚本或命令历史；优先使用 SSH key/agent。

## 客户端状态与流程

`TradingController` 向 QML 暴露端点、catalog、请求版本、查询/同步状态、已收/预期字节和错误。`NasTickSyncPanel` 要求用户明确输入端点、代码、起止日期，并再次选择某个 catalog 条目启动下载与导入。同步可并发下载（默认 4、环境上限 16），校验后的文件进入单一导入队列串行写 SQLite；每个下载使用独立 requestId。H5D.2 已用显式 requestId 门控拒绝取消后的迟到响应，并验证持久化 `ready` 项可继续、既有成功项不重复；真实网络乱序和节点故障长稳仍不能由自动测试替代。

同步成功不等同于只完成下载：校验通过的 CSV 必须继续通过 `TickDataCsvImportService` 流式解析和 SQLite 事务写入。失败不会破坏已有事实或缓存。H5A 起，NAS 文件导入期间不再逐文件全量刷新本地目录；整个同步批次完成后，独立 `BacktestDatasetService` 按用户选择的周期和最低根数逐标的准备 `raw_tick_cache_v1`，最后统一刷新一次 `localBacktestDataCatalog`。单项失败只记录该代码，不覆盖其他成功项。

QML 通过 `nasBacktestPreparationSummary` 读取准备周期、最低根数、总数、完成数、成功/失败代码、当前代码、耗时和状态；`configureNasBacktestPreparation()`、`prepareNasBacktestDatasets()` 与 `cancelNasBacktestPreparation()` 分别配置、显式准备和取消。正常同步完成会自动开始准备；同步被取消时只保留已经事务提交的事实，不自动继续派生，避免把不完整计划伪装为完整回测目录。准备和后续回测都只访问本地 SQLite，不请求 NAS 或历史 Provider。

## 当前限制

v1 已支持 CSV、单代码 catalog、显式多代码 manifest、并发按日下载/串行导入、ETag/SHA-256、`.part` Range 续传、本地缓存、显式取消、跨进程计划恢复、Token 文件热重载、变更摘要轮询、zlib 完整文件压缩、摘要缓存、systemd 用户服务守护，以及同步后按指定周期批量准备本地回测目录。100 档真实临时库、热缓存并发基线和本地进程中断恢复已完成；真实 NAS 闭环仍只到受控单标的/100 档同步证据，500 与全市场真实写库延期。

恢复文件不包含端点和凭据，启动恢复不联网。现有 `.part` 是单文件字节续传，不是多块并行/可校验分块状态机；zlib 中断也不会复用部分压缩体。Parquet/Zstd、pyarrow、WebSocket、Token 配置界面、NAS 原生 HTTPS、监控/审计/备份/容量告警尚未实现。Range/轮换/changes/zlib 的生产真实节点验收仍未完成，不能把自动测试等同于上线。
