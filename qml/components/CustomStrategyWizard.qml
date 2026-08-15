// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings

Item {
    id: root
    required property var theme
    required property var controller

    signal requestOpenJson(string json)
    signal requestClose()

    property string draftName: "多指标趋势策略"
    property string statusText: "草稿已保存"
    property string previewError: ""
    property var previewData: ({})
    property int activeConditionSide: 0
    property int activeStep: 3
    property string positionType: "fixedPercent"
    property string positionAmount: "50000"
    property string positionPercent: "100"
    property string maxPositionPercent: "30"
    property string stopLossPercent: "0"
    property string takeProfitPercent: "0"
    property string maximumHoldingBars: "0"
    property string signalPriceMode: "triggerClose"

    readonly property var catalog: [
        { name: "SMA", hasPeriod: true },
        { name: "EMA", hasPeriod: true },
        { name: "RSI", hasPeriod: true },
        { name: "MACD", hasPeriod: false },
        { name: "BOLLINGER", hasPeriod: false },
        { name: "ATR", hasPeriod: true },
        { name: "VOLUME_SMA", hasPeriod: true },
        { name: "ROC", hasPeriod: true },
        { name: "VWAP", hasPeriod: false },
        { name: "PP_FLOW", hasPeriod: false }
    ]
    readonly property var indicatorColors: ["#21B6E9", "#2AD879", "#F5C451",
        "#9D8CFF", "#4E8CFF", "#FF5B62", "#7E8DA1", "#21B6E9", "#2AD879", "#F5C451"]

    ListModel { id: seriesModel }
    ListModel { id: entryConditionsModel }
    ListModel { id: exitConditionsModel }

    Settings {
        id: draftStore
        property string wizardDraft: ""
    }

    function uniqueAlias(base) {
        let candidate = base.toLowerCase()
        let used = {}
        for (let i = 0; i < seriesModel.count; ++i)
            used[seriesModel.get(i).alias] = true
        if (!used[candidate]) return candidate
        let suffix = 2
        while (used[candidate + suffix]) ++suffix
        return candidate + suffix
    }

    function addIndicator(name) {
        const meta = root.catalog.find(item => item.name === name)
        if (!meta) return
        const color = root.indicatorColors[root.catalog.indexOf(meta)]
        let item = {
            name: name,
            alias: root.uniqueAlias(name),
            period: 14,
            fastPeriod: 12,
            slowPeriod: 26,
            signalPeriod: 9,
            output: name === "MACD" ? "histogram" : (name === "BOLLINGER" ? "middle" : ""),
            standardDeviations: 2.0,
            seriesVisible: true,
            seriesColor: color
        }
        seriesModel.append(item)
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function removeIndicator(index) {
        if (index < 0 || index >= seriesModel.count) return
        const alias = seriesModel.get(index).alias
        seriesModel.remove(index, 1)
        for (let i = entryConditionsModel.count - 1; i >= 0; --i) {
            const row = entryConditionsModel.get(i)
            if (row.leftOperand === alias || row.rightOperand === alias) entryConditionsModel.remove(i, 1)
        }
        for (let i = exitConditionsModel.count - 1; i >= 0; --i) {
            const row = exitConditionsModel.get(i)
            if (row.leftOperand === alias || row.rightOperand === alias) exitConditionsModel.remove(i, 1)
        }
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function addCondition() {
        const model = root.activeConditionSide === 0 ? entryConditionsModel : exitConditionsModel
        const alias = seriesModel.count > 0 ? seriesModel.get(0).alias : "close"
        model.append({ leftOperand: "close", op: "gt", rightOperand: alias, rightConstant: 0 })
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function removeCondition(index) {
        const model = root.activeConditionSide === 0 ? entryConditionsModel : exitConditionsModel
        if (index < 0 || index >= model.count) return
        model.remove(index, 1)
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function operandJson(kind, value, constant) {
        if (kind === "value") return { value: Number(constant) }
        if (kind === "field") return { field: value }
        return { series: value }
    }

    function conditionList(model) {
        let conditions = []
        for (let i = 0; i < model.count; ++i) {
            const row = model.get(i)
            conditions.push({
                op: row.op,
                left: root.operandJson(row.leftKind, row.leftOperand, 0),
                right: root.operandJson(row.rightKind, row.rightOperand, row.rightConstant)
            })
        }
        return conditions
    }

    function buildJson() {
        const now = new Date()
        const stamp = now.toISOString()
        let series = []
        for (let i = 0; i < seriesModel.count; ++i) {
            const row = seriesModel.get(i)
            let item = { alias: row.alias, indicator: row.name }
            if (row.period > 0 && (row.name !== "VWAP" && row.name !== "PP_FLOW"))
                item.period = row.period
            if (row.name === "MACD") {
                item.fastPeriod = row.fastPeriod
                item.slowPeriod = row.slowPeriod
                item.signalPeriod = row.signalPeriod
                item.output = row.output
            }
            if (row.name === "BOLLINGER") {
                item.period = row.period
                item.standardDeviations = row.standardDeviations
                item.output = row.output
            }
            series.push(item)
        }
        const entry = root.conditionList(entryConditionsModel)
        const exit = root.conditionList(exitConditionsModel)
        let position = { type: "fixedPercent", percent: 1.0 }
        if (root.positionType === "fixedAmount") {
            const amount = Number(root.positionAmount)
            position = { type: "fixedAmount", amount: amount > 0 ? amount : 50000 }
        } else {
            const percent = Number(root.positionPercent)
            position = { type: "fixedPercent", percent: percent > 0 ? percent / 100 : 1.0 }
        }
        const risk = { tPlusOne: true }
        const maxPosition = Number(root.maxPositionPercent)
        if (maxPosition > 0 && maxPosition <= 100) risk.maxPositionPercent = maxPosition
        const stopLoss = Number(root.stopLossPercent)
        if (stopLoss > 0) risk.stopLossPercent = stopLoss
        const takeProfit = Number(root.takeProfitPercent)
        if (takeProfit > 0) risk.takeProfitPercent = takeProfit
        const holdingBars = Number(root.maximumHoldingBars)
        if (holdingBars > 0) risk.maximumHoldingBars = holdingBars
        const signal = { priceMode: root.signalPriceMode === "nextOpen" ? "nextOpen" : "triggerClose" }
        return JSON.stringify({
            schemaVersion: 2,
            strategyId: "custom_wizard_draft",
            version: 1,
            name: root.draftName,
            description: "由策略创建向导第 3 步生成，仅用于本地回测",
            author: "本地用户",
            market: "A股",
            executionScope: "backtest_only",
            createdAt: stamp,
            updatedAt: stamp,
            parameters: [],
            series: series,
            position: position,
            risk: risk,
            signal: signal,
            entry: entry.length === 1 ? entry[0] : { op: "and", conditions: entry },
            exit: exit.length === 1 ? exit[0] : { op: "and", conditions: exit }
        })
    }

    function saveDraft() {
        let entry = []
        for (let i = 0; i < entryConditionsModel.count; ++i) entry.push(entryConditionsModel.get(i))
        let exit = []
        for (let i = 0; i < exitConditionsModel.count; ++i) exit.push(exitConditionsModel.get(i))
        let series = []
        for (let i = 0; i < seriesModel.count; ++i) series.push(seriesModel.get(i))
        draftStore.wizardDraft = JSON.stringify({
            name: root.draftName,
            series: series,
            entry: entry,
            exit: exit
        })
        root.statusText = "草稿已保存"
    }

    function restoreDraft() {
        if (!draftStore.wizardDraft) return
        let parsed
        try { parsed = JSON.parse(draftStore.wizardDraft) } catch (e) { return }
        if (!parsed || !Array.isArray(parsed.series)) return
        seriesModel.clear()
        for (const item of parsed.series) {
            if (item.visible !== undefined && item.seriesVisible === undefined)
                item.seriesVisible = item.visible
            seriesModel.append(item)
        }
        entryConditionsModel.clear()
        for (const item of parsed.entry || []) {
            if (item.left !== undefined && item.leftOperand === undefined)
                item.leftOperand = item.left
            if (item.right !== undefined && item.rightOperand === undefined)
                item.rightOperand = item.right
            entryConditionsModel.append(item)
        }
        exitConditionsModel.clear()
        for (const item of parsed.exit || []) {
            if (item.left !== undefined && item.leftOperand === undefined)
                item.leftOperand = item.left
            if (item.right !== undefined && item.rightOperand === undefined)
                item.rightOperand = item.right
            exitConditionsModel.append(item)
        }
        if (parsed.name) root.draftName = parsed.name
        root.previewDirty = true
    }

    function refreshPreview() {
        const json = root.buildJson()
        if (seriesModel.count === 0) {
            root.previewData = {}
            root.previewError = "请先添加指标"
            return
        }
        const symbol = root.controller.selectedBacktestSymbol.length > 0
            ? root.controller.selectedBacktestSymbol : "600519"
        const period = root.controller.selectedBacktestPeriodSeconds > 0
            ? root.controller.selectedBacktestPeriodSeconds : 900
        root.previewData = root.controller.customStrategyPreview(
            json, symbol, period, 300) || {}
        root.previewError = root.previewData.error || ""
        root.previewDirty = false
    }

    function switchToJson() {
        root.saveDraft()
        root.requestOpenJson(root.buildJson())
    }

    function validateLocal() {
        const json = root.buildJson()
        const result = root.controller.validateCustomStrategyJson(json)
        root.statusText = result.valid ? "JSON 校验通过" : "JSON 校验失败：" + result.error
        return result.valid
    }

    function seriesNames() {
        let names = []
        for (let i = 0; i < seriesModel.count; ++i) names.push(seriesModel.get(i).alias)
        return names
    }

    function conditionModel() {
        return root.activeConditionSide === 0 ? entryConditionsModel : exitConditionsModel
    }

    Component.onCompleted: {
        root.restoreDraft()
        if (seriesModel.count === 0) {
            root.addIndicator("SMA")
            root.addIndicator("ROC")
            root.addIndicator("PP_FLOW")
            entryConditionsModel.append({ leftOperand: "fast", leftKind: "series", op: "crossesAbove",
                                          rightOperand: "slow", rightKind: "series", rightConstant: 0 })
            entryConditionsModel.append({ leftOperand: "momentum", leftKind: "series", op: "gt",
                                          rightOperand: "", rightKind: "value", rightConstant: 0.8 })
            exitConditionsModel.append({ leftOperand: "momentum", leftKind: "series", op: "lt",
                                         rightOperand: "", rightKind: "value", rightConstant: 0 })
        }
        root.statusText = "草稿已保存"
    }

    Rectangle {
        anchors.fill: parent
        color: theme.bgCanvas

        // 顶部栏
        Rectangle {
            id: topBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 64
            color: theme.bgBar
            border.color: theme.border

            Rectangle {
                x: 16; y: 15; width: 34; height: 34; radius: 8
                color: theme.purple
                Text {
                    anchors.centerIn: parent
                    text: "PP"
                    color: theme.bgCanvas
                    font.family: theme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
            }
            Text {
                x: 62; y: 8; text: "PPTrader"; color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 18; font.weight: Font.Bold
            }
            PpButton {
                x: 220; y: 14; visualWidth: 92
                theme: root.theme; text: "← 返回中心"
                toolTip: "返回策略研究工作台"
                onClicked: root.requestClose()
            }
            Rectangle {
                x: 326; y: 16; width: 1; height: 32; color: theme.border
            }
            Text {
                x: 346; y: 8; text: "创建自定义策略"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 16; font.weight: Font.Bold
            }
            Text {
                x: 346; y: 34; text: "多指标趋势策略 · 草稿自动保存"
                color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10
            }
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 16
                y: 18; width: 96; height: 28; radius: 6
                color: theme.bgRaised
                Text {
                    anchors.centerIn: parent
                    text: "保存草稿"
                    color: theme.purple
                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.saveDraft()
                }
            }
            PpButton {
                anchors.right: parent.right; anchors.rightMargin: 124
                y: 18; visualWidth: 88
                theme: root.theme; text: "流程说明"
                toolTip: "八步向导：策略信息 → 标的与周期 → 指标组合 → 信号条件 → 仓位与风控 → 执行与审批 → 回测验证 → 发布与运行"
            }
            PpButton {
                anchors.right: parent.right; anchors.rightMargin: 212
                y: 18; visualWidth: 76
                theme: root.theme; text: "草稿"
                active: true
                toolTip: "草稿保存在本机 Codex 会话设置中，不会写入策略库"
            }
        }

        // 左侧向导栏
        Rectangle {
            id: sidebar
            anchors.left: parent.left
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            width: 300
            color: theme.bgBar
            border.color: theme.border

            Text {
                x: 24; y: 22; text: "策略创建向导"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 17; font.weight: Font.Bold
            }
            Text {
                x: 24; y: 54; text: "步骤 " + root.activeStep + " / 8 · 可随时返回修改"
                color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10
            }
            Rectangle {
                x: 24; y: 86; width: 248; height: 5; radius: 2
                color: theme.bgRaised
                Rectangle {
                    width: Math.min(248, 248 * root.activeStep / 8); height: 5; radius: 2
                    color: theme.purple
                }
            }

            Column {
                x: 14; y: 114; width: 272; spacing: 0

                Repeater {
                    model: [
                        { no: "✓", title: "策略信息", desc: "名称、类型、说明", done: true },
                        { no: "✓", title: "标的与周期", desc: "多市场、多标的、多周期", done: true },
                        { no: "03", title: "指标组合", desc: "选择并组合多个指标", done: false, active: true },
                        { no: "04", title: "信号条件", desc: "入场 / 出场条件组", done: false },
                        { no: "05", title: "仓位与风控", desc: "仓位、止损与限制", done: false },
                        { no: "06", title: "执行与审批", desc: "人工审批 / 自动提交", done: false },
                        { no: "07", title: "回测验证", desc: "历史样本与流式回放", done: false },
                        { no: "08", title: "发布与运行", desc: "版本、线程与通知", done: false }
                    ]

                    delegate: Column {
                        width: 272
                        Rectangle {
                            width: 272; height: 66; radius: 8
                            color: modelData.active ? "#211E38"
                                  : (modelData.done ? theme.bgPanel : "transparent")
                            border.color: modelData.active ? theme.purple : "transparent"
                            border.width: 1
                            Text {
                                x: 14; y: 10; width: 34; height: 22
                                text: modelData.no
                                color: modelData.done ? theme.green
                                      : (modelData.active ? theme.purple : theme.textMuted)
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                x: 58; y: 7; width: 176
                                text: modelData.title
                                color: modelData.done || modelData.active ? theme.textPrimary : theme.textMuted
                                font.family: theme.fontFamily; font.pixelSize: 13
                                font.weight: modelData.active ? Font.Bold : Font.Medium
                            }
                            Text {
                                x: 58; y: 31; width: 188
                                text: modelData.desc
                                color: theme.textMuted
                                font.family: theme.fontFamily; font.pixelSize: 10
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData.no === "03") {
                                        root.activeStep = 3
                                    } else if (modelData.no === "04") {
                                        root.activeStep = 4
                                    } else if (modelData.no === "05") {
                                        root.activeStep = 5
                                    } else if (modelData.no === "06") {
                                        root.activeStep = 6
                                    } else if (modelData.no === "07") {
                                        root.activeStep = 7
                                    } else if (modelData.no === "08") {
                                        root.activeStep = 8
                                    } else if (!modelData.active) {
                                        root.statusText = "步骤 " + modelData.no + " 尚未实现，当前可完成第 3 步后切换 JSON 模式"
                                    }
                                }
                            }
                        }
                        Rectangle {
                            x: 30; width: 1; height: modelData.done && !modelData.active ? 12 : 0
                            color: modelData.done ? theme.green : theme.border
                        }
                    }
                }
            }

            Rectangle {
                x: 24; y: 740; width: 248; height: 32; radius: 6
                color: theme.bgRaised
                Text {
                    anchors.centerIn: parent
                    text: "＋ 增加自定义步骤"
                    color: theme.textMuted
                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.statusText = "自定义步骤将在后续阶段开放"
                }
            }
            Rectangle {
                x: 24; y: 786; width: 248; height: 60; radius: 8
                color: theme.bgPanel
                Text {
                    x: 14; y: 8; text: "支持多指标与多标的"
                    color: theme.textPrimary
                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                }
                Text {
                    x: 14; y: 32; text: "每个策略实例独立运行在线程池"
                    color: theme.textMuted
                    font.family: theme.fontFamily; font.pixelSize: 9
                }
            }
        }

        Rectangle {
            anchors.left: sidebar.right
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            width: 1
            color: theme.border
        }

        // 主内容区（第 3 步）
        Rectangle {
            id: mainArea
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            color: theme.bgCanvas

            Flickable {
                id: wizardScroll
                visible: root.activeStep === 3
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: Math.max(height, 720)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: wizardScroll.contentHeight > wizardScroll.height
                             ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Column {
                    width: parent.width
                    spacing: 12

                    Text {
                        width: parent.width
                        text: "自定义策略  /  " + root.draftName + "  /  指标组合"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                    }
                    Row {
                        width: parent.width
                        spacing: 12
                        Text {
                            width: parent.parent.width - 200
                            text: "03  组合指标与数据序列"
                            color: theme.textPrimary
                            font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            width: 178; height: 28; radius: 6
                            color: "#211E38"
                            border.color: theme.purple
                            Text {
                                anchors.centerIn: parent
                                text: {
                                    const symbol = root.controller.selectedBacktestSymbol
                                    const freq = root.controller.selectedBacktestFrequency
                                    return (symbol.length > 0 ? symbol : "600519")
                                        + " · " + (freq.length > 0 ? freq : "15m")
                                }
                                color: theme.purple
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "从内置指标和自定义指标中选择任意数量，统一设置别名、周期和输出序列，供后续条件定义使用。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    // 已选指标
                    Rectangle {
                        width: parent.width
                        height: 196
                        radius: 9
                        color: theme.bgPanel
                        border.color: theme.border

                        Text {
                            x: 18; y: 12
                            text: "已选指标  " + seriesModel.count
                            color: theme.textPrimary
                            font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
                        }
                        Text {
                            x: 190; y: 16
                            text: "拖动排序 · 后续可继续添加"
                            color: theme.textMuted
                            font.family: theme.fontFamily; font.pixelSize: 10
                        }
                        PpButton {
                            x: 470; y: 10; visualWidth: 98
                            theme: root.theme; text: "＋ 添加"
                            active: true
                            toolTip: "从指标白名单添加"
                            onClicked: addMenu.open()
                            Popup {
                                id: addMenu
                                x: 0; y: 34
                                width: 150
                                padding: 4
                                background: Rectangle {
                                    color: theme.bgRaised
                                    border.color: theme.border
                                    radius: 6
                                }
                                Column {
                                    width: parent.width
                                    Repeater {
                                        model: root.catalog
                                        delegate: Rectangle {
                                            width: 142; height: 26; radius: 4
                                            color: hovered ? theme.bgSelected : "transparent"
                                            Text {
                                                x: 10; anchors.verticalCenter: parent.verticalCenter
                                                text: modelData.name
                                                color: theme.textPrimary
                                                font.family: theme.fontFamily; font.pixelSize: 10
                                            }
                                            MouseArea {
                                                id: hoverArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: {
                                                    root.addIndicator(modelData.name)
                                                    addMenu.close()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Row {
                            x: 18; y: 52; width: parent.width - 36; spacing: 8
                            Repeater {
                                model: seriesModel
                                delegate: Rectangle {
                                    id: card
                                    required property int index
                                    required property string name
                                    required property string alias
                                    required property int period
                                    required property bool seriesVisible
                                    required property string seriesColor
                                    width: 164; height: 126; radius: 8
                                    color: theme.bgRaised
                                    border.color: theme.border

                                    Rectangle {
                                        x: 12; y: 14; width: 10; height: 10; radius: 5
                                        color: card.seriesColor
                                    }
                                    Text {
                                        x: 28; y: 8
                                        text: card.name
                                        color: theme.textPrimary
                                        font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold
                                    }
                                    Text {
                                        x: 12; y: 34
                                        text: "别名"
                                        color: theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                    }
                                    TextField {
                                        x: 42; y: 30; width: 78; height: 22
                                        text: card.alias
                                        color: theme.textPrimary
                                        font.family: theme.fontFamily; font.pixelSize: 10
                                        background: Rectangle {
                                            radius: 4; color: theme.bgInput
                                            border.color: theme.border
                                        }
                                        onEditingFinished: {
                                            const value = text.trim()
                                            if (value.length > 0) {
                                                seriesModel.setProperty(card.index, "alias", value)
                                                root.previewDirty = true
                                            } else {
                                                text = seriesModel.get(card.index).alias
                                            }
                                        }
                                    }
                                    Text {
                                        x: 12; y: 62
                                        text: card.name === "VWAP" ? "Session"
                                            : (card.name === "PP_FLOW" ? "自定义"
                                               : card.period + (card.name === "MACD"
                                                   ? " / " + seriesModel.get(card.index).slowPeriod
                                                   : ""))
                                        color: card.seriesColor
                                        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                                    }
                                    Text {
                                        x: 12; y: 84; width: 100
                                        text: root.controller.selectedBacktestFrequency.length > 0
                                            ? " · " + root.controller.selectedBacktestFrequency : " · 15m"
                                        color: theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                    Row {
                                        x: 12; y: 100; spacing: 6
                                        Rectangle {
                                            width: 64; height: 22; radius: 5
                                            color: theme.bgInput
                                            border.color: theme.border
                                            Text {
                                                anchors.centerIn: parent
                                                text: card.seriesVisible ? "显示 ON" : "显示 OFF"
                                                color: card.seriesVisible ? card.seriesColor : theme.textMuted
                                                font.family: theme.fontFamily; font.pixelSize: 9; font.weight: Font.Medium
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: {
                                                    seriesModel.setProperty(card.index, "seriesVisible",
                                                                            !card.seriesVisible)
                                                    root.previewDirty = true
                                                }
                                            }
                                        }
                                        Rectangle {
                                            width: 58; height: 22; radius: 5
                                            color: theme.bgInput
                                            border.color: theme.border
                                            Text {
                                                anchors.centerIn: parent
                                                text: "设置"
                                                color: theme.textMuted
                                                font.family: theme.fontFamily; font.pixelSize: 9
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: root.statusText = "指标参数可在 JSON 模式中完整编辑；周期输入后续版本开放"
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        onClicked: {
                                            root.removeIndicator(card.index)
                                        }
                                    }
                                    Text {
                                        anchors.top: parent.top; anchors.right: parent.right
                                        anchors.topMargin: 2; anchors.rightMargin: 8
                                        text: "×"
                                        color: theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 12
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: root.removeIndicator(card.index)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 信号条件预构建
                    Rectangle {
                        width: parent.width
                        height: 300
                        radius: 9
                        color: theme.bgPanel
                        border.color: theme.border

                        Text {
                            x: 18; y: 14
                            text: "信号条件预构建"
                            color: theme.textPrimary
                            font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
                        }
                        Row {
                            x: 18; y: 50; spacing: 8
                            Rectangle {
                                width: 112; height: 28; radius: 6
                                color: root.activeConditionSide === 0 ? theme.bgSuccess : theme.bgRaised
                                Text {
                                    anchors.centerIn: parent
                                    text: "入场条件  " + entryConditionsModel.count
                                    color: root.activeConditionSide === 0 ? theme.green : theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activeConditionSide = 0
                                }
                            }
                            Rectangle {
                                width: 112; height: 28; radius: 6
                                color: root.activeConditionSide === 1 ? theme.bgSuccess : theme.bgRaised
                                Text {
                                    anchors.centerIn: parent
                                    text: "出场条件  " + exitConditionsModel.count
                                    color: root.activeConditionSide === 1 ? theme.green : theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activeConditionSide = 1
                                }
                            }
                            PpButton {
                                visualWidth: 106
                                theme: root.theme; text: "条件模板"
                                toolTip: "模板库将在后续阶段开放"
                            }
                            PpButton {
                                visualWidth: 72
                                theme: root.theme; text: "＋ 条件"
                                active: true
                                onClicked: root.addCondition()
                            }
                        }

                        Rectangle {
                            x: 18; y: 92; width: parent.width - 36; height: 190; radius: 8
                            color: theme.bgInput
                            border.color: theme.border

                            Text {
                                x: 14; y: 12
                                text: "条件组 A · 趋势确认（AND）"
                                color: theme.textPrimary
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                            }

                            ListView {
                                id: conditionList
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.top: parent.top; anchors.topMargin: 42
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                                anchors.leftMargin: 10; anchors.rightMargin: 10
                                clip: true
                                model: root.conditionModel()
                                spacing: 6

                                delegate: Rectangle {
                                    required property int index
                                    required property string leftOperand
                                    required property string leftKind
                                    required property string op
                                    required property string rightOperand
                                    required property string rightKind
                                    required property double rightConstant
                                    width: conditionList.width
                                    height: 34
                                    radius: 6
                                    color: theme.bgRaised

                                    Text {
                                        x: 8; y: 8; width: 26; height: 20
                                        text: String(index + 1).padStart(2, "0")
                                        color: theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    ComboBox {
                                        id: leftCombo
                                        x: 40; y: 3; width: 112; height: 28
                                        model: ["close", "open", "high", "low", "volume"].concat(root.seriesNames())
                                        currentIndex: {
                                            const values = ["close", "open", "high", "low", "volume"].concat(root.seriesNames())
                                            const idx = values.indexOf(parent.leftOperand)
                                            return idx < 0 ? 0 : idx
                                        }
                                        onActivated: {
                                            const values = ["close", "open", "high", "low", "volume"].concat(root.seriesNames())
                                            root.conditionModel().setProperty(parent.index, "leftOperand", values[currentIndex])
                                            root.conditionModel().setProperty(parent.index, "leftKind",
                                                values[currentIndex].length <= 6 ? "field" : "series")
                                            root.previewDirty = true
                                        }
                                        delegate: ItemDelegate {
                                            width: parent.width
                                            text: modelData
                                            contentItem: Text {
                                                text: modelData
                                                color: root.theme.textPrimary
                                                font.family: root.theme.fontFamily; font.pixelSize: 9
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle { color: root.theme.bgRaised }
                                        }
                                        background: Rectangle { radius: 5; color: theme.bgInput; border.color: theme.border }
                                        contentItem: Text {
                                            text: leftCombo.displayText
                                            color: theme.textPrimary
                                            font.family: theme.fontFamily; font.pixelSize: 9
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 8
                                        }
                                        indicator: Text {
                                            x: parent.width - 18; anchors.verticalCenter: parent.verticalCenter
                                            text: "▾"; color: theme.textMuted; font.pixelSize: 9
                                        }
                                    }
                                    ComboBox {
                                        id: opCombo
                                        x: 160; y: 3; width: 86; height: 28
                                        model: [
                                            { text: "上穿", op: "crossesAbove" },
                                            { text: "下穿", op: "crossesBelow" },
                                            { text: "大于", op: "gt" },
                                            { text: "大于等于", op: "gte" },
                                            { text: "小于", op: "lt" },
                                            { text: "小于等于", op: "lte" },
                                            { text: "等于", op: "eq" }
                                        ]
                                        textRole: "text"
                                        currentIndex: {
                                            for (let i = 0; i < model.length; ++i)
                                                if (model[i].op === parent.op) return i
                                            return 2
                                        }
                                        onActivated: {
                                            root.conditionModel().setProperty(parent.index, "op", model[currentIndex].op)
                                            root.previewDirty = true
                                        }
                                        delegate: ItemDelegate {
                                            width: parent.width
                                            text: modelData.text
                                            contentItem: Text {
                                                text: modelData.text
                                                color: root.theme.textPrimary
                                                font.family: root.theme.fontFamily; font.pixelSize: 9
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle { color: root.theme.bgRaised }
                                        }
                                        background: Rectangle { radius: 5; color: theme.bgInput; border.color: theme.border }
                                        contentItem: Text {
                                            text: opCombo.displayText
                                            color: theme.textPrimary
                                            font.family: theme.fontFamily; font.pixelSize: 9
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 8
                                        }
                                        indicator: Text {
                                            x: parent.width - 18; anchors.verticalCenter: parent.verticalCenter
                                            text: "▾"; color: theme.textMuted; font.pixelSize: 9
                                        }
                                    }
                                    ComboBox {
                                        id: rightCombo
                                        x: 254; y: 3; width: 112; height: 28
                                        model: root.seriesNames()
                                        currentIndex: {
                                            const values = root.seriesNames()
                                            const idx = values.indexOf(parent.rightKind === "value" ? "" : parent.rightOperand)
                                            return idx < 0 ? 0 : idx
                                        }
                                        visible: parent.rightKind !== "value"
                                        onActivated: {
                                            const values = root.seriesNames()
                                            root.conditionModel().setProperty(parent.index, "rightOperand", values[currentIndex])
                                            root.conditionModel().setProperty(parent.index, "rightKind", "series")
                                            root.previewDirty = true
                                        }
                                        delegate: ItemDelegate {
                                            width: parent.width
                                            text: modelData
                                            contentItem: Text {
                                                text: modelData
                                                color: root.theme.textPrimary
                                                font.family: root.theme.fontFamily; font.pixelSize: 9
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            background: Rectangle { color: root.theme.bgRaised }
                                        }
                                        background: Rectangle { radius: 5; color: theme.bgInput; border.color: theme.border }
                                        contentItem: Text {
                                            text: rightCombo.displayText
                                            color: theme.textPrimary
                                            font.family: theme.fontFamily; font.pixelSize: 9
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: 8
                                        }
                                        indicator: Text {
                                            x: parent.width - 18; anchors.verticalCenter: parent.verticalCenter
                                            text: "▾"; color: theme.textMuted; font.pixelSize: 9
                                        }
                                    }
                                    TextField {
                                        x: 254; y: 3; width: 112; height: 28
                                        text: parent.rightKind === "value" ? String(parent.rightConstant) : ""
                                        visible: parent.rightKind === "value"
                                        validator: DoubleValidator {}
                                        color: theme.textPrimary
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                        background: Rectangle { radius: 5; color: theme.bgInput; border.color: theme.border }
                                        onEditingFinished: {
                                            root.conditionModel().setProperty(parent.index, "rightConstant", Number(text))
                                            root.conditionModel().setProperty(parent.index, "rightKind", "value")
                                            root.previewDirty = true
                                        }
                                    }
                                    PpButton {
                                        x: 374; y: 3; visualWidth: 60
                                        theme: root.theme; text: "常量"
                                        active: parent.rightKind !== "value"
                                        onClicked: {
                                            root.conditionModel().setProperty(parent.index, "rightKind", "value")
                                            root.conditionModel().setProperty(parent.index, "rightOperand", "")
                                            root.previewDirty = true
                                        }
                                    }
                                    PpButton {
                                        x: 440; y: 3; visualWidth: 60
                                        theme: root.theme; text: "移除"
                                        onClicked: root.removeCondition(parent.index)
                                    }
                                }
                            }

                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 14
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                                text: "可添加嵌套条件组，不限制层级（后续版本）"
                                color: theme.textMuted
                                font.family: theme.fontFamily; font.pixelSize: 9
                            }
                        }

                        Row {
                            x: 18; y: 296; width: parent.width - 36; spacing: 8
                            Rectangle {
                                width: 104; height: 30; radius: 6
                                color: theme.bgSuccess
                                Text {
                                    anchors.centerIn: parent
                                    text: "BUY / LONG"
                                    color: theme.green
                                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                                }
                            }
                            Rectangle {
                                width: 166; height: 30; radius: 6
                                color: theme.bgInput
                                Text {
                                    anchors.centerIn: parent
                                    text: "价格：下一根开盘价"
                                    color: theme.textPrimary
                                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                                }
                            }
                            Rectangle {
                                width: 150; height: 30; radius: 6
                                color: theme.bgInput
                                Text {
                                    anchors.centerIn: parent
                                    text: "仓位：步骤 05 定义"
                                    color: theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                                }
                            }
                            Rectangle {
                                width: 180; height: 30; radius: 6
                                color: theme.bgInput
                                Text {
                                    anchors.centerIn: parent
                                    text: "通知：桌面 + Webhook"
                                    color: theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                                }
                            }
                        }
                        Text {
                            x: 18; y: 336; width: parent.width - 36
                            text: "点击图表中的 B / S 标记可查看命中条件、价格、成交量和执行意图。"
                            color: theme.textMuted
                            font.family: theme.fontFamily; font.pixelSize: 9
                        }
                    }
                }
            }
        }

        // 右侧预览
        Rectangle {
            id: preview
            anchors.right: parent.right
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            width: 488
            color: theme.bgBar
            border.color: theme.border

            Text {
                x: 24; y: 22
                text: "组合预览"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 16; font.weight: Font.Bold
            }
            Rectangle {
                x: 318; y: 22; width: 142; height: 28; radius: 6
                color: theme.bgRaised
                Text {
                    anchors.centerIn: parent
                    text: (root.controller.selectedBacktestSymbol.length > 0
                           ? root.controller.selectedBacktestSymbol : "600519")
                          + " · " + (root.controller.selectedBacktestFrequency.length > 0
                                     ? root.controller.selectedBacktestFrequency : "15m")
                          + " ▾"
                    color: theme.textMuted
                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                }
            }
            Text {
                x: 24; y: 54
                text: "指标、入场和出场条件会同步显示在 K 线图上"
                color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10
            }

            Rectangle {
                id: chartBox
                x: 24; y: 90; width: 436; height: 334; radius: 8
                color: theme.bgInput
                border.color: theme.border

                Canvas {
                    id: chartCanvas
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const data = root.previewData
                        const bars = data.bars || []
                        if (bars.length === 0) {
                            ctx.fillStyle = theme.textMuted
                            ctx.font = "10px sans-serif"
                            ctx.fillText(root.previewError.length > 0 ? root.previewError : "暂无预览数据", 20, 30)
                            return
                        }
                        const w = width, h = height
                        let min = Infinity, max = -Infinity
                        for (const bar of bars) {
                            min = Math.min(min, bar.l, bar.o, bar.c)
                            max = Math.max(max, bar.h, bar.o, bar.c)
                        }
                        const pad = 6
                        const span = (max - min) || 1
                        const yOf = v => pad + (1 - (v - min) / span) * (h - pad * 2)
                        const step = w / bars.length
                        const cw = Math.max(2, step * 0.55)
                        ctx.strokeStyle = theme.gridY
                        ctx.lineWidth = 1
                        for (let g = 1; g < 5; ++g) {
                            const gy = h * g / 5
                            ctx.beginPath()
                            ctx.moveTo(0, gy)
                            ctx.lineTo(w, gy)
                            ctx.stroke()
                        }
                        for (let i = 0; i < bars.length; ++i) {
                            const bar = bars[i]
                            const x = i * step + step / 2
                            const up = bar.c >= bar.o
                            ctx.strokeStyle = up ? theme.green : theme.red
                            ctx.fillStyle = ctx.strokeStyle
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(x, yOf(bar.h))
                            ctx.lineTo(x, yOf(bar.l))
                            ctx.stroke()
                            const top = yOf(Math.max(bar.o, bar.c))
                            const body = Math.max(1, Math.abs(yOf(bar.o) - yOf(bar.c)))
                            ctx.fillRect(x - cw / 2, top, cw, body)
                        }
                        // 指标序列（各序列独立归一化）
                        const seriesCount = (data.aliases || []).length
                        for (let s = 0; s < seriesCount; ++s) {
                            if (!root.seriesModel.get(s) || !root.seriesModel.get(s).seriesVisible) continue
                            let smin = Infinity, smax = -Infinity
                            const vals = []
                            for (const bar of bars) {
                                const v = bar.s && bar.s.length > s ? bar.s[s] : undefined
                                if (v !== undefined && v !== null && isFinite(v)) {
                                    vals.push(v)
                                    smin = Math.min(smin, v)
                                    smax = Math.max(smax, v)
                                } else vals.push(null)
                            }
                            if (vals.length === 0 || smin === Infinity) continue
                            const sspan = (smax - smin) || 1
                            const sy = v => pad + (1 - (v - smin) / sspan) * (h - pad * 2)
                            ctx.strokeStyle = root.seriesModel.get(s).seriesColor
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            let started = false
                            for (let i = 0; i < bars.length; ++i) {
                                if (vals[i] === null) { started = false; continue }
                                const x = i * step + step / 2
                                if (!started) { ctx.moveTo(x, sy(vals[i])); started = true }
                                else ctx.lineTo(x, sy(vals[i]))
                            }
                            ctx.stroke()
                        }
                        // B/S 信号
                        for (const sig of data.signals || []) {
                            const i = sig.index
                            if (i < 0 || i >= bars.length) continue
                            const x = i * step + step / 2
                            const y = yOf(sig.price)
                            const buy = sig.side === 0
                            ctx.fillStyle = buy ? theme.green : theme.red
                            ctx.beginPath()
                            ctx.arc(x, y, 8, 0, Math.PI * 2)
                            ctx.fill()
                            ctx.fillStyle = buy ? theme.bgCanvas : theme.textPrimary
                            ctx.font = "bold 9px sans-serif"
                            ctx.textAlign = "center"
                            ctx.fillText(buy ? "B" : "S", x, y + 3)
                            ctx.fillStyle = buy ? theme.green : theme.red
                            ctx.font = "bold 8px sans-serif"
                            ctx.textAlign = "left"
                            ctx.fillText((buy ? "入场 " : "出场 ") + sig.price.toFixed(2),
                                         Math.max(2, x - 26), y - 14)
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: !root.previewError && (!root.previewData.bars || root.previewData.bars.length === 0)
                    text: "加载本地数据后显示预览"
                    color: theme.textMuted
                    font.family: theme.fontFamily; font.pixelSize: 10
                }
            }

            Row {
                x: 24; y: 432; spacing: 8
                PpButton { theme: root.theme; visualWidth: 84; text: "−  100%  ＋"; toolTip: "缩放将在后续版本开放" }
                PpButton { theme: root.theme; visualWidth: 92; text: "十字光标"; toolTip: "十字光标将在后续版本开放" }
                PpButton { theme: root.theme; visualWidth: 100; text: "展开"; toolTip: "展开将在后续版本开放" }
            }

            Text {
                x: 24; y: 472
                text: "指标显隐"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
            }
            Grid {
                x: 24; y: 500; width: 436; columns: 2; spacing: 8
                Repeater {
                    model: seriesModel
                    delegate: Rectangle {
                        required property int index
                        required property string name
                        required property string alias
                        required property bool seriesVisible
                        required property string seriesColor
                        width: 214; height: 32; radius: 6
                        color: theme.bgRaised
                        Text {
                            x: 10; anchors.verticalCenter: parent.verticalCenter
                            text: "● " + parent.name + (parent.alias ? " (" + parent.alias + ")" : "") + "   " + (parent.seriesVisible ? "ON" : "OFF")
                            color: parent.seriesVisible ? parent.seriesColor : theme.textMuted
                            font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                seriesModel.setProperty(parent.index, "seriesVisible", !parent.seriesVisible)
                                root.previewDirty = true
                            }
                        }
                    }
                }
            }

            Text {
                x: 24; y: 600
                text: "当前信号命中详情"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
            }
            Rectangle {
                x: 24; y: 630; width: 436; height: 116; radius: 8
                color: theme.bgSuccess
                border.color: theme.border
                readonly property var lastSignal: {
                    const signals = root.previewData.signals || []
                    return signals.length > 0 ? signals[signals.length - 1] : null
                }
                Text {
                    x: 14; y: 14
                    text: parent.lastSignal ? (parent.lastSignal.side === 0 ? "B  入场" : "S  出场") : "B  入场"
                    color: theme.green
                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                }
                Text {
                    x: 108; y: 14; width: 310
                    text: parent.lastSignal
                        ? (root.previewData.symbol || "") + "  " + parent.lastSignal.price.toFixed(2)
                          + "  ·  " + (parent.lastSignal.reason || "")
                        : "暂无命中信号"
                    color: theme.textPrimary
                    font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    x: 14; y: 46; width: 408
                    text: root.previewError.length > 0 ? root.previewError
                        : "规则命中明细将在信号条件步骤完成后逐条展示；当前展示最后一次信号及原因。"
                    color: root.previewError.length > 0 ? theme.red : theme.textMuted
                    font.family: theme.fontFamily; font.pixelSize: 9
                    wrapMode: Text.Wrap
                }
            }

            Text {
                x: 24; y: 770
                text: "后续执行摘要"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
            }
            Rectangle {
                x: 24; y: 800; width: 436; height: 72; radius: 8
                color: theme.bgPanel
                border.color: theme.border
                Column {
                    x: 14; y: 10; spacing: 6
                    Text {
                        text: "仓位             固定金额 ¥20,000 · 最大持仓 15%"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                    }
                    Text {
                        text: "执行模式         人工审批 · 冷却 60 秒"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                    }
                    Text {
                        text: "验证计划         3 年日线 + 90 天 15m"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                    }
                }
            }
        }

        // 步骤 4 面板（信号条件）
        Rectangle {
            id: step4Area
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 4
            color: theme.bgCanvas

            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: Math.max(height, 560)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    width: parent.width
                    spacing: 14

                    Text {
                        width: parent.width
                        text: "04  信号条件"
                        color: theme.textPrimary
                        font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: "入场 / 出场条件组已在第 3 步定义；这里配置信号成交价格模式。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        width: parent.width; height: 76; radius: 8
                        color: theme.bgPanel; border.color: theme.border
                        Row {
                            x: 14; y: 14; spacing: 10
                            Text { text: "入场条件"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold }
                            Text { text: entryConditionsModel.count + " 组"; color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 11 }
                            Text { x: 200; text: "出场条件"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold }
                            Text { text: exitConditionsModel.count + " 组"; color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 11 }
                        }
                        Text {
                            x: 14; y: 46; width: parent.width - 28
                            text: "返回第 3 步可增删条件组；条件使用序列 / 字段 / 常量与七种比较符。"
                            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9
                        }
                    }

                    Text { text: "信号成交价格"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold }
                    Row {
                        spacing: 8
                        PpButton {
                            theme: root.theme; text: "触发 Bar 收盘价（triggerClose）"; visualWidth: 224
                            active: root.signalPriceMode === "triggerClose"
                            toolTip: "入场信号在触发那根 K 线收盘价成交（v1 默认）"
                            onClicked: root.signalPriceMode = "triggerClose"
                        }
                        PpButton {
                            theme: root.theme; text: "下一根开盘价（nextOpen）"; visualWidth: 200
                            active: root.signalPriceMode === "nextOpen"
                            toolTip: "入场信号在下一根 K 线开盘价成交"
                            onClicked: root.signalPriceMode = "nextOpen"
                        }
                    }
                    Text {
                        width: parent.width
                        text: "生成的 signal.priceMode 会写入 DSL v2 JSON；切回 JSON 模式可查看并校验。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // 步骤 5 面板（仓位与风控）
        Rectangle {
            id: step5Area
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 5
            color: theme.bgCanvas

            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: Math.max(height, 620)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    width: parent.width
                    spacing: 14

                    Text {
                        width: parent.width
                        text: "05  仓位与风控"
                        color: theme.textPrimary
                        font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: "设置建仓方式、最大持仓市值占比与止损止盈；生成到策略 JSON 的 position / risk 字段。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Text { text: "仓位类型"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold }
                    Row {
                        spacing: 8
                        PpButton {
                            theme: root.theme; text: "按比例（fixedPercent）"; visualWidth: 170
                            active: root.positionType === "fixedPercent"
                            onClicked: root.positionType = "fixedPercent"
                        }
                        PpButton {
                            theme: root.theme; text: "按金额（fixedAmount）"; visualWidth: 170
                            active: root.positionType === "fixedAmount"
                            onClicked: root.positionType = "fixedAmount"
                        }
                    }
                    Row {
                        spacing: 16
                        Column {
                            spacing: 5
                            visible: root.positionType === "fixedPercent"
                            Text { text: "仓位比例（%）"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                            TextField {
                                width: 150; height: 32
                                text: root.positionPercent
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                                selectByMouse: true
                                onTextChanged: root.positionPercent = text
                                background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                        }
                        Column {
                            spacing: 5
                            visible: root.positionType === "fixedAmount"
                            Text { text: "固定金额（元）"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                            TextField {
                                width: 150; height: 32
                                text: root.positionAmount
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                                selectByMouse: true
                                onTextChanged: root.positionAmount = text
                                background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                        }
                    }

                    Text { text: "风控限制（0 或留空表示关闭）"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold }
                    GridLayout {
                        columns: 4
                        columnSpacing: 12; rowSpacing: 8
                        Text { text: "最大持仓占比（%）"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            Layout.preferredWidth: 100; Layout.preferredHeight: 30
                            text: root.maxPositionPercent
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                            selectByMouse: true
                            onTextChanged: root.maxPositionPercent = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        Text { text: "止损（%）"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            Layout.preferredWidth: 100; Layout.preferredHeight: 30
                            text: root.stopLossPercent
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                            selectByMouse: true
                            onTextChanged: root.stopLossPercent = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        Text { text: "止盈（%）"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            Layout.preferredWidth: 100; Layout.preferredHeight: 30
                            text: root.takeProfitPercent
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                            selectByMouse: true
                            onTextChanged: root.takeProfitPercent = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        Text { text: "最大持仓 Bar"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            Layout.preferredWidth: 100; Layout.preferredHeight: 30
                            text: root.maximumHoldingBars
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10
                            selectByMouse: true
                            onTextChanged: root.maximumHoldingBars = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "生成的 position / risk 会写入 DSL v2 JSON；切回 JSON 模式可查看并校验。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        // 步骤 6 面板（执行与审批）
        Rectangle {
            id: step6Area
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 6
            color: theme.bgCanvas
            Column {
                x: 40; y: 30; width: parent.width - 80; spacing: 14
                Text { width: parent.width; text: "06  执行与审批"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "策略执行审批（人工审批 / 自动提交、冷却、仓位计算）由执行中心与 StrategyExecutionCoordinator 提供，参数走执行配置而非 DSL。当前自定义策略 executionScope 仍为 backtest_only，模拟执行入口后续开放。"
                    color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                }
                Rectangle {
                    width: parent.width; height: 64; radius: 8; color: theme.bgPanel; border.color: theme.border
                    Text {
                        x: 14; y: 12; width: parent.width - 28
                        text: "执行范围：仅历史回测（backtest_only）"
                        color: theme.yellow; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                    }
                    Text {
                        x: 14; y: 36; width: parent.width - 28
                        text: "后续阶段将接入模拟执行审批与通知通道（不接真实下单）。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9
                    }
                }
            }
        }

        // 步骤 7 面板（回测验证）
        Rectangle {
            id: step7Area
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 7
            color: theme.bgCanvas
            Column {
                x: 40; y: 30; width: parent.width - 80; spacing: 14
                Text { width: parent.width; text: "07  回测验证"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "样本窗、逐 Bar 回放与验证记录已在策略页回测结果区提供：样本窗过滤回测范围、replayBacktestBar 回放任意 Bar 的 OHLCV 与买卖信号、验证记录按收益率排序并可恢复。"
                    color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                }
                Rectangle {
                    width: parent.width; height: 64; radius: 8; color: theme.bgPanel; border.color: theme.border
                    Text { x: 14; y: 12; text: "回测验证能力已就绪"; color: theme.green; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                    Text { x: 14; y: 36; width: parent.width - 28; text: "切换到 JSON 模式校验并保存后，即可在策略页运行严格本地回测。"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                }
            }
        }

        // 步骤 8 面板（发布与运行）
        Rectangle {
            id: step8Area
            anchors.left: sidebar.right
            anchors.right: preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 8
            color: theme.bgCanvas
            Column {
                x: 40; y: 30; width: parent.width - 80; spacing: 14
                Text { width: parent.width; text: "08  发布与运行"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "通过校验的自定义策略可保存为不可变版本并进入严格本地回测；模拟执行、线程运行与通知通道属于后续阶段。"
                    color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                }
                Rectangle {
                    width: parent.width; height: 64; radius: 8; color: theme.bgPanel; border.color: theme.border
                    Text { x: 14; y: 12; text: "当前可发布到回测"; color: theme.green; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                    Text { x: 14; y: 36; width: parent.width - 28; text: "切换到 JSON 模式校验并保存新版本，随后在策略页运行回测。"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                }
            }
        }

        // 底部操作栏
        Rectangle {
            id: footer
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 64
            color: theme.bgBar
            border.color: theme.border

            Text {
                x: 28; y: 20; width: 360
                text: root.statusText + " · 指标 " + seriesModel.count + " · 条件组 1"
                color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10
                elide: Text.ElideRight
            }
            PpButton {
                x: 400; y: 15; visualWidth: 112
                theme: root.theme; text: "← 上一步"
                toolTip: "步骤 1–2 后续开放；可先切换 JSON 模式"
                onClicked: {
                    if (root.activeStep > 3) {
                        root.activeStep = root.activeStep - 1
                    } else {
                        root.switchToJson()
                    }
                }
            }
            PpButton {
                x: 524; y: 15; visualWidth: 136
                theme: root.theme; text: "预览信号条件"
                active: true
                onClicked: {
                    if (root.validateLocal()) root.refreshPreview()
                }
            }
            PpButton {
                x: 672; y: 15; visualWidth: 210
                theme: root.theme; text: root.activeStep < 8 ? "下一步：继续  →" : "完成：生成 JSON  →"
                primary: true
                onClicked: {
                    if (root.validateLocal()) {
                        root.saveDraft()
                        if (root.activeStep < 8) {
                            root.activeStep = root.activeStep + 1
                        } else {
                            root.switchToJson()
                            root.statusText = "已生成 DSL v2 JSON，可在 JSON 模式校验并保存版本"
                        }
                    }
                }
            }
        }
    }
}
