// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 两条研究链路共享入口：旧链路扫描自定义策略的派生 K 线参数；新链路对内置
// 原始 Tick 策略执行严格训练选参 + 最终留出验证。两类结果和身份互不混用。
Item {
    id: root
    required property var theme
    required property var controller
    property int scanMode: 0 // 0 自定义 K 线，1 原始 Tick

    readonly property var rawStrategies: {
        const result = []
        const catalog = root.controller.backtestStrategyCatalog || []
        for (const strategy of catalog) {
            if (strategy.supportsRawTicks) result.push(strategy)
        }
        return result
    }
    readonly property var currentRawStrategy: rawStrategyBox.currentIndex >= 0
        && rawStrategyBox.currentIndex < rawStrategies.length
        ? rawStrategies[rawStrategyBox.currentIndex] : ({})
    readonly property var rawResult: root.controller.rawTickSensitivityResult || ({})
    readonly property bool hasRawResult: rawResult.resultId !== undefined
        && String(rawResult.resultId).length > 0
    readonly property bool rawNestedResult: hasRawResult
        && String(rawResult.analysisMode || "") === "nested_walk_forward"

    function parseAxis(keyText, valuesText) {
        const key = keyText.trimmed()
        if (key.length === 0 || valuesText.trimmed().length === 0) return null
        const values = valuesText.split(",").map(function(s) { return Number(s.trimmed()) })
                                     .filter(function(n) { return !isNaN(n) })
        if (values.length === 0) return null
        return { key: key, values: values }
    }

    function rawDefaultParameters() {
        const values = ({})
        const specs = currentRawStrategy.parameters || []
        for (const spec of specs) values[spec.key] = spec.defaultValue
        return values
    }

    function resetRawAxes() {
        const id = String(currentRawStrategy.strategyId || "")
        rawAxis1Key.text = "lookbackTicks"
        rawAxis1Values.text = "10,20,50"
        if (id === "tick_mean_reversion") {
            rawAxis2Key.text = "entryDeviationPercent"
            rawAxis2Values.text = "0.05,0.1,0.2"
        } else if (id === "tick_volume_breakout") {
            rawAxis2Key.text = "volumeMultiplier"
            rawAxis2Values.text = "1.2,1.5,2.0"
        } else if (id === "tick_directional_imbalance") {
            rawAxis2Key.text = "entryImbalancePercent"
            rawAxis2Values.text = "10,30,50"
        } else {
            rawAxis2Key.text = "thresholdPercent"
            rawAxis2Values.text = "0.05,0.1,0.2"
        }
    }

    function runScan() {
        if (scanMode === 0) {
            const customAxis1 = parseAxis(customAxis1Key.text, customAxis1Values.text)
            const customAxis2 = parseAxis(customAxis2Key.text, customAxis2Values.text)
            const customAxes = []
            if (customAxis1) customAxes.push(customAxis1)
            if (customAxis2) customAxes.push(customAxis2)
            root.controller.runParameterGridBacktest(
                customStrategyId.text, Number(customVersion.text) || 1,
                customSymbol.text, Number(customPeriod.text) || 60,
                Number(customBars.text) || 300, customAxes)
            return
        }
        const rawAxis1 = parseAxis(rawAxis1Key.text, rawAxis1Values.text)
        const rawAxis2 = parseAxis(rawAxis2Key.text, rawAxis2Values.text)
        const rawAxes = []
        if (rawAxis1) rawAxes.push(rawAxis1)
        if (rawAxis2) rawAxes.push(rawAxis2)
        root.controller.runRawTickParameterSensitivity(
            String(currentRawStrategy.strategyId || ""), rawDefaultParameters(),
            rawSymbol.text, rawFrom.text, rawTo.text,
            Number(rawValidationDays.text) || 2,
            Number(rawFoldCount.text) || 1, rawAxes)
    }

    function formatParameters(params) {
        if (!params) return "默认参数"
        const keys = Object.keys(params)
        if (keys.length === 0) return "默认参数"
        return keys.map(function(k) { return k + "=" + params[k] }).join(", ")
    }

    function formatDate(value) {
        if (!value) return "--"
        const text = String(value)
        const isoDate = text.match(/^\d{4}-\d{2}-\d{2}/)
        if (isoDate) return isoDate[0]
        const parsed = new Date(value)
        return isNaN(parsed.getTime()) ? text : Qt.formatDate(parsed, "yyyy-MM-dd")
    }

    function rawResultSummary() {
        if (!hasRawResult) return "尚未运行"
        if (rawNestedResult) {
            return "已归档 · 嵌套 walk-forward "
                + Number(rawResult.walkForwardFoldCount) + " 折"
                + " · 验证均值 "
                + Number(rawResult.meanValidationReturnPercent).toFixed(2) + "%"
                + " · 中位数 "
                + Number(rawResult.medianValidationReturnPercent).toFixed(2) + "%"
                + " · 正收益折 "
                + Number(rawResult.positiveValidationFoldCount) + "/"
                + Number(rawResult.walkForwardFoldCount)
                + " · 选参变体 "
                + Number(rawResult.parameterSelectionVariantCount)
                + " · 装载 " + Number(rawResult.loadMilliseconds) + " ms"
                + " · 扫描 " + Number(rawResult.scanMilliseconds) + " ms"
        }
        return "已归档 · 胜出 " + formatParameters(rawResult.selectedParameters)
            + " · 验证 " + Number(rawResult.validationReturnPercent).toFixed(2)
            + "% · 回撤 "
            + Number(rawResult.validationMaxDrawdownPercent).toFixed(2)
            + "% · 装载 " + Number(rawResult.loadMilliseconds) + " ms"
            + " · 扫描 " + Number(rawResult.scanMilliseconds) + " ms"
            + " · 训练 " + formatDate(rawResult.trainingStartDate)
            + " 至 " + formatDate(rawResult.trainingEndDate)
            + " · 验证 " + formatDate(rawResult.validationStartDate)
            + " 至 " + formatDate(rawResult.validationEndDate)
    }

    component ScanField: TextField {
        height: 30
        color: root.theme.textPrimary
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        placeholderTextColor: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        leftPadding: 8
        selectByMouse: true
        background: Rectangle {
            radius: 5; color: root.theme.bgCanvas
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    component FieldLabel: Text {
        color: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 9
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PpButton {
                theme: root.theme; text: "自定义 K线网格"; visualWidth: 130
                active: root.scanMode === 0
                toolTip: "扫描自定义 JSON 策略的本地派生 K 线参数"
                onClicked: root.scanMode = 0
            }
            PpButton {
                theme: root.theme; text: "原始 Tick 敏感性"; visualWidth: 140
                active: root.scanMode === 1
                toolTip: "训练期选择参数，胜出参数只在最终留出段验证一次"
                onClicked: root.scanMode = 1
            }
            Text {
                Layout.fillWidth: true
                text: root.scanMode === 0
                    ? "派生 K 线 · 自定义策略 · 不归入 Tick 研究"
                    : "研究专用 · 训练与最终验证严格隔离 · 成功后自动归档"
                color: root.scanMode === 0 ? root.theme.textMuted : root.theme.cyan
                font.family: root.theme.fontFamily; font.pixelSize: 9
                horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                Accessible.name: text
            }
        }

        RowLayout {
            Layout.fillWidth: true; visible: root.scanMode === 0; spacing: 8
            Column { spacing: 3; Layout.preferredWidth: 145
                FieldLabel { text: "自定义策略 ID" }
                ScanField { id: customStrategyId; width: 145; placeholderText: "custom_xxx"; Accessible.name: "自定义策略 ID" }
            }
            Column { spacing: 3; Layout.preferredWidth: 55
                FieldLabel { text: "版本" }
                ScanField { id: customVersion; width: 55; text: "1"; Accessible.name: "自定义策略版本" }
            }
            Column { spacing: 3; Layout.preferredWidth: 90
                FieldLabel { text: "标的" }
                ScanField { id: customSymbol; width: 90; text: root.controller.selectedBacktestSymbol || "600519"; Accessible.name: "自定义网格标的" }
            }
            Column { spacing: 3; Layout.preferredWidth: 65
                FieldLabel { text: "周期秒" }
                ScanField { id: customPeriod; width: 65; text: "60"; Accessible.name: "自定义网格周期秒" }
            }
            Column { spacing: 3; Layout.preferredWidth: 65
                FieldLabel { text: "根数" }
                ScanField { id: customBars; width: 65; text: "300"; Accessible.name: "自定义网格根数" }
            }
        }

        RowLayout {
            Layout.fillWidth: true; visible: root.scanMode === 1; spacing: 8
            Column { spacing: 3; Layout.preferredWidth: 170
                FieldLabel { text: "原始 Tick 内置策略" }
                ComboBox {
                    id: rawStrategyBox; width: 170; height: 30
                    model: root.rawStrategies; textRole: "name"; valueRole: "strategyId"
                    Accessible.name: "原始 Tick 内置策略"
                    onActivated: root.resetRawAxes()
                }
            }
            Column { spacing: 3; Layout.preferredWidth: 86
                FieldLabel { text: "A 股代码" }
                ScanField { id: rawSymbol; width: 86; text: root.controller.selectedBacktestSymbol || "600519"; Accessible.name: "参数敏感性 A 股代码" }
            }
            Column { spacing: 3; Layout.preferredWidth: 104
                FieldLabel { text: "起始日期" }
                ScanField { id: rawFrom; width: 104; text: root.controller.selectedBacktestFirstTime ? Qt.formatDate(root.controller.selectedBacktestFirstTime, "yyyy-MM-dd") : ""; placeholderText: "yyyy-MM-dd"; Accessible.name: "训练样本起始日期" }
            }
            Column { spacing: 3; Layout.preferredWidth: 104
                FieldLabel { text: "结束日期" }
                ScanField { id: rawTo; width: 104; text: root.controller.selectedBacktestLastTime ? Qt.formatDate(root.controller.selectedBacktestLastTime, "yyyy-MM-dd") : ""; placeholderText: "yyyy-MM-dd"; Accessible.name: "最终验证结束日期" }
            }
            Column { spacing: 3; Layout.preferredWidth: 72
                FieldLabel { text: "每折验证日" }
                ScanField {
                    id: rawValidationDays; width: 72; text: "2"
                    validator: IntValidator { bottom: 2; top: 20 }
                    Accessible.name: "每折验证交易日数"
                }
            }
            Column { spacing: 3; Layout.preferredWidth: 58
                FieldLabel { text: "外层折数" }
                ScanField {
                    id: rawFoldCount; width: 58; text: "3"
                    validator: IntValidator { bottom: 1; top: 20 }
                    Accessible.name: "嵌套 walk-forward 外层折数"
                    Accessible.description: "1 折为单次最终留出，2 至 20 折为扩展窗嵌套验证"
                }
            }
            ComboBox {
                id: rawHistoryBox
                Layout.alignment: Qt.AlignBottom; Layout.fillWidth: true
                Layout.preferredHeight: 30
                model: root.controller.rawTickSensitivityHistory || []
                textRole: "resultId"; valueRole: "resultId"
                displayText: currentIndex >= 0 ? "历史 " + currentText : "暂无历史归档"
                Accessible.name: "原始 Tick 参数敏感性历史归档"
            }
            PpButton {
                Layout.alignment: Qt.AlignBottom
                theme: root.theme; text: "恢复"; visualWidth: 68
                available: rawHistoryBox.currentIndex >= 0
                toolTip: available ? "只从 SQLite 恢复不可变研究结果" : "暂无可恢复结果"
                onClicked: root.controller.loadRawTickParameterSensitivityResult(
                    String(rawHistoryBox.currentValue || ""))
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Column { spacing: 3; Layout.preferredWidth: 125
                FieldLabel { text: "轴 1 参数名" }
                ScanField { id: customAxis1Key; visible: root.scanMode === 0; width: 125; placeholderText: "如 fastPeriod"; Accessible.name: "自定义轴一参数名" }
                ScanField { id: rawAxis1Key; visible: root.scanMode === 1; width: 125; text: "lookbackTicks"; Accessible.name: "Tick 轴一参数名" }
            }
            Column { spacing: 3; Layout.fillWidth: true
                FieldLabel { text: "轴 1 候选值（逗号分隔）" }
                ScanField { id: customAxis1Values; visible: root.scanMode === 0; width: parent.width; placeholderText: "如 5,10,20"; Accessible.name: "自定义轴一候选值" }
                ScanField { id: rawAxis1Values; visible: root.scanMode === 1; width: parent.width; text: "10,20,50"; Accessible.name: "Tick 轴一候选值" }
            }
            Column { spacing: 3; Layout.preferredWidth: 140
                FieldLabel { text: "轴 2 参数名（可选）" }
                ScanField { id: customAxis2Key; visible: root.scanMode === 0; width: 140; placeholderText: "如 slowPeriod"; Accessible.name: "自定义轴二参数名" }
                ScanField { id: rawAxis2Key; visible: root.scanMode === 1; width: 140; text: "thresholdPercent"; Accessible.name: "Tick 轴二参数名" }
            }
            Column { spacing: 3; Layout.fillWidth: true
                FieldLabel { text: "轴 2 候选值（逗号分隔）" }
                ScanField { id: customAxis2Values; visible: root.scanMode === 0; width: parent.width; placeholderText: "如 20,40,60"; Accessible.name: "自定义轴二候选值" }
                ScanField { id: rawAxis2Values; visible: root.scanMode === 1; width: parent.width; text: "0.05,0.1,0.2"; Accessible.name: "Tick 轴二候选值" }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            PpButton {
                theme: root.theme; text: root.scanMode === 0 ? "开始 K线扫描" : "开始 Tick 研究"
                visualWidth: 120; primary: true
                available: root.scanMode === 0
                    ? !root.controller.parameterGridRunning
                    : !root.controller.rawTickSensitivityRunning
                toolTip: root.scanMode === 0
                    ? "读取本地 raw_tick_cache_v1 派生 K 线"
                    : "异步读取本地原始 Tick；每折只用训练段选参，再在严格更晚的验证段评价"
                onClicked: root.runScan()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 64
                available: root.scanMode === 0
                    ? root.controller.parameterGridRunning
                    : root.controller.rawTickSensitivityRunning
                onClicked: {
                    if (root.scanMode === 0) root.controller.cancelParameterGridBacktest()
                    else root.controller.cancelRawTickParameterSensitivity()
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.scanMode === 0
                    ? (root.controller.parameterGridRunning ? "K 线扫描中…"
                       : root.controller.parameterGridResults.length > 0
                         ? "共 " + root.controller.parameterGridResults.length + " 组结果" : "尚未扫描")
                    : (root.controller.rawTickSensitivityRunning
                       ? "Tick 研究中… 请求 " + root.controller.rawTickSensitivityRequestId
                       : root.rawResultSummary())
                color: (root.controller.parameterGridRunning
                        || root.controller.rawTickSensitivityRunning)
                       ? root.theme.yellow : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
                elide: Text.ElideRight; Accessible.name: text
            }
        }

        Text {
            Layout.fillWidth: true
            visible: (root.scanMode === 0 ? root.controller.parameterGridError
                                          : root.controller.rawTickSensitivityError).length > 0
            text: root.scanMode === 0 ? root.controller.parameterGridError
                                      : root.controller.rawTickSensitivityError
            color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap; Accessible.name: text
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.scanMode === 0
            model: root.controller.parameterGridResults
            clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                required property var parameters
                required property double totalReturnPercent
                required property double maxDrawdownPercent
                required property int totalTrades
                width: ListView.view.width; height: 34; radius: 5
                color: index % 2 ? root.theme.bgRaised : "transparent"
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; width: parent.width * 0.56; text: "第 " + (index + 1) + " 名 · " + root.formatParameters(parameters); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: (totalReturnPercent >= 0 ? "+" : "") + Number(totalReturnPercent).toFixed(2) + "% · 回撤 " + Number(maxDrawdownPercent).toFixed(2) + "% · " + totalTrades + " 笔"; color: totalReturnPercent >= 0 ? root.theme.green : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold }
            }
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.scanMode === 1 && !root.rawNestedResult
            model: root.hasRawResult ? root.rawResult.candidates : []
            clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int rank
                required property var parameters
                required property double trainingReturnPercent
                required property double trainingMaxDrawdownPercent
                required property int trainingTrades
                width: ListView.view.width; height: 34; radius: 5
                color: rank === 1 ? root.theme.bgSelected : (rank % 2 ? root.theme.bgRaised : "transparent")
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; width: parent.width * 0.56; text: "训练第 " + rank + " 名 · " + root.formatParameters(parameters); color: rank === 1 ? root.theme.cyan : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: (trainingReturnPercent >= 0 ? "+" : "") + Number(trainingReturnPercent).toFixed(2) + "% · 回撤 " + Number(trainingMaxDrawdownPercent).toFixed(2) + "% · " + trainingTrades + " 笔"; color: trainingReturnPercent >= 0 ? root.theme.green : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold }
            }
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.scanMode === 1 && root.rawNestedResult
            model: root.rawNestedResult ? root.rawResult.folds : []
            clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int foldNumber
                required property var trainingStartDate
                required property var trainingEndDate
                required property var validationStartDate
                required property var validationEndDate
                required property var selectedParameters
                required property double validationReturnPercent
                required property double validationMaxDrawdownPercent
                required property int validationTrades
                width: ListView.view.width; height: 42; radius: 5
                color: foldNumber % 2 ? root.theme.bgRaised : "transparent"
                Text {
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.64
                    text: "第 " + foldNumber + " 折 · 训练 "
                        + root.formatDate(trainingStartDate) + " 至 "
                        + root.formatDate(trainingEndDate) + " · 验证 "
                        + root.formatDate(validationStartDate) + " 至 "
                        + root.formatDate(validationEndDate) + " · "
                        + root.formatParameters(selectedParameters)
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                    elide: Text.ElideRight
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: (validationReturnPercent >= 0 ? "+" : "")
                        + Number(validationReturnPercent).toFixed(2)
                        + "% · 回撤 "
                        + Number(validationMaxDrawdownPercent).toFixed(2)
                        + "% · " + validationTrades + " 笔"
                    color: validationReturnPercent >= 0
                        ? root.theme.green : root.theme.red
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                    font.weight: Font.Bold
                }
            }
        }
    }

    Component.onCompleted: root.resetRawAxes()
}
