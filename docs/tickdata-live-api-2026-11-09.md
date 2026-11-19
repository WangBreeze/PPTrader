# TickData 对外接口契约 v1

- 版本：1（`schemaVersion: 1`）
- 日期：2026-11-09
- 定位：把 TickData 当作**可调用的数据工具**，PPTrader 通过本契约实时获取逐笔数据
- 范围：**只做接口**。性能（并发接多少标的、增量定位优化）不在本期，见 §7

---

## 1. 分层结构

```
┌──────────────────────────────────────────────────────────────┐
│ TickData（数据采集方）                                        │
│   采集器 build/tick_data  →  落盘 ~/.data/<YYYYMMDD>/<code>.csv │
└───────────────────────────┬──────────────────────────────────┘
                            │ 读文件
┌───────────────────────────▼──────────────────────────────────┐
│ 数据节点 nas_data_node.py  （HTTP，127.0.0.1:18765）          │
│   · 归档型：/v1/ticks、/v1/catalog、/v1/manifest、/v1/changes  │
│   · 实时型：/v1/live/<symbol>          ← 本期新增              │
└───────────────────────────┬──────────────────────────────────┘
                            │ HTTP JSON
┌───────────────────────────▼──────────────────────────────────┐
│ 调用方                                                        │
│   · C++ SDK：src/services/TickDataLiveFeed.{h,cpp}            │
│   · 命令行：  tools/tickdata_query.py （零依赖）               │
│   · 任何语言：直接 HTTP + JSON                                │
└──────────────────────────────────────────────────────────────┘
```

**唯一的耦合面是 §4 的 JSON 字段**。服务端换语言、换实现、换传输（共享内存/Unix socket），
只要字段不变，调用方零改动。

---

## 2. 实时接口语义（关键）

TickData 采集器**每轮覆盖写**当日 CSV，所以该文件就是「至今为止」的当日快照。
`/v1/live` 在此之上提供**增量游标**语义：

| 规则 | 说明 |
|---|---|
| `after` 为空 | 从头开始返回（受 `limit` 限制） |
| `time > after` | 只返回**严格晚于** `after` 的记录 |
| `nextAfter` | 下次应携带的游标 = 本批最后一条的 `time`；本批为空时等于请求的 `after` |
| 不重复 | 同一 `time` 只投递一次（`after` 递增，等于已消费） |
| 不遗漏 | `overflow=true` 表示还有更多，应继续用 `nextAfter` 拉取直到 `overflow=false` |
| 停轮询 | `sessionClosed=true` 且 `overflow=false`，且连续 2 次 `nextAfter` 未前进 |

> ⚠️ **同一秒可能有多条 tick**（腾讯分笔是 3 秒聚合，东财是逐笔）。
> 本契约以**秒级 `time`** 为游标，因此同一秒的多条记录会**在该秒首次出现时一起返回**。
> 采集器覆盖写同一秒的记录不会变化，所以不会丢；但若在上游仍更新时读取，该秒的最终值
> 以服务端文件为准（客户端不应假设「同一秒只会有一次推送」）。

---

## 3. 端点

### 3.1 `GET /v1/live/<symbol>` —— 实时增量（本期新增）

| 参数 | 位置 | 必填 | 说明 |
|---|---|---|---|
| `symbol` | path | 是 | 6 位数字代码 |
| `date` | query | 否 | `YYYYMMDD`；缺省为**服务端当日** |
| `after` | query | 否 | `HH:MM:SS`；缺省从头 |
| `limit` | query | 否 | 单次最多返回条数，默认 2000，上限 20000 |

**响应 200**（§4）。参数非法时也返回 200，但带 `error` 字段（便于客户端统一处理）。

### 3.2 归档型端点（已有，未改动）

| 端点 | 用途 |
|---|---|
| `GET /v1/ticks/<symbol>/<date>` | 整日 CSV 原文（字节流） |
| `GET /v1/catalog?symbol&start&end` | 某标的可用日期清单（含 sha256） |
| `POST /v1/manifest` | 多标的可用性批量查询 |
| `GET /v1/changes?since=<epochMs>` | 日期目录变更摘要 |
| `GET /v1/health` | 存活探针（不需要鉴权） |

### 3.3 鉴权

节点可选用 Bearer Token（`PPTRADER_NAS_TOKEN`）。**未设置 token 时全部放行**（本机 loopback 用法）。

---

## 4. 响应契约（`/v1/live`）

```json
{
  "schemaVersion": 1,
  "symbol": "600519",
  "tradingDate": "20261106",
  "serverTime": "2026-11-09T12:14:00",
  "source": "tickdata",
  "available": true,
  "sessionClosed": false,
  "overflow": false,
  "bytes": 125619,
  "mtimeMs": 1762500000000,
  "totalRows": 4282,
  "lastTime": "15:08:49",
  "after": "14:56:00",
  "nextAfter": "15:08:49",
  "ticks": [
    {"time": "15:00:00", "price": 1453.0, "volume": 2826, "amount": 410617800.0, "side": "M"}
  ]
}
```

| 字段 | 类型 | 说明 |
|---|---|---|
| `schemaVersion` | int | 契约版本，当前 1；不兼容变更时递增 |
| `symbol` / `tradingDate` | string | 回显请求 |
| `serverTime` | string | 服务端本地时间，便于排查时钟偏移 |
| `source` | string | 固定 `"tickdata"` |
| `available` | bool | 当日是否已有该标的数据文件；false 时 `ticks` 为空 |
| `sessionClosed` | bool | **时钟口径**：服务端时间 ≥ 15:30:00（盘后固定价格交易结束） |
| `overflow` | bool | 本批因 `limit` 截断，还有更多 |
| `bytes` / `mtimeMs` | int | 文件大小与修改时间（毫秒），可用于判断「是否有更新」 |
| `totalRows` | int | 当日文件总行数 |
| `lastTime` | string | 文件里最后一条的 `time` |
| `after` / `nextAfter` | string | 本次游标 / 下次游标 |
| `ticks[].time` | string | `HH:MM:SS`（**交易所本地时间**，非 UTC） |
| `ticks[].price` | number | 成交价（元） |
| `ticks[].volume` | int | 成交量，**单位「手」**（与 TickData 落盘一致） |
| `ticks[].amount` | number | 成交额（元） |
| `ticks[].side` | string | `B` 买 / `S` 卖 / `M` 中性 |

**错误响应**（仍为 200）：

```json
{"schemaVersion": 1, "error": "date must be YYYYMMDD"}
```

| error | 触发条件 |
|---|---|
| `symbol must be 6 digits` | 代码不是 6 位数字 |
| `date must be YYYYMMDD` | 日期格式错 |
| `after must be HH:MM:SS` | 游标格式错 |

---

## 5. C++ SDK

### 5.1 接口（调用方唯一依赖）

```cpp
#include "services/TickDataLiveFeed.h"

pp::ITickDataLiveFeed *feed = ...;              // 抽象，可换实现
feed->setEndpoint(QUrl("http://127.0.0.1:18765"));

// 增量拉取：after 传空表示从头
const quint64 id = feed->fetch("600519", after, /*date=*/{}, /*limit=*/2000);

// 结果异步返回
connect(feed, &pp::HttpTickDataLiveFeed::batchReady,
        this, [](quint64 requestId, const pp::LiveTickBatch &batch) {
            if (!batch.succeeded) { /* batch.error */ return; }
            for (const auto &tick : batch.ticks) { /* tick.time / price / volume / amount / side */ }
            // 下次用 batch.nextAfter 继续
        });
```

### 5.2 数据契约结构

| 结构 | 字段 |
|---|---|
| `pp::LiveTickRecord` | `time`、`price`、`volume`（手）、`amount`、`side` |
| `pp::LiveTickBatch` | `succeeded`、`error`、`symbol`、`tradingDate`、`serverTime`、`available`、`sessionClosed`、`overflow`、`totalRows`、`lastTime`、`after`、`nextAfter`、`ticks` |

两结构均 `Q_DECLARE_METATYPE`，可跨线程投递。

### 5.3 设计约束

- **异步统一**：即使错误在本地就能判定（代码非法、未配端点），也通过 `batchReady` 返回，
  调用方无需区分同步/异步两条路径。
- **字段容错**：服务端少给字段时按默认值降级，不整批失败（上游接口变更时不至于全断）。
- **不耦合 PPTrader 的 `Tick`**：SDK 返回独立结构，由调用方自行转换，便于在别处复用。

---

## 6. 命令行工具

`TickData/tools/tickdata_query.py`（仅标准库）：

```bash
# 单次
tickdata_query.py --symbol 600519 --date 20261106 --after 14:56:00
# 跟读（每 5 秒，只打印新增）
tickdata_query.py --symbol 600519 --follow 5
# 机器可读
tickdata_query.py --symbol 600519 --json
```

退出码：`0` 成功 / `1` 无数据 / `2` 连接或参数错误。

---

## 7. 本期不做的（明确边界）

| 项 | 说明 |
|---|---|
| **并发能力（已实测上限）** | 见 §7.1 —— 全市场轮询会打满节点，需收窄标的范围 |
| **并发能力** | 每次请求整文件扫描过滤（O(行数)）。单标的 ≤ 5,000 行、~150 KB，本机毫秒级；并发多标的时的瓶颈与优化留待后续 |
| 推送式实时 | 本期是**轮询**（客户端按 `nextAfter` 拉）。SSE/WebSocket 未实现 |
| 多标的批量 | `/v1/live` 一次一只。批量需多次请求（或用 `/v1/manifest` 做归档批量） |
| 跨进程共享内存 | 未实现 |
| 增量定位（按偏移读） | 未实现；当前从头过滤，保证正确性优先 |

---

### 7.1 全市场轮询的实测边界（2026-11-09）

用 `PPTRADER_TICKDATA_LIVE=1` + 全市场标的（5,207 只）实测：

```
[TickData] 本轮 5207 个标的，新增 8757346 条 tick，累计 9624920 条   ← 首轮灌入 875 万条
[TickData] 本轮 5207 个标的，新增   2000 条 tick，累计 9626920 条   ← 之后每轮仅推进 2,000
```

原因两条，都不是接口 bug：

1. **首轮是全量**：每个标的第一次请求（`after` 为空）会返回当日从头开始的 `limit` 条，
   5,207 × 2,000 ≈ 1,040 万条一次性灌进 `handleTick`。
2. **节点被打满**：每轮 5,207 个 HTTP 请求（3 秒一轮 ≈ 1,700 req/s）超过
   `ThreadingHTTPServer` 的处理能力，后续轮次响应稀疏，游标几乎不前进。

**结论：实时模式应只订阅「当下真正关心的少量标的」**（选中标的 + 自选表），
不要把 `buildMarketUniverse()` 的全市场清单直接喂给实时源。
性能优化（批量接口、增量定位、连接复用）留待后续，本期按需求只做接口。

### 7.2 日期覆盖（`PPTRADER_TICKDATA_DATE`）是「批量回放」，不是实时

指定历史日期时，首轮同样会返回每只标的从头开始的 `limit` 条 —— 那是一次**批量回放**，
不是增量。仅用于验证或回看，不要在生产实时会话里使用。

---

## 8. 验收记录（2026-11-09）

| 项 | 结果 |
|---|---|
| 单次查询（600519 / 20261106） | 4,282 行，末笔 15:08:49，返回正确 |
| 增量（`after=09:30:04`） | 返回 09:30:06 / 09:30:09 / 09:30:12，`nextAfter=09:30:12` |
| 游标到底（`after=15:08:49`） | 0 条，`nextAfter` 保持不变 |
| 全量 + limit | `limit=3` → 3 条 + `overflow=true` |
| 错误参数 | `date=bad` / `after=xx` 返回对应 `error` |
| 不存在代码 | `available=false`，CLI 退出码 1 |
| 连接失败 | CLI 退出码 2 |
| C++ SDK 编译 | `TickDataLiveFeed.cpp.o` 构建通过，PPTrader 链接通过 |

---

## 9. PPTrader 接入方式（已实现）

| 项 | 内容 |
|---|---|
| 实现类 | `src/services/TickDataLiveProvider.{h,cpp}`（实现 `MarketDataProvider`） |
| 接线点 | `TradingController` 构造函数；与 `CompositeMarketDataProvider` **互斥** |
| 启用 | 环境变量 `PPTRADER_TICKDATA_LIVE=1` |
| 节点地址 | `PPTRADER_TICKDATA_ENDPOINT`（默认 `http://127.0.0.1:18765`） |
| 轮询间隔 | `PPTRADER_TICKDATA_POLL_MS`（默认 3000） |
| 日期覆盖 | `PPTRADER_TICKDATA_DATE=YYYYMMDD`（批量回放，见 §7.2） |

**为什么互斥**：若与 `CompositeMarketDataProvider` 同时开启，同一笔成交会被投递两次，
污染策略信号与回测统计。`TradingController::activeMarketDataProvider()` 保证二者只取其一。

**单位换算**：TickData 落盘的成交量单位是「手」，PPTrader 内部统一「股」，
provider 里 `volume × 100`（与 `TickDataCsvImporter` 口径一致）。

**不做交易时段门控**：TickData 采到什么就交付什么，盘后/收盘后读取也原样投递；
需要门控的调用方自行用 `MarketSession` 过滤。

### 验收（实测）

```
$ PPTRADER_TICKDATA_LIVE=1 PPTRADER_TICKDATA_DATE=20261106 \
  PPTRADER_HEADLESS_PROBE_SECONDS=12 ./PPTrader --headless-live-probe
[TickData] 本轮 5207 个标的，新增 8757346 条 tick，累计 9624920 条
```

- 默认（不设环境变量）：TickData 输出 **0 行**（完全不影响原有流程）✅
- 开启后：tick 经 `handleTick` 进入既有管道 ✅
- 节点日志确认请求带 `date=20261106`、且出现 `after=`（游标在前进）✅
