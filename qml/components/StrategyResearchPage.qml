// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    property int strategyMode: 0
    property bool identityExpanded: false
    readonly property bool hasBacktestResult: controller.completedBacktestTaskId > 0
    readonly property bool customMode: strategyMode === 1
    readonly property var resultConfiguration: controller.backtestResultConfiguration
    readonly property bool customResultMismatch: customMode && hasBacktestResult
        && !controller.backtestResultMatchesCustomStrategy(
            customWorkbench.selectedStrategyId, customWorkbench.selectedVersion)

    component ConfigField: TextField {
        height: 34
        color: root.theme.textPrimary
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        leftPadding: 10
        selectByMouse: true
        background: Rectangle {
            radius: 5
            color: root.theme.bgInput
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    function commitConfiguration() {
        const bars = Number(barCountField.text)
        const capital = Number(capitalField.text)
        const commission = Number(commissionField.text) / 100.0
        const slippage = Number(slippageField.text) / 100.0
        const frequency = controller.selectedBacktestDataAvailable
                ? controller.selectedBacktestFrequency : frequencyBox.currentValue
        return controller.setBacktestConfiguration(frequency, bars, capital,
                                                   commission, slippage)
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, grid.y + grid.height + 20)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: scroll.contentHeight > scroll.height
                                              ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        Text {
            x: 20; y: 14; width: parent.width - 40; height: 34
            text: "指标与策略"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold
        }
        Text {
            x: 20; y: 50; width: parent.width - 40; height: 22
            text: "内置与自定义策略统一使用 SQLite 中的历史 Tick 派生 K 线；运行回测不会访问网络 Provider"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 11
        }
        Rectangle {
            x: 20; y: 80; width: parent.width - 40; height: 58; radius: 8
            color: root.theme.bgSelected; border.color: root.theme.cyan
            Text {
                x: 12; y: 8; width: parent.width - 280; height: 42
                verticalAlignment: Text.AlignVCenter
                text: root.customMode
                      ? "自定义 DSL 只允许历史回测；定义、版本、参数和数据 SHA-256 身份会随结果保存。"
                      : "已接入 4 项内置策略；品种、周期和根数必须来自历史 Tick 派生数据集，远端 K 线只作研究缓存。"
                color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 10
                elide: Text.ElideRight
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter; spacing: 8
                PpButton { theme: root.theme; text: "内置策略"; visualWidth: 108; active: !root.customMode; toolTip: "使用历史 Tick 派生 K 线，不访问网络"; onClicked: root.strategyMode = 0 }
                PpButton { theme: root.theme; text: "自定义策略"; visualWidth: 108; active: root.customMode; toolTip: "使用受限 JSON DSL 和历史 Tick 派生 K 线"; onClicked: root.strategyMode = 1 }
            }
        }

        GridLayout {
            id: grid
            x: 20; y: 154; width: parent.width - 40
            columns: width >= 1320 ? 3 : 2
            columnSpacing: 16; rowSpacing: 16

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 450
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                LocalBacktestSelector {
                    anchors.fill: parent; anchors.margins: 18
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 270
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Item {
                    anchors.fill: parent; visible: !root.customMode
                    Text { x: 18; y: 16; text: "历史 Tick 回测数据"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                    Text { x: 18; y: 50; width: parent.width - 36; text: root.controller.selectedBacktestDataAvailable ? root.controller.selectedBacktestInstrumentTitle : "尚未选择 Tick 派生数据集"; color: root.controller.selectedBacktestDataAvailable ? root.theme.cyan : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold; elide: Text.ElideRight }
                    Text { x: 18; y: 78; text: root.controller.selectedBacktestDataAvailable ? root.controller.selectedBacktestFrequency + " · " + root.controller.selectedBacktestCandleCount + " 根 · raw_tick_cache_v1" : "远端 K 线不会出现在回测选择器中"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                    PpButton { x: 18; y: 104; theme: root.theme; text: "刷新本地目录"; visualWidth: 126; onClicked: root.controller.refreshLocalBacktestDataCatalog() }
                    PpButton {
                        x: 154; y: 104; theme: root.theme; text: "运行回测"; visualWidth: 110
                        primary: true
                        available: root.controller.selectedBacktestDataAvailable
                                   && root.controller.selectedBacktestCandleCount >= Number(barCountField.text)
                                   && !root.controller.backtestRunning
                        toolTip: available ? "仅从 SQLite 读取历史 Tick 派生 K 线并运行选中策略"
                                           : "请选择根数足够的 Tick 派生数据集，或等待当前回测结束"
                        onClicked: if (root.commitConfiguration()) root.controller.runSelectedBacktest()
                    }
                    Text { x: 18; y: 154; width: parent.width - 36; text: "数据从外部 Tick 仓库显式同步并事务导入；选择品种、打开页面或运行回测均不会自动联网。"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10; wrapMode: Text.Wrap; Accessible.name: text }
                }
                Column {
                    x: 18; y: 16; width: parent.width - 36; spacing: 12
                    visible: root.customMode
                    Text { width: parent.width; text: "严格本地回测边界"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                    Text { width: parent.width; text: "数据来源：SQLite 历史 Tick 派生 catalog"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 11 }
                    Text { width: parent.width; text: root.controller.selectedBacktestDataAvailable ? root.controller.selectedBacktestInstrumentTitle + " · " + root.controller.selectedBacktestFrequency : "尚未选择本地数据集"; color: root.controller.selectedBacktestDataAvailable ? root.theme.textPrimary : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Text { width: parent.width; text: root.controller.selectedBacktestDataAvailable ? "可用 " + root.controller.selectedBacktestCandleCount + " 根 · 当前配置使用 " + root.controller.backtestBarCount + " 根" : "不会调用 fetchHistory，也不会回退网络 Provider"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10; wrapMode: Text.Wrap }
                    Rectangle { width: parent.width; height: 1; color: root.theme.border }
                    Text { width: parent.width; text: "自定义策略默认仅回测（backtest_only）；声明 executionScope=mock_execution 的版本可进入模拟执行（审批/冷却走执行配置，不接真实下单）。"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 10; wrapMode: Text.Wrap; Accessible.name: text }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 270
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 16; text: "回测状态与摘要"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { x: 18; y: 52; width: parent.width - 36; text: root.controller.backtestStatusText; color: root.controller.backtestRunning ? root.theme.yellow : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold }
                Rectangle { x: 18; y: 82; width: parent.width - 36; height: 8; radius: 4; color: root.theme.track; Rectangle { width: parent.width * Math.max(0, Math.min(1, root.controller.backtestProgress)); height: parent.height; radius: 4; color: root.theme.cyan } }
                Text { x: 18; y: 106; text: root.controller.backtestRunning ? "进度 " + (root.controller.backtestProgress * 100).toFixed(1) + "%" : "进度 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 106; text: root.controller.backtestTaskId > 0 ? "任务 " + root.controller.backtestTaskId : "任务 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { x: 18; y: 138; width: parent.width / 2 - 24; text: "最终权益\n" + (root.hasBacktestResult ? root.controller.finalEquity.toFixed(2) : "--"); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; lineHeight: 1.5 }
                Text { x: parent.width / 2; y: 138; width: parent.width / 2 - 18; text: "总收益\n" + (root.hasBacktestResult ? root.controller.totalReturnPercent.toFixed(2) + "%" : "--"); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; lineHeight: 1.5 }
                Text { x: 18; y: 190; width: parent.width / 2 - 24; text: "最大回撤\n" + (root.hasBacktestResult ? root.controller.maxDrawdownPercent.toFixed(2) + "%" : "--"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10; lineHeight: 1.5 }
                Text { x: parent.width / 2; y: 190; width: parent.width / 2 - 18; text: "成交数 / 胜率\n" + (root.hasBacktestResult ? root.controller.backtestTradeCount + " / " + root.controller.winRatePercent.toFixed(2) + "%" : "--"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10; lineHeight: 1.5 }
                Text { x: 18; y: 224; width: parent.width - 150; text: root.hasBacktestResult ? "Sharpe " + root.controller.sharpeRatio.toFixed(3) + " · 手续费 " + root.controller.totalCommission.toFixed(2) : "Sharpe -- · 手续费 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                PpButton { x: parent.width - 110; y: 224; visible: root.controller.backtestRunning; theme: root.theme; text: "取消回测"; visualWidth: 92; onClicked: root.controller.cancelBacktest() }
                Text { x: 18; y: 250; width: parent.width - 132; text: root.controller.backtestError; color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 340
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "远端 K 线研究缓存（不用于回测）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "仅显式下载；不会进入回测数据选择器"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                LocalHistoryDownloadPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 62
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 270
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "NAS Tick 按需同步"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "HTTPS catalog · SHA-256 · 本地事务入库"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                NasTickSyncPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 62
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 392
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "TickData 本地 CSV 导入"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "单代码 · 单交易日 · 只读显式选择的本地文件"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                TickDataImportPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 62
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 500
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "TickData 多交易日受控导入"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "先生成确定性计划 · 单文件事务 · 可取消/继续"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                TickDataBatchImportPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 62
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 470
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "本地分钟序列"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "SQLite 分页 · OHLCV 桶聚合 · Scene Graph 批量几何"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                LocalMinuteSeriesPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 62
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 250
                visible: !root.customMode
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 16; text: "内置回测策略"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 18; text: "单击选择 · 参数按策略独立校验"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                ListView {
                    x: 18; y: 52; width: parent.width - 36; height: 180
                    model: root.controller.backtestStrategyCatalog
                    clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        required property var modelData
                        readonly property string strategyId: modelData.strategyId
                        readonly property bool selected: root.controller.selectedBacktestStrategyId === strategyId
                        width: ListView.view.width; height: 48; radius: 7
                        color: selected ? root.theme.bgSelected : root.theme.bgInput
                        border.width: activeFocus || selected ? 2 : 1
                        border.color: activeFocus || selected ? root.theme.cyan : root.theme.border
                        activeFocusOnTab: true
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name
                        Accessible.description: modelData.description
                        Accessible.checkable: true
                        Accessible.checked: selected
                        Text { x: 12; y: 7; width: parent.width - 180; text: modelData.name; color: parent.selected ? root.theme.cyan : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold; elide: Text.ElideRight }
                        Text { x: 12; y: 27; width: parent.width - 180; text: modelData.description; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                        Text { anchors.right: parent.right; anchors.rightMargin: 12; y: 15; text: "预热 " + modelData.minimumWarmupBars + " 根"; color: parent.selected ? root.theme.cyan : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        MouseArea { anchors.fill: parent; onPressed: parent.forceActiveFocus(Qt.MouseFocusReason); onClicked: root.controller.selectBacktestStrategy(parent.strategyId) }
                        Keys.onSpacePressed: function(event) { root.controller.selectBacktestStrategy(strategyId); event.accepted = true }
                        Keys.onReturnPressed: function(event) { root.controller.selectBacktestStrategy(strategyId); event.accepted = true }
                    }
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 236
                visible: !root.customMode
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 16; text: "回测配置与策略参数"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Flow {
                    x: 18; y: 52; width: parent.width - 36; spacing: 12
                    Column {
                        spacing: 5
                        Text { text: "K 线周期（由 Tick 数据集决定）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        ComboBox {
                            id: frequencyBox
                            width: 130; height: 34
                            model: [{ label: "日线", value: "day" }, { label: "60 分钟", value: "60m" }, { label: "30 分钟", value: "30m" }, { label: "15 分钟", value: "15m" }, { label: "5 分钟", value: "5m" }, { label: "1 分钟", value: "1m" }]
                            textRole: "label"; valueRole: "value"
                            enabled: false
                            currentIndex: {
                                const selected = root.controller.selectedBacktestFrequency
                                for (let index = 0; index < model.length; ++index) {
                                    if (model[index].value === selected)
                                        return index
                                }
                                return 0
                            }
                            Accessible.name: "K 线周期"
                            contentItem: Text {
                                leftPadding: 10
                                verticalAlignment: Text.AlignVCenter
                                text: frequencyBox.displayText
                                color: root.theme.textPrimary
                                font.family: root.theme.fontFamily
                                font.pixelSize: 10
                            }
                            indicator: Text {
                                x: parent.width - width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: "⌄"
                                color: root.theme.textMuted
                                font.pixelSize: 14
                            }
                            background: Rectangle {
                                radius: 5; color: root.theme.bgInput
                                border.width: parent.activeFocus ? 2 : 1
                                border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                            }
                        }
                    }
                    Column {
                        spacing: 5
                        Text { text: "K 线数量"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        ConfigField {
                            id: barCountField
                            width: 120; height: 34
                            text: String(root.controller.backtestBarCount)
                            validator: IntValidator { bottom: 2; top: 5000 }
                            Accessible.name: "K 线数量"
                        }
                    }
                    Column {
                        spacing: 5
                        Text { text: "初始资金（元）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        ConfigField { id: capitalField; width: 150; text: String(root.controller.backtestConfiguredInitialCapital); Accessible.name: "初始资金" }
                    }
                    Column {
                        spacing: 5
                        Text { text: "佣金（%）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        ConfigField { id: commissionField; width: 110; text: String(root.controller.backtestCommissionRate * 100); Accessible.name: "佣金率百分比" }
                    }
                    Column {
                        spacing: 5
                        Text { text: "滑点（%）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        ConfigField { id: slippageField; width: 110; text: String(root.controller.backtestSlippageRate * 100); Accessible.name: "滑点率百分比" }
                    }
                    Column {
                        spacing: 5
                        Text { text: "样本窗（可留空）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        Row {
                            spacing: 6
                            ConfigField { id: sampleFromField; width: 118; placeholderText: "起始 yyyy-MM-dd"; Accessible.name: "样本窗起始日期" }
                            ConfigField { id: sampleToField; width: 118; placeholderText: "结束 yyyy-MM-dd"; Accessible.name: "样本窗结束日期" }
                            PpButton {
                                theme: root.theme; text: "应用"; visualWidth: 56
                                toolTip: "按日期范围过滤回测 K 线（留空则不限制）"
                                onClicked: {
                                    const from = Date.fromLocaleString(Qt.locale("zh_CN"), sampleFromField.text, "yyyy-MM-dd")
                                    const to = Date.fromLocaleString(Qt.locale("zh_CN"), sampleToField.text, "yyyy-MM-dd")
                                    root.controller.setBacktestSampleWindow(from, to)
                                }
                            }
                            PpButton {
                                theme: root.theme; text: "清除"; visualWidth: 56
                                toolTip: "清除样本窗限制"
                                onClicked: root.controller.clearBacktestSampleWindow()
                            }
                        }
                    }
                    Repeater {
                        model: root.controller.selectedBacktestStrategyDescriptor.parameters || []
                        delegate: Column {
                            required property var modelData
                            spacing: 5
                            Text { text: modelData.displayName + "（" + modelData.minimumValue + "–" + modelData.maximumValue + "）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            ConfigField {
                                width: 150
                                text: String(root.controller.backtestParameterValue(modelData.key))
                                Accessible.name: modelData.displayName
                                onEditingFinished: {
                                    if (!root.controller.setSelectedBacktestParameter(modelData.key, text))
                                        text = String(root.controller.backtestParameterValue(modelData.key))
                                }
                            }
                        }
                    }
                }
                Text { x: 18; y: 142; width: parent.width - 250; text: root.controller.backtestConfigurationError; color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 10; elide: Text.ElideRight }
                Text { x: 18; y: 172; width: parent.width - 250; text: "当前：" + root.controller.selectedBacktestStrategyDescriptor.name + " · 参数 " + JSON.stringify(root.controller.selectedBacktestParameters); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                PpButton { anchors.right: parent.right; anchors.rightMargin: 126; y: 164; theme: root.theme; text: "恢复默认"; visualWidth: 96; onClicked: root.controller.resetSelectedBacktestParameters() }
                PpButton { anchors.right: parent.right; anchors.rightMargin: 18; y: 164; theme: root.theme; text: "保存配置"; visualWidth: 96; primary: true; onClicked: root.commitConfiguration() }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 610
                visible: root.customMode
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "自定义策略版本与 JSON"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "声明式 DSL · SQLite 严格本地 · 不进入订单链路"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                CustomStrategyWorkbench {
                    id: customWorkbench
                    x: 18; y: 48; width: parent.width - 36; height: parent.height - 66
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 250
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                BacktestArchivePanel {
                    anchors.fill: parent; anchors.margins: 18
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 500
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                BatchBacktestPanel {
                    anchors.fill: parent; anchors.margins: 18
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 620
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                PortfolioBacktestPanel {
                    anchors.fill: parent; anchors.margins: 18
                    theme: root.theme; controller: root.controller
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true
                Layout.preferredHeight: root.customMode
                    ? (root.identityExpanded ? 540 : 438) : 390
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 16; text: "权益曲线与成交明细"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: root.customMode ? 132 : 18; y: 18; text: root.hasBacktestResult ? (root.controller.backtestResultHistoricalSnapshot ? "历史归档 · " : "当前任务 · ") + root.controller.backtestResultSymbol + " · " + root.controller.backtestResultStrategyId + " · 任务 " + root.controller.completedBacktestTaskId : "尚无结果"; color: root.customResultMismatch || root.controller.backtestResultHistoricalSnapshot ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                PpButton {
                    anchors.right: parent.right; anchors.rightMargin: 14; y: 6
                    visible: root.customMode; theme: root.theme
                    text: root.identityExpanded ? "收起身份" : "结果身份"
                    visualWidth: 104; active: root.identityExpanded
                    toolTip: "查看并复制策略版本、规范化参数和数据 SHA-256"
                    onClicked: root.identityExpanded = !root.identityExpanded
                }
                Rectangle {
                    x: 18; y: 48; width: parent.width - 36
                    height: root.identityExpanded ? 118 : 34
                    visible: root.customMode
                    radius: 6; color: root.theme.bgInput
                    border.color: root.customResultMismatch ? root.theme.yellow : root.theme.border
                    Text {
                        x: 10; y: 9; width: parent.width - 20
                        text: !root.hasBacktestResult ? "尚无已完成回测结果"
                              : root.customResultMismatch
                                ? "结果属于其他策略版本：" + root.resultConfiguration.strategyId
                                  + " v" + root.resultConfiguration.strategyVersion
                                : "结果身份与当前自定义策略版本一致"
                        color: root.customResultMismatch ? root.theme.yellow : root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                        Accessible.name: text
                    }
                    TextArea {
                        x: 8; y: 30; width: parent.width - 16; height: 46
                        visible: root.identityExpanded && root.hasBacktestResult
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                        text: "strategyId=" + (root.resultConfiguration.strategyId || "--")
                              + " · version=" + (root.resultConfiguration.strategyVersion || 0)
                              + " · parameters=" + JSON.stringify(root.resultConfiguration.strategyParameters || {})
                              + "\ndataVersion=" + (root.resultConfiguration.dataVersion || "--")
                              + "\ndatasetVersion="
                              + (root.resultConfiguration.datasetIdentity
                                 && root.resultConfiguration.datasetIdentity.datasetVersion
                                 ? root.resultConfiguration.datasetIdentity.datasetVersion : "--")
                        color: root.theme.textMuted; selectionColor: root.theme.cyan
                        selectedTextColor: root.theme.bgCanvas
                        font.family: "Consolas"; font.pixelSize: 8
                        background: null
                        Accessible.name: "回测结果版本与数据身份"
                        Accessible.description: text
                    }
                    Text {
                        x: 10; y: 96; width: parent.width - 20
                        visible: root.identityExpanded && root.hasBacktestResult
                        text: {
                            const identity = root.resultConfiguration.datasetIdentity
                            if (!identity || !identity.datasetVersion) return "数据集身份：--"
                            return "数据集：" + Qt.formatDate(identity.firstTradingDate, "yyyy-MM-dd")
                                   + " ~ " + Qt.formatDate(identity.lastTradingDate, "yyyy-MM-dd")
                                   + " · " + identity.dayCount + " 交易日 · "
                                   + identity.rawTickCount + " 原始 Tick"
                                   + (identity.gapDayCount > 0
                                      ? " · 缺口 " + identity.gapDayCount : "")
                                   + " · " + (identity.calendarCoverageKnown
                                              ? identity.calendarSource : "日历未覆盖")
                        }
                        color: root.theme.yellow
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 8
                        Accessible.name: text
                    }
                }
                RowLayout {
                    x: 18
                    y: root.customMode ? (root.identityExpanded ? 178 : 94) : 52
                    width: parent.width - 36
                    height: parent.height - y - 20
                    spacing: 16
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 360
                        color: root.theme.bgInput; radius: 8; border.color: root.theme.border
                        Text { x: 12; y: 8; text: "K 线与买卖点"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                        BacktestCandleChart { anchors.fill: parent; anchors.margins: 8; anchors.topMargin: 30; theme: root.theme; model: root.controller.candleModel }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 320
                        color: root.theme.bgInput; radius: 8; border.color: root.theme.border
                        BacktestEquityChart { anchors.fill: parent; anchors.margins: 8; theme: root.theme; model: root.controller.equityCurveModel }
                    }
                    Rectangle {
                        Layout.preferredWidth: Math.min(520, parent.width * 0.42); Layout.fillHeight: true
                        color: root.theme.bgInput; radius: 8; border.color: root.theme.border
                        Text { x: 12; y: 10; text: "成交记录（" + root.controller.backtestTradeModel.count + "）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                        ListView {
                            x: 12; y: 36; width: parent.width - 24; height: parent.height * 0.42 - 40
                            model: root.controller.backtestTradeModel
                            clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property int index
                                required property string symbol
                                required property var entryTime
                                required property var exitTime
                                required property real netPnl
                                required property real returnPercent
                                required property bool profitable
                                width: ListView.view.width; height: 44; radius: 5
                                color: index % 2 ? root.theme.bgRaised : "transparent"
                                Text { x: 8; y: 6; text: symbol + "  " + Qt.formatDateTime(entryTime, "MM-dd") + " → " + Qt.formatDateTime(exitTime, "MM-dd"); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                                Text { anchors.right: parent.right; anchors.rightMargin: 8; y: 6; text: (profitable ? "+" : "") + netPnl.toFixed(2); color: profitable ? root.theme.green : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold }
                                Text { x: 8; y: 25; text: "收益率 " + returnPercent.toFixed(2) + "%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            }
                        }
                        Text { x: 12; y: parent.height * 0.46; text: "验证记录（" + root.controller.validationRuns.length + "）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                        PpButton {
                            x: 12; y: parent.height * 0.46 + 24; visualWidth: 88; visualHeight: 22
                            theme: root.theme; text: "存为验证"
                            available: root.controller.backtestTradeModel.count > 0 && !root.controller.backtestRunning
                            toolTip: available ? "把当前回测结果保存为验证记录" : "请先完成回测"
                            onClicked: root.controller.saveValidationRun(
                                "验证 " + Qt.formatDateTime(new Date(), "MM-dd HH:mm"))
                        }
                        ListView {
                            x: 12; y: parent.height * 0.46 + 52; width: parent.width - 24
                            height: parent.height * 0.54 - 60
                            model: root.controller.validationRuns
                            clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property int index
                                required property string name
                                required property string instrumentSymbol
                                required property double totalReturnPercent
                                required property var completedAt
                                width: ListView.view.width; height: 30; radius: 5
                                color: index % 2 ? root.theme.bgRaised : "transparent"
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        text: name + " · " + instrumentSymbol + " · "
                                              + (totalReturnPercent >= 0 ? "+" : "")
                                              + Number(totalReturnPercent).toFixed(2) + "%"
                                        color: totalReturnPercent >= 0 ? root.theme.green : root.theme.red
                                        font.family: root.theme.fontFamily; font.pixelSize: 8
                                        elide: Text.ElideRight
                                    }
                                    PpButton {
                                        Layout.preferredWidth: 56; Layout.preferredHeight: 20
                                        theme: root.theme; text: "恢复"
                                        available: !root.controller.backtestRunning
                                        toolTip: "恢复该验证的样本窗供重跑"
                                        onClicked: root.controller.loadValidationRun(index)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.columnSpan: grid.columns
                Layout.fillWidth: true; Layout.preferredHeight: 380
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                Text { x: 18; y: 14; text: "执行审批与通知"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.rightMargin: 18; y: 17; text: "模拟执行 · 桌面/Webhook 通知"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                ExecutionApprovalPanel {
                    x: 18; y: 46; width: parent.width - 36; height: parent.height - 60
                    theme: root.theme; controller: root.controller
                }
            }
        }
    }
}
