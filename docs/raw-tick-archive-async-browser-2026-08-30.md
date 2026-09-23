# H3B.25 原始 Tick 归档异步恢复与逐笔浏览

## 结论

历史回测归档的默认恢复和原始 Tick 翻页均已移出 UI 线程。用户显式加载 raw 归档后，可在“已归档回测”面板打开非模态逐笔浏览器；界面每页请求 256 条，显示真实成交时间、价格、股数、金额、B/S/M、源行和来源。关闭或切换归档不会继续展示旧页，且整个路径不访问 Provider、NAS 或网络。

## 契约与边界

- `AsyncBacktestArchiveService::requestLoad()` 在独立 SQLite 连接上使用 `Display` 模式，只恢复身份、摘要、有界权益、成交、信号和 raw 快照可用性。
- `requestRawTickPage()` 稳定按 `sequence_no` 分页。QML 默认 256 条；Controller、服务和存储层共同拒绝超过 4096 条的请求。
- `TradingController` 分别公开归档加载与逐笔页的 requestId、resolvedRequestId、loading 和 error。逐笔页另公开 resultId、offset、pageSize、totalCount、前后页状态及 `backtestArchiveRawTickModel`。
- 两条异步路径使用独立代次门控。新请求、清空结果、存储迁移或退出都会使晚到完成信号失效；旧页不能覆盖当前选择。
- 完整 `rawTicks` 仍仅由显式 `CompleteInputSnapshot` C++ 模式载入。QML 模型替换当前页而非累积页面，因此其 Tick 数不随归档总量增长。
- 当前成交和图表信号明细仍随轻量结果一次恢复，没有单独分页；本功能也不是逐笔回放器。

schema 保持 v24。页面使用局部“读取中”状态，不增加顶层模态 loading，也不改变三页工作台结构。

## 验证

TDD 使用临时 SQLite 验证 60 条 raw Tick 的异步轻量恢复与 offset 4/pageSize 3 分页，返回总数 60、源行 5～7；代次测试验证取消后旧完成被拒绝。真实 `BacktestArchivePanel.qml` 契约测试验证首次选择恢复、打开时请求第 0 页 256 条，以及结果身份变化时关闭旧浏览器。

最终 Debug 串行构建成功，CTest 44/44（72.17 秒）、`all_qmllint` 通过；隔离临时数据库在已保存副屏位置启动，退出探针 PID 9080 以 0 结束。本阶段未访问 NAS/外网，也未使用开发机数据库。尚未形成真实跨月归档翻页 p50/p95、进程 MiB 或 OS 文件缓存基线。
