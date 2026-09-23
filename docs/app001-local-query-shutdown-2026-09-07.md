# APP-001：本地查询积压退出修复

事实截止：2026-09-07。本轮只使用临时 SQLite fixture 与原生窗口关闭探针，不访问 NAS、行情网络或开发数据库。

## 已复现的原因

空库 `--exit-probe-native` 正常回收 PID，不能复现历史上所有无窗口残留。在服务层使用实际 `AsyncLocalCandleSeriesQueryService` / `AsyncLocalTickSeriesQueryService`，为临时数据库导入 20,000 条逐笔 fixture，各排队 64 个查询，再调用 Controller 退出时使用的 `stop()`。原实现耗时 **19,457ms**，超过测试的 1,500ms 预算。

`stop()` 通过阻塞排队调用关闭 SQLite，但关闭动作排在全部查询之后；查询没有取消检查，期间仍全量读取逐笔、计算不同周期缓存，并排队回传结果。这是已证实的退出延迟来源，不等于已经解释此前需要提升权限终止的所有残留。

证据：`artifacts/app001-query-stop-red.txt`。该回归测试无 QML、网络或开发机数据依赖，不将 fixture 称为真实 NAS 验收。

## 最小修复

- 两个异步服务按启动代次持有共享原子取消标志。停止先取消，再在 SQLite 所属线程清理连接。
- 尚未执行的请求直接丢弃；正在执行的逐笔扫描逐行检查取消，周期计算也在交易日边界及写缓存前检查。
- 工作线程完成和 GUI 回调都检查同一代次标志；停止后已排队的结果也不能恢复旧状态。重新启动采用新标志，不复用旧代次。
- 不强杀线程、不提前销毁正在使用的连接；已经开始的缓存事务允许完整结束，不写入取消后的半成品缓存。
- 原生退出脚本增加重复次数、明确退出码检查与启动至进程回收耗时。探针窗口使用副屏（存在时），不改写用户窗口几何设置。

未改 TradingController、NAS preparation、BacktestDatasetService、NAS/QML 业务界面或 SQLite schema；保留实时数据仅内存的边界。

## 验证

定向 Debug 构建完成；`tst_local_candle_series` **21 项通过**，积压关闭 **357ms**，覆盖停止后拒绝请求、旧结果不回传、重启后正常查询。证据：`artifacts/app001-query-stop-green.txt`。

本轮串行验证结果：

| 命令 | 实际结果 |
| --- | --- |
| `cmake --build --preset vs2022-debug --target tst_local_candle_series --parallel 1` | 成功；先运行红测，再修复并运行全部定向测试 |
| `cmake --build --preset vs2022-debug --parallel 1` | 成功，Debug 主程序重新链接 |
| `ctest --preset vs2022-debug --output-on-failure --output-log artifacts/app001-ctest.txt` | **46/47**，85.22 秒；不是全绿 |
| `cmake --build --preset vs2022-debug --target all_qmllint --parallel 1` | 首次因沙箱拒绝读取本机 SDK 失败；显式获准本地读取后重试成功，不联网 |
| `scripts/pptrader-exit-probe.ps1 -TimeoutSeconds 20 -Repeat 10` | 新 Debug 原生窗口关闭 **10/10** 成功，退出码均 0，PID 均自行回收；启动至退出 **3,111–3,379ms**，不是纯关闭阶段计时 |

原生探针使用 `WM_SYSCOMMAND/SC_CLOSE`、临时新库和副屏，不是直接 `app.quit()`；子进程强制 `PPTRADER_AUTOSTART_LIVE=0`，无需禁用无障碍。证据：`artifacts/app001-native-close-green.txt`。另外，以必定非零退出的本机辅助程序反向检查探针，确认它报告 FAIL 而非把进程消失当 PASS。没有强制终止本轮 PPTrader 测试进程，没有操作协调方的审计副本。

唯一失败是 `BatchBacktestTest::identityAndArchiveRoundTrip`，`tests/tst_batch_backtest.cpp:1240` 的 `store.saveBacktestArchive(archive)` 返回 false；单独重跑仍为 31 项通过、1 项失败，详见 `artifacts/app001-batch-unrelated-failure.txt`。该测试目标未链接本轮修改的查询服务或 `main.cpp`，已交回主开发任务处理，未越界修改归档主线。

## 本轮改动文件

- `src/services/AsyncLocalCandleSeriesQueryService.{h,cpp}`、`AsyncLocalTickSeriesQueryService.{h,cpp}`：退出代次取消、排队丢弃和回调抑制。
- `src/services/LocalCandleSeriesQueryService.{h,cpp}`、`LocalTickSeriesQueryService.{h,cpp}`：兼容现有调用的可选取消标志；同步调用默认行为不变。
- `tests/tst_local_candle_series.cpp`：真实临时 SQLite 积压/停止/恢复回归，仅新增测试，保留已有测试改动。
- `src/main.cpp`：仅退出探针副屏位置及不保存测试几何；保留原有生产退出连接与销毁顺序。
- `scripts/pptrader-exit-probe.ps1`：离线环境、重复运行、退出码与精准测试进程核对。
- 本文、`docs/known-issues.md`、`docs/development-progress.md`：事实和限制同步。

## 仍有限制

取消发生在查询/逐笔访问边界，不是 SQLite 指令级强制中断。正在执行的单条 SQL、锁等待、磁盘 I/O 与已开始事务仍需返回；不承诺任意大库或异常存储环境的硬退出上界。没有重演真实 NAS 同步后的全部界面操作，APP-001 保持部分修复、继续观察。若再次出现长时间残留，应采集该 PID 的线程栈及关闭阶段证据，不能靠强制终止宣称修复。
