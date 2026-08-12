# NAS 数据节点协议

## 定位与边界

NAS 数据节点是 PPTrader 与局域网原始 Tick 仓库之间的只读传输边界。SQLite 运行库始终位于本机；远端文件只有在用户显式查询并同步后，才进入本地暂存区并通过现有 TickData 事务导入链写入 `raw_trade_ticks`、刷新派生 K 线。打开页面、浏览本地 catalog 或选择回测品种都不会联网。

当前节点由 `scripts/nas_data_node.py` 提供，基于 Python 标准库，可直接服务既有的 `YYYYMMDD/代码.csv` 分区。它不会递归扫描根目录：每次 catalog 请求只检查用户给出的单代码和最多 367 个日期。

## HTTP v1 契约

- `GET /v1/health`：返回服务状态与协议版本。
- `GET /v1/catalog?symbol=600519&start=20260801&end=20260807`：返回确定日期范围内实际存在的文件清单。
- `POST /v1/manifest`：接收显式代码数组与最多 31 天日期范围，返回受控多代码文件清单；最多 6000 个代码、12000 个条目。
- `GET /v1/ticks/600519/20260807`：下载单代码单交易日文件，支持 `Range`、`ETag` 和 `Accept-Ranges`。

catalog 的 `schemaVersion` 当前为 `1`。每个条目包含 `symbol`、`tradingDate`、`format`、`size`、`sha256`、不可变 `version` 和相对 `downloadPath`。客户端限制 catalog 为 2 MiB，并校验代码、日期、相对路径、大小与 SHA-256；下载使用 `QSaveFile`，只有字节数和摘要同时匹配才原子提交。本地已有同摘要暂存文件时直接复用，不重复下载。

## 安全模型

数据节点默认只监听回环地址。非回环监听必须配置 Bearer Token；Token 仅从运行环境读取，不写入 QSettings、SQLite、日志或文档。PPTrader 拒绝带 userinfo 的 URL，并要求非回环端点使用 HTTPS；开发期若通过 SSH 本地端口转发访问回环端点，可保持节点不暴露到局域网。

推荐生产部署使用 NAS 反向代理终止 TLS、系统服务管理进程，并把访问令牌交给操作系统凭据注入。服务日志不得记录 Authorization、查询参数、仓库绝对路径或文件内容。

### Token 轮换（H4.2）

- 客户端：`PPTRADER_NAS_TOKEN`（环境变量）保持兼容；新增 `PPTRADER_NAS_TOKEN_FILE`（每次请求前按文件修改时间热重载，内容 trim 后整体作为 Token）。收到 401 时自动重新加载 Token 并重试一次，仍失败才报错。
- 节点端：`PPTRADER_NAS_TOKEN` 环境 Token 始终有效；新增 `PPTRADER_NAS_TOKEN_FILE`，文件每行一个有效 Token，**每次鉴权前重建集合**（文件更新后旧 Token 立即失效，环境 Token 保留）。运维可在重叠期把新旧 Token 同时写入文件，再移除旧 Token，实现不停机轮换。
- Token 文件路径只来自运行环境，文件内容、路径与轮换状态不写入 SQLite、日志或文档。

### 变更摘要（H4.3）

`GET /v1/changes?since=<epoch 毫秒>`：只扫描根目录下一层日期目录的修改时间，不递归文件，不下载、不触发回测。返回 `schemaVersion`、`totalDirectories`、`latestDirectoryAt` 与 `changedDirectories`（按日期升序）。客户端 `requestNasNodeChanges()` 显式轮询，QML"检查节点更新"按钮展示目录总数、最近时间与变更数量；它只提示变化，不自动同步。

### 压缩传输评估（Parquet/Zstd，后续项）

当前瓶颈是解析与写库，传输占比约 20%（热缓存约 9%）。Zstd 压缩 CSV 方案预计把冷缓存传输段从 8.6 s 降至 2–3 s，但解析 21.6 s 不变，100 档端到端预计 43 s → 30–35 s。需要新增 `zstandard` Python 依赖与节点端预压缩/客户端解压桥；若引入二进制批量解析（pyarrow），解析段可进一步下降，但改动面更大。建议在并发下载与断点续传稳定后按需实施。

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

`TradingController` 向 QML 暴露端点、catalog、请求版本、查询/同步状态、已收/预期字节和错误。`NasTickSyncPanel` 要求用户明确输入端点、代码、起止日期，并再次选择某个 catalog 条目启动下载与导入。新请求会取消旧请求；过时 requestId 的结果不会覆盖当前状态。

同步成功不等同于只完成下载：校验通过的 CSV 必须继续通过 `TickDataCsvImportService` 流式解析和 SQLite 事务写入。失败不会破坏已有事实或缓存。导入成功后刷新本地历史 catalog、已使用周期缓存和相关主页 K 线。

## 当前限制

v1 已支持 CSV、单代码 catalog、显式多代码 manifest、按日下载、字节范围、ETag、SHA-256、本地校验缓存、显式取消和 systemd 用户服务守护。H1B 的多代码同步固定单文件串行写入，并已完成 100 档真实临时库基线；500 与全市场真实写库留待后续。H1C 已增加应用本地同步计划的跨进程恢复、SHA-256 命中统计以及用户显式触发的暂存扫描/保留期清理。

恢复文件不包含端点和凭据，启动恢复不联网；它也不是字节级断点续传。Parquet/Zstd、真正的分块断点续传状态机、WebSocket 变更通知、令牌轮换界面、NAS 原生 HTTPS 和 manifest 摘要索引缓存尚未实现，不能把轮询 manifest 描述为实时推送。
