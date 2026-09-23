// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "CrossSectionalBacktestContract.js" as CrossContract

Item {
    id: root
    required property var theme
    required property var controller

    property var periodOptions: []
    property int selectedPeriodSeconds: 0
    property var selectedSymbols: []
    property string selectedRankAlias: "mom_rank"
    property string entryAlias: "mom_rank"
    property string exitAlias: "mom_rank"
    property string formError: ""
    property int catalogRevision: 0
    readonly property bool compactLayout: width < 900
    readonly property var crossIndicatorNames: controller.customStrategySchema.crossIndicators || []
    readonly property var fieldNames: controller.customStrategySchema.fields || []
    readonly property var comparisonOperators: ["gt", "gte", "lt", "lte"]

    FileDialog {
        id: factorReportDialog
        title: "保存因子研究报告"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Markdown 报告 (*.md)", "所有文件 (*)"]
        onAccepted: root.controller.exportCrossSectionalFactorReport(
                        String(selectedFile))
    }

    function indexOfValue(values, value) {
        for (let index = 0; index < values.length; ++index) {
            if (String(values[index]) === String(value)) return index
        }
        return -1
    }

    function circuitBreakerReasonLabel(reason) {
        if (reason === "loss_limit") return "累计亏损"
        if (reason === "drawdown_limit") return "峰值回撤"
        if (reason === "loss_and_drawdown") return "累计亏损与峰值回撤"
        return "组合风控"
    }

    function thresholdLabel(value) {
        return Number(value) > 0 ? Number(value).toFixed(1) + "%" : "关闭"
    }

    function industryDatasetOptions() {
        const history = root.controller.aShareIndustryImportHistory || []
        const result = []
        for (let index = 0; index < history.length; ++index) {
            const item = history[index]
            result.push({
                datasetId: String(item.datasetId || ""),
                label: String(item.classification || "--") + " · "
                       + String(item.source || "--") + " · "
                       + String(item.sourceVersion || "--") + " · "
                       + Number(item.recordCount || 0) + " 条"
            })
        }
        return result
    }

    function refreshCatalog() {
        root.periodOptions = CrossContract.periodOptions(
                    root.controller.localBacktestDataCatalog || [])
        if (root.periodOptions.length === 0) {
            root.selectedPeriodSeconds = 0
            root.selectedSymbols = []
            root.formError = "本地目录中没有身份完整的 Tick 派生数据集"
            return
        }
        let matched = -1
        for (let index = 0; index < root.periodOptions.length; ++index) {
            if (Number(root.periodOptions[index].periodSeconds)
                    === Number(root.selectedPeriodSeconds)) {
                matched = index
                break
            }
        }
        if (matched < 0) {
            matched = 0
            root.selectedPeriodSeconds = Number(root.periodOptions[0].periodSeconds)
        }
        periodCombo.currentIndex = matched
        root.pruneSelectedSymbols()
        root.catalogRevision += 1
    }

    function eligibleDatasets(query) {
        const revision = root.catalogRevision
        return CrossContract.eligibleDatasets(
                    root.controller.localBacktestDataCatalog || [],
                    root.selectedPeriodSeconds, query)
    }

    function setPeriod(periodSeconds) {
        root.selectedPeriodSeconds = Number(periodSeconds)
        root.pruneSelectedSymbols()
        root.formError = ""
        root.catalogRevision += 1
    }

    function pruneSelectedSymbols() {
        const datasets = CrossContract.eligibleDatasets(
                    root.controller.localBacktestDataCatalog || [],
                    root.selectedPeriodSeconds, "")
        const available = {}
        for (let index = 0; index < datasets.length; ++index)
            available[datasets[index].symbol] = true
        const kept = []
        for (let index = 0; index < root.selectedSymbols.length; ++index) {
            if (available[root.selectedSymbols[index]])
                kept.push(root.selectedSymbols[index])
        }
        root.selectedSymbols = kept
    }

    function symbolSelected(symbol) {
        return root.selectedSymbols.indexOf(symbol) >= 0
    }

    function toggleSymbol(symbol) {
        const values = root.selectedSymbols.slice(0)
        const index = values.indexOf(symbol)
        if (index >= 0) values.splice(index, 1)
        else values.push(symbol)
        root.selectedSymbols = values
        root.formError = ""
    }

    function selectFirstEligibleSymbols(maximumCount) {
        const datasets = root.eligibleDatasets("")
        const values = []
        for (let index = 0; index < datasets.length && index < maximumCount; ++index)
            values.push(datasets[index].symbol)
        root.selectedSymbols = values
        root.formError = ""
    }

    function indicatorRows() {
        const result = []
        for (let index = 0; index < crossIndicatorModel.count; ++index)
            result.push(crossIndicatorModel.get(index))
        return result
    }

    function indicatorAliases() {
        return CrossContract.indicatorAliases(root.indicatorRows())
    }

    function syncConditionAliases() {
        const aliases = root.indicatorAliases()
        if (aliases.length === 0) {
            root.selectedRankAlias = ""
            root.entryAlias = ""
            root.exitAlias = ""
            return
        }
        if (aliases.indexOf(root.selectedRankAlias) < 0)
            root.selectedRankAlias = aliases[0]
        if (aliases.indexOf(root.entryAlias) < 0)
            root.entryAlias = aliases[0]
        if (aliases.indexOf(root.exitAlias) < 0)
            root.exitAlias = aliases[0]
    }

    function addIndicator() {
        const suffix = crossIndicatorModel.count + 1
        crossIndicatorModel.append({
            alias: "factor" + suffix,
            indicatorName: root.crossIndicatorNames.length > 0
                           ? String(root.crossIndicatorNames[0]) : "RANK",
            sourceKind: "field",
            sourceField: root.fieldNames.length > 0 ? String(root.fieldNames[0]) : "close",
            periodValue: 20,
            peerSymbol: "",
            ascendingValue: false
        })
        root.syncConditionAliases()
        root.formError = ""
    }

    function removeIndicator(rowIndex) {
        if (rowIndex < 0 || rowIndex >= crossIndicatorModel.count) return
        crossIndicatorModel.remove(rowIndex)
        root.syncConditionAliases()
        root.formError = ""
    }

    function draftState() {
        return {
            strategyId: strategyIdField.text.trim(),
            version: 1,
            name: strategyNameField.text.trim(),
            description: "由多标的向导生成，只读取本地 Tick 派生数据",
            author: "本地用户",
            periodSeconds: root.selectedPeriodSeconds,
            barCount: Number(barCountField.text),
            symbols: root.selectedSymbols,
            indicators: root.indicatorRows(),
            rankBy: root.selectedRankAlias,
            topN: Number(topNField.text),
            selectionAscending: selectionAscending.checked,
            entryAlias: root.entryAlias,
            entryOp: entryOperator.currentText,
            entryValue: Number(entryValueField.text),
            exitAlias: root.exitAlias,
            exitOp: exitOperator.currentText,
            exitValue: Number(exitValueField.text)
        }
    }

    function buildJson(createdAt) {
        return JSON.stringify(CrossContract.buildDefinition(
                                  root.draftState(),
                                  createdAt || new Date().toISOString()), null, 2)
    }

    function validateForm() {
        const draft = root.draftState()
        const local = CrossContract.validateDraft(
                    root.controller.localBacktestDataCatalog || [], draft,
                    root.crossIndicatorNames, root.fieldNames)
        if (!local.valid) {
            root.formError = local.error
            return local
        }
        const riskValid = root.controller.setCrossSectionalRiskConfiguration(
                    Number(maximumLegWeightField.text) / 100.0,
                    Number(maximumActiveLegsField.text),
                    Number(maximumMissingBarsField.text))
        if (!riskValid) {
            root.formError = root.controller.crossSectionalRiskConfigurationError
            return { valid: false, error: root.formError }
        }
        const circuitValid = root.controller.setCrossSectionalCircuitBreakerConfiguration(
                    Number(maximumPortfolioLossField.text),
                    Number(maximumPortfolioDrawdownField.text))
        if (!circuitValid) {
            root.formError = root.controller.crossSectionalRiskConfigurationError
            return { valid: false, error: root.formError }
        }
        const industryValid = root.controller.setCrossSectionalIndustryConfiguration(
                    Number(maximumIndustryWeightField.text) / 100.0,
                    String(industryDatasetCombo.currentValue || ""))
        if (!industryValid) {
            root.formError = root.controller.crossSectionalRiskConfigurationError
            return { valid: false, error: root.formError }
        }
        const factorValid = root.controller.setCrossSectionalFactorExposureConfiguration(
                    Number(maximumFactorExposureField.text),
                    String(factorExposureAliasCombo.currentText || ""))
        if (!factorValid) {
            root.formError = root.controller.crossSectionalRiskConfigurationError
            return { valid: false, error: root.formError }
        }
        const factorResearchValid = root.controller.setCrossSectionalFactorResearchConfiguration(
                    factorResearchEnabled.checked
                    ? String(factorResearchAliasCombo.currentText || "") : "",
                    Number(factorWinsorLowerField.text),
                    Number(factorWinsorUpperField.text),
                    Number(factorMinimumPairsField.text),
                    Number(factorResearchCostField.text),
                    factorResearchEnabled.checked
                    && factorIndustryNeutralization.checked,
                    Number(factorIndustryMinimumGroupField.text))
        if (!factorResearchValid) {
            root.formError = root.controller.crossSectionalRiskConfigurationError
            return { valid: false, error: root.formError }
        }
        const backend = root.controller.validateCrossSectionalBacktestRequest(
                    root.buildJson(), Number(draft.barCount),
                    Number(draft.periodSeconds))
        root.formError = backend.valid ? "" : backend.error
        return backend
    }

    function runCrossSectional() {
        const result = root.validateForm()
        if (!result.valid) return 0
        return root.controller.runCrossSectionalBacktestForPeriod(
                    root.buildJson(), Number(barCountField.text),
                    root.selectedPeriodSeconds)
    }

    ListModel {
        id: crossIndicatorModel
        ListElement {
            alias: "mom_rank"
            indicatorName: "RANK"
            sourceKind: "ROC"
            sourceField: "close"
            periodValue: 20
            peerSymbol: ""
            ascendingValue: false
        }
    }

    component SectionTitle: Text {
        required property string labelText
        text: labelText
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily
        font.pixelSize: 12
        font.weight: Font.Bold
    }

    component FormField: TextField {
        Layout.preferredHeight: 28
        color: root.theme.textPrimary
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        placeholderTextColor: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        leftPadding: 8
        rightPadding: 8
        selectByMouse: true
        background: Rectangle {
            radius: 5
            color: root.theme.bgInput
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    component FormCombo: ComboBox {
        Layout.preferredHeight: 28
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        Accessible.name: "多股回测配置选择框"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: "DSL v3 · 本地数据身份门禁 · 每期横截面选股并等权调仓"
                color: root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily
                font.pixelSize: 10
            }
            Rectangle {
                Layout.preferredWidth: 126
                Layout.preferredHeight: 28
                radius: 6
                color: root.theme.bgInput
                border.color: root.controller.crossSectionalBacktestRunning
                              ? root.theme.yellow : root.theme.border
                Text {
                    anchors.centerIn: parent
                    text: root.controller.crossSectionalBacktestStatusText
                    color: root.controller.crossSectionalBacktestRunning
                           ? root.theme.yellow : root.theme.textMuted
                    elide: Text.ElideRight
                    width: parent.width - 12
                    horizontalAlignment: Text.AlignHCenter
                    font.family: root.theme.fontFamily
                    font.pixelSize: 9
                }
            }
            PpButton {
                theme: root.theme
                text: "取消回测"
                visualWidth: 88
                available: root.controller.crossSectionalBacktestRunning
                toolTip: available ? "请求取消当前多股回测" : "当前没有运行中的多股回测"
                onClicked: root.controller.cancelCrossSectionalBacktest()
            }
            PpButton {
                theme: root.theme
                text: "开始回测"
                primary: true
                visualWidth: 102
                available: !root.controller.crossSectionalBacktestRunning
                           && !root.controller.batchBacktestRunning
                           && !root.controller.backtestRunning
                           && !root.controller.portfolioBacktestRunning
                toolTip: available ? "校验本地数据身份后启动横截面回测"
                                   : "请等待其他回测任务结束"
                onClicked: root.runCrossSectional()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.formError.length > 0
                     || root.controller.crossSectionalBacktestError.length > 0
                     || root.controller.crossSectionalRiskConfigurationError.length > 0
            text: root.formError.length > 0 ? root.formError
                  : root.controller.crossSectionalBacktestError.length > 0
                    ? root.controller.crossSectionalBacktestError
                    : root.controller.crossSectionalRiskConfigurationError
            color: root.theme.red
            wrapMode: Text.Wrap
            font.family: root.theme.fontFamily
            font.pixelSize: 10
            Accessible.name: "多股回测错误：" + text
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: root.compactLayout ? 1 : 2
            columnSpacing: 12
            rowSpacing: 12

            Rectangle {
                objectName: "crossSectionalConfigurationCard"
                Layout.fillWidth: root.compactLayout
                Layout.fillHeight: !root.compactLayout
                Layout.preferredWidth: root.compactLayout ? -1 : 430
                Layout.minimumWidth: root.compactLayout ? 0 : 430
                Layout.maximumWidth: root.compactLayout ? 16777215 : 430
                Layout.preferredHeight: root.compactLayout ? 520 : -1
                radius: 10
                color: root.theme.bgCanvas
                border.color: root.theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    ColumnLayout {
                        width: parent.availableWidth
                        spacing: 9

                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { labelText: "股票池 Universe"; Layout.fillWidth: true }
                            Rectangle {
                                Layout.preferredWidth: 70; Layout.preferredHeight: 22
                                radius: 6; color: root.theme.bgSelected; border.color: root.theme.purple
                                Text { anchors.centerIn: parent; text: "explicit"; color: root.theme.purple; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8; rowSpacing: 6
                            Text { text: "周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormCombo {
                                id: periodCombo
                                Layout.fillWidth: true
                                model: root.periodOptions
                                textRole: "label"
                                onActivated: {
                                    const option = root.periodOptions[currentIndex]
                                    if (option) root.setPeriod(option.periodSeconds)
                                }
                                Accessible.name: "多股回测本地数据周期"
                            }
                            Text { text: "回测根数"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: barCountField
                                Layout.fillWidth: true
                                text: "200"
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "多股回测 K 线根数"
                                onTextEdited: root.formError = ""
                            }
                            Text { text: "策略 ID"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField { id: strategyIdField; Layout.fillWidth: true; text: "custom_cross_momentum"; Accessible.name: "多股策略 ID" }
                            Text { text: "策略名称"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField { id: strategyNameField; Layout.fillWidth: true; text: "横截面动量轮动"; Accessible.name: "多股策略名称" }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            FormField {
                                id: datasetSearch
                                Layout.fillWidth: true
                                placeholderText: "搜索本地代码或名称"
                                Accessible.name: "搜索多股本地数据集"
                            }
                            PpButton {
                                theme: root.theme; text: "选前 6 只"; visualWidth: 82
                                available: root.eligibleDatasets("").length >= 2
                                toolTip: available ? "从当前周期的实际本地目录选择前 6 只"
                                                   : "当前周期不足 2 个可回测数据集"
                                onClicked: root.selectFirstEligibleSymbols(6)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "已选 " + root.selectedSymbols.length + " 只："
                                  + (root.selectedSymbols.length > 0
                                     ? root.selectedSymbols.join("  ") : "--")
                            color: root.selectedSymbols.length >= 2 ? root.theme.cyan : root.theme.yellow
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }

                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 104
                            radius: 6; color: root.theme.bgInput; border.color: root.theme.border
                            clip: true
                            ListView {
                                anchors.fill: parent; anchors.margins: 4
                                model: root.eligibleDatasets(datasetSearch.text)
                                spacing: 3
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Rectangle {
                                    required property var modelData
                                    width: ListView.view.width; height: 28; radius: 5
                                    color: root.symbolSelected(modelData.symbol)
                                           ? root.theme.bgSelected : root.theme.bgCanvas
                                    border.color: root.symbolSelected(modelData.symbol)
                                                  ? root.theme.cyan : root.theme.border
                                    Text {
                                        anchors.left: parent.left; anchors.leftMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 100
                                        text: modelData.symbol + "  " + modelData.name
                                              + " · " + modelData.candleCount + " 根"
                                        color: root.theme.textPrimary; elide: Text.ElideRight
                                        font.family: root.theme.fontFamily; font.pixelSize: 9
                                    }
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.symbolSelected(modelData.symbol) ? "已选择" : "选择"
                                        color: root.symbolSelected(modelData.symbol)
                                               ? root.theme.cyan : root.theme.textMuted
                                        font.family: root.theme.fontFamily; font.pixelSize: 9
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: root.toggleSymbol(modelData.symbol) }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { labelText: "横截面指标 Cross Indicators"; Layout.fillWidth: true }
                            PpButton {
                                theme: root.theme; text: "添加"; visualWidth: 58
                                available: crossIndicatorModel.count < 6
                                toolTip: "最多在界面配置 6 个横截面指标；后端上限保持 32"
                                onClicked: root.addIndicator()
                            }
                        }

                        ListView {
                            id: indicatorList
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(300, Math.max(104, contentHeight))
                            model: crossIndicatorModel
                            spacing: 6
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: indicatorList.contentHeight > indicatorList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                            delegate: Rectangle {
                                required property int index
                                required property string alias
                                required property string indicatorName
                                required property string sourceKind
                                required property string sourceField
                                required property int periodValue
                                required property string peerSymbol
                                required property bool ascendingValue
                                width: ListView.view.width; height: 96; radius: 6
                                color: root.theme.bgInput; border.color: root.theme.border
                                GridLayout {
                                    anchors.fill: parent; anchors.margins: 6
                                    columns: 4; columnSpacing: 6; rowSpacing: 5
                                    FormField {
                                        Layout.fillWidth: true; text: alias
                                        Accessible.name: "横截面指标别名"
                                        onEditingFinished: {
                                            crossIndicatorModel.setProperty(index, "alias", text.trim())
                                            root.syncConditionAliases(); root.formError = ""
                                        }
                                    }
                                    FormCombo {
                                        Layout.fillWidth: true; model: root.crossIndicatorNames
                                        currentIndex: root.indexOfValue(root.crossIndicatorNames, indicatorName)
                                        onActivated: {
                                            crossIndicatorModel.setProperty(index, "indicatorName", currentText)
                                            root.formError = ""
                                        }
                                        Accessible.name: "横截面指标类型"
                                    }
                                    FormCombo {
                                        Layout.fillWidth: true
                                        model: indicatorName === "PEER" ? ["字段"] : ["字段", "ROC"]
                                        currentIndex: sourceKind === "ROC" ? 1 : 0
                                        onActivated: {
                                            crossIndicatorModel.setProperty(index, "sourceKind", currentIndex === 1 ? "ROC" : "field")
                                            root.formError = ""
                                        }
                                        Accessible.name: "横截面指标输入类型"
                                    }
                                    PpButton {
                                        theme: root.theme; text: "删除"; danger: true; visualWidth: 52
                                        available: crossIndicatorModel.count > 1
                                        toolTip: available ? "删除该横截面指标" : "至少保留一个横截面指标"
                                        onClicked: root.removeIndicator(index)
                                    }
                                    FormCombo {
                                        Layout.fillWidth: true; model: root.fieldNames
                                        currentIndex: root.indexOfValue(root.fieldNames, sourceField)
                                        onActivated: {
                                            crossIndicatorModel.setProperty(index, "sourceField", currentText)
                                            root.formError = ""
                                        }
                                        Accessible.name: "横截面指标字段"
                                    }
                                    FormField {
                                        Layout.fillWidth: true
                                        visible: sourceKind === "ROC" && indicatorName !== "PEER"
                                        text: String(periodValue); placeholderText: "ROC 周期"
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        Accessible.name: "ROC 周期"
                                        onEditingFinished: crossIndicatorModel.setProperty(index, "periodValue", Number(text))
                                    }
                                    FormField {
                                        Layout.fillWidth: true
                                        visible: indicatorName === "PEER"
                                        text: peerSymbol; placeholderText: "对照代码"
                                        Accessible.name: "PEER 对照代码"
                                        onEditingFinished: crossIndicatorModel.setProperty(index, "peerSymbol", text.trim())
                                    }
                                    CheckBox {
                                        visible: indicatorName === "RANK"
                                        text: "升序排名"; checked: ascendingValue
                                        onToggled: crossIndicatorModel.setProperty(index, "ascendingValue", checked)
                                        Accessible.name: "横截面排名升序"
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                        SectionTitle { labelText: "选股 Selection / 仓位 Position" }
                        GridLayout {
                            Layout.fillWidth: true; columns: 4; columnSpacing: 6; rowSpacing: 6
                            Text { text: "排序指标"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormCombo {
                                Layout.fillWidth: true; model: root.indicatorAliases()
                                currentIndex: root.indexOfValue(model, root.selectedRankAlias)
                                onActivated: root.selectedRankAlias = currentText
                                Accessible.name: "Top-N 排序指标"
                            }
                            Text { text: "Top-N"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField { id: topNField; Layout.fillWidth: true; text: "3"; inputMethodHints: Qt.ImhDigitsOnly; Accessible.name: "Top-N 数量" }
                            CheckBox { id: selectionAscending; text: "升序选择"; Accessible.name: "Top-N 升序选择" }
                            Rectangle {
                                Layout.columnSpan: 3; Layout.fillWidth: true; Layout.preferredHeight: 26
                                radius: 6; color: root.theme.bgSelected; border.color: root.theme.purple
                                Text { anchors.centerIn: parent; text: "equalWeight · 每期入选标的等权"; color: root.theme.purple; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            }
                            Text { text: "单腿上限"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumLegWeightField
                                Layout.fillWidth: true
                                text: "35"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "多股回测单腿最大权重百分比"
                                Accessible.description: "风险裁剪后不重新归一化，未使用比例保留为现金"
                            }
                            Text { text: "最大活动腿"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumActiveLegsField
                                Layout.fillWidth: true
                                text: "10"
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "多股回测最大活动腿数"
                                Accessible.description: "输入零表示不限制"
                            }
                            Text { text: "连续缺失退出"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumMissingBarsField
                                Layout.fillWidth: true
                                text: "5"
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "连续缺失退出阈值 Bar 数"
                                Accessible.description: "达到阈值后等待首个真实报价退出；零表示关闭"
                            }
                            Text { text: "累计亏损熔断%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumPortfolioLossField
                                Layout.fillWidth: true
                                text: "0"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "组合累计亏损熔断百分比"
                                Accessible.description: "相对初始资金计算，输入零表示关闭"
                            }
                            Text { text: "峰值回撤熔断%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumPortfolioDrawdownField
                                Layout.fillWidth: true
                                text: "0"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "组合峰值回撤熔断百分比"
                                Accessible.description: "相对运行中最高权益计算，输入零表示关闭"
                            }
                            Text { text: "行业目标上限%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumIndustryWeightField
                                Layout.fillWidth: true
                                text: "0"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "横截面行业目标权重上限百分比"
                                Accessible.description: "输入零表示关闭；启用后按固定行业快照同比例裁剪目标权重，剩余资金保留为现金"
                            }
                            Text { text: "行业数据集"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormCombo {
                                id: industryDatasetCombo
                                Layout.fillWidth: true
                                model: root.industryDatasetOptions()
                                textRole: "label"
                                valueRole: "datasetId"
                                enabled: Number(maximumIndustryWeightField.text) > 0
                                         || factorIndustryNeutralization.checked
                                Accessible.name: "横截面回测不可变行业数据集"
                                Accessible.description: enabled
                                    ? "按回测首帧日期解析全部股票池，覆盖不完整时拒绝运行"
                                    : "行业目标上限与因子行业去均值均已关闭"
                            }
                            Text { text: "因子暴露别名"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormCombo {
                                id: factorExposureAliasCombo
                                Layout.fillWidth: true
                                model: root.indicatorAliases()
                                enabled: Number(maximumFactorExposureField.text) > 0
                                Accessible.name: "横截面因子暴露指标别名"
                                Accessible.description: enabled
                                    ? "使用该策略内横截面指标的总体 Z 分数约束组合暴露"
                                    : "因子绝对暴露上限为零，因子约束已关闭"
                            }
                            Text { text: "因子绝对上限 Z"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: maximumFactorExposureField
                                Layout.fillWidth: true
                                text: "0"
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "组合因子绝对暴露上限 Z"
                                Accessible.description: "输入零表示关闭；超限时同比例降低风险仓位并保留现金，不执行优化或中性化"
                            }
                            CheckBox {
                                id: factorResearchEnabled
                                text: "计算 IC / Rank IC"
                                Accessible.name: "启用策略内因子研究统计"
                                Accessible.description: "使用当前帧因子预测下一可用对齐帧收盘收益，不使用同帧收益"
                            }
                            FormCombo {
                                id: factorResearchAliasCombo
                                Layout.fillWidth: true
                                model: root.indicatorAliases()
                                enabled: factorResearchEnabled.checked
                                Accessible.name: "因子研究指标别名"
                            }
                            Text { text: "去极值分位%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 4
                                FormField {
                                    id: factorWinsorLowerField
                                    Layout.fillWidth: true; text: "1"
                                    enabled: factorResearchEnabled.checked
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    Accessible.name: "因子去极值下分位百分比"
                                }
                                Text { text: "–"; color: root.theme.textMuted }
                                FormField {
                                    id: factorWinsorUpperField
                                    Layout.fillWidth: true; text: "99"
                                    enabled: factorResearchEnabled.checked
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    Accessible.name: "因子去极值上分位百分比"
                                }
                            }
                            Text { text: "每帧最少样本"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: factorMinimumPairsField
                                Layout.fillWidth: true; text: "3"
                                enabled: factorResearchEnabled.checked
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "因子研究每帧最少有效样本数"
                            }
                            Text { text: "单边成本 bps"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: factorResearchCostField
                                objectName: "factorResearchCostField"
                                Layout.fillWidth: true; text: "10"
                                enabled: factorResearchEnabled.checked
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                Accessible.name: "因子分层研究单边交易成本基点"
                                Accessible.description: "用于换手成本归因的研究假设；不是组合回测实际佣金或滑点"
                            }
                            Text { text: "行业内去均值"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            CheckBox {
                                id: factorIndustryNeutralization
                                objectName: "factorIndustryNeutralization"
                                text: "启用"
                                enabled: factorResearchEnabled.checked
                                Accessible.name: "启用因子行业内去均值"
                                Accessible.description: "先成对剔除，再全局去极值，最后按同一不可变行业快照对因子值去均值；未来收益不做行业调整"
                            }
                            Text { text: "行业最少样本"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                            FormField {
                                id: factorIndustryMinimumGroupField
                                objectName: "factorIndustryMinimumGroupField"
                                Layout.fillWidth: true
                                text: "2"
                                enabled: factorResearchEnabled.checked
                                         && factorIndustryNeutralization.checked
                                inputMethodHints: Qt.ImhDigitsOnly
                                Accessible.name: "行业中性化每组最少有效样本数"
                                Accessible.description: "不足阈值的行业整组剔除，不补零、不合并"
                            }
                            Text {
                                Layout.columnSpan: 2; Layout.fillWidth: true
                                text: "版本库 " + (root.controller.crossSectionalFactorDefinitionCatalog || []).length
                                      + " 条 · 缺失值成对剔除"
                                color: root.theme.textMuted; elide: Text.ElideRight
                                font.family: root.theme.fontFamily; font.pixelSize: 8
                                Accessible.name: text
                            }
                            Text {
                                Layout.columnSpan: 4
                                Layout.fillWidth: true
                                text: "A 股 T+1 固定启用 · 缺报价仅估值，不按旧价成交"
                                color: root.theme.cyan
                                wrapMode: Text.Wrap
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                                Accessible.name: text
                            }
                        }

                        SectionTitle { labelText: "进出场 Entry / Exit" }
                        GridLayout {
                            Layout.fillWidth: true; columns: 3; columnSpacing: 6; rowSpacing: 6
                            FormCombo {
                                Layout.fillWidth: true; model: root.indicatorAliases()
                                currentIndex: root.indexOfValue(model, root.entryAlias)
                                onActivated: root.entryAlias = currentText
                                Accessible.name: "入场横截面指标"
                            }
                            FormCombo { id: entryOperator; Layout.fillWidth: true; model: root.comparisonOperators; currentIndex: 1; Accessible.name: "入场比较符" }
                            FormField { id: entryValueField; Layout.fillWidth: true; text: "0.8"; Accessible.name: "入场阈值" }
                            FormCombo {
                                Layout.fillWidth: true; model: root.indicatorAliases()
                                currentIndex: root.indexOfValue(model, root.exitAlias)
                                onActivated: root.exitAlias = currentText
                                Accessible.name: "出场横截面指标"
                            }
                            FormCombo { id: exitOperator; Layout.fillWidth: true; model: root.comparisonOperators; currentIndex: 2; Accessible.name: "出场比较符" }
                            FormField { id: exitValueField; Layout.fillWidth: true; text: "0"; Accessible.name: "出场阈值" }
                        }
                    }
                }
            }

            ScrollView {
                id: resultsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: root.compactLayout ? 500 : -1
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: resultsScroll.availableWidth
                    spacing: 10

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 48
                    radius: 8; color: root.theme.bgCanvas; border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.crossSectionalBacktestStatusText
                                  + " · " + root.selectedSymbols.length + " 个标的"
                                  + " · " + (root.selectedPeriodSeconds > 0
                                              ? root.selectedPeriodSeconds + " 秒" : "未选周期")
                            color: root.theme.textPrimary; elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 10
                        }
                        Text { text: "数据身份逐腿校验"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                }

                Rectangle {
                    id: portfolioRiskSummaryCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: 144
                    radius: 8
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    readonly property var configuredRisk: root.controller.crossSectionalRiskConfiguration || ({})
                    readonly property var resultRisk: (root.controller.crossSectionalResultConfiguration || ({})).riskSummary || ({})
                    readonly property var factorResearch: (root.controller.crossSectionalResultConfiguration || ({})).factorResearch || ({})
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: "组合风控 · 单腿 "
                                  + Number(portfolioRiskSummaryCard.configuredRisk.maximumLegWeight * 100).toFixed(0)
                                  + "% · 活动腿 " + portfolioRiskSummaryCard.configuredRisk.maximumActiveLegs
                                  + " · 连续缺失 " + portfolioRiskSummaryCard.configuredRisk.maximumConsecutiveMissingBars
                                  + " Bar · 亏损 "
                                  + root.thresholdLabel(portfolioRiskSummaryCard.configuredRisk.maximumPortfolioLossPercent)
                                  + " · 回撤 "
                                  + root.thresholdLabel(portfolioRiskSummaryCard.configuredRisk.maximumPortfolioDrawdownPercent)
                                  + " · 行业 "
                                  + root.thresholdLabel(Number(portfolioRiskSummaryCard.configuredRisk.maximumIndustryWeight) * 100)
                                  + " · 因子 "
                                  + (Number(portfolioRiskSummaryCard.configuredRisk.maximumAbsoluteFactorExposure) > 0
                                     ? String(portfolioRiskSummaryCard.configuredRisk.factorExposureAlias)
                                       + " ≤ " + Number(portfolioRiskSummaryCard.configuredRisk.maximumAbsoluteFactorExposure).toFixed(2) + " Z"
                                     : "关闭")
                            color: root.theme.textPrimary
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                            Accessible.name: text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: portfolioRiskSummaryCard.resultRisk.circuitBreakerTriggered !== undefined
                                  ? (portfolioRiskSummaryCard.resultRisk.circuitBreakerTriggered
                                     ? "熔断已触发 · "
                                       + root.circuitBreakerReasonLabel(portfolioRiskSummaryCard.resultRisk.circuitBreakerReason)
                                       + " · 亏损 " + Number(portfolioRiskSummaryCard.resultRisk.circuitBreakerLossPercent).toFixed(2)
                                       + "% · 回撤 " + Number(portfolioRiskSummaryCard.resultRisk.circuitBreakerDrawdownPercent).toFixed(2)
                                       + "% · 已退出 " + portfolioRiskSummaryCard.resultRisk.completedCircuitBreakerExitCount
                                       + " · 待退出 " + portfolioRiskSummaryCard.resultRisk.pendingCircuitBreakerExitCount
                                     : "熔断未触发 · 最大亏损 "
                                       + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedLossPercent).toFixed(2)
                                       + "% · 最大回撤 "
                                       + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedDrawdownPercent).toFixed(2) + "%")
                                  : "完成真实回测后显示组合净值熔断状态"
                            color: portfolioRiskSummaryCard.resultRisk.circuitBreakerTriggered
                                   || portfolioRiskSummaryCard.resultRisk.pendingCircuitBreakerExitCount > 0
                                   ? root.theme.yellow : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: portfolioRiskSummaryCard.resultRisk.maximumObservedActiveLegs !== undefined
                                  ? "结果 · 峰值单腿 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedLegWeight * 100).toFixed(1)
                                    + "% · Top3 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedTop3Weight * 100).toFixed(1)
                                    + "% · 裁剪 " + portfolioRiskSummaryCard.resultRisk.constrainedFrameCount
                                    + " 帧 · 缺失退出 " + portfolioRiskSummaryCard.resultRisk.completedMissingRiskExitCount
                                    + " · 待退出 " + portfolioRiskSummaryCard.resultRisk.pendingMissingRiskExitCount
                                  : "完成真实回测后显示实际集中度、裁剪帧和缺失退出状态"
                            color: portfolioRiskSummaryCard.resultRisk.pendingMissingRiskExitCount > 0
                                   ? root.theme.yellow : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: portfolioRiskSummaryCard.resultRisk.calendarSnapshotVersion !== undefined
                                  ? "交易日历 "
                                    + (portfolioRiskSummaryCard.resultRisk.marketCalendarCoverageComplete
                                       ? "权威覆盖" : "覆盖不完整")
                                    + " · 停牌事实 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.authoritativeSuspensionPointCount)
                                    + " 腿帧 · 未知缺报价 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.unknownMissingQuotePointCount)
                                    + " 腿帧"
                                  : "完成真实回测后显示交易日历与停复牌事实覆盖"
                            color: portfolioRiskSummaryCard.resultRisk.instrumentStatusCoverageComplete
                                   ? root.theme.textMuted : root.theme.yellow
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "缺少权威停复牌事实时保持未知，不从缺失 K 线推断停牌"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: portfolioRiskSummaryCard.resultRisk.industrySnapshotVersion !== undefined
                                  && String(portfolioRiskSummaryCard.resultRisk.industrySnapshotVersion).length > 0
                                  ? "行业快照已固定 · 裁剪行业 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.industryConstrainedGroupCount)
                                    + " · 峰值行业 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedIndustryWeight * 100).toFixed(1)
                                    + "% · 漂移越限 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.industryLimitBreachFrameCount)
                                    + " 帧"
                                  : (Number(portfolioRiskSummaryCard.configuredRisk.maximumIndustryWeight) > 0
                                     ? "行业约束已配置；运行时将校验首帧日期的完整覆盖"
                                     : "行业目标约束关闭")
                            color: Number(portfolioRiskSummaryCard.resultRisk.industryLimitBreachFrameCount) > 0
                                   ? root.theme.yellow : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "目标上限只约束调仓目标，价格漂移可能使实际行业权重暂时越限"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: String(portfolioRiskSummaryCard.resultRisk.factorExposureAlias || "").length > 0
                                  ? "因子暴露 " + String(portfolioRiskSummaryCard.resultRisk.factorExposureAlias)
                                    + " · 目标峰值 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.maximumRequestedAbsoluteFactorExposure).toFixed(3)
                                    + " Z · 实际峰值 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.maximumObservedAbsoluteFactorExposure).toFixed(3)
                                    + " Z · 裁剪 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.factorExposureConstrainedFrameCount)
                                    + " 帧 · 未测 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.factorExposureUnmeasuredFrameCount)
                                    + " 帧 · 漂移越限 "
                                    + Number(portfolioRiskSummaryCard.resultRisk.factorExposureLimitBreachFrameCount)
                                    + " 帧"
                                  : (Number(portfolioRiskSummaryCard.configuredRisk.maximumAbsoluteFactorExposure) > 0
                                     ? "因子暴露约束已配置；运行时固定策略内因子定义与标准化口径"
                                     : "策略内因子暴露约束关闭")
                            color: Number(portfolioRiskSummaryCard.resultRisk.factorExposureUnmeasuredFrameCount) > 0
                                   || Number(portfolioRiskSummaryCard.resultRisk.factorExposureLimitBreachFrameCount) > 0
                                   ? root.theme.yellow : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "因子缺失时不前向填充、不伪造覆盖；本功能不执行优化器、行业中性化或多因子协方差约束"
                        }
                        Text {
                            Layout.fillWidth: true
                            readonly property var summary: portfolioRiskSummaryCard.factorResearch.summary || ({})
                            text: Number(summary.evaluatedFrameCount) > 0
                                  ? "因子研究 " + String(summary.factorAlias)
                                    + " v" + Number(summary.factorVersion)
                                    + " · IC " + Number(summary.meanIc).toFixed(4)
                                    + " · Rank IC " + Number(summary.meanRankIc).toFixed(4)
                                    + " · 覆盖 " + (Number(summary.coverageRatio) * 100).toFixed(1)
                                    + "% · 有效帧 " + Number(summary.evaluatedFrameCount)
                                  : (String(portfolioRiskSummaryCard.configuredRisk.factorResearchAlias || "").length > 0
                                     ? "因子研究已配置；完成回测后显示下一对齐帧 IC / Rank IC"
                                     : "因子研究关闭")
                            color: Number(summary.evaluatedFrameCount) > 0
                                   ? root.theme.cyan : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "IC 使用当前帧因子与下一可用对齐帧收盘收益；缺失值按股票成对剔除"
                        }
                    }
                }

                Rectangle {
                    id: factorResearchResultCard
                    objectName: "factorResearchResultCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 274
                    radius: 8
                    color: root.theme.bgCanvas
                    border.color: Number(summary.evaluatedFrameCount) > 0
                                  ? root.theme.cyan : root.theme.border
                    readonly property var identity: (root.controller.crossSectionalResultConfiguration
                                                     || ({})).factorResearch || ({})
                    readonly property var summary: identity.summary || ({})
                    readonly property var series: summary.timeSeries || []
                    readonly property var quantiles: summary.quantileReturns || []
                    readonly property var segments: summary.temporalSegments || []
                    function segmentMeans(key) {
                        if (!summary.temporalStabilityAvailable || segments.length !== 3)
                            return "有效帧不足 9，暂不分段"
                        return "S1 " + Number(segments[0][key]).toFixed(3)
                                + " · S2 " + Number(segments[1][key]).toFixed(3)
                                + " · S3 " + Number(segments[2][key]).toFixed(3)
                    }
                    onSummaryChanged: {
                        icSeriesCanvas.requestPaint()
                        quantileCanvas.requestPaint()
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                labelText: "因子研究 · 下一对齐帧"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: Number(factorResearchResultCard.summary.evaluatedFrameCount) > 0
                                      ? "ICIR "
                                        + (factorResearchResultCard.summary.icirAvailable
                                           ? Number(factorResearchResultCard.summary.icInformationRatio).toFixed(3)
                                           : "--")
                                        + " · Rank ICIR "
                                        + (factorResearchResultCard.summary.rankIcirAvailable
                                           ? Number(factorResearchResultCard.summary.rankIcInformationRatio).toFixed(3)
                                           : "--")
                                      : "尚无真实结果"
                                color: Number(factorResearchResultCard.summary.evaluatedFrameCount) > 0
                                       ? root.theme.cyan : root.theme.textMuted
                                font.family: root.theme.fontFamily
                                font.pixelSize: 9
                                Accessible.name: text
                                Accessible.description: "ICIR 为均值除以样本标准差，不做年化"
                            }
                            PpButton {
                                theme: root.theme
                                text: root.controller.crossSectionalReportExporting
                                      ? "保存中…" : "导出报告"
                                visualWidth: 76
                                available: !root.controller.crossSectionalReportExporting
                                           && root.controller.selectedCrossSectionalArchiveId.length > 0
                                           && Number(factorResearchResultCard.summary.evaluatedFrameCount) > 0
                                toolTip: available
                                         ? "将当前已归档因子研究快照异步保存为本地 Markdown；不联网、不重跑"
                                         : root.controller.crossSectionalReportExporting
                                           ? "报告正在后台原子保存"
                                           : "请先加载一个包含真实因子研究结果的归档"
                                Accessible.name: "导出已归档因子研究报告"
                                Accessible.description: toolTip
                                onClicked: factorReportDialog.open()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.controller.crossSectionalReportExporting
                                     || root.controller.crossSectionalReportExportError.length > 0
                                     || root.controller.crossSectionalReportExportPath.length > 0
                            text: root.controller.crossSectionalReportExportError.length > 0
                                  ? root.controller.crossSectionalReportExportError
                                  : root.controller.crossSectionalReportExportStatusText
                                    + (root.controller.crossSectionalReportExportPath.length > 0
                                       ? " · " + root.controller.crossSectionalReportExportPath : "")
                            color: root.controller.crossSectionalReportExportError.length > 0
                                   ? root.theme.red
                                   : root.controller.crossSectionalReportExporting
                                     ? root.theme.yellow : root.theme.green
                            elide: Text.ElideMiddle
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }

                        Text {
                            Layout.fillWidth: true
                            readonly property bool hacIdentity:
                                ["factor_research_v5", "factor_research_v6"]
                                    .indexOf(String(factorResearchResultCard.identity.researchMethodVersion)) >= 0
                            text: !hacIdentity
                                  ? "HAC 统计：当前结果未包含 v5/v6 推断身份"
                                  : Number(factorResearchResultCard.summary.inferenceFrameCount) < 20
                                    ? "HAC 统计：有效帧 "
                                      + Number(factorResearchResultCard.summary.inferenceFrameCount)
                                      + " / 20，不提供区间与 p 值"
                                    : factorResearchResultCard.summary.icInferenceAvailable
                                      ? "IC 95% HAC CI ["
                                        + Number(factorResearchResultCard.summary.icConfidenceLower).toFixed(4)
                                        + ", "
                                        + Number(factorResearchResultCard.summary.icConfidenceUpper).toFixed(4)
                                        + "] · 双侧 p "
                                        + Number(factorResearchResultCard.summary.icTwoSidedPValue).toFixed(4)
                                        + (factorResearchResultCard.summary.rankIcInferenceAvailable
                                           ? " · Rank p "
                                             + Number(factorResearchResultCard.summary.rankIcTwoSidedPValue).toFixed(4)
                                           : " · Rank 区间不可估计")
                                      : "HAC 统计：序列方差不足，区间与 p 值不可估计"
                            color: root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "Newey-West Bartlett 相关稳健标准误；置信区间和 p 值不代表因果或样本外有效"
                        }

                        Text {
                            Layout.fillWidth: true
                            readonly property var oos:
                                factorResearchResultCard.summary.outOfSample || ({})
                            text: String(factorResearchResultCard.identity.researchMethodVersion)
                                  !== "factor_research_v6"
                                  ? "样本外：旧归档未包含扩展窗验证身份"
                                  : !oos.available
                                    ? "样本外：" + (oos.availabilityReason
                                                      || "完整帧不足 30，无法形成首个测试窗")
                                    : "样本外扩展窗 " + (oos.windows || []).length
                                      + " · 测试 " + Number(oos.evaluationFrameCount) + " 帧"
                                      + " · IC " + Number(oos.meanIc).toFixed(4)
                                      + (oos.icInferenceAvailable
                                         ? " · HAC 95% ["
                                           + Number(oos.icConfidenceLower).toFixed(4)
                                           + ", " + Number(oos.icConfidenceUpper).toFixed(4)
                                           + "] · p " + Number(oos.icTwoSidedPValue).toFixed(4)
                                         : " · HAC 推断不可用")
                            color: oos.available ? root.theme.cyan : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "扩展训练窗 20 帧，固定且不重叠测试窗 10 帧；只使用时间上更晚的测试帧"
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "样本内三段稳定性 · IC "
                                  + factorResearchResultCard.segmentMeans("meanIc")
                                  + (factorResearchResultCard.summary.temporalStabilityAvailable
                                     ? (factorResearchResultCard.summary.icSameSignAcrossSegments
                                        ? " · 同号" : " · 存在变号") : "")
                            color: factorResearchResultCard.summary.temporalStabilityAvailable
                                   && !factorResearchResultCard.summary.icSameSignAcrossSegments
                                   ? root.theme.yellow : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "三个连续区段均来自同一回测样本，不是样本外验证"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 8
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 5
                                color: root.theme.bgInput
                                border.color: root.theme.border
                                Canvas {
                                    id: icSeriesCanvas
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    Accessible.name: "IC 与 Rank IC 时间序列图"
                                    Accessible.description: "青色为 IC，紫色为 Rank IC；仅绘制真实有效帧"
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.strokeStyle = root.theme.border
                                        ctx.lineWidth = 1
                                        ctx.beginPath()
                                        ctx.moveTo(0, height / 2)
                                        ctx.lineTo(width, height / 2)
                                        ctx.stroke()
                                        const values = factorResearchResultCard.series
                                        if (!values || values.length === 0)
                                            return
                                        function drawSeries(key, strokeColor) {
                                            ctx.strokeStyle = strokeColor
                                            ctx.lineWidth = 1.5
                                            ctx.beginPath()
                                            for (let index = 0; index < values.length; ++index) {
                                                const value = Math.max(-1, Math.min(1,
                                                    Number(values[index][key] || 0)))
                                                const px = values.length > 1
                                                    ? index * width / (values.length - 1) : width / 2
                                                const py = (1 - (value + 1) / 2) * height
                                                if (index === 0) ctx.moveTo(px, py)
                                                else ctx.lineTo(px, py)
                                            }
                                            ctx.stroke()
                                        }
                                        drawSeries("ic", root.theme.cyan)
                                        drawSeries("rankIc", root.theme.purple)
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: factorResearchResultCard.series.length === 0
                                    text: "完成启用因子研究的真实多股回测后显示 IC 序列"
                                    color: root.theme.textMuted
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 8
                                    Accessible.name: text
                                }
                            }
                            Rectangle {
                                Layout.preferredWidth: Math.min(286,
                                                                factorResearchResultCard.width * 0.36)
                                Layout.fillHeight: true
                                radius: 5
                                color: root.theme.bgInput
                                border.color: root.theme.border
                                Canvas {
                                    id: quantileCanvas
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    Accessible.name: "因子 Q1 到 Q5 分层收益图"
                                    Accessible.description: "每层先帧内等权，再跨帧等权；高减低为 Q5 减 Q1"
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        const values = factorResearchResultCard.quantiles
                                        if (!values || values.length === 0)
                                            return
                                        let maximum = 0
                                        for (let index = 0; index < values.length; ++index)
                                            maximum = Math.max(maximum,
                                                Math.abs(Number(values[index].meanForwardReturn || 0)))
                                        maximum = Math.max(maximum, 0.000001)
                                        const baseline = height / 2
                                        ctx.strokeStyle = root.theme.border
                                        ctx.lineWidth = 1
                                        ctx.beginPath()
                                        ctx.moveTo(0, baseline)
                                        ctx.lineTo(width, baseline)
                                        ctx.stroke()
                                        const gap = 4
                                        const barWidth = Math.max(2,
                                            (width - gap * (values.length + 1)) / values.length)
                                        for (let index = 0; index < values.length; ++index) {
                                            const value = Number(values[index].meanForwardReturn || 0)
                                            const barHeight = Math.abs(value) / maximum
                                                * Math.max(1, baseline - 5)
                                            ctx.fillStyle = value >= 0
                                                ? root.theme.green : root.theme.red
                                            ctx.fillRect(gap + index * (barWidth + gap),
                                                value >= 0 ? baseline - barHeight : baseline,
                                                barWidth, barHeight)
                                        }
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: factorResearchResultCard.quantiles.length === 0
                                    text: "Q1–Q5 --"
                                    color: root.theme.textMuted
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 8
                                    Accessible.name: text
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: Number(factorResearchResultCard.summary.topMinusBottomFrameCount) > 0
                                  ? "Q5−Q1 毛 "
                                    + (Number(factorResearchResultCard.summary.topMinusBottomMeanReturn) * 100).toFixed(3)
                                    + "% · 成本后 "
                                    + (Number(factorResearchResultCard.summary.topMinusBottomMeanNetReturn) * 100).toFixed(3)
                                    + "% · 五分层帧 "
                                    + Number(factorResearchResultCard.summary.topMinusBottomFrameCount)
                                    + " · IC 序列 " + factorResearchResultCard.series.length
                                  : "固定五分层；至少 5 个有效样本才计算，空值不伪造为 0 收益"
                            color: Number(factorResearchResultCard.summary.topMinusBottomFrameCount) > 0
                                   ? root.theme.textPrimary : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Number(factorResearchResultCard.summary.turnoverObservationCount) > 0
                                  ? "Q5/Q1 合计单边换手 "
                                    + (Number(factorResearchResultCard.summary.topMinusBottomMeanOneWayTurnover) * 100).toFixed(2)
                                    + "% · 换手观测 "
                                    + Number(factorResearchResultCard.summary.turnoverObservationCount)
                                    + " · 单边成本 "
                                    + Number((factorResearchResultCard.summary.costPolicy || ({})).oneWayCostBps || 0).toFixed(2)
                                    + " bps"
                                  : "换手需要至少两个有效五分层帧；首帧建仓明确排除"
                            color: Number(factorResearchResultCard.summary.turnoverObservationCount) > 0
                                   ? root.theme.cyan : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                            Accessible.description: "单边换手为相邻分层目标权重变化绝对值之和的一半"
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (factorResearchResultCard.summary.neutralizationPolicy || ({})).method
                                  === "within_group_demean"
                                  ? "行业内去均值 · 有效帧 "
                                    + Number(factorResearchResultCard.summary.neutralizationAppliedFrameCount || 0)
                                    + " · 中性化样本 "
                                    + Number(factorResearchResultCard.summary.neutralizedPairCount || 0)
                                    + " · 小组剔除 "
                                    + Number(factorResearchResultCard.summary.smallGroupDroppedPairCount || 0)
                                  : "行业中性化未启用；当前统计为全市场原始因子暴露"
                            color: (factorResearchResultCard.summary.neutralizationPolicy || ({})).method
                                   === "within_group_demean"
                                   ? root.theme.purple : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                    }
                }

                Rectangle {
                    id: archiveCard
                    objectName: "crossSectionalArchiveCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 154
                    radius: 10
                    color: root.theme.bgCanvas
                    border.color: root.controller.crossSectionalResultHistoricalSnapshot
                                  ? root.theme.cyan : root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                labelText: "多股回测归档（"
                                           + root.controller.crossSectionalArchiveCount
                                           + "）"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: root.controller.crossSectionalResultHistoricalSnapshot
                                      ? "历史快照 · 未重新运行" : "SQLite 本地"
                                color: root.controller.crossSectionalResultHistoricalSnapshot
                                       ? root.theme.cyan : root.theme.textMuted
                                font.family: root.theme.fontFamily
                                font.pixelSize: 9
                                Accessible.name: text
                            }
                            PpButton {
                                theme: root.theme
                                text: "刷新"
                                visualWidth: 58
                                available: !root.controller.crossSectionalArchiveLoading
                                toolTip: available ? "重新读取本地多股回测归档列表"
                                                   : "归档正在加载"
                                onClicked: root.controller.refreshCrossSectionalBacktestArchiveList()
                            }
                        }
                        ListView {
                            id: archiveList
                            objectName: "crossSectionalArchiveList"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            model: root.controller.crossSectionalArchiveModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property string resultId
                                required property string name
                                required property string strategyId
                                required property int strategyVersion
                                required property var completedAt
                                required property int legCount
                                required property double totalReturnPercent
                                width: ListView.view.width
                                height: 34
                                radius: 5
                                color: resultId === root.controller.selectedCrossSectionalArchiveId
                                       ? root.theme.bgSelected : root.theme.bgInput
                                border.color: resultId === root.controller.selectedCrossSectionalArchiveId
                                              ? root.theme.cyan : root.theme.border
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 7
                                    anchors.rightMargin: 5
                                    spacing: 7
                                    Text {
                                        Layout.preferredWidth: 104
                                        text: Qt.formatDateTime(completedAt, "MM-dd HH:mm")
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: name + " · " + strategyId + " v" + strategyVersion
                                              + " · " + legCount + " 股"
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 9
                                    }
                                    Text {
                                        Layout.preferredWidth: 62
                                        text: Number(totalReturnPercent).toFixed(2) + "%"
                                        color: totalReturnPercent >= 0
                                               ? root.theme.green : root.theme.red
                                        horizontalAlignment: Text.AlignRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 9
                                    }
                                    PpButton {
                                        theme: root.theme
                                        text: "加载"
                                        visualWidth: 54
                                        available: !root.controller.crossSectionalArchiveLoading
                                        toolTip: "仅从 SQLite 恢复权益、成交与不可变身份，不联网、不重跑"
                                        Accessible.name: "加载多股历史归档 " + resultId
                                        onClicked: root.controller.loadCrossSectionalBacktestArchive(resultId)
                                    }
                                    PpButton {
                                        readonly property bool selectedForComparison:
                                            root.controller.crossSectionalFactorComparisonCount >= 0
                                            && root.controller.isCrossSectionalFactorComparisonSelected(resultId)
                                        theme: root.theme
                                        text: selectedForComparison ? "移出" : "对比"
                                        visualWidth: 54
                                        available: !root.controller.crossSectionalArchiveLoading
                                        toolTip: selectedForComparison
                                                 ? "从因子研究并排对照中移除"
                                                 : "从 SQLite 读取不可变因子研究身份；不联网、不重跑"
                                        Accessible.name: (selectedForComparison ? "移出因子对比 "
                                                                                 : "加入因子对比 ")
                                                         + resultId
                                        onClicked: root.controller.toggleCrossSectionalFactorComparison(resultId)
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.controller.crossSectionalArchiveCount === 0
                                     || root.controller.crossSectionalArchiveError.length > 0
                            text: root.controller.crossSectionalArchiveError.length > 0
                                  ? root.controller.crossSectionalArchiveError
                                  : "尚无多股归档；成功回测且身份校验通过后自动保存。"
                            color: root.controller.crossSectionalArchiveError.length > 0
                                   ? root.theme.red : root.theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                            Accessible.name: text
                        }
                    }
                }

                Rectangle {
                    id: factorResearchComparisonCard
                    objectName: "factorResearchComparisonCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 226
                    radius: 10
                    color: root.theme.bgCanvas
                    border.color: root.controller.crossSectionalFactorComparisonError.length > 0
                                  ? root.theme.red : root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                labelText: "因子研究归档对比（"
                                           + root.controller.crossSectionalFactorComparisonCount
                                           + "/4）"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "首项为基准 · 仅 SQLite"
                                color: root.theme.textMuted
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                                Accessible.name: text
                            }
                            PpButton {
                                theme: root.theme
                                text: "清空"
                                visualWidth: 54
                                available: root.controller.crossSectionalFactorComparisonCount > 0
                                toolTip: available ? "清空当前因子研究归档对比" : "尚未选择归档"
                                Accessible.name: "清空因子研究归档对比"
                                onClicked: root.controller.clearCrossSectionalFactorComparison()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.crossSectionalFactorComparisonError.length > 0
                                  ? root.controller.crossSectionalFactorComparisonError
                                  : "同周期、数据与研究身份才标记受控可比；"
                                    + root.controller.crossSectionalMultipleTestingStatusText
                            color: root.controller.crossSectionalFactorComparisonError.length > 0
                                   ? root.theme.red : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                        ListView {
                            id: factorComparisonList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            orientation: ListView.Horizontal
                            spacing: 6
                            clip: true
                            model: root.controller.crossSectionalFactorComparisonModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property string resultId
                                required property string name
                                required property var completedAt
                                required property string factorId
                                required property int factorVersion
                                required property string factorAlias
                                required property int evaluatedFrameCount
                                required property double coverageRatio
                                required property double meanIc
                                required property double meanRankIc
                                required property double icInformationRatio
                                required property bool icirAvailable
                                required property bool icInferenceAvailable
                                required property double icConfidenceLower
                                required property double icConfidenceUpper
                                required property double icTwoSidedPValue
                                required property bool temporalStabilityAvailable
                                required property bool icSameSignAcrossSegments
                                required property double topMinusBottomMeanReturn
                                required property double topMinusBottomMeanNetReturn
                                required property bool controlledComparable
                                required property string comparabilityLabel
                                required property string comparabilityReason
                                required property bool outOfSampleAvailable
                                required property int outOfSampleEvaluationFrameCount
                                required property int outOfSampleWindowCount
                                required property double outOfSampleMeanIc
                                required property bool outOfSampleIcInferenceAvailable
                                required property double outOfSampleIcConfidenceLower
                                required property double outOfSampleIcConfidenceUpper
                                required property double outOfSampleIcTwoSidedPValue
                                required property bool multipleTestingEligible
                                required property double holmAdjustedPValue
                                required property double bhAdjustedPValue
                                required property bool holmRejected
                                required property bool bhRejected
                                width: 246
                                height: ListView.view.height - 2
                                radius: 6
                                color: root.theme.bgInput
                                border.color: controlledComparable
                                              ? root.theme.cyan : root.theme.yellow
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            Layout.fillWidth: true
                                            text: factorAlias + " · v" + factorVersion
                                            color: root.theme.textPrimary
                                            elide: Text.ElideRight
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: 9
                                            font.bold: true
                                        }
                                        Text {
                                            text: comparabilityLabel
                                            color: controlledComparable
                                                   ? root.theme.cyan : root.theme.yellow
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: 8
                                            Accessible.name: comparabilityLabel + "：" + comparabilityReason
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "IC " + Number(meanIc).toFixed(4)
                                              + " · Rank " + Number(meanRankIc).toFixed(4)
                                              + " · ICIR "
                                              + (icirAvailable
                                                 ? Number(icInformationRatio).toFixed(3) : "--")
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "Q5−Q1 毛 "
                                              + (Number(topMinusBottomMeanReturn) * 100).toFixed(3)
                                              + "% · 净 "
                                              + (Number(topMinusBottomMeanNetReturn) * 100).toFixed(3) + "%"
                                        color: root.theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: icInferenceAvailable
                                              ? "HAC 95% ["
                                                + Number(icConfidenceLower).toFixed(3)
                                                + ", " + Number(icConfidenceUpper).toFixed(3)
                                                + "] · p "
                                                + Number(icTwoSidedPValue).toFixed(3)
                                                + (temporalStabilityAvailable
                                                   ? (icSameSignAcrossSegments
                                                      ? " · 三段同号" : " · 三段变号") : "")
                                              : "HAC 区间不可用"
                                        color: temporalStabilityAvailable
                                               && !icSameSignAcrossSegments
                                               ? root.theme.yellow : root.theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                        Accessible.name: text
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: !outOfSampleAvailable
                                              ? "样本外：不可用"
                                              : "样本外 IC " + Number(outOfSampleMeanIc).toFixed(3)
                                                + " · " + outOfSampleWindowCount + " 窗 / "
                                                + outOfSampleEvaluationFrameCount + " 帧"
                                                + (outOfSampleIcInferenceAvailable
                                                   ? " · p "
                                                     + Number(outOfSampleIcTwoSidedPValue).toFixed(3)
                                                   : "")
                                        color: outOfSampleAvailable
                                               ? root.theme.cyan : root.theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                        Accessible.name: text
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: !multipleTestingEligible
                                              ? "多重校正：未进入候选集"
                                              : root.controller.crossSectionalMultipleTestingCandidateCount < 2
                                                ? "多重校正：需至少 2 个候选"
                                                : "Holm " + Number(holmAdjustedPValue).toFixed(3)
                                                  + (holmRejected ? " ✓" : "")
                                                  + " · BH " + Number(bhAdjustedPValue).toFixed(3)
                                                  + (bhRejected ? " ✓" : "")
                                        color: (holmRejected || bhRejected)
                                               ? root.theme.green : root.theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                        Accessible.name: text
                                        Accessible.description: "仅对当前显式选择且受控可比的样本外 IC p 值执行校正"
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: "覆盖 " + (Number(coverageRatio) * 100).toFixed(1)
                                              + "% · " + evaluatedFrameCount + " 帧 · "
                                              + Qt.formatDateTime(completedAt, "MM-dd HH:mm")
                                        color: root.theme.textMuted
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: comparabilityReason
                                        color: controlledComparable
                                               ? root.theme.textMuted : root.theme.yellow
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 8
                                    }
                                    Item { Layout.fillHeight: true }
                                    PpButton {
                                        theme: root.theme
                                        text: "移除"
                                        visualWidth: 52
                                        Layout.alignment: Qt.AlignRight
                                        toolTip: "移出当前因子研究归档对比"
                                        Accessible.name: "移出因子对比 " + factorId
                                        onClicked: root.controller.toggleCrossSectionalFactorComparison(resultId)
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: root.controller.crossSectionalFactorComparisonCount === 0
                            text: "从上方归档选择 2–4 个带因子研究身份的结果；当前不会重跑策略或合并结果。"
                            color: root.theme.textMuted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                            Accessible.name: text
                        }
                    }
                }

                Rectangle {
                    id: factorCandidateGovernanceCard
                    objectName: "factorCandidateGovernanceCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.compactLayout ? 390 : 320
                    radius: 10
                    color: root.theme.bgCanvas
                    border.color: root.controller.factorCandidateGovernanceError.length > 0
                                  ? root.theme.red : root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                labelText: "候选因子预登记与验证"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "仅本地 SQLite · 不自动扫描"
                                color: root.theme.textMuted
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                                Accessible.name: text
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.factorCandidateGovernanceError.length > 0
                                  ? root.controller.factorCandidateGovernanceError
                                  : root.controller.factorCandidateGovernanceStatusText
                            color: root.controller.factorCandidateGovernanceError.length > 0
                                   ? root.theme.red : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                            Accessible.name: text
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.compactLayout ? 1 : 2
                            columnSpacing: 8
                            rowSpacing: 5
                            TextField {
                                id: factorCandidateIdField
                                Layout.fillWidth: true
                                placeholderText: "候选 ID，例如 candidate.momentum"
                                color: root.theme.textPrimary
                                placeholderTextColor: root.theme.textMuted
                                selectByMouse: true
                                Accessible.name: "候选因子 ID"
                                background: Rectangle {
                                    radius: 5; color: root.theme.bgInput
                                    border.color: factorCandidateIdField.activeFocus
                                                  ? root.theme.cyan : root.theme.border
                                }
                            }
                            TextField {
                                id: factorCandidateNameField
                                Layout.fillWidth: true
                                placeholderText: "候选名称"
                                color: root.theme.textPrimary
                                placeholderTextColor: root.theme.textMuted
                                selectByMouse: true
                                Accessible.name: "候选因子名称"
                                background: Rectangle {
                                    radius: 5; color: root.theme.bgInput
                                    border.color: factorCandidateNameField.activeFocus
                                                  ? root.theme.cyan : root.theme.border
                                }
                            }
                            TextField {
                                id: factorCandidateHypothesisField
                                Layout.fillWidth: true
                                placeholderText: "事前假设（冻结后不可修改）"
                                color: root.theme.textPrimary
                                placeholderTextColor: root.theme.textMuted
                                selectByMouse: true
                                Accessible.name: "候选因子事前假设"
                                background: Rectangle {
                                    radius: 5; color: root.theme.bgInput
                                    border.color: factorCandidateHypothesisField.activeFocus
                                                  ? root.theme.cyan : root.theme.border
                                }
                            }
                            ComboBox {
                                id: factorDefinitionCombo
                                Layout.fillWidth: true
                                model: root.controller.crossSectionalFactorDefinitionCatalog
                                textRole: "name"
                                Accessible.name: "候选因子不可变定义"
                                Accessible.description: "只使用已经注册的本地因子定义版本"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: factorDefinitionCombo.count > 0
                                      ? "来源：人工登记 · 定义来自已注册因子目录"
                                      : "尚无已注册因子定义，不能登记候选"
                                color: factorDefinitionCombo.count > 0
                                       ? root.theme.textMuted : root.theme.yellow
                                elide: Text.ElideRight
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                            }
                            PpButton {
                                theme: root.theme
                                text: "登记版本"
                                visualWidth: 78
                                available: factorDefinitionCombo.currentIndex >= 0
                                           && factorCandidateIdField.text.trim().length > 0
                                           && factorCandidateNameField.text.trim().length > 0
                                           && factorCandidateHypothesisField.text.trim().length > 0
                                toolTip: available
                                         ? "登记不可变候选版本；相同内容会复用现有版本"
                                         : "请填写 ID、名称、假设并选择已注册因子定义"
                                Accessible.name: "登记候选因子版本"
                                onClicked: {
                                    const selectedDefinition =
                                        root.controller.crossSectionalFactorDefinitionCatalog[
                                            factorDefinitionCombo.currentIndex]
                                    root.controller.registerFactorCandidate(
                                        factorCandidateIdField.text,
                                        factorCandidateNameField.text,
                                        factorCandidateHypothesisField.text,
                                        "manual", "",
                                        selectedDefinition.definition)
                                }
                            }
                        }
                        ListView {
                            id: factorCandidateList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            orientation: ListView.Horizontal
                            spacing: 6
                            clip: true
                            model: root.controller.factorCandidateCatalog
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property var modelData
                                width: 220
                                height: 66
                                radius: 6
                                color: root.controller.selectedFactorCandidateId
                                       === modelData.candidateId
                                       && root.controller.selectedFactorCandidateVersion
                                          === modelData.version
                                       ? root.theme.bgHover : root.theme.bgInput
                                border.color: modelData.frozen
                                              ? root.theme.green : root.theme.yellow
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 5
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.name + " · v" + modelData.version
                                            color: root.theme.textPrimary
                                            elide: Text.ElideRight
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: 9
                                            font.bold: true
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: modelData.frozen ? "已冻结" : "待冻结"
                                            color: modelData.frozen
                                                   ? root.theme.green : root.theme.yellow
                                            font.family: root.theme.fontFamily
                                            font.pixelSize: 8
                                        }
                                    }
                                    PpButton {
                                        theme: root.theme
                                        text: "选择"
                                        visualWidth: 48
                                        Accessible.name: "选择候选 " + modelData.name
                                        onClicked: root.controller.selectFactorCandidate(
                                                       modelData.candidateId,
                                                       modelData.version)
                                    }
                                    PpButton {
                                        theme: root.theme
                                        text: "冻结"
                                        visualWidth: 48
                                        available: !modelData.frozen
                                        toolTip: available
                                                 ? "冻结后内容和冻结时间都不可修改"
                                                 : "该候选版本已经冻结"
                                        Accessible.name: "冻结候选 " + modelData.name
                                        onClicked: root.controller.freezeFactorCandidate(
                                                       modelData.candidateId,
                                                       modelData.version)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: root.controller.selectedFactorCandidateId.length > 0
                                      ? "当前 " + root.controller.selectedFactorCandidateId
                                        + " v" + root.controller.selectedFactorCandidateVersion
                                        + " · 已关联 "
                                        + root.controller.factorCandidateEvaluationCatalog.length
                                        + " 条验证"
                                      : "选择候选版本后，可关联当前明确加载的归档"
                                color: root.theme.textMuted
                                elide: Text.ElideRight
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                                Accessible.name: text
                            }
                            PpButton {
                                theme: root.theme
                                text: "回溯参考"
                                visualWidth: 76
                                available: root.controller.selectedFactorCandidateId.length > 0
                                           && root.controller.selectedCrossSectionalArchiveId.length > 0
                                toolTip: available
                                         ? "关联旧归档，但明确不计入预注册证据"
                                         : "请先选择候选版本并加载归档"
                                Accessible.name: "关联为回溯参考"
                                onClicked: root.controller.linkSelectedArchiveToFactorCandidate(false)
                            }
                            PpButton {
                                theme: root.theme
                                text: "前瞻验证"
                                visualWidth: 76
                                available: root.controller.selectedFactorCandidateId.length > 0
                                           && root.controller.selectedCrossSectionalArchiveId.length > 0
                                toolTip: available
                                         ? "仅冻结后完成的 v6 样本外归档可计入预注册证据"
                                         : "请先选择候选版本并加载归档"
                                Accessible.name: "关联为前瞻验证"
                                onClicked: root.controller.linkSelectedArchiveToFactorCandidate(true)
                            }
                        }
                    }
                }

                Rectangle {
                    id: authorizedFactorImportCard
                    objectName: "authorizedFactorImportCard"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.compactLayout ? 448 : 398
                    radius: 10
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                labelText: "授权外部因子数据集"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: "CSV · 异步预览/确认 · 仅本地 SQLite"
                                color: root.theme.textMuted
                                font.family: root.theme.fontFamily
                                font.pixelSize: 8
                                Accessible.name: text
                            }
                        }
                        AuthorizedFactorImportPanel {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            theme: root.theme
                            controller: root.controller
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: root.compactLayout ? 210 : 270
                    radius: 10; color: root.theme.bgCanvas; border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { labelText: "权益曲线"; Layout.fillWidth: true }
                            readonly property var config: root.controller.crossSectionalResultConfiguration
                            Text {
                                text: parent.config && parent.config.finalEquity
                                      ? "收益 " + Number(parent.config.totalReturnPercent).toFixed(2) + "%"
                                        + " · 回撤 " + Number(parent.config.maxDrawdownPercent).toFixed(2) + "%"
                                        + (parent.config.historicalSnapshot ? " · 历史快照" : "")
                                      : "尚无真实结果"
                                color: parent.config && parent.config.finalEquity ? root.theme.green : root.theme.textMuted
                                font.family: root.theme.fontFamily; font.pixelSize: 9
                            }
                        }
                        BacktestEquityChart {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            theme: root.theme
                            model: root.controller.crossSectionalEquityCurveModel
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 240
                    radius: 10; color: root.theme.bgCanvas; border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle { labelText: "调仓 / 成交记录"; Layout.fillWidth: true }
                            Text {
                                text: "共 " + root.controller.crossSectionalTradeModel.count + " 笔"
                                color: root.theme.textMuted
                                font.family: root.theme.fontFamily; font.pixelSize: 9
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.preferredWidth: 74; text: "时间"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { Layout.preferredWidth: 42; text: "动作"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { Layout.preferredWidth: 56; text: "代码"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { Layout.preferredWidth: 92; text: "数量 / 价格"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { Layout.fillWidth: true; text: "原因"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                        }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            model: root.controller.crossSectionalTradeModel
                            spacing: 3; clip: true; boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property string symbol
                                required property string side
                                required property double quantity
                                required property double price
                                required property double commission
                                required property double amount
                                required property var timestamp
                                required property string reason
                                width: ListView.view.width; height: 28; radius: 4
                                color: root.theme.bgInput; border.color: root.theme.border
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 6
                                    Text { Layout.preferredWidth: 74; text: Qt.formatDateTime(timestamp, "MM-dd HH:mm"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                                    Text { Layout.preferredWidth: 42; text: side === "buy" ? "买入" : "卖出"; color: side === "buy" ? root.theme.green : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                                    Text { Layout.preferredWidth: 56; text: symbol; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                                    Text { Layout.preferredWidth: 92; text: quantity + " @" + Number(price).toFixed(2); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                                    Text { Layout.fillWidth: true; text: reason + " · 费 " + Number(commission).toFixed(2); color: root.theme.textMuted; elide: Text.ElideRight; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.controller.crossSectionalTradeModel.count === 0
                            text: "完成真实本地多股回测后显示调仓成交；当前不生成示例记录。"
                            color: root.theme.textMuted; horizontalAlignment: Text.AlignHCenter
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                    }
                }
                }
            }
        }
    }

    Component.onCompleted: {
        root.refreshCatalog()
        root.syncConditionAliases()
    }

    Connections {
        target: root.controller
        function onLocalBacktestDataCatalogChanged() { root.refreshCatalog() }
    }
}
