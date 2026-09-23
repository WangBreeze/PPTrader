# 原始 Tick 参数敏感性分日执行验收（H3B.22）

## 目标与结论

`RawTickParameterSensitivityService` 不再在扫描前把请求范围内的全部原始 Tick 读入一个 `QVector`。服务只读取并冻结 `BacktestDatasetIdentity.days`，由扫描器规划候选、训练段和验证段；每次候选执行再通过 `BacktestEngine::startStreamedRawTicks` 按交易日读取。单次留出和嵌套 walk-forward 使用相同执行边界，SQLite schema 保持 v24，候选排序、逐折选参、`researchOnly`、归档身份和 requestId/取消语义不变。

## 身份与失败保护

- 每个交易日读取前后均复验事实数和内容 SHA-256；旧身份缺摘要时仍按最大 ID 兼容。
- 同一折的全部训练候选必须得到相同执行数据版本，否则整次研究失败；验证段也必须返回与身份事实数一致的 SHA-256 数据版本。
- 服务公开轻量 `rawTickResidencyChanged(requestId, observations)` 信号。它只描述当前输入分段，不是进度，也不进入 QML 或归档。
- 既有耗时字段保持口径：`loadMilliseconds` 累加身份读取及候选实际分日 I/O，`scanMilliseconds` 记录扣除该 I/O 后的计算/调度时间；候选重复读取会如实体现在装载耗时中。
- 数据读取、候选计算和归档仍在独立工作线程完成，不调用 NAS、历史 Provider 或实时行情。

## 可重复验证

临时 SQLite 使用 8 个交易日，逐日 Tick 数为 9、12、15、18、21、24、27、30；配置 2 个候选、2 日验证和 3 个外层折。三折训练/验证事实数保持为 21/33、54/45、99/57，验证合计 135，按研究定义累计处理 309 条。执行期间峰值输入驻留为最大单日 30，而不是完整范围 156，结束后回到 0。

定向测试 `sqlite_local_data_store` 与 `parameter_grid_scanner` 已通过；活动候选中止用例确认取消完成并将驻留量归零后，才销毁工作线程 SQLite 连接。最终 Debug 串行全量构建通过，CTest 43/43（70.80 秒）与 `all_qmllint` 通过；隔离临时库 `--exit-probe-native` 按已保存副屏位置启动并正常退出，PID 已释放。

## 性能边界与后续

本改动优先给输入内存建立确定上界，代价是每个候选都会重新读取其训练日；扩展窗还会在后续折重复读取早期日期。因此候选数和折数增加时，磁盘 I/O 与计算量仍近似按“候选 × 训练段 + 每折一次验证段”增长。本阶段未做候选并行、日块共享缓存或真实跨月复验，不能用 fixture 外推真实耗时。下一项内存风险是显式异步归档仍需暂时物化完整验证 Tick；应单独设计分块事务或受控峰值预算，不能削弱 v19+ 完整输入快照语义。
