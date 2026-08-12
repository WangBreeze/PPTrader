// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

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

    function startBatch() {
        const strategyId = strategyCombo.currentIndex >= 0
            ? root.controller.backtestStrategyCatalog[strategyCombo.currentIndex].id : ""
        const frequency = frequencyCombo.currentIndex >= 0
            ? root.frequencyModel()[frequencyCombo.currentIndex] : ""
        const bars = Math.max(2, Number(barCountField.text) || root.controller.backtestBarCount)
        const symbols = Math.max(1, Number(maxSymbolsField.text) || 500)
        root.controller.runLocalBatchBacktest(
            strategyId, root.controller.selectedBacktestParameters,
            frequency, bars, symbols)
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
            text: "同一内置策略 · 每标独立资金 · 只读取 raw_tick_cache_v1 · 结果可逐项归档与恢复"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 8; rowSpacing: 6
            Text { text: "策略"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: strategyCombo
                Layout.fillWidth: true; Layout.preferredHeight: 30
                model: root.controller.backtestStrategyCatalog
                textRole: "name"; valueRole: "id"
                Component.onCompleted: {
                    for (let index = 0; index < model.length; ++index) {
                        if (model[index].id === root.controller.selectedBacktestStrategyId) {
                            currentIndex = index
                            break
                        }
                    }
                }
                Accessible.name: "批量回测策略"
            }
            Text { text: "周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: frequencyCombo
                Layout.fillWidth: true; Layout.preferredHeight: 30
                model: root.frequencyModel()
                Component.onCompleted: root.syncFrequencyIndex()
                Accessible.name: "批量回测周期"
            }
            Text { text: "根数"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: barCountField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: String(root.controller.backtestBarCount)
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { text: "标的数上限"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: maxSymbolsField
                Layout.fillWidth: true; Layout.preferredHeight: 30
                text: "500"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            PpButton {
                theme: root.theme; text: "启动批量回测"; primary: true; visualWidth: 132
                available: !root.controller.batchBacktestRunning
                           && !root.controller.backtestRunning
                           && strategyCombo.currentIndex >= 0
                           && frequencyCombo.currentIndex >= 0
                toolTip: available ? "对本地目录中同一周期的 Tick 派生数据集运行独立回测"
                                   : "请等待当前回测结束并选择有效策略与周期"
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
                    required property string frequency
                    required property bool succeeded
                    required property string error
                    required property string datasetVersion
                    required property real totalReturnPercent
                    required property bool archived
                    required property string archivedResultId
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
                            Layout.preferredWidth: 86
                            text: succeeded ? "收益 " + totalReturnPercent.toFixed(2) + "%" : "失败"
                            color: succeeded ? root.theme.cyan : root.theme.red
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                        Text {
                            Layout.fillWidth: true
                            text: succeeded
                                  ? (datasetVersion ? "数据集 " + datasetVersion.slice(7, 17) : "")
                                  : error
                            color: root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 90
                            text: archived ? "已归档" : (succeeded ? "可归档" : "--")
                            color: archived ? root.theme.cyan : root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        PpButton {
                            Layout.preferredWidth: 72; Layout.preferredHeight: 24
                            theme: root.theme; text: "归档"
                            available: succeeded && !archived
                                       && !root.controller.batchBacktestRunning
                            toolTip: "把该单项的权益曲线、成交与数据集身份写入回测归档"
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
                    required property string frequency
                    required property string status
                    required property int succeededSymbols
                    required property int failedSymbols
                    required property var completedAt
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
                            text: strategyId + " · " + frequency
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
