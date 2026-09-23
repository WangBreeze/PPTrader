# 授权外部因子严格本地研究

阶段 12U 只处理用户已经显式导入、并绑定到冻结候选版本的授权外部因子数据集。启动研究是独立显式操作；浏览候选、数据集或归档不会运行研究，也不会访问网络 Provider。

## 时间与数据口径

- 因子观测时间统一为 UTC。
- 每个观测时刻只使用 `startTime + periodSeconds <= observationTime` 的最近本地 K 线收盘价；尚未完成的 Bar 一律忽略。
- 下一观测收益由当前观测可见完成价到下一观测可见完成价计算，不使用同帧或未来 Bar。
- 每个标的都必须具有同周期、无陈旧状态的 `raw_tick_cache_v1` 与完整 `BacktestDatasetIdentity`；缺少任一腿即失败，不联网补齐。

统计复用既有 `FactorResearchAnalyzer` 和 `FactorOutOfSampleAnalyzer`，包括 IC/Rank IC、HAC、样本内三段、固定五分层、成本归因和扩展窗 OOS。样本不足时相关统计明确不可用，不以零填充。

## 异步与归档

`ExternalFactorResearchService` 在工作线程读取 SQLite 和计算。`TradingController` 暴露显式启动、取消、刷新目录和加载历史；requestId 门控保证旧响应不能覆盖新请求。数据库搬迁会在导入或研究运行时拒绝。

阶段 12V-A 增加独立异步预检。预检校验冻结候选、不可变绑定、观测范围，以及数据集中每个标的在所选周期上的无陈旧 `BacktestDatasetIdentity` 和 `raw_tick_cache_v1` 完成 Bar；缺少任一标的会返回真实就绪/总数和缺失代码，不生成归档。QML 只有在候选、版本、数据集、周期和研究参数与最近一次成功预检完全一致时才开放“研究”。

schema v17 的 `external_factor_research_archives` 事务保存候选/数据集摘要、研究策略、完整收益数据身份、统计摘要和归档 SHA-256。加载历史只读取 SQLite，不联网、不重新计算，也不使用当前编辑参数解释旧结果。

## 验证边界

Qt Test 使用临时 SQLite/原始 Tick fixture 覆盖完成 Bar 无前视、异步预检缺腿/补齐、异步执行、旧 requestId 拒绝和重启 round-trip。当前尚无用户授权真实文件，因此没有完成真实独立时期验收；归档存在不代表因子有效，也不构成投资建议。真实验收清单见 `real-authorized-factor-acceptance.md`。
