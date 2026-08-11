# 全市场工作台性能基线

最后更新：2026-08-02

## 测量边界

本基线面向 5207 个 A 股快照点，使用 Windows 11、Qt 6.4.3、MSVC Debug、20 个逻辑处理器的本机环境。结果用于发现相对退化，不等同于所有硬件上的发布性能。

`CrossSectionScatterItem` 暴露以下可开关 CPU 指标：

- `modelPreparationMilliseconds`：从 `MarketSnapshotListModel` 复制紧凑点数据并生成摘要；不包含 SQLite 查询。
- `sceneGraphUpdateMilliseconds`：`updatePaintNode()` 中分组、顶点分配和节点构建；兼容属性 `renderMilliseconds` 返回同一值。
- `visiblePointCount`、`geometryVertexCount` 和两类采样计数：用于确认筛选与几何版本一致。

这些指标不包含 GPU 提交、交换链呈现、显示器刷新或完整帧时。Debug 默认启用；Release 默认关闭。可用 `PPTRADER_PERF_METRICS=1/0` 显式覆盖。关闭时不启动计时器、不发送逐帧日志，排队中的旧采样也会按代次丢弃。

## 当前结果

`tst_cross_section_performance` 对每个场景预热后统计：

| Debug 场景 | 样本 | p50 | p95 | 防回归硬上界 |
| --- | ---: | ---: | ---: | ---: |
| 5207 点摘要/连续板块过滤 | 80 | 2.585 ms | 3.449 ms | 50 ms |
| 5207 点线性/对数坐标遍历 | 60 | 0.219 ms | 0.547 ms | 50 ms |
| 每批 1000 次 hover 命中 | 60 | 6.560 ms | 7.566 ms | 100 ms |
| 模型装载、排序和点缓存准备 | 24 | 51.363 ms | 55.581 ms | 250 ms |
| 板块/轴/尺寸变化后的 SG 几何重建 | 32 | 6.978 ms | 8.189 ms | 250 ms |

交互预算：几何重建 p95 目标低于 16.7 ms；自动刷新模型准备的 Debug 告警线为 70 ms，Release 目标为 40 ms。硬上界刻意放宽以容纳共享 CI 的 Debug 波动；超过目标但未超过硬上界时仍应人工分析。

## 刷新与线程稳定性

受控测试连续写入 3 个、每个 5207 行的快照，同时发起异步历史查询：GUI 线程上的 `enqueue/request` 最大派发耗时低于 1 ms，三批均落库，事件循环持续响应；请求门只接受最终代次，至少两个旧响应被拒绝。SQLite 写入和查询各使用独立工作线程/连接，Scene Graph 只读取 GUI 线程准备的紧凑缓存。

模型准备仍在 GUI 线程同步发生，本机 Debug p95 为 55.581 ms；它不会与 SQLite 互锁，但并非完整帧无停顿证明。当前 5 秒自动刷新下可接受，若刷新频率提升或 Release 超过 40 ms，应把排序/点缓存准备移到不可变后台快照，再在 GUI 线程一次交换。

## DPI 与连续交互

运行截图位于 `artifacts/performance-dpi/`：100% 为 1600×1000，125% 为 2000×1250 物理像素；150% 因验收显示器工作区高度限制，仅取得 2400×1421，不能声称完成了 1600×1000 逻辑高度的精确像素复核。三档均恢复真实 5076 点，未见点云越界。逻辑半径固定 2 px，测试确认对应 100%/125%/150% 的 2/2.5/3 物理像素半径。最终构建截图 `final-100.png` 同时确认底栏使用 `SG CPU` 口径。

另执行 20 轮 1120×900 ↔ 1600×1000 缩放、线性/对数切换和主板关闭/恢复，进程持续存活，最终点云、图例和摘要一致。截图只能证明最终稳定态，不能替代视频级闪烁检测或 GPU 帧时间采集。

## 重复执行

```powershell
cmake --build --preset vs2022-debug --parallel 1
ctest --preset vs2022-debug -R "cross_section_performance|market_snapshot_contract" --output-on-failure
ctest --preset vs2022-debug --output-on-failure
cmake --build --preset vs2022-debug --target all_qmllint --parallel 1
```

详细分位数可由测试程序输出到日志；比较结果时必须保持 Qt、编译配置、点数和机器负载一致。下一轮应补充 Release、独立 GPU 帧采集、125%/150% 等逻辑窗口高度和 30 分钟持续刷新 p95。
