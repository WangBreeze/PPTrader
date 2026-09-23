pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    required property var theme
    required property var controller
    readonly property bool historicalReplay: controller.paperStrategyRuns.length > 0
        && controller.paperStrategyRuns[0].sourceMode === "nas-historical-replay"
    readonly property var selectedDefinition: strategyChoice.currentIndex >= 0
        ? controller.customStrategyExecutionCatalog[strategyChoice.currentIndex] : null
    property var executionScrollYByRunId: ({})

    function storedExecutionScrollY(runId) {
        const value = executionScrollYByRunId[String(runId)]
        return value === undefined || !isFinite(Number(value)) ? -1 : Number(value)
    }

    function rememberExecutionScrollY(runId, contentY) {
        const nextPositions = {}
        for (const key in executionScrollYByRunId)
            nextPositions[key] = executionScrollYByRunId[key]
        nextPositions[String(runId)] = Math.max(0, Number(contentY))
        executionScrollYByRunId = nextPositions
    }

    component PaperField: TextField {
        implicitHeight: 32
        color: root.theme.textPrimary
        placeholderTextColor: root.theme.textMuted
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        font.family: root.theme.fontFamily
        selectByMouse: true
        background: Rectangle {
            radius: 5; color: root.theme.bgInput
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    FileDialog {
        id: reportDialog
        title: "导出模拟实验报告（非收益认证）"
        fileMode: FileDialog.SaveFile
        nameFilters: ["JSON 报告 (*.json)"]
        defaultSuffix: "json"
        onAccepted: root.controller.exportPaperStrategyReport(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Label { text: "策略流程 · 工作台统一模拟账户"; color: root.theme.textPrimary; font.pixelSize: 17; font.bold: true }
        Label {
            Layout.fillWidth: true
            text: root.historicalReplay
                ? "历史 Tick 按原始时间回放，派生 1m 指标输入；股票共用工作台唯一资金持仓。来源和覆盖以本次报告为准，不接真实下单。"
                : "一个生效策略控制工作台唯一资金持仓，指标按股票独立计算。当前实时页面入口仍为单股预检，尚未开放全市场实时容量。"
            color: root.theme.textMuted; wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: root.historicalReplay
                ? "历史回放：源时钟到分钟标签后至少 180 秒才计算；原始成交价只作为研究报价代理，不具备真实盘口深度。不把历史时刻改成今天。"
                : "研究确认：分钟两次相隔至少 30 秒不变、有后继标签、标签后至少 180 秒。不是交易所闭合证明；信号只等待后续新报价模拟成交。"
            color: root.theme.cyan; wrapMode: Text.Wrap; font.pixelSize: 11
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.width < 850 ? 2 : 4
            columnSpacing: 12; rowSpacing: 6
            Label { text: "已保存策略"; color: root.theme.textMuted }
            ComboBox {
                id: strategyChoice
                Layout.fillWidth: true; Layout.minimumWidth: 160
                model: root.controller.customStrategyExecutionCatalog
                textRole: "name"
                palette.text: root.theme.textPrimary
                palette.base: root.theme.bgInput
                palette.button: root.theme.bgRaised
                palette.buttonText: root.theme.textPrimary
                palette.highlight: root.theme.cyan
                palette.highlightedText: root.theme.bgCanvas
            }
            Label { text: "固定 A 股代码"; color: root.theme.textMuted }
            PaperField { id: symbolInput; Layout.fillWidth: true; text: "600519"; maximumLength: 6; placeholderText: "沪深股票代码" }
            Label { text: "工作台初始资金（只读）"; color: root.theme.textMuted }
            PaperField { id: capitalInput; Layout.fillWidth: true; text: String(root.controller.accountInitialEquity); readOnly: true }
            Label { text: "佣金比例 / 滑点比例"; color: root.theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                PaperField { id: feeInput; Layout.fillWidth: true; text: "0.0003"; readOnly: true }
                PaperField { id: slipInput; Layout.fillWidth: true; text: "0.0002"; readOnly: true }
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
            color: root.theme.textMuted
            text: root.selectedDefinition ? root.selectedDefinition.strategyId + " · v" + root.selectedDefinition.version
                + " · " + root.selectedDefinition.executionScope : "请先在向导中保存策略；仅回测版本需显式发布新的模拟版本。"
        }
        Flow {
            Layout.fillWidth: true; spacing: 8
            PpButton {
                theme: root.theme; visualWidth: 136
                text: "发布新的模拟版本"
                available: !!root.selectedDefinition && root.selectedDefinition.executionScope === "backtest_only" && !root.controller.customStrategySaveLoading
                onClicked: root.controller.publishPaperStrategyVersion(root.selectedDefinition.strategyId, root.selectedDefinition.version)
            }
            PpButton {
                theme: root.theme; visualWidth: 130; primary: true
                text: "绑定策略到一仓"
                available: !!root.selectedDefinition && root.selectedDefinition.executionScope === "mock_execution"
                    && root.controller.paperStrategyRuns.length === 0
                onClicked: root.controller.startPaperStrategyRun(root.selectedDefinition.strategyId,
                    root.selectedDefinition.version, symbolInput.text, Number(capitalInput.text), Number(feeInput.text), Number(slipInput.text))
            }
            PpButton { theme: root.theme; visualWidth: 112; text: "停止全部模拟"; onClicked: root.controller.stopAllPaperStrategies() }
            PpButton { theme: root.theme; visualWidth: 112; text: "导出实验报告"; available: root.controller.paperStrategyRuns.length > 0; onClicked: reportDialog.open() }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.cyan
            text: root.controller.paperStrategyError || root.controller.customStrategySaveError
            visible: text.length > 0
        }
        ListView {
            id: runsView
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 160
            clip: true; spacing: 8
            model: root.controller.paperStrategyRuns
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: runCard
                required property var modelData
                width: runsView.width - 14; height: runDetails.implicitHeight + 22
                radius: 6; color: root.theme.bgPanel; border.color: root.theme.border
                ColumnLayout {
                    id: runDetails
                    x: 10; y: 10; width: parent.width - 20; spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.textPrimary
                            text: (runCard.modelData.sourceMode === "nas-historical-replay" ? "[NAS 历史回放] " : "[真实行情] ")
                                + runCard.modelData.name + " · v" + runCard.modelData.version + " · 股票池 " + runCard.modelData.symbols.length + " 只"
                                + (runCard.modelData.stopped ? " · 已停止（保留持仓）" : runCard.modelData.waitingForQuote ? " · 等待新报价" : runCard.modelData.seeded ? " · 模拟运行" : " · 预热中")
                        }
                        PpButton { theme: root.theme; visualWidth: 60; text: "停止"; available: !runCard.modelData.stopped; onClicked: root.controller.stopPaperStrategyRun(runCard.modelData.runId) }
                    }
                    Label {
                        objectName: "paperAccountSummary"
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.textPrimary
                        text: "可用现金 " + Number(runCard.modelData.cash).toFixed(2)
                            + " / 账户净值 " + Number(runCard.modelData.equity).toFixed(2)
                            + " / 总盈亏 " + Number(runCard.modelData.totalPnl).toFixed(2)
                            + " / 收益率 " + Number(runCard.modelData.returnPercent).toFixed(2) + "%"
                            + "\n持仓市值 " + Number(runCard.modelData.marketValue).toFixed(2)
                            + " / 总仓位 " + Number(runCard.modelData.positionPercent).toFixed(2) + "%"
                            + " / 持仓标的 " + runCard.modelData.holdingSymbolCount + " 只"
                            + " / 可售标的 " + runCard.modelData.sellableSymbolCount + " 只"
                            + " / 累计已实现 " + Number(runCard.modelData.realizedPnl).toFixed(2)
                            + " / 持仓浮盈亏 " + Number(runCard.modelData.unrealizedPnl).toFixed(2)
                    }
                    Label {
                        objectName: "paperOrderSummary"
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.textMuted; font.pixelSize: 11
                        text: "计算 " + runCard.modelData.barCount + " 根（预热要求 " + runCard.modelData.warmupBars + "） · 信号 " + runCard.modelData.signalCount
                            + " · 已提交订单 " + runCard.modelData.orderCount
                            + " · 成交 " + runCard.modelData.fillCount
                            + "（买 " + runCard.modelData.buyFillCount + " / 卖 " + runCard.modelData.sellFillCount + "）"
                            + " · 未提交信号 " + runCard.modelData.blockedSignalCount
                            + " · 订单拒绝 " + runCard.modelData.orderRejectedCount
                            + " · 分钟请求成功/失败 " + runCard.modelData.minuteSuccessCount + "/" + runCard.modelData.minuteFailureCount
                            + "\n未提交原因：买入资金不足一手 " + runCard.modelData.rejectedBuyCashCount
                            + " · 买入触及仓位门禁 " + runCard.modelData.rejectedBuyPositionLimitCount
                            + " · 卖出受 T+1 限制 " + runCard.modelData.rejectedSellTPlusOneCount
                            + " · 卖出无持仓 " + runCard.modelData.rejectedSellNoPositionCount
                            + " · 订单提交失败 " + runCard.modelData.rejectedSubmitCount
                            + "\n最后计算 UTC " + runCard.modelData.lastBarUtc + " · " + runCard.modelData.runId
                    }
                    Label {
                        Layout.fillWidth: true; color: root.theme.textPrimary; font.pixelSize: 12; font.bold: true
                        text: "持仓明细（" + runCard.modelData.holdingSymbolCount + " 只）"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Repeater {
                            model: runCard.modelData.positionRecords
                            delegate: Rectangle {
                                id: positionRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 27
                                color: positionRow.modelData.symbol.charCodeAt(5) % 2 === 0
                                    ? root.theme.bgRaised : root.theme.bgCanvas
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    Label {
                                        Layout.preferredWidth: 108
                                        text: positionRow.modelData.symbol + (positionRow.modelData.name ? " " + positionRow.modelData.name : "")
                                        elide: Text.ElideRight
                                        color: root.theme.textPrimary; font.pixelSize: 10
                                    }
                                    Label { Layout.preferredWidth: 82; text: "持有 " + Number(positionRow.modelData.quantity).toFixed(0); color: root.theme.textPrimary; font.pixelSize: 10 }
                                    Label { Layout.preferredWidth: 82; text: "可售 " + Number(positionRow.modelData.availableQuantity).toFixed(0); color: root.theme.textPrimary; font.pixelSize: 10 }
                                    Label { Layout.preferredWidth: 94; text: "市值 " + Number(positionRow.modelData.marketValue).toFixed(2); color: root.theme.textPrimary; font.pixelSize: 10 }
                                    Label { Layout.preferredWidth: 82; text: "现价 " + Number(positionRow.modelData.marketPrice).toFixed(3); color: root.theme.textMuted; font.pixelSize: 10 }
                                    Label {
                                        Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
                                        text: "浮盈亏 " + Number(positionRow.modelData.unrealizedPnl).toFixed(2)
                                        color: Number(positionRow.modelData.unrealizedPnl) >= 0 ? root.theme.red : root.theme.green
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: runCard.modelData.positionRecords.length === 0
                            text: "当前无持仓"
                            color: root.theme.textMuted; font.pixelSize: 11
                        }
                    }
                    Label {
                        Layout.fillWidth: true; color: root.theme.textPrimary; font.pixelSize: 12; font.bold: true
                        text: "成交记录（显示最近 " + runCard.modelData.displayedExecutionRecordCount
                            + " / 共 " + runCard.modelData.executionRecordCount + "；可滚轮或拖动右侧滚动条）"
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(196, Math.max(34,
                            runCard.modelData.executionRecords.length * 30 + 2))
                        radius: 5
                        color: root.theme.bgCanvas
                        border.color: root.theme.border
                        ListView {
                            id: executionView
                            objectName: "paperExecutionList"
                            property bool restoringScrollPosition: false
                            property bool modelRefreshPending: false
                            readonly property string runIdentity: String(runCard.modelData.runId)
                            readonly property real retainedContentY: root.storedExecutionScrollY(runCard.modelData.runId)
                            anchors.fill: parent
                            anchors.margins: 1
                            clip: true
                            spacing: 1
                            model: runCard.modelData.executionRecords
                            interactive: contentHeight > height
                            boundsBehavior: Flickable.StopAtBounds
                            flickableDirection: Flickable.VerticalFlick
                            function restoreStoredScrollPosition() {
                                const storedY = root.storedExecutionScrollY(runCard.modelData.runId)
                                if (storedY < 0) {
                                    modelRefreshPending = false
                                    return
                                }
                                const maximumY = Math.max(0, contentHeight - height)
                                restoringScrollPosition = true
                                contentY = Math.max(0, Math.min(maximumY, storedY))
                                restoringScrollPosition = false
                            }
                            function scheduleScrollPositionRestore() {
                                modelRefreshPending = true
                                restoreStoredScrollPosition()
                            }
                            onModelChanged: scheduleScrollPositionRestore()
                            onContentHeightChanged: {
                                if (root.storedExecutionScrollY(runCard.modelData.runId) >= 0)
                                    restoreStoredScrollPosition()
                            }
                            onContentYChanged: {
                                if (restoringScrollPosition)
                                    return
                                if (modelRefreshPending) {
                                    restoreStoredScrollPosition()
                                    return
                                }
                                if (!restoringScrollPosition)
                                    root.rememberExecutionScrollY(runCard.modelData.runId, contentY)
                            }
                            onMovementStarted: modelRefreshPending = false
                            onMovementEnded: {
                                if (!restoringScrollPosition)
                                    root.rememberExecutionScrollY(runCard.modelData.runId, contentY)
                            }
                            Component.onCompleted: scheduleScrollPositionRestore()
                            ScrollBar.vertical: ScrollBar {
                                id: executionScrollBar
                                z: 3
                                policy: executionView.contentHeight > executionView.height
                                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                                interactive: true
                                onPositionChanged: {
                                    if (pressed && !executionView.restoringScrollPosition)
                                        root.rememberExecutionScrollY(runCard.modelData.runId,
                                            executionView.contentY)
                                }
                                onPressedChanged: {
                                    if (pressed)
                                        executionView.modelRefreshPending = false
                                    if (!pressed && !executionView.restoringScrollPosition)
                                        root.rememberExecutionScrollY(runCard.modelData.runId,
                                            executionView.contentY)
                                }
                            }
                            delegate: Rectangle {
                                id: executionRow
                                required property var modelData
                                width: executionView.width
                                height: 29
                                color: executionRow.modelData.recordNumber % 2 === 0 ? root.theme.bgRaised : root.theme.bgCanvas
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    Label {
                                        Layout.preferredWidth: 132
                                        text: executionRow.modelData.timestampLocal
                                        color: root.theme.textMuted
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        Layout.preferredWidth: 106
                                        text: executionRow.modelData.symbol + (executionRow.modelData.name ? " " + executionRow.modelData.name : "")
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        Layout.preferredWidth: 34
                                        text: executionRow.modelData.side
                                        color: executionRow.modelData.sideCode === "buy" ? root.theme.red : root.theme.green
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                    Label {
                                        Layout.preferredWidth: 74
                                        horizontalAlignment: Text.AlignRight
                                        text: Number(executionRow.modelData.quantity).toFixed(0) + " 股"
                                        color: root.theme.textPrimary
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        Layout.preferredWidth: 72
                                        horizontalAlignment: Text.AlignRight
                                        text: "@ " + Number(executionRow.modelData.price).toFixed(3)
                                        color: root.theme.textPrimary
                                        font.pixelSize: 10
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignRight
                                        text: "费用 " + Number(executionRow.modelData.commission).toFixed(2)
                                        color: root.theme.textMuted
                                        font.pixelSize: 10
                                    }
                                }
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: runCard.modelData.executionRecords.length === 0
                                text: "暂无成交；买入和卖出成交后会逐笔显示"
                                color: root.theme.textMuted
                                font.pixelSize: 11
                            }
                            MouseArea {
                                objectName: "paperExecutionWheelArea"
                                anchors.fill: parent
                                anchors.rightMargin: 16
                                z: 2
                                acceptedButtons: Qt.NoButton
                                hoverEnabled: true
                                onWheel: function(wheel) {
                                    const maximumY = Math.max(0, executionView.contentHeight - executionView.height)
                                    const nextY = Math.max(0, Math.min(maximumY,
                                        executionView.contentY - wheel.angleDelta.y))
                                    executionView.contentY = nextY
                                    root.rememberExecutionScrollY(runCard.modelData.runId, nextY)
                                    wheel.accepted = true
                                }
                            }
                        }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.cyan; text: runCard.modelData.feedError || runCard.modelData.error; visible: text.length > 0 }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.textMuted
                        text: (runCard.modelData.dataQualityWarning || "")
                            + (runCard.modelData.dataQualityWarning ? "\n" + runCard.modelData.indicatorWindowBasis : "")
                        visible: text.length > 0
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: root.theme.textMuted; font.pixelSize: 11
            text: "工作台只允许一个生效策略。停止撤销本策略余单、不强平、不重置账户；安全接管验收前禁止再次启动。请导出运行报告；重启接管、真实盘口深度和完整费用模型尚未验收，不代表盈利。"
        }
    }
}
