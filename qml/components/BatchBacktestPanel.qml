// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    readonly property string selectedDataMode: dataModeCombo.currentIndex >= 0
                                               ? dataModeCombo.currentValue : "raw_ticks"
    readonly property var availabilityResult: root.controller.rawTickAvailabilityResult || ({})
    property bool applyRecommendationAfterRefresh: false

    function frequencyModel() {
        const seen = []
        const catalog = root.controller.localBacktestDataCatalog || []
        for (const entry of catalog) {
            if (entry.frequency && seen.indexOf(entry.frequency) < 0) {
                seen.push(entry.frequency)
            }
        }
        return seen
    }

    function syncFrequencyIndex() {
        const frequencies = root.frequencyModel()
        let match = frequencies.indexOf(root.controller.selectedBacktestFrequency)
        if (match < 0 && frequencies.length > 0) match = 0
        frequencyCombo.currentIndex = match
    }

    function strategyModel() {
        const catalog = root.controller.backtestStrategyCatalog || []
        const result = []
        for (const strategy of catalog) {
            if ((root.selectedDataMode === "raw_ticks" && strategy.supportsRawTicks)
                    || (root.selectedDataMode === "candles" && strategy.supportsCandles)) {
                result.push(strategy)
            }
        }
        return result
    }

    function strategyParameters(strategy) {
        if (!strategy) return ({})
        if (strategy.strategyId === root.controller.selectedBacktestStrategyId)
            return root.controller.selectedBacktestParameters
        const defaults = ({})
        for (const spec of strategy.parameters || [])
            defaults[spec.key] = spec.defaultValue
        return defaults
    }

    function syncStrategyIndex() {
        const strategies = root.strategyModel()
        let match = -1
        for (let index = 0; index < strategies.length; ++index) {
            if (strategies[index].strategyId === root.controller.selectedBacktestStrategyId) {
                match = index
                break
            }
        }
        strategyCombo.currentIndex = match >= 0 ? match : (strategies.length > 0 ? 0 : -1)
    }

    function startBatch() {
        const strategies = root.strategyModel()
        const strategy = strategyCombo.currentIndex >= 0
            ? strategies[strategyCombo.currentIndex] : null
        const strategyId = strategyCombo.currentIndex >= 0
            ? strategy.strategyId : ""
        const frequency = frequencyCombo.currentIndex >= 0
            ? root.frequencyModel()[frequencyCombo.currentIndex] : ""
        const symbols = Math.max(1, Number(maxSymbolsField.text) || 500)
        root.controller.runLocalBatchBacktestRange(
            strategyId, root.strategyParameters(strategy), root.selectedDataMode,
            root.selectedDataMode === "candles" ? frequency : "",
            startDateField.text, endDateField.text, symbols,
            holdoutCheck.checked ? Math.max(2, Number(holdoutDaysField.text) || 2) : 0,
            holdoutCheck.checked ? Math.max(1, Number(walkForwardFoldsField.text) || 1) : 0,
            segmentedResearchCheck.checked)
    }

    function analyzeAvailability() {
        root.controller.analyzeLocalRawTickAvailability(
            startDateField.text, endDateField.text,
            Math.max(1, Number(maxSymbolsField.text) || 500))
    }

    function applyRecommendedAvailability() {
        if (!root.availabilityInputsMatch()
                || !root.availabilityResult.hasRecommendation) return
        // 推荐是 SQLite 一致读快照。采用前再做一次异步重扫，防止外部导入
        // 或审计更新发生在上次分析之后；新事实不合格时绝不套用旧日期。
        root.applyRecommendationAfterRefresh = true
        root.analyzeAvailability()
    }

    function availabilityText() {
        if (root.controller.rawTickAvailabilityLoading)
            return "正在后台核对逐日身份、质量审计和交易日历…"
        if (root.controller.rawTickAvailabilityError.length > 0)
            return root.controller.rawTickAvailabilityError
        if (!root.availabilityResult.hasRecommendation)
            return "尚未分析；分析只读取本地 SQLite，不联网、不补数"
        if (!root.availabilityInputsMatch())
            return "日期或标的上限已变化，当前分析结果已过期，请重新分析"
        return "共同可审计推荐 " + root.availabilityResult.recommendedStartDate
                + " → " + root.availabilityResult.recommendedEndDate
                + "（" + root.availabilityResult.recommendedTradingDayCount
                + " 个交易日，切分 " + root.availabilityResult.segmentCount + " 段）"
    }

    function availabilityInputsMatch() {
        if (root.controller.rawTickAvailabilityResolvedRequestId === 0)
            return false
        const symbols = Math.max(1, Number(maxSymbolsField.text) || 500)
        return root.availabilityResult.requestedStartDate === startDateField.text.trim()
                && root.availabilityResult.requestedEndDate === endDateField.text.trim()
                && Number(root.availabilityResult.requestedMaximumSymbols) === symbols
    }

    Connections {
        target: root.controller
        function onRawTickAvailabilityStateChanged() {
            if (!root.applyRecommendationAfterRefresh
                    || root.controller.rawTickAvailabilityLoading) return
            root.applyRecommendationAfterRefresh = false
            if (root.controller.rawTickAvailabilityError.length > 0
                    || !root.availabilityInputsMatch()
                    || !root.availabilityResult.hasRecommendation) return
            const recommendedStart = root.availabilityResult.recommendedStartDate || ""
            const recommendedEnd = root.availabilityResult.recommendedEndDate || ""
            startDateField.text = recommendedStart
            endDateField.text = recommendedEnd
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "批量回测"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: "默认原始 Tick 逐笔回测；只有显式选择周期 K 线时才读取派生缓存 · 日期范围内按标的完整加载"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 8; rowSpacing: 6
            Text { text: "数据模式"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: dataModeCombo
                Layout.fillWidth: true; Layout.preferredHeight: 30
                model: [
                    { label: "原始 Tick（默认）", value: "raw_ticks" },
                    { label: "周期 K 线（显式）", value: "candles" }
                ]
                textRole: "label"; valueRole: "value"
                Accessible.name: "批量回测数据模式"
                onCurrentIndexChanged: {
                    if (root.selectedDataMode !== "raw_ticks") {
                        holdoutCheck.checked = false
                        segmentedResearchCheck.checked = false
                    }
                    root.syncStrategyIndex()
                }
            }
            Text { text: "策略"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: strategyCombo
                Layout.fillWidth: true; Layout.preferredHeight: 30
                model: root.strategyModel()
                textRole: "name"; valueRole: "strategyId"
                Component.onCompleted: root.syncStrategyIndex()
                Accessible.name: "批量回测策略"
            }
            Text { text: "开始日期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: startDateField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: root.controller.backtestSampleFromDate
                placeholderText: "YYYY-MM-DD"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "批量回测开始日期"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "结束日期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: endDateField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: root.controller.backtestSampleToDate
                placeholderText: "YYYY-MM-DD"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "批量回测结束日期"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "K 线周期"; color: root.theme.textMuted; opacity: root.selectedDataMode === "candles" ? 1.0 : 0.55; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: frequencyCombo
                Layout.fillWidth: true; Layout.preferredHeight: 30
                model: root.frequencyModel()
                enabled: root.selectedDataMode === "candles"
                Component.onCompleted: root.syncFrequencyIndex()
                Accessible.name: "批量回测周期"
                Accessible.description: enabled ? "显式 K 线模式的派生周期" : "原始 Tick 模式不使用周期缓存"
            }
            Text { text: "标的数上限"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: maxSymbolsField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "500"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "批量回测标的数上限"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text {
                text: "可用区间"
                color: root.theme.textMuted
                opacity: root.selectedDataMode === "raw_ticks" ? 1.0 : 0.55
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            RowLayout {
                Layout.columnSpan: 3
                Layout.fillWidth: true
                spacing: 6
                PpButton {
                    theme: root.theme; text: "分析完整区间"; visualWidth: 104
                    available: root.selectedDataMode === "raw_ticks"
                               && !root.controller.rawTickAvailabilityLoading
                               && !root.controller.batchBacktestRunning
                    toolTip: available
                             ? "异步只读扫描当前标的集合；旧请求结果不会覆盖新请求"
                             : "只支持原始 Tick，且需等待当前分析或批量回测结束"
                    onClicked: root.analyzeAvailability()
                }
                PpButton {
                    theme: root.theme; text: "采用推荐"; visualWidth: 82
                    available: root.selectedDataMode === "raw_ticks"
                               && !root.controller.rawTickAvailabilityLoading
                               && root.availabilityInputsMatch()
                               && root.availabilityResult.hasRecommendation === true
                    toolTip: available
                             ? "先异步复核最新本地事实，再把最长完整共同区间写入日期"
                             : "先完成可用区间分析"
                    onClicked: root.applyRecommendedAvailability()
                }
                Text {
                    Layout.fillWidth: true
                    text: root.availabilityText()
                    color: root.controller.rawTickAvailabilityError.length > 0
                           ? root.theme.red
                           : root.controller.rawTickAvailabilityLoading
                             ? root.theme.yellow : root.theme.textMuted
                    elide: Text.ElideRight
                    font.family: root.theme.fontFamily; font.pixelSize: 8
                    Accessible.name: text
                }
            }
            Text {
                text: "样本外验证"
                color: root.theme.textMuted
                opacity: root.selectedDataMode === "raw_ticks" ? 1.0 : 0.55
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            CheckBox {
                id: holdoutCheck
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "固定参数时序留出"
                enabled: root.selectedDataMode === "raw_ticks"
                checked: false
                Accessible.name: "启用原始 Tick 样本外验证"
                Accessible.description: enabled
                    ? "最后若干实际交易日独立验证；训练状态不会进入验证段"
                    : "样本外验证目前只支持原始 Tick"
                onToggled: {
                    if (checked) segmentedResearchCheck.checked = false
                }
            }
            Text {
                text: "验证交易日"
                color: root.theme.textMuted
                opacity: holdoutCheck.enabled && holdoutCheck.checked ? 1.0 : 0.55
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            TextField {
                id: holdoutDaysField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "2"
                enabled: holdoutCheck.enabled && holdoutCheck.checked
                validator: IntValidator { bottom: 2; top: 250 }
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "样本外验证交易日数"
                Accessible.description: "至少两个交易日，以便 A 股 T 加一退出具有意义"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text {
                text: "验证折数"
                color: root.theme.textMuted
                opacity: holdoutCheck.enabled && holdoutCheck.checked ? 1.0 : 0.55
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            TextField {
                id: walkForwardFoldsField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "1"
                enabled: holdoutCheck.enabled && holdoutCheck.checked
                validator: IntValidator { bottom: 1; top: 20 }
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "walk-forward 验证折数"
                Accessible.description: "一折是尾部固定留出；两折以上使用扩展训练窗和互不重叠验证窗，只生成研究汇总"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text {
                Layout.columnSpan: 4
                Layout.fillWidth: true
                text: Number(walkForwardFoldsField.text) > 1
                      ? "多折结果仅用于稳定性研究，不生成可归档的单次权益曲线"
                      : "一折保持现有尾部固定参数留出和单项归档能力"
                color: Number(walkForwardFoldsField.text) > 1
                       ? root.theme.yellow : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 8
                elide: Text.ElideRight
            }
            Text {
                text: "缺口处理"
                color: root.theme.textMuted
                opacity: root.selectedDataMode === "raw_ticks" ? 1.0 : 0.55
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            CheckBox {
                id: segmentedResearchCheck
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "独立分段研究"
                enabled: root.selectedDataMode === "raw_ticks"
                         && !holdoutCheck.checked
                checked: false
                Accessible.name: "启用独立分段 Tick 研究"
                Accessible.description: enabled
                    ? "缺口两侧分别重置资金和策略状态；只显示区间分布，不生成连续权益，也不能归档"
                    : "独立分段研究只支持原始 Tick，且不能与样本外验证同时启用"
                onToggled: {
                    if (checked) holdoutCheck.checked = false
                }
            }
            Text {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                text: "分段结果不代表一条连续回测；段末未平仓会披露并在下一段重置"
                color: segmentedResearchCheck.checked
                       ? root.theme.yellow : root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 8
                Accessible.name: text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            PpButton {
                theme: root.theme; text: "启动批量回测"; primary: true; visualWidth: 132
                available: !root.controller.batchBacktestRunning
                           && !root.controller.backtestRunning
                           && strategyCombo.currentIndex >= 0
                           && startDateField.text.length > 0
                           && endDateField.text.length > 0
                           && (root.selectedDataMode === "raw_ticks"
                               || frequencyCombo.currentIndex >= 0)
                toolTip: available
                         ? (segmentedResearchCheck.checked
                            ? "按当前质量门禁切分独立研究段；不生成连续权益或归档"
                            : root.selectedDataMode === "raw_ticks"
                            ? "按日期范围完整读取每个标的的原始 Tick，不读取周期缓存"
                            : "显式使用所选周期的 Tick 派生 K 线，并按日期完整加载")
                         : "请等待当前回测结束，并选择有效模式、策略和日期范围"
                onClicked: root.startBatch()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 76
                available: root.controller.batchBacktestRunning
                toolTip: "取消批量回测；已完成项保留并可归档"
                onClicked: root.controller.cancelLocalBatchBacktest()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.batchBacktestRunning
                      ? "进度 " + (root.controller.batchBacktestProgress * 100).toFixed(1) + "%"
                        + " · " + root.controller.batchBacktestStatusText
                      : root.controller.batchBacktestStatusText
                color: root.controller.batchBacktestRunning ? root.theme.yellow : root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.batchBacktestError.length > 0
            text: root.controller.batchBacktestError
            color: root.theme.red; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.batchBacktestArchiveLoading
                     || root.controller.batchBacktestArchiveError.length > 0
            text: root.controller.batchBacktestArchiveLoading
                  ? "正在后台归档 " + root.controller.batchBacktestArchiveSymbol
                    + "；界面可继续操作"
                  : root.controller.batchBacktestArchiveError
            color: root.controller.batchBacktestArchiveLoading
                   ? root.theme.yellow : root.theme.red
            wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 168
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ListView {
                id: resultList
                anchors.fill: parent; anchors.margins: 6
                model: root.controller.batchBacktestModel
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                header: Text {
                    height: 18
                    text: "结果（" + root.controller.batchBacktestModel.count + "）"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
                delegate: Rectangle {
                    required property int index
                    required property string symbol
                    required property string name
                    required property string dataMode
                    required property var startDate
                    required property var endDate
                    required property string frequency
                    required property int actualBars
                    required property bool succeeded
                    required property string error
                    required property string datasetVersion
                    required property real totalReturnPercent
                    required property bool hasOpenPosition
                    required property real openPositionQuantity
                    required property real openPositionAveragePrice
                    required property bool archived
                    required property bool archivable
                    required property string archivedResultId
                    required property bool holdoutEnabled
                    required property int holdoutTradingDays
                    required property var trainingStartDate
                    required property var trainingEndDate
                    required property var holdoutStartDate
                    required property var holdoutEndDate
                    required property int trainingActualTicks
                    required property int holdoutActualTicks
                    required property real trainingTotalReturnPercent
                    required property real holdoutTotalReturnPercent
                    required property bool walkForwardEnabled
                    required property int walkForwardFoldCount
                    required property bool researchOnly
                    required property var walkForwardFolds
                    required property real walkForwardMeanReturnPercent
                    required property real walkForwardMedianReturnPercent
                    required property real walkForwardWorstReturnPercent
                    required property real walkForwardBestReturnPercent
                    required property bool segmentedResearch
                    required property var researchSegments
                    required property int researchSegmentCount
                    required property real segmentedMeanReturnPercent
                    required property real segmentedMedianReturnPercent
                    required property real segmentedWorstReturnPercent
                    required property real segmentedBestReturnPercent
                    required property real segmentedWorstDrawdownPercent
                    required property string dataQualityStatus
                    required property bool dataQualityEligible
                    readonly property int endingOpenSegmentCount: {
                        let count = 0
                        for (const segment of researchSegments || []) {
                            if (segment.hasEndingOpenPosition) ++count
                        }
                        return count
                    }
                    width: resultList.width
                    height: 34
                    radius: 5
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 96
                            text: symbol + "  " + name
                            color: root.theme.textPrimary
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                        Text {
                            Layout.preferredWidth: 146
                            text: succeeded
                                  ? (segmentedResearch
                                     ? researchSegmentCount + " 段 · 分段等权均值 "
                                       + segmentedMeanReturnPercent.toFixed(2) + "%"
                                     : walkForwardEnabled
                                     ? walkForwardFoldCount + " 折 · 均值 "
                                       + walkForwardMeanReturnPercent.toFixed(2) + "%"
                                     : holdoutEnabled
                                     ? "训练 " + trainingTotalReturnPercent.toFixed(2)
                                       + "% → 验证 " + holdoutTotalReturnPercent.toFixed(2) + "%"
                                     : (dataMode === "raw_ticks" ? actualBars + " 笔" : actualBars + " 根")
                                       + " · " + totalReturnPercent.toFixed(2) + "%")
                                  : "失败"
                            color: succeeded ? root.theme.cyan : root.theme.red
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                        Text {
                            Layout.fillWidth: true
                            text: succeeded
                                  ? (segmentedResearch
                                     ? "中位 " + segmentedMedianReturnPercent.toFixed(2)
                                       + "% · 最差/最好 "
                                       + segmentedWorstReturnPercent.toFixed(2) + "% / "
                                       + segmentedBestReturnPercent.toFixed(2) + "% · 最差回撤 "
                                       + segmentedWorstDrawdownPercent.toFixed(2) + "% · "
                                       + actualBars + " 笔"
                                       + (endingOpenSegmentCount > 0
                                          ? " · " + endingOpenSegmentCount + " 段末未平仓" : "")
                                       + (dataQualityEligible
                                          ? "" : " · 原请求范围门禁 " + dataQualityStatus)
                                     : walkForwardEnabled
                                     ? "验证中位数 " + walkForwardMedianReturnPercent.toFixed(2)
                                       + "% · 最差/最好 "
                                       + walkForwardWorstReturnPercent.toFixed(2) + "% / "
                                       + walkForwardBestReturnPercent.toFixed(2) + "% · 验证 "
                                       + actualBars + " 笔"
                                     : holdoutEnabled
                                     ? Qt.formatDate(trainingStartDate, "MM-dd") + "→"
                                       + Qt.formatDate(trainingEndDate, "MM-dd") + " 训练 "
                                       + trainingActualTicks + " 笔 · "
                                       + Qt.formatDate(holdoutStartDate, "MM-dd") + "→"
                                       + Qt.formatDate(holdoutEndDate, "MM-dd") + " 验证 "
                                       + holdoutActualTicks + " 笔"
                                     : (datasetVersion ? "数据集 " + datasetVersion.slice(7, 17) : ""))
                                    + (hasOpenPosition
                                       ? " · 未平仓 " + openPositionQuantity.toFixed(0)
                                         + " 股@" + openPositionAveragePrice.toFixed(3)
                                       : "")
                                  : error
                            color: root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 90
                            text: segmentedResearch ? "独立分段"
                                  : researchOnly ? "研究汇总"
                                  : archived ? "已归档"
                                  : (succeeded ? (archivable ? "可归档" : "结果已释放") : "--")
                            color: archived ? root.theme.cyan : root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        PpButton {
                            readonly property bool archivingThis:
                                root.controller.batchBacktestArchiveLoading
                                && root.controller.batchBacktestArchiveSymbol === symbol
                            Layout.preferredWidth: 72; Layout.preferredHeight: 24
                            theme: root.theme; text: archivingThis ? "归档中" : "归档"
                            available: succeeded && archivable && !archived && !researchOnly
                                       && !root.controller.batchBacktestRunning
                                       && !root.controller.batchBacktestArchiveLoading
                            toolTip: researchOnly
                                     ? (segmentedResearch
                                        ? "独立分段会重置资金和策略状态，不等于连续权益，不能归档"
                                        : "多折汇总不等于一条连续权益曲线，不能作为单次结果归档")
                                     : root.controller.batchBacktestArchiveLoading
                                     ? "已有归档任务在后台执行；完成后可继续归档其他结果"
                                     : archivable
                                     ? "后台物化并校验完整输入快照，再写入回测归档"
                                     : "内存结果已释放；请重新运行该标的后归档"
                            Accessible.name: symbol + (archivingThis ? "正在归档" : "归档")
                            Accessible.description: toolTip
                            onClicked: root.controller.archiveBatchBacktestItem(symbol)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 92
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ListView {
                anchors.fill: parent; anchors.margins: 6
                model: root.controller.batchBacktestRunHistory
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                header: Text {
                    height: 18
                    text: "运行历史（可恢复失败/取消项）"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
                delegate: Rectangle {
                    required property string runId
                    required property string strategyId
                    required property string dataMode
                    required property var startDate
                    required property var endDate
                    required property string frequency
                    required property string status
                    required property int succeededSymbols
                    required property int failedSymbols
                    required property var completedAt
                    required property int holdoutTradingDays
                    required property int walkForwardFoldCount
                    width: ListView.view.width
                    height: 30
                    radius: 5
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 96
                            text: runId.slice(0, 8)
                            color: root.theme.textMuted
                            font.family: "Consolas"; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 66
                            text: status
                            color: status === "completed" ? root.theme.cyan
                                  : status === "running" ? root.theme.yellow : root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                        Text {
                            Layout.fillWidth: true
                            text: strategyId + " · "
                                  + (dataMode === "raw_ticks" ? "原始 Tick" : frequency)
                                  + (holdoutTradingDays > 0
                                     ? " · 样本外 " + holdoutTradingDays + " 日"
                                       + (walkForwardFoldCount > 1
                                          ? " × " + walkForwardFoldCount + " 折" : "")
                                     : "")
                                  + " · " + Qt.formatDate(startDate, "yyyy-MM-dd")
                                  + " → " + Qt.formatDate(endDate, "yyyy-MM-dd")
                                  + " · 成功 " + succeededSymbols + " / 失败 " + failedSymbols
                                  + (completedAt ? " · " + Qt.formatDateTime(completedAt, "MM-dd HH:mm") : "")
                            color: root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        PpButton {
                            Layout.preferredWidth: 86; Layout.preferredHeight: 24
                            theme: root.theme; text: "恢复失败项"
                            available: status !== "running"
                                       && !root.controller.batchBacktestRunning
                                       && !root.controller.backtestRunning
                            toolTip: "只重跑该次运行中失败或取消的标的"
                            onClicked: root.controller.resumeBatchBacktest(runId)
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.controller
        function onLocalBacktestDataCatalogChanged() { root.syncFrequencyIndex() }
        function onBatchBacktestStateChanged() {
            if (frequencyCombo.currentIndex < 0) root.syncFrequencyIndex()
        }
    }
}
