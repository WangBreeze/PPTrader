// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var theme
    required property var controller
    property int boardMask: 31
    readonly property bool loading: controller.snapshotLoading
    property bool autoRefresh: false
    property bool summaryOpen: false
    readonly property bool compact: width < 1360
    property alias renderMilliseconds: chart.renderMilliseconds

    function refreshLatest() {
        if (loading) return
        root.controller.requestLatestMarketSnapshot()
    }

    Timer {
        interval: 5000
        repeat: true
        running: root.autoRefresh
        onTriggered: root.refreshLatest()
    }

    onCompactChanged: if (!compact) summaryOpen = false

    Text {
        x: 20; y: 8; width: 560; height: 34
        text: "A股全市场 · 时刻股价分布"
        color: theme.textPrimary
        font.family: theme.fontFamily; font.pixelSize: 24; font.weight: Font.Bold
    }
    Text {
        x: 20; y: 44; width: 560; height: 22
        text: "X：股票代码稳定序列  ·  Y：快照价格  ·  当前 "
              + controller.marketSnapshotModel.count + " 只"
        color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 11
    }
    Rectangle {
        x: 600; y: 18; width: 122; height: 28; radius: 6
        color: theme.bgSelected; border.color: theme.cyan
        Text { anchors.centerIn: parent; text: "横截面快照"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
    }

    SnapshotTimeControls {
        id: timeControls
        x: 20; y: 74; width: parent.width - 40
        theme: root.theme; controller: root.controller
        busy: root.loading
    }

    Item {
        id: mainArea
        x: 20
        y: timeControls.y + timeControls.height + 16
        width: parent.width - 40
        height: parent.height - y - 16

        CrossSectionChartCard {
            id: chart
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            anchors.right: root.compact ? parent.right : summaryHost.left
            anchors.rightMargin: root.compact ? 0 : 16
            theme: root.theme; controller: root.controller
            boardMask: root.boardMask
            loading: root.loading
            onInstrumentActivated: function(symbol) {
                root.controller.openInstrumentResearch(symbol)
            }
        }

        PpButton {
            visible: root.compact
            z: 32
            anchors.right: parent.right; anchors.top: parent.top
            anchors.rightMargin: 12; anchors.topMargin: 44
            theme: root.theme; text: root.summaryOpen ? "关闭摘要" : "市场摘要"
            visualWidth: 92; active: root.summaryOpen
            onClicked: root.summaryOpen = !root.summaryOpen
        }

        Rectangle {
            visible: root.compact && root.summaryOpen
            z: 30
            anchors.fill: parent
            color: Qt.rgba(6 / 255, 9 / 255, 13 / 255, 0.45)
            MouseArea { anchors.fill: parent; onClicked: root.summaryOpen = false }
        }
        Flickable {
            id: summaryHost
            visible: !root.compact || root.summaryOpen
            z: root.compact ? 31 : 0
            anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 318
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, 690)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: summaryHost.contentHeight > summaryHost.height
                                                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            SnapshotSummaryPanel {
                width: summaryHost.width
                height: Math.max(690, summaryHost.height)
                theme: root.theme; controller: root.controller
                summary: chart.summary; selectedPoint: chart.selectedPoint
                boardMask: root.boardMask; autoRefresh: root.autoRefresh
                onBoardMaskRequested: function(mask) { root.boardMask = mask }
                onAutoRefreshRequested: function(enabled) { root.autoRefresh = enabled }
            }
        }
    }

}
