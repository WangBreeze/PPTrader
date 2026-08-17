# NAS 数据节点接入配置（192.168.3.7）

> 状态：节点已部署并验证（2026-08-18）。本文档记录配置步骤与客户端连接方式，便于后续维护。

## 概述

`192.168.3.7` 作为只读 TickData NAS 节点，服务真实的原始 Tick CSV（深市代码，`YYYYMMDD/代码.csv` 分区，约 25 万文件 / 60 交易日）。客户端 PPTrader 通过 HTTPS 显式查 catalog → 下载单日 CSV → SHA-256 校验 → 本地事务导入。

- **节点地址**：`https://192.168.3.7:8765`
- **节点脚本**：`/home/wanglc/PPTrader/scripts/nas_data_node.py`（随共享文件夹同步）
- **数据根目录**：`/home/wanglc/workspace/TickData/data`
- **Token 文件**：`/home/wanglc/nas/token.txt`（64 位 hex，`openssl rand -hex 32` 生成）
- **证书**：`/home/wanglc/nas/nas.crt` / `nas.key`（自签名，CN=192.168.3.7）

## 一、节点端配置（在 192.168.3.7 上）

### 1. 数据目录结构

```
/home/wanglc/workspace/TickData/data/
  20260602/000001.csv  000007.csv  000010.csv ...
  20260603/...
  ...
```
- 日期目录名 `YYYYMMDD`，文件名 6 位代码 `.csv`
- 每行格式：`time,price,volume,amount,type`（逗号分隔，含表头）

### 2. 生成 Token 与证书（一次性）

```bash
mkdir -p /home/wanglc/nas
openssl rand -hex 32 > /home/wanglc/nas/token.txt
openssl req -x509 -newkey rsa:2048 -keyout /home/wanglc/nas/nas.key \
  -out /home/wanglc/nas/nas.crt -days 3650 -nodes -subj "/CN=192.168.3.7"
```

### 3. 启动节点

```bash
cd /home/wanglc/PPTrader/scripts
PPTRADER_NAS_TOKEN_FILE=/home/wanglc/nas/token.txt \
  nohup python3 nas_data_node.py \
    --root /home/wanglc/workspace/TickData/data \
    --bind 0.0.0.0 --port 8765 \
    --cert /home/wanglc/nas/nas.crt --key /home/wanglc/nas/nas.key \
    > /home/wanglc/nas/nas.log 2>&1 &
```

参数说明：
- `--root`：Tick 数据根目录
- `--bind 0.0.0.0`：对局域网开放（非回环监听必须配 Token）
- `--port 8765`：HTTPS 端口
- `--cert`/`--key`：成对提供即启用 HTTPS

### 4. 验证

```bash
# 健康检查（无需鉴权）
curl -k https://127.0.0.1:8765/v1/health
# catalog（需 Token；start/end 必填，最多 367 天）
curl -k "https://127.0.0.1:8765/v1/catalog?symbol=000001&start=20260602&end=20260630" \
  -H "Authorization: Bearer $(cat /home/wanglc/nas/token.txt)"
# 单日 Tick 下载
curl -k "https://127.0.0.1:8765/v1/ticks/000001/20260602" \
  -H "Authorization: Bearer $(cat /home/wanglc/nas/token.txt)"
```

## 二、客户端连接（PPTrader）

### 1. 信任自签名证书（二选一）

节点用自签名证书，客户端 `QNetworkAccessManager` 默认会拒绝。两种方式：

**方案 A（推荐，持久）**：把 `nas.crt` 导入 Windows「受信任的根证书颁发机构」：
```powershell
certutil -addstore -f Root nas.crt
```

**方案 B（仅内网调试）**：改用 HTTP 并显式允许不安全模式：
```powershell
$env:PPTRADER_ALLOW_INSECURE_NAS = "1"
```
端点写 `http://192.168.3.7:8765`（节点启动时去掉 `--cert/--key`）。

### 2. 配置 Token（环境变量，不写 QSettings/DB）

| 环境变量 | 说明 |
|---|---|
| `PPTRADER_NAS_TOKEN` | 直接填 Token（`cat /home/wanglc/nas/token.txt` 的值） |
| `PPTRADER_NAS_TOKEN_FILE` | Token 文件路径，每行一个，每次请求前按 mtime 热重载（推荐） |

```powershell
# 方式一：直接
$env:PPTRADER_NAS_TOKEN = "<64位hex>"
# 方式二：Token 文件（可把 192.168.3.7 的 token.txt 复制到本机）
$env:PPTRADER_NAS_TOKEN_FILE = "C:\path\to\token.txt"
```

可选：`$env:PPTRADER_NAS_DOWNLOAD_CONCURRENCY = "4"`（并发下载，默认 4）。

### 3. 配置端点

启动 PPTrader →「策略研究」→ 左侧抽屉「数据管理」→「数据导入与同步」→ NAS Tick 面板，输入：

```
https://192.168.3.7:8765
```

端点写入 QSettings（`nas/endpoint`），下次启动自动恢复。

### 4. 同步数据

NAS 面板 → 输入 A 股代码（真实数据为深市代码，如 `000001`）+ 日期范围 → 查 catalog →「同步单日」或「同步全部」→ 下载 → SHA-256 校验 → 事务导入 SQLite + 刷新派生 K 线。

## 三、安全约束（协议硬性要求）

- 节点默认只监听回环；非回环监听必须配 Token（脚本会拒绝启动）。
- 客户端拒绝带 userinfo 的 URL（`http://user:pass@host`）；Token 只走 Authorization Bearer 头。
- 客户端拒绝非回环的 `http://`（除非 `PPTRADER_ALLOW_INSECURE_NAS=1`）。
- 节点不递归扫描根目录：catalog 只检查用户给定的单代码 + 最多 367 个确定日期路径。
- 打开页面/浏览本地 catalog/选回测品种都不联网，只有显式同步才下载。

## 四、当前部署状态

- 节点进程：`python3 nas_data_node.py --root /home/wanglc/workspace/TickData/data --bind 0.0.0.0 --port 8765`（HTTPS）
- 已验证：`/v1/health`、`/v1/catalog?symbol=000001&start=...&end=...`、`/v1/ticks/000001/20260602` 均返回正确。
- 注意：真实数据为**深市代码**（000/002/300 等），沪市 600519 不在仓库中；接入前先确认目标代码在 catalog 中存在。

## 五、相关文档

- 协议细节：`docs/nas-data-node-protocol.md`
- 多日同步队列：`docs/nas-multiday-sync.md`
