// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var theme
    required property var controller
    readonly property var selectedSummary: archiveList.currentIndex >= 0
        ? controller.backtestArchiveModel.get(archiveList.currentIndex) : ({})

    function restoreSelection() {
        const selected = controller.selectedBacktestArchiveId
        const row = selected.length > 0
            ? controller.backtestArchiveModel.indexOfResultId(selected) : -1
        if (row >= 0)
            archiveList.currentIndex = row
        else if (archiveList.currentIndex >= controller.backtestArchiveCount)
            archiveList.currentIndex = controller.backtestArchiveCount > 0 ? 0 : -1
    }

    Text {
        x: 0; y: 0; text: "已归档回测"
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
    }
    Text {
        anchors.right: parent.right; y: 3
        text: root.controller.backtestArchiveCount + " 条 · SQLite 本地只读恢复"
        color: root.theme.textMuted
        font.family: root.theme.fontFamily; font.pixelSize: 9
    }

    Rectangle {
        x: 0; y: 32; width: Math.max(420, parent.width - 340); height: parent.height - 32
        radius: 7; color: root.theme.bgInput; border.color: root.theme.border
        ListView {
            id: archiveList
            anchors.fill: parent; anchors.margins: 6
            model: root.controller.backtestArchiveModel
            clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1; keyNavigationEnabled: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: archiveDelegate
                required property int index
                required property string resultId
                required property string taskId
                required property var completedAt
                required property string strategyId
                required property int strategyVersion
                required property string symbol
                required property string name
                required property string frequency
                required property int actualBars
                required property real totalReturnPercent
                required property int totalTrades
                width: ListView.view.width; height: 47
                activeFocusOnTab: true; highlighted: ListView.isCurrentItem
                onClicked: archiveList.currentIndex = index
                Keys.onReturnPressed: archiveList.currentIndex = index
                Keys.onEnterPressed: archiveList.currentIndex = index
                Accessible.name: "归档回测 " + symbol + " " + strategyId
                    + "，完成于 " + Qt.formatDateTime(completedAt, "yyyy-MM-dd HH:mm:ss")
                Accessible.description: "按回车选择，再使用右侧按钮从 SQLite 加载"
                background: Rectangle {
                    radius: 5
                    color: archiveDelegate.highlighted ? root.theme.bgSelected
                                                       : (archiveDelegate.hovered ? root.theme.bgRaised : "transparent")
                    border.color: archiveDelegate.activeFocus ? root.theme.cyan : "transparent"
                    border.width: archiveDelegate.activeFocus ? 2 : 1
                }
                contentItem: Item {
                    Text {
                        x: 8; y: 5; width: parent.width - 178
                        text: symbol + " " + name + " · " + strategyId
                              + (strategyVersion > 0 ? " v" + strategyVersion : "")
                        color: root.theme.textPrimary; elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold
                    }
                    Text {
                        x: 8; y: 25; width: parent.width - 178
                        text: Qt.formatDateTime(completedAt, "yyyy-MM-dd HH:mm:ss")
                              + " · " + frequency + " · " + actualBars + " 根 · 任务 " + taskId
                        color: root.theme.textMuted; elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 8
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 8; y: 8
                        text: (totalReturnPercent >= 0 ? "+" : "")
                              + totalReturnPercent.toFixed(2) + "% · " + totalTrades + " 笔"
                        color: totalReturnPercent >= 0 ? root.theme.green : root.theme.red
                        font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.controller.backtestArchiveCount === 0
            text: "尚无归档结果\n成功完成回测后会自动事务化保存"
            horizontalAlignment: Text.AlignHCenter
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 10
            Accessible.name: text
        }
    }

    Rectangle {
        anchors.right: parent.right; y: 32; width: 324; height: parent.height - 32
        radius: 7; color: root.theme.bgInput; border.color: root.theme.border
        Text {
            x: 12; y: 10; width: parent.width - 24
            text: root.selectedSummary.resultId
                  ? root.selectedSummary.symbol + " · " + root.selectedSummary.strategyId
                  : "选择一条归档查看身份"
            color: root.theme.textPrimary; elide: Text.ElideRight
            font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
        }
        Text {
            x: 12; y: 36; width: parent.width - 24; height: 72
            text: root.selectedSummary.resultId
                ? "请求/实际：" + root.selectedSummary.requestedBars + "/" + root.selectedSummary.actualBars
                  + " 根\n结果 ID：" + root.selectedSummary.resultId
                  + "\ndataVersion：" + root.selectedSummary.dataVersion
                : "归档加载只读取 SQLite，不联网，也不会重新运行策略。"
            color: root.theme.textMuted; wrapMode: Text.WrapAnywhere
            font.family: root.theme.fontFamily; font.pixelSize: 8
            Accessible.name: text
        }
        Text {
            x: 12; y: 112; width: parent.width - 24; height: 34
            visible: root.controller.backtestResultHistoricalSnapshot
                     && root.controller.selectedBacktestArchiveId === root.selectedSummary.resultId
            text: "历史快照，与当前策略、参数或数据缓存可能不同"
            color: root.theme.yellow; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
            Accessible.name: text
        }
        PpButton {
            x: 12; y: parent.height - 42
            theme: root.theme; text: root.controller.backtestArchiveLoading ? "加载中" : "加载所选结果"
            visualWidth: 116; primary: true
            available: !!root.selectedSummary.resultId && !root.controller.backtestRunning
                       && !root.controller.backtestArchiveLoading
            toolTip: available ? "从 SQLite 恢复权益曲线、成交与不可变身份"
                               : "请先选择归档，且等待当前回测结束"
            Accessible.name: "加载所选归档回测"; Accessible.description: toolTip
            onClicked: root.controller.loadBacktestArchive(root.selectedSummary.resultId)
        }
        PpButton {
            x: 138; y: parent.height - 42
            theme: root.theme; text: "刷新列表"; visualWidth: 88
            available: !root.controller.backtestArchiveLoading
            toolTip: "仅刷新 SQLite 归档目录，不访问网络"
            Accessible.name: "刷新归档回测列表"
            onClicked: root.controller.refreshBacktestArchiveList()
        }
    }

    Text {
        x: Math.max(420, parent.width - 340) + 16; y: 4
        width: 320; visible: root.controller.backtestArchiveError.length > 0
                            || root.controller.backtestArchiveSaveError.length > 0
        text: root.controller.backtestArchiveError.length > 0
              ? root.controller.backtestArchiveError : root.controller.backtestArchiveSaveError
        color: root.theme.red; elide: Text.ElideRight
        font.family: root.theme.fontFamily; font.pixelSize: 9
        Accessible.name: text
    }

    Connections {
        target: root.controller.backtestArchiveModel
        function onModelReset() { root.restoreSelection() }
        function onCountChanged() { root.restoreSelection() }
    }
    Connections {
        target: root.controller
        function onBacktestArchiveStateChanged() { root.restoreSelection() }
    }
}
