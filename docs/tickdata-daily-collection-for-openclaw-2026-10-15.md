# TickData 每日数据采集任务 —— OpenClaw 执行说明书

- 文档版本：2026-10-15
- 交付对象：本机 OpenClaw
- 职责边界：**TickData 只做数据采集**。不做策略、回测、信号、选股；不修改已归档数据

---

## 0. 任务定义

| 项 | 要求 |
|---|---|
| 触发时间 | **每个交易日 18:00** |
| 动作 | 采集当日全市场 A 股逐笔（tick）数据，并做盘后校验与补采 |
| 交付 | 采集完成后**把结果发送到用户微信** |
| 时区 | Asia/Shanghai（本机即此时区） |

> 18:00 的理由：收盘 15:00，盘后定价交易至 15:30，盘后工作流（校验/补采/导出）需要时间余量。

---

## 1. 环境与路径（必须严格按此，勿自行推断）

| 用途 | 路径 |
|---|---|
| **项目根（代码/脚本/配置/构建/日志）** | `~/workspace/TickData` |
| 采集器二进制 | `~/workspace/TickData/build/tick_data` |
| 配置文件 | `~/workspace/TickData/config/config.json` |
| 标的池 | `~/workspace/TickData/data/stock_list.csv` |
| **数据真实位置** | `~/.data/<YYYYMMDD>/` |
| 数据（经项目路径访问） | `~/workspace/TickData/data/` → **软链接**到 `~/.data` |
| 日志（留在项目内） | `~/workspace/TickData/logs/` |
| 盘后工作流 | `~/workspace/TickData/scripts/post_market_workflow.sh` |
| 交易日判定 | `~/workspace/TickData/scripts/is_trading_day.sh` |

### ⚠️ 项目与数据是分离的（2026-10-15 重构）

```
~/workspace/TickData/          项目本体：build/ config/ scripts/ src/ docs/ logs/ .git/
    data -> /home/wanglc/.data  ← 软链接（不是目录！）
~/.data/                       数据本体：174 个日期目录 + 34 个一级文件 + .incoming-clean
```

- 项目内所有 `<项目>/data/...` 的相对路径**照常可用**（经由软链接落到 `~/.data`）。
- **不要删除、替换或移动这个 `data` 软链接**，否则工作流与校验脚本会全部失效。
- **不要**把数据内容复制进项目目录——数据只应存在于 `~/.data`。

### ⚠️ 硬约束：必须在项目根目录下执行

`config.json` 里的 `data_dir` 与 `log_dir` 是**相对路径**（`"data"` / `"logs"`）：

- `data` → 经软链接落到 **`~/.data`**（真正的数据存放处）
- `logs` → 落在**项目内** `~/workspace/TickData/logs/`

**必须 `cd ~/workspace/TickData` 之后**再调用采集器，否则两者都会落到错误的相对位置。

---

## 2. 前置依赖检查（首次部署、或系统升级后必做）

```bash
cd ~/workspace/TickData

# 2.1 采集器是否存在
test -x build/tick_data || { echo "❌ 缺采集器，需编译: bash build.sh"; exit 1; }

# 2.2 Python 依赖 —— akshare 已装（2026-10-15 由 OpenClaw 部署任务时安装，1.19.1）
python3 -c "import akshare" 2>/dev/null && echo "✅ akshare 已装" || echo "❌ akshare 缺失"
python3 -c "import pandas, requests" 2>/dev/null && echo "✅ pandas/requests 已装"

# 2.3 若缺失再安装（supplement_ticks.py 依赖；缺它会导致第 3 步补采失败）
python3 -m pip install --user akshare

# 2.4 编译（仅当 2.1 失败时）
bash build.sh          # 生成 build/tick_data
```

> 说明：`post_market_workflow.sh` 调用的是**裸 `python3`**（本机为 `/usr/bin/python3`，3.14.8），
> 不是项目 venv。因此 akshare 要装在系统 python3 下。

---

## 3. 每日执行流程

### Step 0 —— 交易日判定（必须先做，勿跳过）

```bash
cd ~/workspace/TickData
bash scripts/is_trading_day.sh            # 省略参数 = 判断今天
# exit 0 → 交易日，继续
# exit 1 → 休市/非交易日，【直接结束，不要采集】
```

该脚本通过腾讯行情接口的最新时间戳日期判断，**已内置休市日拦截**，专门用于规避
2026-09-25 中秋误采事件（休市日接口会返回上一交易日数据，导致日期错位）。
接口异常时它保守地按「交易日」处理——此时仍可继续，但请留意结果。

### Step 1 —— 采集（推荐用现成工作流）

```bash
cd ~/workspace/TickData
bash scripts/post_market_workflow.sh "$(date +%Y%m%d)"
```

该工作流包含 **7 个步骤，每步最多重试 5 次**（退避 30/60/120/300/600 秒）：

| 顺序 | 步骤 | 实际命令 |
|---:|---|---|
| 1 | catchup 补采集 | `build/tick_data --config config/config.json --catchup --date <DATE>` |
| 2 | validate 校验 | `python3 scripts/validate_ticks_fast.py <DATE> data` |
| 3 | supplement 多源补充 | `python3 scripts/supplement_ticks.py --date <DATE> --data-dir data --missing-file <...>` |
| 4 | revalidate 复验 | `python3 scripts/validate_ticks_fast.py <DATE> data` |
| 5 | fetch_external_5min | `python3 scripts/fetch_5min_batch.py --date <DATE> --source tencent` |
| 6 | export_external_klines | `python3 scripts/export_external_klines.py --date <DATE> --data-dir data` |
| 7 | verify_completeness | `python3 scripts/verify_completeness.py <DATE> data` |

**最小替代方案**（只采 tick，不做校验/补充/导出）：

```bash
cd ~/workspace/TickData
./build/tick_data --config config/config.json -r
```

> `-r/--realtime` = 采集全部 A 股，单次运行，收盘后退出。
> 若只走这条路径，第 4 节的「异常判读」将缺少校验数据，请在报文里说明。

---

## 4. 结果判读：从哪些文件取哪些数

采集完成后，按顺序读这些文件（不要靠猜）：

| 数据 | 来源 |
|---|---|
| 本次工作流总况、各步骤成败、耗时 | `logs/postmarket_<DATE>.log` |
| 单步骤详情 | `logs/step_<步骤名>_<DATE>.log`（如 `step_catchup_20261015.log`） |
| 逐只股票校验结果 | `data/<DATE>/report/validation_<DATE>.csv` |
| 缺失/失败股票清单 | `data/<DATE>/missing_stocks.txt` |
| 实际落盘股票数 | `ls data/<DATE>/*.csv \| wc -l` |
| tick 总量 | `data/<DATE>/report/validation_<DATE>.csv` 的 `tick_count` 列求和 |

> ⚠️ **`logs/` 目录当前是空的**：迁移时刻意未搬运历史日志。
> `logs/postmarket_*.log` 与 `logs/step_*.log` **只会在本机跑过工作流之后才出现**。
> 首次运行后请确认它们已生成；若缺失，说明工作流没真正执行到那一步。
>
> 而 `data/<DATE>/report/validation_<DATE>.csv` 与 `data/<DATE>/missing_stocks.txt`
> 是数据的一部分，**已随迁移存在**（如 `20261015` 就已有）。

**`validation_<DATE>.csv` 的列**：

```
code,name,tick_count,status,issues,local_open,remote_open,local_high,remote_high,
local_low,remote_low,local_close,remote_close,local_vol,remote_vol,source
```

判读口径：

- `status` 非 `pass` 的行数 → 异常只数
- `issues` 非空 → 具体原因
- **参考基准**：本机 2026-09-28 ~ 10-15 的干净交易日，每日落盘约 **5,350–5,550 只**，
  单只中位约 **2,300–2,600 条** tick。若某日股票数明显低于 5,300，或多数股票 tick 数
  骤降，应作为异常上报。

---

## 5. 微信报送

**报送渠道**：用本机 OpenClaw 的 `openclaw-weixin` 渠道（2026-10-15 已修复并端到端验证通过）。

### 发送命令（实测可用）

```bash
openclaw message send \
  --channel openclaw-weixin \
  --target 'o9cq80x08lhJFaU0UFRAWTK4HbKg@im.wechat' \
  -m "<报文>"
```

成功输出形如：`✅ Sent via openclaw-weixin. Message ID: openclaw-weixin:<ts>-<hash>`

### ⚠️ 两个必须注意的坑（均已实测踩到）

**① 目标大小写敏感，必须用原文**

```
o9cq80x08lhjfau0ufrawtk4hbkg@im.wechat  → ret=-3 invalid arguments   ✗ 小写（来自 session key，被 OpenClaw 小写化）
o9cq80x08lhJFaU0UFRAWTK4HbKg@im.wechat  → 成功                        ✓ 原始大小写
```

**不要**从 session key（`agent:main:openclaw-weixin:...:direct:<id>`）里拼接目标——那里的 id 已被小写化。
若目标失效，用以下命令重新发现原文：

```bash
grep -raohiE '<小写的id>' ~/.openclaw/state ~/.openclaw/agents | sort | uniq -c | sort -rn | head
# 出现次数最多的那一行就是原始大小写
```

**② contextToken 会过期，需周期性「入站」刷新**

微信客服类接口要求「用户近期发过消息」才能主动推送。令牌存在：

```
~/.openclaw/openclaw-weixin/accounts/<account>.context-tokens.json
```

- 该文件 mtime 即令牌刷新时间；**超过约 48 小时即失效**
- 失效时发送报 `ret=-2 errmsg=prepare failed`，日志提示 `contextToken missing ... sending without context`
- 修复方式：**让用户在微信里给 bot 发一条任意消息**，令牌立即刷新，随后即可正常推送
- 因此：若某日推送失败且错误码为 `-2`，**不要反复重试**，直接提示用户发一条消息即可

> 常见错误码对照：`-3` = 目标格式/大小写不对；`-2` = 令牌过期或投递准备失败。

### 报文模板（建议固定格式，便于长期对比）

**① 正常**

```
【TickData】2026-10-15 采集完成
交易日: 星期三 | 落盘 5,551 只 | tick 4,72万条 | 耗时 12 分钟
校验: 通过 5,540 / 异常 11 / 缺失 0
下一步: 无需人工介入
```

**② 有异常**

```
【TickData】2026-10-15 采集完成（有异常）
落盘 5,401 只（较基准少 154）| tick 4,31万条
校验: 通过 5,380 / 异常 21 / 缺失 15
异常样例: 000001(vol差2%), 600519(issues=...)
缺失清单: data/20261015/missing_stocks.txt
建议: 检查 step_supplement_20261015.log
```

**③ 失败**

```
【TickData】2026-10-15 采集失败
失败步骤: catchup（已达 5 次重试上限）
错误摘要: <从 postmarket_20261015.log 提取最后一条 ERROR>
日志: logs/postmarket_20261015.log
```

**原则**：无论如何都要发一条。**失败必须显式上报，不要静默**。

---

## 6. 历史事故与必须规避的坑

这些是从 2026-06 ~ 10 的真实故障里提炼的，**采集逻辑已修，但你仍需知道如何识别**：

| # | 坑 | 现象 | 你的应对 |
|---:|---|---|---|
| 1 | **休市日日期错位** | 休市日接口返回上一交易日数据，被写成休市日目录（2026-09-25 整日数据被隔离） | 必须先用 `is_trading_day.sh` 拦截 |
| 2 | **空响应静默截断** | 某股只落盘 70 条（当日实际 4,721 条）。根因：把「空响应」当作「翻到底」 | 已于 2026-10-15 修复（空响应重试 3 次）。若见某股 tick 数骤降，按异常上报 |
| 3 | **非 JSON 响应（返回 HTML）** | 北交所分钟接口返回 HTML，`json parse error ... last read: '<'`，日志 271 次 | 已修复：前缀不匹配纳入重试，重试耗尽后 error 级报错并打印响应头 80 字节 |
| 4 | **重复写入** | 2026-06-09 ~ 06-24 共 10 个交易日被重复写 3–300 倍（单日 55 GB） | 已识别并恢复。如果你发现某日体积异常大（正常约 370 MB/日），按异常上报 |
| 5 | **落盘门禁** | 序号不连续时**不写文件**，并打 `❌ 落盘门禁拦截` error 日志 | 这是**预期行为**，不是崩溃。该股会由 catchup 补采；请在报文里统计此类条数 |
| 6 | **科创板成交量单位** | 日线接口科创板用「股」、逐笔用「手」，相差 100 倍 | 做量比对照时注意单位，**不要把 0.01 的比值当缺陷** |
| 7 | **对账不变量有误报** | `amount ≈ price × volume × 100` 对 3 秒聚合桶**不成立**（实测误报 8.6%） | 不要用它判定数据好坏；用 `seq` 连续性 |

---

## 7. 失败与重跑

- 工作流自带 5 次重试；**重跑是安全的**（步骤幂等，会覆盖当日文件）
- 手工重跑：

```bash
cd ~/workspace/TickData
bash scripts/is_trading_day.sh <YYYYMMDD> && bash scripts/post_market_workflow.sh <YYYYMMDD>
```

- 重跑前**不要**手工删除 `data/<DATE>/`——工作流按股票覆盖，删了会导致当日数据不完整
- 若同一天重跑超过 3 次仍失败，报微信并停止，等待人工介入

---

## 8. 禁止事项

1. **不要**修改或删除 `data/` 下任何已归档的日期目录（历史数据是事实库）
2. **不要**删除 `logs/`（事故追溯依赖它）
3. **不要**向 NAS `192.168.3.7` 写入任何内容（它只作为只读参考源）
4. **不要**把数据提交进 git。数据在 `~/.data`（项目之外），本就不会被仓库收录；
   项目内只有一个指向它的软链接。若要把项目本体入库，先确认 `.gitignore` 已排除 `data`
5. **不要**在非交易日强行采集（会产生日期错位数据，参见坑 #1）
6. **不要**自行改动 `config/config.json` 里的 `data_dir` / `log_dir`（相对路径依赖 cwd）

---

## 8.5 已部署的定时任务（2026-10-15，由 OpenClaw 建立）

| | 任务 A | 任务 B |
|---|---|---|
| ID | `d4f4f566-6b2b-4beb-851e-e8da9c610fc4` | `0b76f2fe-74a5-4c04-8d05-599aa7d978b9` |
| 名称 | TickData 闭市后采集 | TickData 每日汇报 |
| cron | `0 16 * * 1-5` @ Asia/Shanghai | `0 18 * * 1-5` @ Asia/Shanghai |
| 载荷 | `bash ~/.openclaw/workspace/monitor/tickdata_collect.sh` | `bash ~/.openclaw/workspace/monitor/tickdata_report.sh` |
| 投递 | 静默（休市不发；失败即微信告警） | `announce -> openclaw-weixin` |

实现选择（与原方案不同，均更稳妥）：
1. 报文走**调度器原生投递**而非 `openclaw message send` —— 多行中文免过 shell 引号
2. 汇总用**确定性脚本、不调模型** —— 零 token 且文案不漂移
3. 命令载荷只能用 CLI 建（`automations` 工具仅接受 agentTurn/script/systemEvent）

> 注意：`logs/postmarket_<DATE>.log` 只在本机跑过工作流后才存在；
> 首次实跑（2026-10-16 16:00）之前该文件不存在属正常。

## 9. 附：调度建议

若由你的调度器触发，建议：

```
触发: 每周一至周五 18:00（Asia/Shanghai）
流程:
  1. cd ~/workspace/TickData
  2. bash scripts/is_trading_day.sh || 退出（休市，不发消息）
  3. bash scripts/post_market_workflow.sh $(date +%Y%m%d)
  4. 读第 4 节的判读文件，按第 5 节模板发微信
超时: 建议 2 小时（工作流单步上限 7200 秒）
```

> 若你的调度器只能按固定间隔触发，请自行用 `is_trading_day.sh` 做交易日过滤——
> 它是本任务唯一的「是否该跑」判据。

---

## 10. 快速自检清单（首次部署请逐项确认）

- [ ] `~/workspace/TickData/build/tick_data` 存在且可执行
- [ ] `python3 -c "import akshare"` 通过（**当前缺失，需安装**）
- [ ] `bash scripts/is_trading_day.sh` 在交易日返回 0
- [ ] `cd ~/workspace/TickData` 后执行，数据落到 `data/<日期>/`
- [ ] 能读取 `data/<DATE>/report/validation_<DATE>.csv`
- [ ] 能向用户微信发送测试消息
- [ ] 已确认调度时间与超时设置
