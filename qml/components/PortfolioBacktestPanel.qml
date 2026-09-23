// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    property var legList: []

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

    function periodSecondsFor(frequency) {
        const catalog = root.controller.localBacktestDataCatalog || []
        for (const entry of catalog) {
            if (entry.frequency === frequency) return entry.periodSeconds
        }
        return 60
    }

    function parseLegs() {
        const result = []
        const codes = legsField.text.split(/[,，\s]+/)
        const catalog = root.controller.localBacktestDataCatalog || []
        for (const code of codes) {
            if (code.length !== 6) continue
            let match = null
            for (const entry of catalog) {
                if (entry.symbol === code) { match = entry; break }
            }
            if (match) {
                result.push({symbol: match.symbol, name: match.name})
            }
        }
        root.legList = result
    }

    function runPortfolio() {
        root.parseLegs()
        if (root.legList.length < 2) return
        const legs = root.legList.map(leg => ({
            symbol: leg.symbol, name: leg.name, targetWeight: 0.0
        }))
        const definition = {
            portfolioId: "portfolio_ui",
            name: nameField.text || "组合回测",
            periodSeconds: root.periodSecondsFor(frequencyCombo.currentText) || 60,
            rebalanceEveryTradingDays: Math.max(1, Number(rebalanceField.text) || 1),
            initialCapital: root.controller.backtestConfiguredInitialCapital,
            commissionRate: root.controller.backtestCommissionRate,
            slippageRate: root.controller.backtestSlippageRate,
            lotSize: 100,
            tPlusOne: true,
            maximumLegWeight: 1.0,
            maximumActiveLegs: 0,
            maximumConsecutiveMissingBars: 0,
            maximumPortfolioLossPercent: Number(maximumPortfolioLossField.text),
            maximumPortfolioDrawdownPercent: Number(maximumPortfolioDrawdownField.text),
            legs: legs
        }
        root.controller.runPortfolioBacktest(
            JSON.stringify(definition), Math.max(2, Number(barCountField.text) || 200))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "组合资金回测"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: "共享资金池 · 观测交易日并集逐日估值 · 共同交易日调仓 · 只读取 raw_tick_cache_v1"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 8; rowSpacing: 6
            Text { text: "组合名称"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: nameField
                Layout.fillWidth: true; Layout.preferredHeight: 28
                text: "组合回测"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: frequencyCombo
                Layout.fillWidth: true; Layout.preferredHeight: 28
                model: root.frequencyModel()
                Component.onCompleted: root.syncFrequencyIndex()
                Accessible.name: "组合回测周期"
            }
            Text { text: "调仓间隔(日)"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: rebalanceField
                Layout.fillWidth: true; Layout.preferredHeight: 28
                text: "1"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "回测根数"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: barCountField
                Layout.fillWidth: true; Layout.preferredHeight: 28
                text: "200"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "初始资金"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text {
                Layout.fillWidth: true
                text: Number(root.controller.backtestConfiguredInitialCapital).toLocaleString(Qt.locale("zh_CN"))
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
            }
            Text { text: "累计亏损熔断%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: maximumPortfolioLossField
                Layout.fillWidth: true; Layout.preferredHeight: 28
                text: "0"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "固定组合累计亏损熔断百分比"
                Accessible.description: "相对初始资金计算，输入零表示关闭"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "峰值回撤熔断%"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: maximumPortfolioDrawdownField
                Layout.fillWidth: true; Layout.preferredHeight: 28
                text: "0"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                Accessible.name: "固定组合峰值回撤熔断百分比"
                Accessible.description: "相对运行中最高权益计算，输入零表示关闭"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: legsField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                placeholderText: "输入腿代码，逗号或空格分隔（如 600519 000001）"
                placeholderTextColor: root.theme.textMuted
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            PpButton {
                theme: root.theme; text: "解析腿"; visualWidth: 72
                onClicked: root.parseLegs()
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.legList.length > 0
                  ? "已解析 " + root.legList.length + " 腿：" + root.legList.map(leg => leg.symbol).join(" ")
                  : "等权分配；至少 2 腿"
            color: root.legList.length >= 2 ? root.theme.cyan : root.theme.yellow
            elide: Text.ElideRight
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        RowLayout {
            Layout.fillWidth: true
            PpButton {
                theme: root.theme; text: "运行组合回测"; primary: true; visualWidth: 140
                available: root.legList.length >= 2
                           && !root.controller.portfolioBacktestRunning
                           && !root.controller.batchBacktestRunning
                           && !root.controller.backtestRunning
                toolTip: available ? "对共享资金池运行固定权重定期再平衡回测"
                                   : "请先解析至少 2 个本地 Tick 数据集腿并等待其他回测结束"
                onClicked: root.runPortfolio()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 76
                available: root.controller.portfolioBacktestRunning
                onClicked: root.controller.cancelPortfolioBacktest()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.portfolioBacktestStatusText
                color: root.controller.portfolioBacktestRunning ? root.theme.yellow : root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.portfolioBacktestError.length > 0
            text: root.controller.portfolioBacktestError
            color: root.theme.red; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 150
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            BacktestEquityChart {
                anchors.fill: parent; anchors.margins: 8
                theme: root.theme
                model: root.controller.portfolioEquityCurveModel
            }
        }
        RowLayout {
            Layout.fillWidth: true
            readonly property var config: root.controller.portfolioResultConfiguration
            readonly property var risk: (config || ({})).riskSummary || ({})
            Text {
                Layout.fillWidth: true
                text: parent.config && parent.config.finalEquity
                      ? "最终权益 " + Number(parent.config.finalEquity).toFixed(2)
                        + " · 收益 " + Number(parent.config.totalReturnPercent).toFixed(2) + "%"
                        + " · 回撤 " + Number(parent.config.maxDrawdownPercent).toFixed(2) + "%"
                        + " · Sharpe " + Number(parent.config.sharpeRatio).toFixed(3)
                        + " · 手续费 " + Number(parent.config.totalCommission).toFixed(2)
                        + " · 调仓 " + parent.config.totalTrades + " 笔"
                        + (Number(parent.config.staleValuationPointCount) > 0
                            ? " · 缺日报价估值 " + parent.config.staleValuationPointCount + " 点"
                            : "")
                        + (parent.risk.circuitBreakerTriggered
                           ? " · 已熔断（"
                             + (parent.risk.circuitBreakerReason === "drawdown_limit"
                                ? "峰值回撤" : parent.risk.circuitBreakerReason === "loss_limit"
                                  ? "累计亏损" : "亏损与回撤")
                             + "）· 已退出 " + parent.risk.completedCircuitBreakerExitCount
                             + " · 待退出 " + parent.risk.pendingCircuitBreakerExitCount
                           : "")
                       : "尚未运行组合回测"
                color: root.theme.textPrimary; elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }
        Text {
            Layout.fillWidth: true
            readonly property var calendarRisk:
                (root.controller.portfolioResultConfiguration || ({})).riskSummary || ({})
            visible: calendarRisk.calendarSnapshotVersion !== undefined
                     && String(calendarRisk.calendarSnapshotVersion).length > 0
            text: "交易日历 "
                  + (calendarRisk.marketCalendarCoverageComplete
                     ? "权威覆盖" : "覆盖不完整")
                  + " · 停牌事实 " + Number(calendarRisk.authoritativeSuspensionPointCount)
                  + " 腿日 · 未知缺报价 " + Number(calendarRisk.unknownMissingQuotePointCount)
                  + " 腿日 · 日历补估值 " + Number(calendarRisk.calendarOnlyValuationPointCount)
                  + " 日"
            color: calendarRisk.instrumentStatusCoverageComplete
                   ? root.theme.textMuted : root.theme.yellow
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: text
            Accessible.description: "缺少权威停复牌事实时保持未知，不从缺失 K 线推断停牌"
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 96
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ListView {
                anchors.fill: parent; anchors.margins: 6
                model: root.controller.portfolioTradeModel
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                header: Text {
                    height: 16
                    text: "调仓成交（" + root.controller.portfolioTradeModel.count + "）"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
                delegate: Rectangle {
                    required property string symbol
                    required property string side
                    required property double quantity
                    required property double price
                    required property double commission
                    required property double amount
                    required property var timestamp
                    required property string reason
                    width: ListView.view.width
                    height: 22
                    radius: 4
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 64
                            text: symbol
                            color: root.theme.textPrimary
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 30
                            text: side === "buy" ? "买" : "卖"
                            color: side === "buy" ? root.theme.red : root.theme.cyan
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 76
                            text: quantity + " 股 @" + Number(price).toFixed(2)
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 76
                            text: "金额 " + Number(amount).toFixed(0) + " · 费 " + Number(commission).toFixed(2)
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.fillWidth: true
                            text: reason + " · " + Qt.formatDateTime(timestamp, "MM-dd HH:mm")
                            color: root.theme.textMuted; elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 78
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ListView {
                anchors.fill: parent; anchors.margins: 6
                model: root.controller.portfolioArchiveModel
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds
                header: Text {
                    height: 16
                    text: "组合归档（" + root.controller.portfolioArchiveModel.count + "）"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
                delegate: Rectangle {
                    required property string resultId
                    required property string name
                    required property int legCount
                    required property int barCount
                    required property double totalReturnPercent
                    required property var completedAt
                    width: ListView.view.width
                    height: 22
                    radius: 4
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: name + " · " + legCount + " 腿 · " + barCount + " 日"
                                  + " · 收益 " + Number(totalReturnPercent).toFixed(2) + "%"
                                  + " · " + Qt.formatDateTime(completedAt, "MM-dd HH:mm")
                            color: root.theme.textMuted; elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        PpButton {
                            Layout.preferredWidth: 72; Layout.preferredHeight: 20
                            theme: root.theme; text: "加载"
                            available: !root.controller.portfolioArchiveLoading
                            toolTip: "从 SQLite 恢复组合权益曲线与调仓成交"
                            onClicked: root.controller.loadPortfolioBacktestArchive(resultId)
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: root.controller
        function onLocalBacktestDataCatalogChanged() { root.syncFrequencyIndex() }
    }
}
