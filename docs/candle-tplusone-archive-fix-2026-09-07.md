# 周期 K 回测末日持仓归档兼容修复

日期：2026-09-07。范围仅为已确认的批量回测归档失败，不扩展指标目录、向导交互或回测撮合规则。

## 根因与复现

`BatchBacktestTest::identityAndArchiveRoundTrip` 在实际 `BatchBacktestService` 完成周期 K 回测后归档失败。STRAT-R02 修正后，引擎按 A 股 T+1 保留末日不可售持仓，但 SQLite 保存和恢复两处校验仍只允许 `RawTicks` 结果含有 `finalPosition`。

原测试失败及写库诊断分别见 `artifacts/candle-tplusone-archive-red.txt` 和 `artifacts/candle-tplusone-archive-diagnostic-red.txt`。新增模式对照测试中，相同合法持仓在 RawTicks 模式通过、Candle 模式失败，见 `artifacts/candle-tplusone-storage-red.txt`。这是实际归档规则冲突，不是指标运行错误或数据库锁冲突。

## 修复与边界

只移除保存与恢复中的输入模式限制。持仓数量仍须有限且非负，持仓均价须有限且在有持仓时为正，入场费用须有限且非负；有持仓时必须有有效入场时间，且不得晚于回测结束时间。已有输入快照、身份、事务和完整性校验保持不变。

复用已有 `final_position_*` 列，**不增加 schema 版本、不迁移数据、不改写既有归档**。末日未平仓部分仍按引擎真实现金和权益保存，不清空持仓、不补造平仓交易，也不把未实现盈亏改记为已实现成交。已失败且未落库的旧结果不会凭此自动恢复，需在仍持有完整结果时重新显式归档，或重新运行。

## 验证

- 使用临时 SQLite 和合成输入，不读取开发库、不联网、不访问 NAS。
- 实际批量结果完整往返：持仓数量、均价、入场费用、时间、原因、权益、现金、持仓市值、交易数及数据集身份均保留。
- Candle/RawTicks 两种模式：关闭并重开存储后，完整输入恢复与展示恢复均保留原始持仓；8 类非法持仓仍拒绝写入且不残留半个结果，篡改归档均价后拒绝恢复。
- 定向完整 Qt Test：`tst_batch_backtest` **32/32**（6.728 秒）、`tst_sqlite_local_data_store` **34/34**（2.237 秒），含初始化/清理。证据：`artifacts/candle-tplusone-batch-green.txt`、`artifacts/candle-tplusone-storage-green.txt`。
- Debug 全量构建成功；CTest **48/48、0 失败，101.15 秒**，包含本轮指标扩展的生命周期测试；QML lint 成功。构建需获准读取本机 Windows SDK；未申请或使用网络权限。
- 新二进制离线原生窗口关闭冒烟测试 **1/1**，退出码 0，PID 19924 自行回收，启动至退出 6,207ms（不是纯关闭阶段耗时）。使用临时库、副屏、禁用自动行情；不把此启动检查视作条件菜单或策略编制体验验收。

最终串行命令：

```powershell
cmake --build --preset vs2022-debug --parallel 1
ctest --preset vs2022-debug --output-on-failure --output-log artifacts/candle-tplusone-ctest.txt
cmake --build --preset vs2022-debug --target all_qmllint --parallel 1
./scripts/pptrader-exit-probe.ps1 -TimeoutSeconds 20 -Repeat 1
```

完整测试与启动证据：`artifacts/candle-tplusone-ctest.txt`、`artifacts/candle-tplusone-native-close.txt`。修复避免再以清仓 fixture 独自证明归档能力，保留实际引擎末日持仓回归来防止执行层与持久化层再次发生能力漂移。

## 改动文件

- `src/services/SqliteLocalDataStore.cpp`：保存/恢复的两处模式限制及原因注释。
- `tests/tst_batch_backtest.cpp`：保留原失败入口，增加实际末日持仓和资产保真断言、失败信息。
- `tests/tst_sqlite_local_data_store.cpp`：模式对照、重启恢复和非法/损坏持仓测试。
- 本文、`development-progress.md`、`known-issues.md`：追加本次修复状态；此前指标扩展验收和 APP-001 的历史结果不重写。

本轮没有 QML 视觉修改，亦未复验原生条件下拉框；该线索仍需区分捕获/输入时序与产品行为。零交易 UI 控制样例不因此变成有效策略或完整编制体验验收。
