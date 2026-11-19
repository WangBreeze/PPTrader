// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    property string selectedResultId: ""
    property string inputError: ""
    property string deletingRunId: ""

    function parsedStrategies(overrides) {
        const entries = []
        for (const token of strategiesField.text.split(/[，,\s]+/)) {
            if (!token) continue
            const labelled = token.split("#")
            const parts = labelled[0].split("@")
            entries.push({"strategyId": parts[0],
                          "version": parts.length > 1 ? Number(parts[1]) : 0,
                          "profileLabel": labelled.length > 1 ? labelled[1] : "",
                          "parameters": overrides[token] || ({})})
        }
        return entries
    }

    function parsedSymbols() {
        return symbolsField.text.split(/[，,\s]+/).filter(value => value.length > 0)
    }

    function startMatrix() {
        let overrides = ({})
        try {
            if (parametersField.text.trim().length > 0)
                overrides = JSON.parse(parametersField.text)
            if (!overrides || Array.isArray(overrides) || typeof overrides !== "object")
                throw new Error("参数格式无效")
        } catch (error) {
            root.inputError = "参数微调格式无效，请填写策略 ID 与参数的对应表"
            root.controller.reportStrategyMatrixInputError()
            return
        }
        root.inputError = ""
        root.selectedResultId = ""
        root.controller.runStrategyMatrix(root.parsedStrategies(overrides),
            root.parsedSymbols(), frequencyBox.currentText,
            startField.text, endField.text)
    }

    function availableStrategyText() {
        const names = []
        for (const entry of root.controller.backtestStrategyCatalog || []) {
            if (entry.supportsCandles) names.push(entry.strategyId)
        }
        const custom = root.controller.customStrategyModel
        if (custom) {
            for (let row = 0; row < custom.count; ++row) {
                const entry = custom.get(row)
                if (entry.latest) names.push(entry.strategyId + "@" + entry.version)
            }
        }
        return names.join("、")
    }

    function preflightText() {
        const rows = root.controller.strategyMatrixPreflightResults || []
        const parts = []
        for (const row of rows)
            parts.push(row.symbol + "：" + row.message)
        return parts.join("  ·  ")
    }

    function selectedTokens(field) {
        return field.text.split(/[，,\s]+/).filter(value => value.length > 0)
    }

    function toggleSelection(field, token, kind) {
        const selected = selectedTokens(field)
        const index = selected.indexOf(token)
        const added = index < 0
        if (added) selected.push(token)
        else selected.splice(index, 1)
        field.text = selected.join(",")
        root.controller.logStrategyMatrixSelection(kind, token, added)
    }

    function addFiltered(field, tokens, kind) {
        const selected = selectedTokens(field)
        let added = 0
        for (const token of tokens) {
            if (selected.indexOf(token) >= 0) continue
            selected.push(token)
            ++added
        }
        field.text = selected.join(",")
        root.controller.logStrategyMatrixSelection(kind + "_batch",
            String(added), added > 0)
    }

    function currentOverrides() {
        const value = parametersField.text.trim()
        if (!value) return ({})
        try {
            const result = JSON.parse(value)
            if (result && !Array.isArray(result) && typeof result === "object")
                return result
        } catch (error) {}
        root.inputError = "参数微调格式无效，请先修正 JSON 内容"
        root.controller.reportStrategyMatrixInputError()
        return null
    }

    function parameterSpecs(token) {
        const parts = token.split("#")[0].split("@")
        for (const entry of root.controller.backtestStrategyCatalog || []) {
            if (entry.strategyId === parts[0]) return entry.parameters || []
        }
        const custom = root.controller.customStrategyModel
        if (custom) {
            for (let row = 0; row < custom.count; ++row) {
                const entry = custom.get(row)
                if (entry.strategyId === parts[0]
                    && (parts.length === 1 || Number(parts[1]) === entry.version))
                    return entry.parameters || []
            }
        }
        return []
    }

    function openParameterEditor(token) {
        const overrides = currentOverrides()
        if (overrides === null) return
        const specs = parameterSpecs(token)
        if (specs.length === 0) {
            root.inputError = "该策略没有可编辑参数，或策略版本不存在：" + token
            return
        }
        root.inputError = ""
        parameterDialog.configure(token, specs, overrides[token] || ({}))
    }

    function createParameterVariant(token) {
        const overrides = currentOverrides()
        if (overrides === null || !token) return
        const base = token.split("#")[0]
        const selected = selectedTokens(strategiesField)
        let number = 2
        while (selected.indexOf(base + "#方案" + number) >= 0) ++number
        const variant = base + "#方案" + number
        const next = Object.assign({}, overrides)
        next[variant] = Object.assign({}, overrides[token] || ({}))
        parametersField.text = JSON.stringify(next)
        selected.push(variant)
        strategiesField.text = selected.join(",")
        root.controller.logStrategyMatrixSelection("strategy", variant, true)
        root.openParameterEditor(variant)
    }

    function filteredResults() {
        const query = resultSearch.text.trim().toLowerCase()
        const status = resultStatus.currentIndex
        const results = root.controller.strategyMatrixResults || []
        return results.filter(row =>
            (status === 0 || (status === 1 && row.succeeded)
             || (status === 2 && !row.succeeded))
            && (!query || (row.strategyId + " " + row.profileLabel + " "
                           + row.symbol).toLowerCase().indexOf(query) >= 0))
    }

    function strategyOptions() {
        const query = strategySearch.text.trim().toLowerCase()
        const values = []
        for (const entry of root.controller.backtestStrategyCatalog || []) {
            if (entry.supportsCandles) values.push(entry.strategyId)
        }
        const custom = root.controller.customStrategyModel
        if (custom) {
            for (let row = 0; row < custom.count; ++row) {
                const entry = custom.get(row)
                values.push(entry.strategyId + "@" + entry.version)
            }
        }
        return values.filter(value => value.toLowerCase().indexOf(query) >= 0)
                     .slice(0, 100)
    }

    function stockOptions() {
        const query = stockSearch.text.trim().toLowerCase()
        const seen = []
        const values = []
        for (const entry of root.controller.localBacktestDataCatalog || []) {
            if (entry.frequency !== frequencyBox.currentText
                || !entry.backtestEligible || seen.indexOf(entry.symbol) >= 0)
                continue
            seen.push(entry.symbol)
            const label = entry.symbol + " " + entry.name
            if (label.toLowerCase().indexOf(query) >= 0) values.push(label)
            if (values.length >= 100) break
        }
        return values
    }

    Component.onCompleted: {
        const catalog = root.controller.localBacktestDataCatalog || []
        const seen = []
        let firstDate = ""
        let lastDate = ""
        for (const entry of catalog) {
            if (entry.frequency !== "day" || seen.indexOf(entry.symbol) >= 0) continue
            seen.push(entry.symbol)
            if (seen.length === 1) {
                firstDate = Qt.formatDate(entry.firstTime, "yyyy-MM-dd")
                lastDate = Qt.formatDate(entry.lastTime, "yyyy-MM-dd")
            }
            if (seen.length >= 3) break
        }
        symbolsField.text = seen.join(",")
        startField.text = firstDate
        endField.text = lastDate
        const strategies = root.controller.backtestStrategyCatalog || []
        const defaults = []
        for (const entry of strategies) {
            if (entry.supportsCandles) defaults.push(entry.strategyId)
            if (defaults.length >= 2) break
        }
        strategiesField.text = defaults.join(",")
    }

    StrategyMatrixParameterDialog {
        id: parameterDialog
        theme: root.theme
        onSaved: (token, values) => {
            const overrides = root.currentOverrides()
            if (overrides === null) return
            const next = Object.assign({}, overrides)
            next[token] = values
            parametersField.text = JSON.stringify(next)
            root.inputError = ""
            root.controller.logStrategyMatrixSelection("parameter", token, true)
        }
    }

    Popup {
        id: deleteRunConfirm
        parent: Overlay.overlay
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        width: Math.min(parent ? parent.width - 32 : 430, 430)
        height: 160
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        background: Rectangle {
            radius: 9; color: root.theme.bgPanel; border.color: root.theme.border
        }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 10
            Text {
                Layout.fillWidth: true
                text: "删除整个矩阵批次？"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 14
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true; Layout.fillHeight: true
                text: "该批次的任务、冻结快照及全部归档结果将一并删除。"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                PpButton {
                    theme: root.theme; text: "取消"; visualWidth: 72
                    onClicked: deleteRunConfirm.close()
                }
                PpButton {
                    theme: root.theme; text: "确认删除"; visualWidth: 92
                    onClicked: {
                        if (root.controller.deleteStrategyMatrixRun(root.deletingRunId))
                            root.selectedResultId = ""
                        deleteRunConfirm.close()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 7
        Text {
            text: "策略 × 股票回测矩阵"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 15
            font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: "填写多个策略 ID 与股票代码，用逗号分隔；自定义策略可写成 custom_x@版本。同一策略的不同参数可用 #名称 区分。"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 10
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: "可选策略：" + root.availableStrategyText()
            color: root.theme.cyan
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: strategySearch
                Layout.fillWidth: true
                placeholderText: "搜索策略或版本，点击加入"
                Accessible.name: "搜索可回测策略"
            }
            TextField {
                id: stockSearch
                Layout.fillWidth: true
                placeholderText: "搜索股票代码或名称，点击加入"
                Accessible.name: "搜索可回测股票"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: 83
                model: root.strategyOptions()
                clip: true; spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: RowLayout {
                    required property string modelData
                    width: ListView.view.width; height: 26
                    Text {
                        Layout.fillWidth: true
                        text: modelData
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    PpButton {
                        theme: root.theme
                        text: root.selectedTokens(strategiesField).indexOf(modelData) >= 0
                            ? "移除" : "加入"
                        visualWidth: 48; visualHeight: 22
                        onClicked: root.toggleSelection(strategiesField, modelData, "strategy")
                    }
                }
            }
            ListView {
                Layout.fillWidth: true; Layout.preferredHeight: 83
                model: root.stockOptions()
                clip: true; spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: RowLayout {
                    required property string modelData
                    width: ListView.view.width; height: 26
                    Text {
                        Layout.fillWidth: true
                        text: modelData
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    PpButton {
                        theme: root.theme
                        text: root.selectedTokens(symbolsField)
                              .indexOf(modelData.split(" ")[0]) >= 0
                            ? "移除" : "加入"
                        visualWidth: 48; visualHeight: 22
                        onClicked: root.toggleSelection(
                            symbolsField, modelData.split(" ")[0], "symbol")
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            PpButton {
                theme: root.theme; text: "加入匹配策略（前 100）"; visualWidth: 154
                onClicked: root.addFiltered(strategiesField,
                    root.strategyOptions(), "strategy")
            }
            PpButton {
                theme: root.theme; text: "加入匹配股票（前 100）"; visualWidth: 154
                onClicked: root.addFiltered(symbolsField,
                    root.stockOptions().map(value => value.split(" ")[0]), "symbol")
            }
            Text {
                Layout.fillWidth: true
                text: "已选 " + root.selectedTokens(strategiesField).length
                    + " 策略 × " + root.selectedTokens(symbolsField).length
                    + " 股票 = " + (root.selectedTokens(strategiesField).length
                                  * root.selectedTokens(symbolsField).length)
                    + " 任务（最多 200）"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            TextField {
                id: strategiesField
                Layout.fillWidth: true
                placeholderText: "策略 ID，例如 sma_crossover,custom_x@2"
                Accessible.name: "回测矩阵策略列表"
            }
            TextField {
                id: symbolsField
                Layout.fillWidth: true
                placeholderText: "股票代码，例如 000001,600519"
                Accessible.name: "回测矩阵股票列表"
            }
        }
        TextField {
            id: parametersField
            Layout.fillWidth: true
            placeholderText: "参数微调（可选）：例如 {\"sma_crossover#快线5\":{\"fastPeriod\":5,\"slowPeriod\":20}}"
            Accessible.name: "矩阵策略参数微调"
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ComboBox {
                id: editStrategyBox
                Layout.fillWidth: true
                model: root.selectedTokens(strategiesField)
                Accessible.name: "选择要微调的策略方案"
            }
            PpButton {
                theme: root.theme; text: "编辑参数"; visualWidth: 84
                available: editStrategyBox.currentIndex >= 0
                onClicked: root.openParameterEditor(editStrategyBox.currentText)
            }
            PpButton {
                theme: root.theme; text: "复制方案"; visualWidth: 84
                available: editStrategyBox.currentIndex >= 0
                onClicked: root.createParameterVariant(editStrategyBox.currentText)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ComboBox { id: frequencyBox; model: ["day", "60m", "30m", "15m", "5m", "1m"] }
            TextField { id: startField; Layout.preferredWidth: 120; placeholderText: "开始 YYYY-MM-DD"; Accessible.name: "矩阵开始日期" }
            TextField { id: endField; Layout.preferredWidth: 120; placeholderText: "结束 YYYY-MM-DD"; Accessible.name: "矩阵结束日期" }
            PpButton {
                theme: root.theme; text: "预检数据"; visualWidth: 86
                onClicked: root.controller.preflightStrategyMatrix(
                    root.parsedSymbols(), frequencyBox.currentText,
                    startField.text, endField.text)
            }
            PpButton {
                theme: root.theme; text: "运行矩阵"; visualWidth: 108; primary: true
                available: !root.controller.strategyMatrixRunning
                onClicked: root.startMatrix()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 70
                available: root.controller.strategyMatrixRunning
                onClicked: root.controller.cancelStrategyMatrix()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.strategyMatrixStatus
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                elide: Text.ElideRight
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.strategyMatrixPreflightResults.length > 0
            text: root.preflightText()
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 10
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            ComboBox {
                id: historyBox
                Layout.fillWidth: true
                model: root.controller.strategyMatrixRunHistory
                textRole: "label"
                valueRole: "runId"
                Accessible.name: "历史矩阵批次"
            }
            PpButton {
                theme: root.theme; text: "查看历史"; visualWidth: 85
                available: !root.controller.strategyMatrixRunning
                           && historyBox.currentIndex >= 0
                onClicked: {
                    root.selectedResultId = ""
                    root.controller.closeBacktestReplay()
                    root.controller.loadStrategyMatrixRun(historyBox.currentValue)
                }
            }
            PpButton {
                theme: root.theme; text: "恢复未成功"; visualWidth: 96
                available: !root.controller.strategyMatrixRunning
                           && historyBox.currentIndex >= 0
                onClicked: {
                    root.selectedResultId = ""
                    root.controller.closeBacktestReplay()
                    root.controller.resumeStrategyMatrixRun(historyBox.currentValue)
                }
            }
            PpButton {
                theme: root.theme; text: "删除批次"; visualWidth: 84
                available: !root.controller.strategyMatrixRunning
                           && historyBox.currentIndex >= 0
                onClicked: {
                    root.deletingRunId = historyBox.currentValue
                    deleteRunConfirm.open()
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.inputError.length > 0
                     || root.controller.strategyMatrixError.length > 0
            text: root.inputError.length > 0 ? root.inputError
                : root.controller.strategyMatrixError
            color: root.theme.red
            font.family: root.theme.fontFamily; font.pixelSize: 10
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: resultSearch
                Layout.fillWidth: true
                placeholderText: "筛选结果：策略、方案或股票"
                Accessible.name: "筛选矩阵结果"
            }
            ComboBox {
                id: resultStatus
                model: ["全部结果", "仅成功", "仅失败"]
                Accessible.name: "矩阵结果状态筛选"
            }
            Text {
                text: root.filteredResults().length + "/"
                    + root.controller.strategyMatrixResults.length
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
            }
        }
        ListView {
            Layout.fillWidth: true; Layout.preferredHeight: 150
            model: root.filteredResults()
            clip: true; spacing: 3
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 30
                radius: 4; color: root.theme.bgInput
                RowLayout {
                    anchors.fill: parent; anchors.margins: 4; spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: modelData.strategyId
                              + (modelData.profileLabel ? "#" + modelData.profileLabel : "")
                              + (modelData.strategyVersion > 0
                              ? " v" + modelData.strategyVersion : "") + " · "
                              + modelData.symbol + " · "
                              + (modelData.succeeded
                                 ? "收益 " + Number(modelData.totalReturnPercent).toFixed(2)
                                   + "% · 回撤 " + Number(modelData.maxDrawdownPercent).toFixed(2)
                                   + "% · 成交 " + modelData.totalTrades
                                   + (modelData.resultId.length > 0
                                      ? " · 已归档" : " · 未盈利不归档")
                                 : modelData.error)
                        color: modelData.succeeded ? root.theme.textPrimary : root.theme.red
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                    PpButton {
                        theme: root.theme; text: "查看 K 线"; visualWidth: 90; visualHeight: 23
                        available: modelData.succeeded && modelData.resultId.length > 0
                        onClicked: {
                            root.selectedResultId = modelData.resultId
                            root.controller.loadBacktestArchive(modelData.resultId)
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.selectedResultId.length > 0
            PpButton {
                theme: root.theme
                text: root.controller.backtestReplayRunning ? "暂停" : "播放"
                visualWidth: 72
                available: root.controller.selectedBacktestArchiveId === root.selectedResultId
                           && root.controller.backtestReplayTotal > 0
                onClicked: {
                    if (root.controller.backtestReplayRunning) {
                        root.controller.logBacktestChartAction("pause_replay")
                        root.controller.pauseBacktestReplay()
                    } else {
                        root.controller.logBacktestChartAction("play_replay")
                        root.controller.startBacktestReplay(Number(speedBox.currentText))
                        matrixBacktestChart.followLatest()
                    }
                }
            }
            ComboBox {
                id: speedBox
                model: ["1", "5", "10", "20"]
                currentIndex: 2
                Accessible.name: "K 线回放速度，根每秒"
            }
            PpButton {
                theme: root.theme; text: "逐根"; visualWidth: 65
                available: root.controller.selectedBacktestArchiveId === root.selectedResultId
                           && root.controller.backtestReplayTotal > 0
                onClicked: {
                    root.controller.logBacktestChartAction("step_replay")
                    if (!root.controller.backtestReplayActive)
                        root.controller.seekBacktestReplay(0)
                    root.controller.pauseBacktestReplay()
                    root.controller.stepBacktestReplay()
                    matrixBacktestChart.followLatest()
                }
            }
            PpButton {
                theme: root.theme; text: "完整图"; visualWidth: 72
                onClicked: {
                    root.controller.logBacktestChartAction("show_full_chart")
                    root.controller.closeBacktestReplay()
                }
            }
            PpButton {
                theme: root.theme
                text: matrixBacktestChart.replayFollowLatest ? "跟随中" : "跟随最新"
                visualWidth: 88
                available: root.controller.backtestReplayActive
                onClicked: {
                    matrixBacktestChart.followLatest()
                    root.controller.logBacktestChartAction("follow_latest")
                }
            }
            Slider {
                Layout.fillWidth: true
                from: 0; to: Math.max(1, root.controller.backtestReplayTotal)
                value: root.controller.backtestReplayIndex
                onPressedChanged: if (!pressed) {
                    root.controller.logBacktestChartAction("seek_replay")
                    root.controller.seekBacktestReplay(Math.round(value))
                    matrixBacktestChart.followLatest()
                }
                Accessible.name: "K 线历史回放定位"
            }
            Text {
                text: root.controller.backtestReplayIndex + "/"
                      + root.controller.backtestReplayTotal
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }
        BacktestCandleChart {
            id: matrixBacktestChart
            Layout.fillWidth: true; Layout.fillHeight: true
            theme: root.theme
            replayMode: root.controller.selectedBacktestArchiveId === root.selectedResultId
                        && root.controller.backtestReplayActive
            model: root.controller.selectedBacktestArchiveId === root.selectedResultId
                ? (root.controller.backtestReplayActive
                   ? root.controller.backtestReplayCandleModel
                   : root.controller.candleModel) : null
            emptyText: root.controller.backtestArchiveLoading
                ? "正在读取回测 K 线…" : "选择已完成的任务查看 K 线与实际成交"
        }
    }
}
