// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// Settings 自 Qt 6.5 起由 QtCore 提供；Qt.labs.settings 已弃用，在 6.8+ 每次加载都会
// 打印告警。项目最低版本随之为 Qt 6.5，Qt 6.4.3 支持已退役。
import QtCore
import "CustomStrategyWizardContract.js" as WizardContract

Item {
    id: root
    objectName: "customStrategyWizard"
    required property var theme
    required property var controller
    readonly property bool compactLayout: width < 1360
    property bool compactPreviewVisible: false
    onActiveStepChanged: compactPreviewVisible = false
    onCompactLayoutChanged: compactPreviewVisible = false

    signal requestOpenJson(string json)
    signal requestClose()

    property string draftStrategyId: "custom_wizard_draft"
    property string draftName: "多指标趋势策略"
    property string draftDescription: "由策略创建向导生成，仅用于本地历史回测"
    property string draftAuthor: "本地用户"
    property string statusText: "草稿已保存"
    property string previewError: ""
    property var previewData: ({})
    property int activeConditionSide: 0
    property var entryConditionTree: ({ op: "and", conditions: [] })
    property var exitConditionTree: ({ op: "and", conditions: [] })
    property int conditionTreeRevision: 0
    property int activeStep: 1
    property string positionType: "fixedPercent"
    property string positionAmount: "50000"
    property string positionPercent: "100"
    property string maxPositionPercent: "30"
    property string stopLossPercent: "0"
    property string takeProfitPercent: "0"
    property string maximumHoldingBars: "0"
    property string signalPriceMode: "triggerClose"
    property bool previewDirty: false
    property var workflowValidation: ({ valid: false, error: "尚未校验" })
    property var workflowAnalysis: ({ executable: false, error: "尚未分析" })
    property var workflowRunAvailability: ({ available: false, error: "请先保存策略版本" })
    property string savedStrategyId: ""
    property int savedStrategyVersion: 0
    property string savedDraftSignature: ""
    property string workflowMessage: "尚未校验"
    property double workflowTaskId: 0
    property string workflowSampleFrom: ""
    property string workflowSampleTo: ""
    property string workflowReplayIndex: "0"
    property var workflowReplayData: ({})
    property double pendingPreviewRequestId: 0
    property double pendingSaveRequestId: 0
    property string pendingSaveStrategyId: ""
    property int pendingSaveVersion: 0
    property string pendingSaveDraftSignature: ""
    property int selectedIndicatorIndex: -1
    property int indicatorEditRevision: 0
    readonly property bool workflowResultMatches: root.savedStrategyId.length > 0
        && root.savedStrategyVersion > 0
        && root.controller.backtestResultConfiguration !== undefined
        && root.controller.backtestResultMatchesCustomStrategy(
            root.savedStrategyId, root.savedStrategyVersion)

    readonly property var catalog: root.controller.customStrategySchema.indicatorSchemas || []
    readonly property var selectedIndicatorDefinition: {
        const revision = root.indicatorEditRevision
        if (root.selectedIndicatorIndex < 0
                || root.selectedIndicatorIndex >= seriesModel.count)
            return null
        return WizardContract.indicatorSchema(
            root.controller.customStrategySchema,
            seriesModel.get(root.selectedIndicatorIndex).name)
    }
    readonly property var selectedIndicatorParameters:
        root.selectedIndicatorDefinition
        ? WizardContract.sequence(root.selectedIndicatorDefinition.parameters) : []
    readonly property var selectedIndicatorValidation: {
        const revision = root.indicatorEditRevision
        if (root.selectedIndicatorIndex < 0
                || root.selectedIndicatorIndex >= seriesModel.count)
            return ({ valid: false, key: "", error: "请选择一个指标" })
        return WizardContract.validateIndicatorRow(
            root.controller.customStrategySchema,
            seriesModel.get(root.selectedIndicatorIndex))
    }
    readonly property var indicatorColors: ["#21B6E9", "#2AD879", "#F5C451",
        "#9D8CFF", "#4E8CFF", "#FF5B62", "#7E8DA1", "#21B6E9", "#2AD879", "#F5C451"]
    readonly property int entryConditionCount: {
        const revision = root.conditionTreeRevision
        return WizardContract.conditionLeafCount(root.entryConditionTree)
    }
    readonly property int exitConditionCount: {
        const revision = root.conditionTreeRevision
        return WizardContract.conditionLeafCount(root.exitConditionTree)
    }

    ListModel { id: seriesModel }
    ListModel { id: conditionEditorModel }
    ListModel { id: conditionPreviewModel }

    onActiveConditionSideChanged: root.refreshConditionModels()

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
        const meta = WizardContract.indicatorSchema(
            root.controller.customStrategySchema, name)
        if (!meta) return
        let catalogIndex = 0
        for (let index = 0; index < root.catalog.length; ++index) {
            if (root.catalog[index].name === name) {
                catalogIndex = index
                break
            }
        }
        const color = root.indicatorColors[Math.max(0, catalogIndex)
                                            % root.indicatorColors.length]
        const item = WizardContract.newIndicatorRow(
            root.controller.customStrategySchema, name,
            root.uniqueAlias(name), color)
        seriesModel.append(item)
        root.selectedIndicatorIndex = seriesModel.count - 1
        root.indicatorEditRevision++
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function removeIndicator(index) {
        if (index < 0 || index >= seriesModel.count) return
        const alias = seriesModel.get(index).alias
        seriesModel.remove(index, 1)
        if (seriesModel.count === 0)
            root.selectedIndicatorIndex = -1
        else if (root.selectedIndicatorIndex >= seriesModel.count)
            root.selectedIndicatorIndex = seriesModel.count - 1
        else if (index < root.selectedIndicatorIndex)
            root.selectedIndicatorIndex--
        root.indicatorEditRevision++
        root.entryConditionTree = WizardContract.removeConditionSeries(
            root.entryConditionTree, alias)
        root.exitConditionTree = WizardContract.removeConditionSeries(
            root.exitConditionTree, alias)
        root.conditionTreeRevision++
        root.refreshConditionModels()
        root.statusText = "草稿未保存"
        root.saveDraft()
        root.previewDirty = true
    }

    function currentConditionTree() {
        return root.activeConditionSide === 0
            ? root.entryConditionTree : root.exitConditionTree
    }

    function setCurrentConditionTree(tree) {
        if (root.activeConditionSide === 0)
            root.entryConditionTree = tree
        else
            root.exitConditionTree = tree
        root.conditionTreeRevision++
        root.refreshConditionModels()
    }

    function refreshConditionModels() {
        const rows = WizardContract.conditionEditorRows(root.currentConditionTree())
        conditionEditorModel.clear()
        conditionPreviewModel.clear()
        for (let index = 0; index < rows.length; ++index) {
            conditionEditorModel.append(rows[index])
            if (rows[index].nodeType === "comparison")
                conditionPreviewModel.append(rows[index])
        }
    }

    function finishConditionEdit(message) {
        root.saveDraft()
        root.statusText = message || "条件树已更新"
        root.previewDirty = true
    }

    function addCondition(parentPath) {
        root.setCurrentConditionTree(WizardContract.appendCondition(
            root.currentConditionTree(), parentPath || ""))
        root.finishConditionEdit("已添加比较条件")
    }

    function addConditionGroup(parentPath, op) {
        root.setCurrentConditionTree(WizardContract.appendConditionGroup(
            root.currentConditionTree(), parentPath || "", op))
        root.finishConditionEdit("已添加" + (op === "or" ? " OR " : " AND ") + "子组")
    }

    function updateCondition(path, patch) {
        root.setCurrentConditionTree(WizardContract.updateConditionNode(
            root.currentConditionTree(), path, patch))
        root.finishConditionEdit("条件树已更新")
    }

    function removeCondition(path) {
        root.setCurrentConditionTree(WizardContract.removeConditionNode(
            root.currentConditionTree(), path))
        root.finishConditionEdit("已移除条件节点")
    }

    function buildJson(version, fixedTimestamp) {
        const stamp = fixedTimestamp !== undefined
            ? fixedTimestamp : new Date().toISOString()
        let series = []
        for (let i = 0; i < seriesModel.count; ++i) {
            const row = seriesModel.get(i)
            series.push(WizardContract.seriesDocument(
                root.controller.customStrategySchema, row))
        }
        const entry = root.entryConditionTree
        const exit = root.exitConditionTree
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
        return JSON.stringify(WizardContract.buildDefinition({
            strategyId: root.draftStrategyId.trim(),
            version: version !== undefined && version > 0 ? Math.floor(version) : 1,
            name: root.draftName.trim(),
            description: root.draftDescription.trim(),
            author: root.draftAuthor.trim(),
            selectedSymbol: root.controller.selectedBacktestSymbol,
            selectedFrequency: root.controller.selectedBacktestFrequency
        }, series, entry, exit, {
            position: position,
            risk: risk,
            signal: signal
        }, stamp))
    }

    function nextVersionForDraft() {
        const strategyId = root.draftStrategyId.trim()
        const row = root.controller.customStrategyModel.indexOf(strategyId, 0)
        if (row < 0)
            return 1
        return root.controller.customStrategyModel.get(row).version + 1
    }

    function draftSignature() {
        let definition
        try {
            definition = JSON.parse(root.buildJson(1, "2000-01-01T00:00:00.000Z"))
        } catch (error) {
            return ""
        }
        delete definition.version
        delete definition.createdAt
        delete definition.updatedAt
        return JSON.stringify(definition)
    }

    function savedDraftUnchanged() {
        return root.savedStrategyId === root.draftStrategyId.trim()
            && root.savedStrategyVersion > 0
            && root.savedDraftSignature.length > 0
            && root.savedDraftSignature === root.draftSignature()
    }

    function refreshWorkflowAvailability() {
        if (root.savedStrategyId.length === 0 || root.savedStrategyVersion < 1) {
            root.workflowRunAvailability = {
                available: false,
                error: "请先校验并保存当前草稿的新版本"
            }
            return
        }
        root.workflowRunAvailability = root.controller.customStrategyLocalRunAvailability(
            root.savedStrategyId, root.savedStrategyVersion, {})
    }

    function validateWorkflow(version) {
        const indicatorCheck = root.validateIndicatorEditor(false)
        if (!indicatorCheck.valid) {
            root.workflowMessage = indicatorCheck.error
            root.statusText = indicatorCheck.error
            return false
        }
        const json = root.buildJson(version > 0 ? version : root.nextVersionForDraft())
        root.workflowValidation = root.controller.validateCustomStrategyJson(json)
        root.workflowAnalysis = root.controller.analyzeCustomStrategyJson(json, {})
        if (!root.workflowValidation.valid) {
            root.workflowMessage = "校验失败：" + root.workflowValidation.error
            root.statusText = root.workflowMessage
            return false
        }
        if (!root.workflowAnalysis.executable) {
            root.workflowMessage = "编译失败：" + root.workflowAnalysis.error
            root.statusText = root.workflowMessage
            return false
        }
        root.workflowMessage = "校验与编译通过，可保存为 v" + (version > 0
            ? version : root.nextVersionForDraft())
        root.statusText = root.workflowMessage
        return true
    }

    function saveWorkflowVersion() {
        if (root.controller.customStrategySaveLoading)
            return false
        const targetVersion = root.nextVersionForDraft()
        if (!root.validateWorkflow(targetVersion))
            return false
        const json = root.buildJson(targetVersion)
        const requestId = root.controller.requestSaveCustomStrategyJson(json)
        if (requestId <= 0) {
            root.workflowMessage = root.controller.customStrategySaveError.length > 0
                ? root.controller.customStrategySaveError : "策略版本保存请求失败"
            root.statusText = root.workflowMessage
            return false
        }
        root.pendingSaveRequestId = requestId
        root.pendingSaveStrategyId = root.draftStrategyId.trim()
        root.pendingSaveVersion = targetVersion
        root.pendingSaveDraftSignature = root.draftSignature()
        root.workflowMessage = "正在后台保存不可变版本…"
        root.statusText = root.workflowMessage
        return true
    }

    function runWorkflowBacktest() {
        if (!root.savedDraftUnchanged()) {
            root.workflowMessage = "当前草稿已不同于已保存版本，请先保存新版本"
            root.statusText = root.workflowMessage
            return false
        }
        root.refreshWorkflowAvailability()
        if (!root.workflowRunAvailability.available) {
            root.workflowMessage = root.workflowRunAvailability.error
            root.statusText = root.workflowMessage
            return false
        }
        const taskId = root.controller.runSelectedLocalCustomStrategyBacktest(
            root.savedStrategyId, root.savedStrategyVersion, {})
        root.workflowTaskId = taskId
        root.workflowMessage = taskId > 0
            ? "已启动严格本地回测任务 " + taskId
            : root.controller.backtestError
        root.statusText = root.workflowMessage
        root.refreshWorkflowAvailability()
        return taskId > 0
    }

    function metadataError() {
        const strategyId = root.draftStrategyId.trim()
        if (!/^custom_[a-z0-9_]+$/.test(strategyId) || strategyId.length < 8
                || strategyId.length > 64)
            return "策略 ID 必须以 custom_ 开头，只含小写字母、数字、下划线（8–64 位）"
        if (root.draftName.trim().length < 1 || root.draftName.trim().length > 100)
            return "策略名称必须为 1–100 个字符"
        if (root.draftDescription.trim().length > 500)
            return "策略说明不能超过 500 个字符"
        if (root.draftAuthor.trim().length < 1 || root.draftAuthor.trim().length > 64)
            return "作者必须为 1–64 个字符"
        return ""
    }

    function canLeaveCurrentStep() {
        if (root.activeStep === 1) {
            const error = root.metadataError()
            if (error.length > 0) {
                root.statusText = error
                return false
            }
            return true
        }
        if (root.activeStep === 2) {
            if (!root.controller.selectedBacktestDataAvailable) {
                root.statusText = "请选择 SQLite 中可用的历史 Tick 派生数据集"
                return false
            }
            return true
        }
        if (root.activeStep === 3) {
            const result = root.validateIndicatorEditor(true)
            if (!result.valid) {
                root.statusText = result.error
                return false
            }
            return root.validateLocal()
        }
        return root.validateLocal()
    }

    function saveDraft() {
        let series = []
        for (let i = 0; i < seriesModel.count; ++i) series.push(seriesModel.get(i))
        draftStore.wizardDraft = JSON.stringify({
            strategyId: root.draftStrategyId,
            name: root.draftName,
            description: root.draftDescription,
            author: root.draftAuthor,
            activeStep: root.activeStep,
            positionType: root.positionType,
            positionAmount: root.positionAmount,
            positionPercent: root.positionPercent,
            maxPositionPercent: root.maxPositionPercent,
            stopLossPercent: root.stopLossPercent,
            takeProfitPercent: root.takeProfitPercent,
            maximumHoldingBars: root.maximumHoldingBars,
            signalPriceMode: root.signalPriceMode,
            workflowSampleFrom: root.workflowSampleFrom,
            workflowSampleTo: root.workflowSampleTo,
            savedStrategyId: root.savedStrategyId,
            savedStrategyVersion: root.savedStrategyVersion,
            savedDraftSignature: root.savedDraftSignature,
            series: series,
            entryCondition: root.entryConditionTree,
            exitCondition: root.exitConditionTree
        })
        root.statusText = "草稿已保存"
    }

    function restoreDraft() {
        if (!draftStore.wizardDraft) return
        let parsed
        try { parsed = JSON.parse(draftStore.wizardDraft) } catch (e) { return }
        parsed = WizardContract.migrateDraft(
            parsed, root.controller.customStrategySchema)
        if (!parsed || !Array.isArray(parsed.series)) return
        seriesModel.clear()
        for (const item of parsed.series)
            seriesModel.append(item)
        root.selectedIndicatorIndex = seriesModel.count > 0 ? 0 : -1
        root.indicatorEditRevision++
        root.entryConditionTree = parsed.entryCondition
            || ({ op: "and", conditions: [] })
        root.exitConditionTree = parsed.exitCondition
            || ({ op: "and", conditions: [] })
        root.conditionTreeRevision++
        root.refreshConditionModels()
        if (parsed.strategyId) root.draftStrategyId = parsed.strategyId
        if (parsed.name) root.draftName = parsed.name
        if (parsed.description !== undefined) root.draftDescription = parsed.description
        if (parsed.author) root.draftAuthor = parsed.author
        if (parsed.positionType) root.positionType = parsed.positionType
        if (parsed.positionAmount !== undefined) root.positionAmount = parsed.positionAmount
        if (parsed.positionPercent !== undefined) root.positionPercent = parsed.positionPercent
        if (parsed.maxPositionPercent !== undefined) root.maxPositionPercent = parsed.maxPositionPercent
        if (parsed.stopLossPercent !== undefined) root.stopLossPercent = parsed.stopLossPercent
        if (parsed.takeProfitPercent !== undefined) root.takeProfitPercent = parsed.takeProfitPercent
        if (parsed.maximumHoldingBars !== undefined) root.maximumHoldingBars = parsed.maximumHoldingBars
        if (parsed.signalPriceMode) root.signalPriceMode = parsed.signalPriceMode
        if (parsed.workflowSampleFrom !== undefined)
            root.workflowSampleFrom = parsed.workflowSampleFrom
        if (parsed.workflowSampleTo !== undefined)
            root.workflowSampleTo = parsed.workflowSampleTo
        if (parsed.savedStrategyId
                && root.controller.customStrategyModel.indexOf(
                    parsed.savedStrategyId, parsed.savedStrategyVersion) >= 0) {
            root.savedStrategyId = parsed.savedStrategyId
            root.savedStrategyVersion = parsed.savedStrategyVersion
            root.savedDraftSignature = parsed.savedDraftSignature || ""
        }
        if (parsed.activeStep >= 1 && parsed.activeStep <= 8)
            root.activeStep = parsed.activeStep
        root.previewDirty = true
    }

    function refreshPreview() {
        if (root.controller.customStrategyPreviewLoading)
            return
        const json = root.buildJson()
        if (seriesModel.count === 0) {
            root.previewData = {}
            root.previewError = "请先添加指标"
            return
        }
        if (!root.controller.selectedBacktestDataAvailable) {
            root.previewData = {}
            root.previewError = "请先在步骤 2 选择本地历史 Tick 派生数据集"
            return
        }
        const symbol = root.controller.selectedBacktestSymbol
        const period = root.controller.selectedBacktestPeriodSeconds
        const requestId = root.controller.requestCustomStrategyPreview(
            json, symbol, period, 300)
        if (requestId <= 0) {
            root.previewError = root.controller.customStrategyPreviewError
            return
        }
        root.pendingPreviewRequestId = requestId
        root.previewError = ""
        root.statusText = "正在后台生成本地预览…"
    }

    function switchToJson() {
        root.saveDraft()
        root.requestOpenJson(root.buildJson(root.nextVersionForDraft()))
    }

    function validateLocal() {
        return root.validateWorkflow(root.nextVersionForDraft())
    }

    function seriesNames() {
        let names = []
        for (let i = 0; i < seriesModel.count; ++i) names.push(seriesModel.get(i).alias)
        return names
    }

    function indicatorRows() {
        let rows = []
        for (let index = 0; index < seriesModel.count; ++index)
            rows.push(seriesModel.get(index))
        return rows
    }

    function validateIndicatorEditor(selectInvalidRow) {
        const result = WizardContract.validateIndicatorRows(
            root.controller.customStrategySchema, root.indicatorRows())
        if (!result.valid && selectInvalidRow && result.row >= 0)
            root.selectedIndicatorIndex = result.row
        return result
    }

    function setIndicatorParameter(key, value) {
        const row = root.selectedIndicatorIndex
        if (row < 0 || row >= seriesModel.count)
            return
        seriesModel.setProperty(row, key, value)
        root.indicatorEditRevision++
        const result = WizardContract.validateIndicatorRow(
            root.controller.customStrategySchema, seriesModel.get(row))
        root.previewDirty = true
        root.saveDraft()
        root.statusText = result.valid ? "指标参数已更新" : result.error
    }

    function parameterDisplayValue(parameter, value) {
        if (parameter.type !== "choice")
            return String(value)
        const choices = parameter.choices || []
        const labels = parameter.choiceLabels || []
        let index = -1
        for (let choiceIndex = 0; choiceIndex < choices.length; ++choiceIndex) {
            if (String(choices[choiceIndex]) === String(value)) {
                index = choiceIndex
                break
            }
        }
        return index >= 0 && index < labels.length ? labels[index] : String(value)
    }

    function indicatorParameterSummary(index) {
        if (index < 0 || index >= seriesModel.count)
            return ""
        const row = seriesModel.get(index)
        const definition = WizardContract.indicatorSchema(
            root.controller.customStrategySchema, row.name)
        const parameters = definition
            ? WizardContract.sequence(definition.parameters) : []
        if (parameters.length === 0)
            return "无需参数"
        let values = []
        for (let parameterIndex = 0; parameterIndex < parameters.length; ++parameterIndex) {
            const parameter = parameters[parameterIndex]
            values.push(root.parameterDisplayValue(parameter, row[parameter.key]))
        }
        return values.join(" · ")
    }

    function fieldNames() {
        return ["close", "open", "high", "low", "volume"]
    }

    function applyWorkflowSampleWindow() {
        const request = WizardContract.sampleWindowRequest(
            root.workflowSampleFrom, root.workflowSampleTo)
        if (!request.valid) {
            root.workflowMessage = request.error
            root.statusText = request.error
            return false
        }
        const from = Date.fromLocaleString(Qt.locale("zh_CN"), request.from, "yyyy-MM-dd")
        const to = Date.fromLocaleString(Qt.locale("zh_CN"), request.to, "yyyy-MM-dd")
        root.controller.setBacktestSampleWindow(from, to)
        root.workflowMessage = request.from.length > 0 || request.to.length > 0
            ? "样本窗已应用到下一次严格本地回测" : "样本窗已设为全部可用数据"
        root.statusText = root.workflowMessage
        return true
    }

    function clearWorkflowSampleWindow() {
        root.workflowSampleFrom = ""
        root.workflowSampleTo = ""
        root.controller.clearBacktestSampleWindow()
        root.workflowMessage = "已清除样本窗，下一次回测使用全部可用数据"
        root.statusText = root.workflowMessage
    }

    function restoreWorkflowValidationRun(index) {
        const runs = root.controller.validationRuns
        if (index < 0 || index >= runs.length || !root.controller.loadValidationRun(index)) {
            root.workflowMessage = "验证记录恢复失败"
            root.statusText = root.workflowMessage
            return false
        }
        const run = runs[index]
        const from = new Date(run.sampleFrom)
        const to = new Date(run.sampleTo)
        root.workflowSampleFrom = !isNaN(from.getTime())
            ? Qt.formatDate(from, "yyyy-MM-dd") : ""
        root.workflowSampleTo = !isNaN(to.getTime())
            ? Qt.formatDate(to, "yyyy-MM-dd") : ""
        root.workflowMessage = "已恢复验证记录“" + run.name + "”的样本窗"
        root.statusText = root.workflowMessage
        return true
    }

    function replayWorkflowBar() {
        const index = Math.floor(Number(root.workflowReplayIndex))
        if (!Number.isFinite(index) || index < 0) {
            root.workflowMessage = "回放索引必须为非负整数"
            root.statusText = root.workflowMessage
            return false
        }
        const replayData = root.controller.replayBacktestBar(index) || {}
        if (replayData.timestamp === undefined) {
            root.workflowReplayData = ({})
            root.workflowMessage = "该索引没有可回放的已完成回测 Bar"
            root.statusText = root.workflowMessage
            return false
        }
        // Qt 6.11 对 QVariantMap 中 QDateTime 的 JavaScript 包装与旧版本存在差异。
        // 在 QML 边界统一为 JS Date，避免显示层和 C++ 回放时刻产生类型歧义。
        replayData.timestamp = new Date(replayData.timestamp)
        root.workflowReplayData = replayData
        root.workflowMessage = "已回放 Bar " + index
        root.statusText = root.workflowMessage
        return true
    }

    function conditionModel() {
        return conditionPreviewModel
    }

    Component.onCompleted: {
        root.restoreDraft()
        if (seriesModel.count === 0) {
            root.addIndicator("SMA")
            root.addIndicator("SMA")
            root.addIndicator("ROC")
            root.addIndicator("PP_FLOW")
            seriesModel.setProperty(0, "alias", "fast")
            seriesModel.setProperty(1, "alias", "slow")
            seriesModel.setProperty(2, "alias", "momentum")
            seriesModel.setProperty(3, "alias", "flow")
            root.entryConditionTree = WizardContract.migrateConditionTree([
                { leftOperand: "fast", leftKind: "series", op: "crossesAbove",
                  rightOperand: "slow", rightKind: "series", rightConstant: 0 },
                { leftOperand: "momentum", leftKind: "series", op: "gt",
                  rightOperand: "", rightKind: "value", rightConstant: 0.8 }
            ], ["fast", "slow", "momentum", "flow"])
            root.exitConditionTree = WizardContract.migrateConditionTree([
                { leftOperand: "momentum", leftKind: "series", op: "lt",
                  rightOperand: "", rightKind: "value", rightConstant: 0 }
            ], ["fast", "slow", "momentum", "flow"])
            root.conditionTreeRevision++
            root.refreshConditionModels()
            root.saveDraft()
        }
        root.refreshConditionModels()
        root.refreshWorkflowAvailability()
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
                x: 346; y: root.compactLayout ? 22 : 8
                text: root.compactLayout ? "步骤 " + root.activeStep + " / 8" : "创建自定义策略"
                color: theme.textPrimary
                font.family: theme.fontFamily; font.pixelSize: 16; font.weight: Font.Bold
            }
            Text {
                x: 346; y: 34; text: root.draftName + " · 草稿自动保存"
                visible: !root.compactLayout
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
                theme: root.theme; text: root.compactLayout
                    ? (root.compactPreviewVisible ? "返回编辑" : "查看预览") : "流程说明"
                onClicked: if (root.compactLayout) root.compactPreviewVisible = !root.compactPreviewVisible
                toolTip: "八步向导：策略信息 → 标的与周期 → 指标组合 → 信号条件 → 仓位与风控 → 执行与审批 → 回测验证 → 发布与运行"
            }
            PpButton {
                anchors.right: parent.right; anchors.rightMargin: 212
                y: 18; visualWidth: 76
                theme: root.theme; text: "草稿"
                visible: !root.compactLayout
                active: true
                toolTip: "草稿保存在本机应用设置中，不会写入策略库"
            }
        }

        // 左侧向导栏
        Rectangle {
            id: sidebar
            anchors.left: parent.left
            anchors.top: topBar.bottom
            anchors.bottom: parent.bottom
            width: root.compactLayout ? 0 : 300
            visible: !root.compactLayout
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
                        { stepNumber: 1, title: "策略信息", desc: "名称、ID、作者与说明" },
                        { stepNumber: 2, title: "标的与周期", desc: "本地 A 股数据集" },
                        { stepNumber: 3, title: "指标组合", desc: "选择并组合多个指标" },
                        { stepNumber: 4, title: "信号条件", desc: "入场 / 出场条件组" },
                        { stepNumber: 5, title: "仓位与风控", desc: "仓位、止损与限制" },
                        { stepNumber: 6, title: "执行与审批", desc: "人工审批 / 自动提交" },
                        { stepNumber: 7, title: "回测验证", desc: "历史样本与流式回放" },
                        { stepNumber: 8, title: "发布与运行", desc: "版本、线程与通知" }
                    ]

                    delegate: Column {
                        width: 272
                        Rectangle {
                            readonly property bool currentStep: modelData.stepNumber === root.activeStep
                            readonly property bool completedStep: modelData.stepNumber < root.activeStep
                            width: 272; height: 66; radius: 8
                            color: currentStep ? "#211E38"
                                  : (completedStep ? theme.bgPanel : "transparent")
                            border.color: currentStep ? theme.purple : "transparent"
                            border.width: 1
                            Text {
                                x: 14; y: 10; width: 34; height: 22
                                text: parent.completedStep ? "✓" : (modelData.stepNumber < 10
                                      ? "0" + modelData.stepNumber : modelData.stepNumber)
                                color: parent.completedStep ? theme.green
                                      : (parent.currentStep ? theme.purple : theme.textMuted)
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                x: 58; y: 7; width: 176
                                text: modelData.title
                                color: parent.currentStep || parent.completedStep ? theme.textPrimary : theme.textMuted
                                font.family: theme.fontFamily; font.pixelSize: 13
                                font.weight: parent.currentStep ? Font.Bold : Font.Medium
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
                                    if (modelData.stepNumber <= root.activeStep)
                                        root.activeStep = modelData.stepNumber
                                    else if (modelData.stepNumber === root.activeStep + 1
                                             && root.canLeaveCurrentStep())
                                        root.activeStep = modelData.stepNumber
                                }
                            }
                        }
                        Rectangle {
                            x: 30; width: 1; height: modelData.stepNumber < root.activeStep ? 12 : 0
                            color: modelData.stepNumber < root.activeStep ? theme.green : theme.border
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

        // 步骤 1：策略元数据
        Rectangle {
            id: step1Area
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 1 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas

            Flickable {
                anchors.fill: parent
                anchors.margins: 28
                contentWidth: width
                contentHeight: Math.max(height, metadataColumn.implicitHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: metadataColumn
                    width: parent.width
                    spacing: 14

                    Text { width: parent.width; text: "01  策略信息"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                    Text {
                        width: parent.width
                        text: "定义不可变策略版本的身份信息。当前向导仅生成 A 股、严格本地历史回测策略。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                    }
                    Rectangle {
                        width: parent.width; height: 390; radius: 9
                        color: theme.bgPanel; border.color: theme.border
                        Column {
                            x: 18; y: 16; width: parent.width - 36; spacing: 10
                            Text { text: "策略 ID"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                            TextField {
                                width: parent.width; height: 36
                                text: root.draftStrategyId
                                placeholderText: "custom_my_strategy"
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11
                                selectByMouse: true; activeFocusOnTab: true
                                Accessible.name: "自定义策略 ID"
                                Accessible.description: "必须以 custom_ 开头，只能使用小写字母、数字和下划线"
                                onTextChanged: root.draftStrategyId = text
                                onEditingFinished: root.saveDraft()
                                background: Rectangle { radius: 6; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                            Text { text: "策略名称"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                            TextField {
                                width: parent.width; height: 36
                                text: root.draftName
                                placeholderText: "例如：量价动量过滤"
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11
                                selectByMouse: true; activeFocusOnTab: true
                                Accessible.name: "自定义策略名称"
                                onTextChanged: root.draftName = text
                                onEditingFinished: root.saveDraft()
                                background: Rectangle { radius: 6; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                            Text { text: "作者"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                            TextField {
                                width: parent.width; height: 36
                                text: root.draftAuthor
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11
                                selectByMouse: true; activeFocusOnTab: true
                                Accessible.name: "策略作者"
                                onTextChanged: root.draftAuthor = text
                                onEditingFinished: root.saveDraft()
                                background: Rectangle { radius: 6; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                            Text { text: "策略说明"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                            TextArea {
                                width: parent.width; height: 82
                                text: root.draftDescription
                                placeholderText: "说明策略逻辑、适用场景和限制"
                                color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11
                                selectByMouse: true; wrapMode: TextEdit.Wrap; activeFocusOnTab: true
                                Accessible.name: "策略说明"
                                onTextChanged: root.draftDescription = text
                                onActiveFocusChanged: if (!activeFocus) root.saveDraft()
                                background: Rectangle { radius: 6; color: theme.bgInput; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? theme.cyan : theme.border }
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 62; radius: 8
                        color: "#171F2A"; border.color: theme.border
                        Text { x: 14; y: 10; text: "固定范围：A 股 · backtest_only · DSL v2"; color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                        Text { x: 14; y: 34; width: parent.width - 28; text: "不会执行脚本、联网或真实下单；保存后形成不可变版本。"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                    }
                    Text {
                        width: parent.width; visible: root.metadataError().length > 0
                        text: root.metadataError(); color: theme.yellow
                        font.family: theme.fontFamily; font.pixelSize: 10; wrapMode: Text.Wrap
                        Accessible.name: text
                    }
                }
            }
        }

        // 步骤 2：严格本地回测上下文
        Rectangle {
            id: step2Area
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 2 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas

            Flickable {
                id: datasetStepScroll
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: Math.max(height, datasetStepColumn.implicitHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: datasetStepScroll.contentHeight > datasetStepScroll.height
                             ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Column {
                id: datasetStepColumn
                width: parent.width; spacing: 14
                Text { width: parent.width; text: "02  标的与周期"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "选择 SQLite 中已经存在的历史 Tick 派生 K 线。浏览和选择不会读取原始文件，也不会访问网络。"
                    color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                }
                Rectangle {
                    width: parent.width; height: 310; radius: 9
                    color: theme.bgPanel; border.color: theme.border
                    LocalBacktestSelector {
                        anchors.fill: parent; anchors.margins: 18
                        theme: root.theme; controller: root.controller
                        showWorkspaceActions: false
                    }
                }
                Rectangle {
                    width: parent.width; height: 72; radius: 8
                    color: root.controller.selectedBacktestDataAvailable ? "#14251F" : "#2A2115"
                    border.color: root.controller.selectedBacktestDataAvailable ? theme.green : theme.yellow
                    Text {
                        x: 14; y: 11; width: parent.width - 28
                        text: root.controller.selectedBacktestDataAvailable
                              ? "已绑定回测上下文：" + root.controller.selectedBacktestInstrumentTitle
                                + " · " + root.controller.selectedBacktestFrequency
                              : "尚未选择可用的本地数据集"
                        color: root.controller.selectedBacktestDataAvailable ? theme.green : theme.yellow
                        font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold; elide: Text.ElideRight
                    }
                    Text {
                        x: 14; y: 39; width: parent.width - 28
                        text: "标的、周期和数据版本属于回测请求身份，不写入策略 DSL；切换数据集不会自动下载。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap
                    }
                }
            }
            }
        }

        // 主内容区（第 3 步）
        Rectangle {
            id: mainArea
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 3 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas

            Flickable {
                id: wizardScroll
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: Math.max(height, indicatorColumn.implicitHeight)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: wizardScroll.contentHeight > wizardScroll.height
                             ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                Column {
                    id: indicatorColumn
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
                            text: "03  指标组合"
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
                                    return (symbol.length > 0 ? symbol : "--")
                                        + " · " + (freq.length > 0 ? freq : "--")
                                }
                                color: theme.purple
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "从指标目录中选择最多 16 个命名序列，统一设置别名、周期和输出序列，供后续条件定义使用。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    // 已选指标与 schema 驱动参数编辑器
                    Rectangle {
                        width: parent.width
                        height: 340
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
                            x: 112; y: 16
                            text: "选择卡片后按 C++ Schema 编辑参数"
                            color: theme.textMuted
                            font.family: theme.fontFamily; font.pixelSize: 9
                        }
                        PpButton {
                            objectName: "addIndicatorButton"
                            anchors.right: parent.right; anchors.rightMargin: 18; y: 10
                            visualWidth: 88
                            theme: root.theme; text: "＋ 添加"
                            active: true
                            toolTip: "从 C++ 校验器公开的指标白名单添加"
                            onClicked: addMenu.open()
                            Popup {
                                id: addMenu
                                objectName: "indicatorCatalogPopup"
                                x: 88 - width; y: 34
                                width: 240
                                height: Math.min(328, Math.max(0, root.height - 16),
                                                 root.catalog.length * 30 + topPadding + bottomPadding)
                                padding: 4
                                margins: 8
                                background: Rectangle {
                                    color: theme.bgRaised
                                    border.color: theme.border
                                    radius: 6
                                }
                                contentItem: ListView {
                                    objectName: "indicatorCatalog"
                                    clip: true
                                    model: root.catalog
                                    boundsBehavior: Flickable.StopAtBounds
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        required property var modelData
                                        objectName: "addIndicator_" + modelData.name
                                        width: ListView.view.width; height: 30; radius: 4
                                        color: "transparent"
                                        Accessible.name: "添加指标 " + modelData.name
                                        Text {
                                            x: 10; anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 24
                                            text: parent.modelData.name + " · "
                                                  + parent.modelData.displayName
                                            color: theme.textPrimary
                                            font.family: theme.fontFamily; font.pixelSize: 9
                                            elide: Text.ElideRight
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onEntered: parent.color = theme.bgSelected
                                            onExited: parent.color = "transparent"
                                            onClicked: {
                                                root.addIndicator(parent.modelData.name)
                                                addMenu.close()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ListView {
                            id: seriesStrip
                            x: 18; y: 48; width: parent.width - 36; height: 96
                            orientation: ListView.Horizontal
                            spacing: 8
                            clip: true
                            model: seriesModel
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: indicatorCard
                                required property int index
                                required property string name
                                required property string alias
                                required property bool seriesVisible
                                required property string seriesColor
                                width: 150; height: 84; radius: 8
                                color: root.selectedIndicatorIndex === indicatorCard.index
                                       ? theme.bgSelected : theme.bgRaised
                                border.color: root.selectedIndicatorIndex === indicatorCard.index
                                              ? theme.purple : theme.border
                                Accessible.name: "指标 " + indicatorCard.name + "，别名 "
                                                 + indicatorCard.alias

                                Rectangle {
                                    x: 10; y: 12; width: 8; height: 8; radius: 4
                                    color: indicatorCard.seriesColor
                                }
                                Text {
                                    x: 24; y: 6; width: 92
                                    text: indicatorCard.name
                                    color: theme.textPrimary
                                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    anchors.right: parent.right; anchors.rightMargin: 8; y: 4
                                    text: "×"; color: theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 12
                                    Accessible.name: "移除指标 " + indicatorCard.name
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.removeIndicator(indicatorCard.index)
                                    }
                                }
                                TextField {
                                    x: 10; y: 28; width: 92; height: 23
                                    text: indicatorCard.alias
                                    color: theme.textPrimary
                                    font.family: theme.fontFamily; font.pixelSize: 9
                                    Accessible.name: indicatorCard.name + " 指标别名"
                                    background: Rectangle {
                                        radius: 4; color: theme.bgInput; border.color: theme.border
                                    }
                                    onActiveFocusChanged: {
                                        if (activeFocus)
                                            root.selectedIndicatorIndex = indicatorCard.index
                                    }
                                    onEditingFinished: {
                                        const value = text.trim()
                                        if (value.length > 0) {
                                            seriesModel.setProperty(indicatorCard.index, "alias", value)
                                            root.selectedIndicatorIndex = indicatorCard.index
                                            root.indicatorEditRevision++
                                            const result = root.validateIndicatorEditor(false)
                                            root.statusText = result.valid ? "指标别名已更新" : result.error
                                            root.previewDirty = true
                                            root.saveDraft()
                                        } else {
                                            text = seriesModel.get(indicatorCard.index).alias
                                        }
                                    }
                                }
                                Rectangle {
                                    x: 108; y: 30; width: 32; height: 20; radius: 4
                                    color: theme.bgInput; border.color: theme.border
                                    Text {
                                        anchors.centerIn: parent
                                        text: indicatorCard.seriesVisible ? "ON" : "OFF"
                                        color: indicatorCard.seriesVisible
                                               ? indicatorCard.seriesColor : theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 8
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            seriesModel.setProperty(indicatorCard.index, "seriesVisible",
                                                                    !indicatorCard.seriesVisible)
                                            root.previewDirty = true
                                            root.saveDraft()
                                        }
                                    }
                                }
                                Text {
                                    x: 10; y: 58; width: 130
                                    text: root.indicatorParameterSummary(indicatorCard.index)
                                    color: indicatorCard.seriesColor
                                    font.family: theme.fontFamily; font.pixelSize: 8
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    x: 0; y: 0; width: parent.width - 34; height: 27
                                    onClicked: root.selectedIndicatorIndex = indicatorCard.index
                                }
                            }
                        }

                        Rectangle {
                            id: parameterEditor
                            x: 18; y: 154; width: parent.width - 36; height: 168
                            radius: 8; color: theme.bgRaised; border.color: theme.border

                            Text {
                                x: 12; y: 9; width: parent.width - 24
                                text: {
                                    if (root.selectedIndicatorIndex < 0
                                            || root.selectedIndicatorIndex >= seriesModel.count)
                                        return "参数"
                                    const definition = root.selectedIndicatorDefinition
                                    return "参数 · " + seriesModel.get(
                                        root.selectedIndicatorIndex).name
                                        + (definition ? "（" + definition.displayName + "）" : "")
                                }
                                color: theme.textPrimary
                                font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                                elide: Text.ElideRight
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: root.selectedIndicatorIndex < 0
                                         || root.selectedIndicatorParameters.length === 0
                                text: root.selectedIndicatorIndex < 0
                                      ? "请选择或添加指标"
                                      : "该指标无需配置参数"
                                color: theme.textMuted
                                font.family: theme.fontFamily; font.pixelSize: 10
                            }
                            Flow {
                                x: 12; y: 34; width: parent.width - 24; height: 112
                                spacing: 8
                                visible: root.selectedIndicatorParameters.length > 0
                                Repeater {
                                    model: root.selectedIndicatorParameters
                                    delegate: Item {
                                        id: parameterDelegate
                                        required property var modelData
                                        property var parameterSpec: parameterDelegate.modelData
                                        width: (parameterEditor.width - 32) / 2
                                        height: 50
                                        Text {
                                            x: 0; y: 0; width: parent.width
                                            text: parameterDelegate.parameterSpec.displayName
                                                  + (parameterDelegate.parameterSpec.type === "choice"
                                                     ? "" : "  [" + parameterDelegate.parameterSpec.minimumValue
                                                       + "–" + parameterDelegate.parameterSpec.maximumValue + "]")
                                            color: theme.textMuted
                                            font.family: theme.fontFamily; font.pixelSize: 8
                                            elide: Text.ElideRight
                                        }
                                        TextField {
                                            x: 0; y: 18; width: parent.width; height: 28
                                            visible: parameterDelegate.parameterSpec.type !== "choice"
                                            text: root.selectedIndicatorIndex >= 0
                                                  ? String(seriesModel.get(root.selectedIndicatorIndex)[
                                                       parameterDelegate.parameterSpec.key]) : ""
                                            color: theme.textPrimary
                                            font.family: theme.fontFamily; font.pixelSize: 9
                                            selectByMouse: true
                                            Accessible.name: "指标参数 "
                                                + parameterDelegate.parameterSpec.displayName
                                            validator: DoubleValidator {
                                                bottom: parameterDelegate.parameterSpec.minimumValue !== undefined
                                                        ? Number(parameterDelegate.parameterSpec.minimumValue) : -1e12
                                                top: parameterDelegate.parameterSpec.maximumValue !== undefined
                                                     ? Number(parameterDelegate.parameterSpec.maximumValue) : 1e12
                                                decimals: parameterDelegate.parameterSpec.type === "integer" ? 0 : 4
                                            }
                                            background: Rectangle {
                                                radius: 5; color: theme.bgInput; border.color: theme.border
                                            }
                                            onEditingFinished: {
                                                let value = Number(text)
                                                if (parameterDelegate.parameterSpec.type === "integer")
                                                    value = Math.round(value)
                                                root.setIndicatorParameter(
                                                    parameterDelegate.parameterSpec.key, value)
                                            }
                                        }
                                        ComboBox {
                                            id: parameterChoice
                                            x: 0; y: 18; width: parent.width; height: 28
                                            visible: parameterDelegate.parameterSpec.type === "choice"
                                            model: parameterDelegate.parameterSpec.choices || []
                                            currentIndex: root.selectedIndicatorIndex >= 0
                                                ? model.indexOf(String(seriesModel.get(
                                                    root.selectedIndicatorIndex)[
                                                        parameterDelegate.parameterSpec.key])) : -1
                                            Accessible.name: "指标参数 "
                                                + parameterDelegate.parameterSpec.displayName
                                            onActivated: root.setIndicatorParameter(
                                                parameterDelegate.parameterSpec.key, model[currentIndex])
                                            background: Rectangle {
                                                radius: 5; color: theme.bgInput; border.color: theme.border
                                            }
                                            contentItem: Text {
                                                leftPadding: 8
                                                text: root.parameterDisplayValue(
                                                    parameterDelegate.parameterSpec,
                                                    parameterChoice.currentIndex >= 0
                                                        ? parameterChoice.model[
                                                            parameterChoice.currentIndex] : "")
                                                color: theme.textPrimary
                                                font.family: theme.fontFamily; font.pixelSize: 9
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            indicator: Text {
                                                x: parent.width - 18
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "▾"; color: theme.textMuted; font.pixelSize: 9
                                            }
                                        }
                                    }
                                }
                            }
                            Text {
                                x: 12; anchors.bottom: parent.bottom; anchors.bottomMargin: 7
                                width: parent.width - 24
                                text: root.selectedIndicatorValidation.valid
                                      ? "参数合法，将写入不可变策略版本"
                                      : root.selectedIndicatorValidation.error
                                color: root.selectedIndicatorValidation.valid ? theme.green : theme.red
                                font.family: theme.fontFamily; font.pixelSize: 8
                                elide: Text.ElideRight
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
                                width: 88; height: 28; radius: 6
                                color: root.activeConditionSide === 0 ? theme.bgSuccess : theme.bgRaised
                                Text {
                                    anchors.centerIn: parent
                                    text: "入场条件  " + root.entryConditionCount
                                    color: root.activeConditionSide === 0 ? theme.green : theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activeConditionSide = 0
                                }
                            }
                            Rectangle {
                                width: 88; height: 28; radius: 6
                                color: root.activeConditionSide === 1 ? theme.bgSuccess : theme.bgRaised
                                Text {
                                    anchors.centerIn: parent
                                    text: "出场条件  " + root.exitConditionCount
                                    color: root.activeConditionSide === 1 ? theme.green : theme.textMuted
                                    font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.activeConditionSide = 1
                                }
                            }
                            PpButton {
                                visualWidth: 92
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
                                text: (root.activeConditionSide === 0 ? "入场" : "出场")
                                      + "条件树 · 在步骤 4 编辑 AND / OR / NOT"
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
                                    required property string path
                                    required property bool negated
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
                                        text: (parent.negated ? "¬" : "")
                                              + String(index + 1).padStart(2, "0")
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
                                            root.updateCondition(parent.path, {
                                                leftOperand: values[currentIndex],
                                                leftKind: currentIndex < 5 ? "field" : "series"
                                            })
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
                                            root.updateCondition(parent.path,
                                                { op: model[currentIndex].op })
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
                                            root.updateCondition(parent.path, {
                                                rightOperand: values[currentIndex],
                                                rightKind: "series"
                                            })
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
                                            root.updateCondition(parent.path, {
                                                rightConstant: Number(text), rightKind: "value"
                                            })
                                        }
                                    }
                                    PpButton {
                                        x: 374; y: 3; visualWidth: 60
                                        theme: root.theme; text: "常量"
                                        active: parent.rightKind !== "value"
                                        onClicked: {
                                            root.updateCondition(parent.path,
                                                { rightKind: "value", rightOperand: "" })
                                        }
                                    }
                                    PpButton {
                                        x: 440; y: 3; visualWidth: 60
                                        theme: root.theme; text: "移除"
                                        onClicked: root.removeCondition(parent.path)
                                    }
                                }
                            }

                            Text {
                                anchors.right: parent.right; anchors.rightMargin: 14
                                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                                text: "步骤 4 支持嵌套 AND / OR 与节点 NOT；编辑深度上限为 3 层"
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
            width: root.compactLayout ? parent.width : 488
            visible: !root.compactLayout || root.compactPreviewVisible
            color: theme.bgBar
            border.color: theme.border

            Flickable {
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: 896
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                Item {
                    x: Math.max(0, (parent.width - width) / 2)
                    width: 488; height: 896
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
                           ? root.controller.selectedBacktestSymbol : "--")
                          + " · " + (root.controller.selectedBacktestFrequency.length > 0
                                     ? root.controller.selectedBacktestFrequency : "--")
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
                            if (!seriesModel.get(s) || !seriesModel.get(s).seriesVisible) continue
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
                            ctx.strokeStyle = seriesModel.get(s).seriesColor
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

            }
            }

        // 步骤 4 面板（信号条件）
        Rectangle {
            id: step4Area
            objectName: "wizardConditionArea"
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 4 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas

            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: Math.max(height, step4Content.implicitHeight + 24)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: step4Content
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
                        text: "独立编辑入场与出场条件树。组节点支持 AND / OR，任一组或比较节点可取 NOT；操作数只允许字段、已定义指标序列或常量。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    Flow {
                        width: parent.width
                        height: childrenRect.height
                        spacing: 8
                        PpButton {
                            theme: root.theme
                            text: "入场条件  " + root.entryConditionCount
                            visualWidth: 126
                            active: root.activeConditionSide === 0
                            toolTip: "编辑开仓条件树"
                            onClicked: root.activeConditionSide = 0
                        }
                        PpButton {
                            theme: root.theme
                            text: "出场条件  " + root.exitConditionCount
                            visualWidth: 126
                            active: root.activeConditionSide === 1
                            toolTip: "编辑平仓条件树"
                            onClicked: root.activeConditionSide = 1
                        }
                        PpButton {
                            theme: root.theme; text: "＋ 比较"; visualWidth: 78
                            primary: true
                            toolTip: "向根组添加一项比较条件"
                            onClicked: root.addCondition("")
                        }
                        PpButton {
                            theme: root.theme; text: "＋ AND 组"; visualWidth: 86
                            toolTip: "向根组添加一个含两项比较的 AND 子组"
                            onClicked: root.addConditionGroup("", "and")
                        }
                        PpButton {
                            theme: root.theme; text: "＋ OR 组"; visualWidth: 80
                            toolTip: "向根组添加一个含两项比较的 OR 子组"
                            onClicked: root.addConditionGroup("", "or")
                        }
                    }

                    Rectangle {
                        width: parent.width; height: 410; radius: 8
                        color: theme.bgPanel; border.color: theme.border
                        Text {
                            x: 14; y: 11
                            text: (root.activeConditionSide === 0 ? "入场" : "出场")
                                  + "条件树 · " + (root.activeConditionSide === 0
                                      ? root.entryConditionCount : root.exitConditionCount)
                                  + " 个比较节点"
                            color: theme.textPrimary; font.family: theme.fontFamily
                            font.pixelSize: 11; font.weight: Font.Bold
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 14; y: 12
                            text: "DSL 限制：深度 ≤ 8、节点 ≤ 64；向导编辑深度 ≤ 3"
                            color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 9
                        }
                        ListView {
                            id: step4ConditionList
                            x: 10; y: 38; width: parent.width - 20; height: parent.height - 48
                            model: conditionEditorModel
                            spacing: 6; clip: true; boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                id: conditionCard
                                required property int index
                                required property string path
                                required property string nodeType
                                required property int depth
                                required property bool negated
                                required property string groupOp
                                required property int childCount
                                required property string leftOperand
                                required property string leftKind
                                required property string op
                                required property string rightOperand
                                required property string rightKind
                                required property double rightConstant
                                x: Math.min(54, conditionCard.depth * 18)
                                width: step4ConditionList.width - x
                                height: conditionCard.nodeType === "group" ? 96 : 92
                                radius: 6
                                color: conditionCard.nodeType === "group"
                                       ? theme.bgSelected : theme.bgRaised
                                border.color: conditionCard.negated ? theme.yellow : theme.border

                                RowLayout {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.top: parent.top; anchors.margins: 8
                                    height: 32; spacing: 6
                                    visible: conditionCard.nodeType === "group"
                                    Text {
                                        Layout.preferredWidth: 76
                                        text: (conditionCard.negated ? "NOT · " : "")
                                              + (conditionCard.path === "" ? "根组" : "子组")
                                        color: conditionCard.negated ? theme.yellow : theme.cyan
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                        font.weight: Font.Bold
                                    }
                                    ComboBox {
                                        id: groupOpCombo
                                        Layout.preferredWidth: 82; Layout.fillHeight: true
                                        model: ["AND", "OR"]
                                        currentIndex: conditionCard.groupOp === "or" ? 1 : 0
                                        Accessible.name: "条件组逻辑"
                                        onActivated: root.updateCondition(conditionCard.path,
                                            { op: currentIndex === 1 ? "or" : "and" })
                                        background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                        contentItem: Text { text: groupOpCombo.displayText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: conditionCard.childCount + " 个直接子节点"
                                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 8
                                        elide: Text.ElideRight
                                    }
                                    PpButton {
                                        theme: root.theme; text: "NOT"; visualWidth: 52
                                        active: conditionCard.negated
                                        toolTip: "切换本条件组的 NOT"
                                        onClicked: root.updateCondition(conditionCard.path,
                                            { negated: !conditionCard.negated })
                                    }
                                    PpButton {
                                        theme: root.theme; text: "移除"; visualWidth: 54
                                        available: conditionCard.path !== ""
                                        toolTip: available ? "移除整个子组" : "根组不可移除"
                                        onClicked: root.removeCondition(conditionCard.path)
                                    }
                                }

                                Row {
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.top: parent.top; anchors.topMargin: 52
                                    height: 32; spacing: 6
                                    visible: conditionCard.nodeType === "group"
                                    PpButton {
                                        theme: root.theme; text: "＋比较"; visualWidth: 62
                                        available: conditionCard.depth < 3
                                        toolTip: available ? "向本组添加比较条件" : "已达到向导编辑深度上限"
                                        onClicked: root.addCondition(conditionCard.path)
                                    }
                                    PpButton {
                                        theme: root.theme; text: "＋AND"; visualWidth: 58
                                        available: conditionCard.depth < 3
                                        toolTip: available ? "向本组添加 AND 子组" : "已达到向导编辑深度上限"
                                        onClicked: root.addConditionGroup(conditionCard.path, "and")
                                    }
                                    PpButton {
                                        theme: root.theme; text: "＋OR"; visualWidth: 54
                                        available: conditionCard.depth < 3
                                        toolTip: available ? "向本组添加 OR 子组" : "已达到向导编辑深度上限"
                                        onClicked: root.addConditionGroup(conditionCard.path, "or")
                                    }
                                }

                                RowLayout {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.top: parent.top; anchors.margins: 8
                                    height: 32; spacing: 6
                                    visible: conditionCard.nodeType === "comparison"
                                    Text {
                                        Layout.preferredWidth: 28
                                        text: (conditionCard.negated ? "¬" : "")
                                              + String(conditionCard.index + 1).padStart(2, "0")
                                        color: conditionCard.negated ? theme.yellow : theme.textMuted
                                        font.family: theme.fontFamily; font.pixelSize: 9
                                    }
                                    ComboBox {
                                    id: step4LeftCombo
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    model: root.fieldNames().concat(root.seriesNames())
                                    Accessible.name: "条件 " + (conditionCard.index + 1) + " 左操作数"
                                    currentIndex: {
                                        const values = root.fieldNames().concat(root.seriesNames())
                                        const found = values.indexOf(conditionCard.leftOperand)
                                        return found < 0 ? 0 : found
                                    }
                                    onActivated: {
                                        const fields = root.fieldNames()
                                        const values = fields.concat(root.seriesNames())
                                        root.updateCondition(conditionCard.path, {
                                            leftOperand: values[currentIndex],
                                            leftKind: currentIndex < fields.length ? "field" : "series"
                                        })
                                    }
                                    background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                    contentItem: Text { text: step4LeftCombo.displayText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                                }
                                ComboBox {
                                    id: step4OpCombo
                                    Layout.preferredWidth: 112; Layout.fillHeight: true
                                    model: [
                                        { text: "上穿", value: "crossesAbove" },
                                        { text: "下穿", value: "crossesBelow" },
                                        { text: "大于", value: "gt" },
                                        { text: "大于等于", value: "gte" },
                                        { text: "小于", value: "lt" },
                                        { text: "小于等于", value: "lte" },
                                        { text: "等于", value: "eq" }
                                    ]
                                    textRole: "text"
                                    Accessible.name: "条件 " + (conditionCard.index + 1) + " 比较符"
                                    currentIndex: {
                                        for (let i = 0; i < model.length; ++i)
                                            if (model[i].value === conditionCard.op) return i
                                        return 2
                                    }
                                    onActivated: {
                                        root.updateCondition(conditionCard.path,
                                            { op: model[currentIndex].value })
                                    }
                                    background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                    contentItem: Text { text: step4OpCombo.displayText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                                }
                                PpButton {
                                    theme: root.theme; text: "NOT"; visualWidth: 52
                                    active: conditionCard.negated
                                    toolTip: "切换本比较条件的 NOT"
                                    onClicked: root.updateCondition(conditionCard.path,
                                        { negated: !conditionCard.negated })
                                }
                                PpButton {
                                    theme: root.theme; text: "移除"; visualWidth: 54
                                    toolTip: "删除当前比较条件"
                                    onClicked: root.removeCondition(conditionCard.path)
                                }
                                }
                                RowLayout {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.bottom: parent.bottom; anchors.margins: 8
                                    height: 32; spacing: 6
                                    visible: conditionCard.nodeType === "comparison"
                                    Text { text: "右值"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                                ComboBox {
                                    id: step4RightKindCombo
                                    Layout.preferredWidth: 88; Layout.fillHeight: true
                                    model: ["字段", "指标", "常量"]
                                    Accessible.name: "条件 " + (conditionCard.index + 1) + " 右值类型"
                                    currentIndex: conditionCard.rightKind === "field" ? 0
                                                  : (conditionCard.rightKind === "series" ? 1 : 2)
                                    onActivated: {
                                        const kind = currentIndex === 0 ? "field" : (currentIndex === 1 ? "series" : "value")
                                        root.updateCondition(conditionCard.path, {
                                            rightKind: kind,
                                            rightOperand: kind === "field" ? "close"
                                                : (kind === "series" && root.seriesNames().length > 0
                                                   ? root.seriesNames()[0] : "")
                                        })
                                    }
                                    background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                    contentItem: Text { text: step4RightKindCombo.displayText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                                }
                                ComboBox {
                                    id: step4RightOperandCombo
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    visible: conditionCard.rightKind !== "value"
                                    model: conditionCard.rightKind === "field" ? root.fieldNames() : root.seriesNames()
                                    Accessible.name: "条件 " + (conditionCard.index + 1) + " 右操作数"
                                    currentIndex: {
                                        const values = conditionCard.rightKind === "field" ? root.fieldNames() : root.seriesNames()
                                        const found = values.indexOf(conditionCard.rightOperand)
                                        return found < 0 ? 0 : found
                                    }
                                    onActivated: {
                                        const values = conditionCard.rightKind === "field" ? root.fieldNames() : root.seriesNames()
                                        root.updateCondition(conditionCard.path,
                                            { rightOperand: values[currentIndex] })
                                    }
                                    background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                    contentItem: Text { text: step4RightOperandCombo.displayText; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                                }
                                TextField {
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    visible: conditionCard.rightKind === "value"
                                    text: String(conditionCard.rightConstant)
                                    validator: DoubleValidator {}
                                    selectByMouse: true
                                    color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9
                                    Accessible.name: "条件 " + (conditionCard.index + 1) + " 常量"
                                    onEditingFinished: {
                                        root.updateCondition(conditionCard.path,
                                            { rightConstant: Number(text) })
                                    }
                                    background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                                }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: conditionEditorModel.count <= 1
                                text: "当前条件组为空，请添加至少一项"
                                color: theme.yellow; font.family: theme.fontFamily; font.pixelSize: 10
                            }
                        }
                    }

                    Text { text: "信号成交价格"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold }
                    Row {
                        spacing: 8
                        PpButton {
                            theme: root.theme; text: "触发 Bar 收盘价（triggerClose）"; visualWidth: 206
                            active: root.signalPriceMode === "triggerClose"
                            toolTip: "入场信号在触发那根 K 线收盘价成交（v1 默认）"
                            onClicked: {
                                root.signalPriceMode = "triggerClose"
                                root.previewDirty = true
                                root.saveDraft()
                            }
                        }
                        PpButton {
                            theme: root.theme; text: "下一根开盘价（nextOpen）"; visualWidth: 188
                            active: root.signalPriceMode === "nextOpen"
                            toolTip: "入场信号在下一根 K 线开盘价成交"
                            onClicked: {
                                root.signalPriceMode = "nextOpen"
                                root.previewDirty = true
                                root.saveDraft()
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: "signal.priceMode 会写入 DSL v2。上穿/下穿需要连续有效数据；指标预热期间不会伪造信号。"
                        color: theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        width: parent.width; height: 64; radius: 8
                        color: root.workflowValidation.valid && root.workflowAnalysis.executable
                               ? "#14251F" : theme.bgPanel
                        border.color: root.workflowValidation.valid && root.workflowAnalysis.executable
                                      ? theme.green : theme.border
                        Text {
                            x: 12; y: 9; width: parent.width - 148
                            text: root.workflowValidation.valid && root.workflowAnalysis.executable
                                  ? "信号条件通过校验与编译"
                                  : (root.workflowMessage || "尚未检查信号条件")
                            color: root.workflowValidation.valid && root.workflowAnalysis.executable
                                   ? theme.green : theme.yellow
                            font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                            Accessible.name: text
                        }
                        Text {
                            x: 12; y: 35; width: parent.width - 148
                            text: "入场 " + root.entryConditionCount + " · 出场 "
                                  + root.exitConditionCount + " · 支持 AND / OR / NOT"
                            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 8
                        }
                        PpButton {
                            anchors.right: parent.right; anchors.rightMargin: 10; y: 16
                            theme: root.theme; text: "检查条件"; visualWidth: 112
                            toolTip: "调用 C++ 校验器与编译器检查当前完整策略"
                            onClicked: root.validateWorkflow(root.nextVersionForDraft())
                        }
                    }
                }
            }
        }

        // 步骤 5 面板（仓位与风控）
        Rectangle {
            id: step5Area
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 5 && (!root.compactLayout || !root.compactPreviewVisible)
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
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 6 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas
            Column {
                x: 40; y: 30; width: parent.width - 80; spacing: 14
                Text { width: parent.width; text: "06  执行与审批"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "策略执行审批（人工审批 / 自动提交、冷却、仓位计算）已由执行中心与 StrategyExecutionCoordinator 提供，参数走执行配置而非 DSL。本向导当前固定发布 backtest_only，不会自动启用模拟执行。"
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
                        text: "如需模拟执行，必须在保存版本后由执行中心显式启用；真实下单仍不在范围。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9
                    }
                }
            }
        }

        // 步骤 7 面板（回测验证）
        Rectangle {
            id: step7Area
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 7 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas
            Flickable {
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: width
                contentHeight: Math.max(height, step7Content.implicitHeight + 24)
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: step7Content
                    width: parent.width; spacing: 14
                    Text { width: parent.width; text: "07  回测验证"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                    Text {
                        width: parent.width
                        text: "运行当前已保存版本的严格本地回测。样本窗、验证记录和逐 Bar 回放均基于 SQLite 已完成结果，不访问 Provider。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                    }
                    Rectangle {
                        width: parent.width; height: 86; radius: 8; color: theme.bgPanel; border.color: theme.border
                        Text {
                            x: 14; y: 11; width: parent.width - 28
                            text: root.savedStrategyId.length > 0
                                  ? root.savedStrategyId + " · v" + root.savedStrategyVersion
                                  : "尚未保存向导策略版本"
                            color: root.savedStrategyId.length > 0 ? theme.cyan : theme.yellow
                            font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold; elide: Text.ElideRight
                        }
                        Text {
                            x: 14; y: 36; width: parent.width - 28
                            text: root.controller.selectedBacktestDataAvailable
                                  ? root.controller.selectedBacktestInstrumentTitle + " · "
                                    + root.controller.selectedBacktestFrequency + " · "
                                    + root.controller.selectedBacktestCandleCount + " 根"
                                  : "未选择本地历史 Tick 派生数据集"
                            color: root.controller.selectedBacktestDataAvailable ? theme.textPrimary : theme.yellow
                            font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                        }
                        Text {
                            x: 14; y: 59; width: parent.width - 28
                            text: root.savedDraftUnchanged()
                                  ? (root.workflowRunAvailability.available
                                     ? "运行检查通过" : root.workflowRunAvailability.error)
                                  : "当前草稿与已保存版本不同"
                            color: root.savedDraftUnchanged() && root.workflowRunAvailability.available
                                   ? theme.green : theme.yellow
                            font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                            Accessible.name: text
                        }
                    }
                    Text { text: "样本窗（留空表示不限制）"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                    Row {
                        spacing: 6
                        TextField {
                            width: 118; height: 30; text: root.workflowSampleFrom
                            placeholderText: "起始 yyyy-MM-dd"; selectByMouse: true
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9
                            Accessible.name: "向导回测样本窗起始日期"
                            onTextEdited: root.workflowSampleFrom = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        TextField {
                            width: 118; height: 30; text: root.workflowSampleTo
                            placeholderText: "结束 yyyy-MM-dd"; selectByMouse: true
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9
                            Accessible.name: "向导回测样本窗结束日期"
                            onTextEdited: root.workflowSampleTo = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        PpButton { theme: root.theme; text: "应用"; visualWidth: 58; visualHeight: 30; toolTip: "校验日期并应用到下一次回测"; onClicked: root.applyWorkflowSampleWindow() }
                        PpButton { theme: root.theme; text: "清除"; visualWidth: 58; visualHeight: 30; toolTip: "清除样本窗限制"; onClicked: root.clearWorkflowSampleWindow() }
                    }
                    Row {
                        spacing: 10
                        PpButton {
                            theme: root.theme; text: root.controller.backtestRunning
                                ? "回测运行中" : "运行严格本地回测"
                            visualWidth: 156; primary: true
                            available: !root.controller.backtestRunning
                                       && root.savedDraftUnchanged()
                                       && root.workflowRunAvailability.available
                            toolTip: available ? "运行已保存版本；只读取 SQLite 历史 Tick 派生数据"
                                               : (root.workflowRunAvailability.error || "请先保存当前草稿的新版本")
                            onClicked: root.runWorkflowBacktest()
                        }
                        PpButton {
                            theme: root.theme; text: "重新检查"; visualWidth: 92
                            toolTip: "刷新版本、本地数据集和任务可用性"
                            onClicked: root.refreshWorkflowAvailability()
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 112; radius: 8
                        color: root.workflowResultMatches ? "#14251F" : theme.bgPanel
                        border.color: root.workflowResultMatches ? theme.green : theme.border
                        Text {
                            x: 14; y: 11; width: parent.width - 28
                            text: root.controller.backtestRunning
                                  ? root.controller.backtestStatusText
                                  : (root.workflowResultMatches ? "当前版本回测完成"
                                     : (root.controller.backtestResultStrategyId.length > 0
                                        ? "现有结果属于其他策略版本" : "尚无回测结果"))
                            color: root.controller.backtestRunning ? theme.cyan
                                   : (root.workflowResultMatches ? theme.green : theme.textMuted)
                            font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold; elide: Text.ElideRight
                        }
                        Text {
                            x: 14; y: 38; width: parent.width - 28
                            text: root.workflowResultMatches
                                  ? "身份：" + root.controller.backtestResultStrategyId
                                    + " v" + root.savedStrategyVersion + " · "
                                    + root.controller.backtestResultSymbol
                                  : "任务：" + (root.workflowTaskId > 0 ? root.workflowTaskId : "--")
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                        }
                        Text {
                            x: 14; y: 62; width: parent.width - 28
                            text: root.workflowResultMatches
                                  ? "dataVersion：" + (root.controller.backtestResultConfiguration.dataVersion || "--")
                                  : (root.controller.backtestError.length > 0
                                     ? root.controller.backtestError : root.workflowMessage)
                            color: root.controller.backtestError.length > 0 ? theme.red : theme.textMuted
                            font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                        }
                        Text {
                            x: 14; y: 86; width: parent.width - 28
                            text: root.workflowResultMatches
                                  ? "交易 " + root.controller.backtestTradeCount
                                    + " · 总收益 " + Number(root.controller.totalReturnPercent).toFixed(2) + "%"
                                  : "完成当前保存版本的回测后可保存验证记录和逐 Bar 回放"
                            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 8; elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 176; radius: 8; color: theme.bgPanel; border.color: theme.border
                        Text { x: 12; y: 10; text: "验证记录（" + root.controller.validationRuns.length + " / 10）"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                        PpButton {
                            anchors.right: parent.right; anchors.rightMargin: 10; y: 6
                            theme: root.theme; text: "保存当前验证"; visualWidth: 112; visualHeight: 28
                            available: root.workflowResultMatches && !root.controller.backtestRunning
                            toolTip: available ? "保存当前结果配置与摘要，不复制或重跑数据" : "请先完成当前保存版本的回测"
                            onClicked: {
                                const saved = root.controller.saveValidationRun(
                                    root.draftName + " " + Qt.formatDateTime(new Date(), "MM-dd HH:mm"))
                                root.workflowMessage = saved ? "验证记录已保存"
                                    : (root.controller.statusText || "验证记录保存失败")
                                root.statusText = root.workflowMessage
                            }
                        }
                        ListView {
                            x: 10; y: 42; width: parent.width - 20; height: 124
                            model: root.controller.validationRuns; clip: true; spacing: 4
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property int index
                                required property string name
                                required property string strategyId
                                required property int strategyVersion
                                required property string instrumentSymbol
                                required property real totalReturnPercent
                                width: ListView.view.width; height: 38; radius: 5
                                color: strategyId === root.savedStrategyId && strategyVersion === root.savedStrategyVersion
                                       ? "#14251F" : theme.bgRaised
                                Text {
                                    x: 8; y: 5; width: parent.width - 76
                                    text: name + " · " + instrumentSymbol + " · "
                                          + strategyId + " v" + strategyVersion
                                    color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 8; elide: Text.ElideRight
                                }
                                Text {
                                    x: 8; y: 21
                                    text: (totalReturnPercent >= 0 ? "+" : "")
                                          + Number(totalReturnPercent).toFixed(2) + "%"
                                    color: totalReturnPercent >= 0 ? theme.green : theme.red
                                    font.family: theme.fontFamily; font.pixelSize: 8
                                }
                                PpButton {
                                    anchors.right: parent.right; anchors.rightMargin: 6; y: 7
                                    theme: root.theme; text: "恢复"; visualWidth: 56; visualHeight: 24
                                    available: !root.controller.backtestRunning
                                    toolTip: "恢复该记录的样本窗；不会自动重跑或联网"
                                    onClicked: root.restoreWorkflowValidationRun(index)
                                }
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width; height: 98; radius: 8; color: theme.bgPanel; border.color: theme.border
                        Text { x: 12; y: 10; text: "逐 Bar 回放"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                        TextField {
                            x: 12; y: 34; width: 88; height: 28; text: root.workflowReplayIndex
                            validator: IntValidator { bottom: 0 }
                            selectByMouse: true
                            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 9
                            Accessible.name: "回放 Bar 索引"
                            onTextEdited: root.workflowReplayIndex = text
                            background: Rectangle { radius: 5; color: theme.bgInput; border.color: parent.activeFocus ? theme.cyan : theme.border }
                        }
                        PpButton {
                            x: 108; y: 34; theme: root.theme; text: "读取 Bar"; visualWidth: 88; visualHeight: 28
                            available: root.workflowResultMatches && root.controller.candleModel.count > 0
                            toolTip: available ? "从当前已完成结果只读回放指定索引" : "请先完成当前保存版本的回测"
                            onClicked: root.replayWorkflowBar()
                        }
                        Text {
                            x: 210; y: 34; width: parent.width - 222; height: 48
                            text: root.workflowReplayData.timestamp !== undefined
                                  ? Qt.formatDateTime(root.workflowReplayData.timestamp, "yyyy-MM-dd HH:mm")
                                    + " · O " + Number(root.workflowReplayData.open).toFixed(2)
                                    + " H " + Number(root.workflowReplayData.high).toFixed(2)
                                    + " L " + Number(root.workflowReplayData.low).toFixed(2)
                                    + " C " + Number(root.workflowReplayData.close).toFixed(2)
                                    + (root.workflowReplayData.hasSignal
                                       ? " · " + root.workflowReplayData.signalSide + " "
                                         + root.workflowReplayData.signalReason : " · 无信号")
                                  : "输入 0 起始的 Bar 索引；只读当前结果，不修改策略或数据。"
                            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 8
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

        // 步骤 8 面板（发布与运行）
        Rectangle {
            id: step8Area
            anchors.left: sidebar.right
            anchors.right: root.compactLayout ? parent.right : preview.left
            anchors.top: topBar.bottom
            anchors.bottom: footer.top
            visible: root.activeStep === 8 && (!root.compactLayout || !root.compactPreviewVisible)
            color: theme.bgCanvas
            Column {
                x: 40; y: 30; width: parent.width - 80; spacing: 14
                Text { width: parent.width; text: "08  发布与运行"; color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    width: parent.width
                    text: "校验并保存当前草稿的新不可变版本。保存成功后可留在向导第 7 步直接运行严格本地回测。"
                    color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 11; wrapMode: Text.Wrap
                }
                Rectangle {
                    width: parent.width; height: 92; radius: 8; color: theme.bgPanel; border.color: theme.border
                    Text { x: 14; y: 11; text: "目标版本"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
                    Text {
                        x: 14; y: 31; width: parent.width - 28
                        text: root.draftStrategyId.trim() + " · v" + root.nextVersionForDraft()
                        color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold; elide: Text.ElideRight
                    }
                    Text {
                        x: 14; y: 59; width: parent.width - 28
                        text: root.workflowValidation.valid && root.workflowAnalysis.executable
                              ? "校验与编译通过 · backtest_ready" : root.workflowMessage
                        color: root.workflowValidation.valid && root.workflowAnalysis.executable
                               ? theme.green : theme.yellow
                        font.family: theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                        Accessible.name: text
                    }
                }
                Row {
                    spacing: 10
                    PpButton {
                        theme: root.theme; text: "校验当前草稿"; visualWidth: 122
                        toolTip: "通过 C++ 校验器与编译器检查字段、指标和规则"
                        onClicked: root.validateWorkflow(root.nextVersionForDraft())
                    }
                    PpButton {
                        theme: root.theme
                        text: root.controller.customStrategySaveLoading ? "保存中…" : "保存新版本"
                        visualWidth: 122; primary: true
                        available: !root.controller.customStrategySaveLoading
                        toolTip: "事务写入 SQLite 不可变策略版本"
                        onClicked: root.saveWorkflowVersion()
                    }
                    PpButton {
                        theme: root.theme; text: "查看 JSON"; visualWidth: 92
                        toolTip: "切换到 JSON 编辑模式查看将要保存的 DSL v2"
                        onClicked: root.switchToJson()
                    }
                }
                Rectangle {
                    width: parent.width; height: 74; radius: 8
                    color: root.savedDraftUnchanged() ? "#14251F" : theme.bgPanel
                    border.color: root.savedDraftUnchanged() ? theme.green : theme.border
                    Text {
                        x: 14; y: 11; width: parent.width - 28
                        text: root.savedStrategyId.length > 0
                              ? "已保存：" + root.savedStrategyId + " v" + root.savedStrategyVersion
                              : "尚未保存向导策略"
                        color: root.savedStrategyId.length > 0 ? theme.green : theme.textMuted
                        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold; elide: Text.ElideRight
                    }
                    Text {
                        x: 14; y: 38; width: parent.width - 28
                        text: "发布范围固定为历史回测；保存不会启动网络请求、模拟执行或真实下单。"
                        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap
                    }
                }
            }
        }

        // 底部操作栏
        Rectangle {
            id: footer
            objectName: "wizardFooter"
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.compactLayout ? 92 : 64
            color: theme.bgBar
            border.color: theme.border

            Text {
                x: root.compactLayout ? 16 : 28
                y: root.compactLayout ? 8 : 20
                width: root.compactLayout ? parent.width - 32 : 360
                text: root.statusText + " · 指标 " + seriesModel.count
                      + " · 条件 " + (root.entryConditionCount + root.exitConditionCount)
                color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10
                elide: Text.ElideRight
            }
            PpButton {
                x: root.compactLayout ? 16 : 400
                y: root.compactLayout ? 42 : 15; visualWidth: 112
                theme: root.theme; text: "← 上一步"
                available: root.activeStep > 1
                toolTip: available ? "返回上一步修改" : "当前已是第一步"
                onClicked: {
                    if (root.activeStep > 1) {
                        root.activeStep = root.activeStep - 1
                        root.saveDraft()
                    }
                }
            }
            PpButton {
                x: root.compactLayout ? 140 : 524
                y: root.compactLayout ? 42 : 15; visualWidth: 136
                theme: root.theme; text: "预览信号条件"
                active: true
                available: root.activeStep >= 3
                           && root.controller.selectedBacktestDataAvailable
                           && !root.controller.customStrategyPreviewLoading
                toolTip: available ? "使用当前严格本地数据集预览指标和信号"
                                   : (root.activeStep < 3 ? "完成步骤 1、2 后可预览"
                                      : "请先选择本地历史 Tick 派生数据集")
                onClicked: {
                    if (root.validateLocal()) {
                        root.refreshPreview()
                        if (root.compactLayout) root.compactPreviewVisible = true
                    }
                }
            }
            PpButton {
                x: root.compactLayout ? parent.width - 226 : 672
                y: root.compactLayout ? 42 : 15; visualWidth: 210
                objectName: "wizardNextButton"
                theme: root.theme; text: root.activeStep < 8 ? "下一步：继续  →" : "完成：保存新版本  →"
                primary: true
                onClicked: {
                    if (root.canLeaveCurrentStep()) {
                        root.saveDraft()
                        if (root.activeStep < 8) {
                            root.activeStep = root.activeStep + 1
                        } else {
                            root.saveWorkflowVersion()
                        }
                    }
                }
            }
        }

        Connections {
            target: root.controller
            function onBacktestInstrumentChanged() { root.refreshWorkflowAvailability() }
            function onBacktestConfigurationChanged() { root.refreshWorkflowAvailability() }
            function onBacktestRunningChanged() { root.refreshWorkflowAvailability() }
            function onBacktestStateChanged() {
                if (root.controller.backtestError.length > 0) {
                    root.workflowMessage = root.controller.backtestError
                    root.statusText = root.workflowMessage
                } else if (!root.controller.backtestRunning
                           && root.savedStrategyId.length > 0
                           && root.savedStrategyVersion > 0
                           && root.controller.backtestResultMatchesCustomStrategy(
                               root.savedStrategyId, root.savedStrategyVersion)) {
                    root.workflowMessage = "严格本地回测完成：" + root.savedStrategyId
                        + " v" + root.savedStrategyVersion
                    root.statusText = root.workflowMessage
                }
            }
            function onCustomStrategyStateChanged() { root.refreshWorkflowAvailability() }
            function onCustomStrategyPreviewStateChanged() {
                if (root.controller.customStrategyPreviewLoading
                        || root.controller.customStrategyPreviewResolvedRequestId
                           !== root.pendingPreviewRequestId)
                    return
                root.previewData = root.controller.customStrategyPreviewResult || {}
                root.previewError = root.controller.customStrategyPreviewError || ""
                root.previewDirty = false
                root.statusText = root.previewError.length > 0
                    ? root.previewError : "本地预览已更新"
            }
            function onCustomStrategySaveStateChanged() {
                if (root.controller.customStrategySaveLoading
                        || root.controller.customStrategySaveResolvedRequestId
                           !== root.pendingSaveRequestId)
                    return
                if (root.controller.customStrategySaveError.length > 0) {
                    root.workflowMessage = root.controller.customStrategySaveError
                    root.statusText = root.workflowMessage
                    return
                }
                root.savedStrategyId = root.pendingSaveStrategyId
                root.savedStrategyVersion = root.pendingSaveVersion
                root.savedDraftSignature = root.pendingSaveDraftSignature
                root.workflowMessage = "已保存不可变版本：" + root.savedStrategyId
                    + " v" + root.savedStrategyVersion
                root.refreshWorkflowAvailability()
                root.saveDraft()
                root.statusText = root.workflowMessage
            }
        }
    }
}
