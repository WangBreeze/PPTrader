# Qt 6.12.0 全面验收报告

- 日期：2026-11-10
- 工具链：Qt 6.12.0，**项目安装副本** `~/ProgramData/Qt/6.12.0/gcc_64`（`PPTRADER_QT_ALLOW_SYSTEM=OFF`）
- 构建目录：`out/build/linux-qt612-debug`（**从 fresh 配置开始的干净构建**）
- 机器：Arch Linux，CMake 4.4.5，GCC 20260810

## 1. 配置

```
-- PPTrader Qt：请求 6.12.0，使用前缀 /home/wanglc/ProgramData/Qt/6.12.0/gcc_64（项目约定安装位置）
-- PPTrader Qt：已确认 Qt 6.12.0（.../gcc_64/lib/cmake/Qt6）
```

| 缓存项 | 值 |
|---|---|
| `PPTRADER_QT_VERSION` | 6.12.0 |
| `PPTRADER_QT_ALLOW_SYSTEM` | **OFF**（未回退发行版 `/usr`） |
| `Qt6Core_DIR` | `~/ProgramData/Qt/6.12.0/gcc_64/lib/cmake/Qt6Core` |

## 2. 构建

`cmake --build --preset linux-qt612-debug --parallel` → **952/952，EXIT=0**

## 3. 自动化门禁

| 门禁 | 结果 | 备注 |
|---|---|---|
| **CTest** | **56/56 通过** | 48.44 秒；**必须 `QT_QPA_PLATFORM=offscreen`** |
| `all_qmllint` | **通过** | 0 错误，1 警告（`InstrumentSearchPanel.qml:35` 的 `var` 块作用域提升，既有问题） |

CTest 耗时 Top5：`application_exit` 16.86s、`custom_strategy_wizard_qml` 7.91s、
`realtime_chart_session` 4.88s、`candle_series_item` 4.82s、`batch_backtest` 2.17s。

### ⚠️ headless 环境的坑

无 GL 上下文时直接跑全量 CTest，`paper_strategy_controller` 会稳定失败（1 秒内退出）：

```
MESA-EGL: warning: failed to get driver name for fd -1
MESA: error: ZINK: failed to choose pdev
MESA-EGL: warning: egl: failed to create dri2 screen
```

该测试用 PPTrader 本体跑 `--paper-panel-probe`，会创建真实窗口 ⇒ 需要显示/GL。
**加 `QT_QPA_PLATFORM=offscreen` 即 56/56**，不是代码回归。

## 4. 应用级探针

| 探针 | 结果 |
|---|---|
| `--exit-probe` | **退出码 0** |
| `--wizard-contract-probe` | **101/101 检查通过**，退出码 0 |
| 主窗口加载（offscreen 12 秒） | 未提前退出（124=跑满超时），**QML 错误 0** |
| `PPTRADER_TICKDATA_LIVE=1` 实时源 | **正常投递**（`[TickData] 本轮 5207 个标的，新增 …条 tick`） |
| 同上但不设环境变量 | TickData 输出 **0 行**（默认零影响） |

## 5. 未通过项（环境依赖，非缺陷）

`--paper-flow-acceptance` 的 `preflight` 模式：

```
passed: false
minuteRows: 121
lastBarUtc: 2026-11-10T03:30:00Z   (= 北京时间 11:30，上午收盘)
error: A股行情: 启动中 python .../scripts/mootdx_bridge.py
```

- 拿到 **121 根上午分钟线** ⇒ 数据链路本身是通的
- 但**没有 tick** ⇒ 探针判失败。本次执行时刻为 **11:57，午休时段**，本来就没有新成交
- 另查到一处环境缺口：`mootdx` 0.11.7 装在 `/home/wanglc/.venvs/a-stock-data/bin/python`，
  而桥按 `python3` 解析（系统 Python 无 mootdx）；把 venv 放到 `PATH` 前置后错误信息不变，
  说明当时的阻塞点是「无盘中行情」而非模块缺失

**结论**：该探针需要在**盘中窗口**执行，且它不在 CTest 门禁内（属场景化人工验收，
还需 `PPTRADER_PAPER_FLOW_END`／`SOURCE`／`DATES` 等完整环境契约）。本次不作通过结论。

## 6. 仍未覆盖（不得宣称「6.12 已验证」）

| 门禁 | 原因 |
|---|---|
| APP-001 受控滚动条 | Windows 专属，Linux 无法覆盖 |
| GPU 渲染路径 | 本次全部用 offscreen 跑，未验证真实 GL 渲染 |
| 盘中实时长稳 | 需完整交易时段 + 真实 Provider |
| 6.11.2 时代的布局/退出/长稳结论 | **不能套用到 6.12.0** |

## 7. 一句话结论

**Qt 6.12.0 在项目安装副本上的自动化门禁全部通过（CTest 56/56 + qmllint + 三个应用探针），
但从零构建到应用启动的绿色不等于版本已验证**——APP-001、GPU 渲染、盘中长稳仍未覆盖。
