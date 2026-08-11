// qmllint disable unqualified
pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root
    required property var theme
    required property var controller
    property int boardMask: 31
    property bool loading: false
    property bool logarithmic: true
    property alias summary: scatter.summary
    property alias selectedPoint: scatter.selectedPoint
    property alias renderMilliseconds: scatter.renderMilliseconds
    signal instrumentActivated(string symbol)

    radius: 10
    color: theme.bgPanel
    border.color: theme.border

    Text {
        x: 20; y: 11; width: 360; height: 28
        text: "全市场个股价格散点"
        color: theme.textPrimary
        font.family: theme.fontFamily; font.pixelSize: 16; font.weight: Font.Bold
    }
    Text {
        x: 20; y: 39; width: Math.max(300, parent.width - 620); height: 20
        elide: Text.ElideRight
        text: (controller.resolvedSnapshotTime.toString().length > 0
               ? Qt.formatDateTime(controller.resolvedSnapshotTime, "yyyy-MM-dd HH:mm:ss") : "实际快照 --")
              + " · 数据覆盖 " + controller.snapshotReceivedCount + " / " + controller.snapshotExpectedCount
              + " · 批次耗时 " + controller.snapshotLatencyMilliseconds + " ms"
        color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 10
    }

    Row {
        x: parent.width - 590
        y: 9; height: 36; spacing: 8
        PpButton { theme: root.theme; text: "范围：全部"; visualWidth: 136; available: false; toolTip: "范围缩放尚未实现" }
        PpButton { theme: root.theme; text: "X轴：股票代码"; visualWidth: 126; toolTip: "当前按全市场股票代码稳定排序" }
        PpButton { theme: root.theme; text: "Y轴：对数"; visualWidth: 122; active: root.logarithmic; onClicked: root.logarithmic = true }
        PpButton { theme: root.theme; text: "线性"; visualWidth: 86; active: !root.logarithmic; onClicked: root.logarithmic = false }
        PpButton { theme: root.theme; text: "全屏"; visualWidth: 68; available: false; toolTip: "全屏功能尚未实现" }
    }

    Text {
        x: 14; y: 58; width: 160; height: 18
        text: root.logarithmic ? "股价（元 · 对数）" : "股价（元 · 线性）"
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
    }

    Rectangle {
        id: plotFrame
        x: 66; y: 84
        width: Math.max(300, parent.width - 98)
        height: Math.max(220, parent.height - 260)
        radius: 7
        color: theme.bgInput
        border.color: theme.border
        clip: false

        ScatterPlot {
            id: scatter
            anchors.fill: parent
            anchors.margins: 1
            theme: root.theme
            model: root.controller.marketSnapshotModel
            boardMask: root.boardMask
            logarithmic: root.logarithmic
            onInstrumentActivated: function(symbol) {
                root.instrumentActivated(symbol)
            }
        }

        SnapshotStateOverlay {
            anchors.fill: parent
            theme: root.theme
            health: root.controller.snapshotHealth
            loading: root.loading
            errorText: root.controller.snapshotError
            completeness: root.controller.snapshotCompleteness
            stalenessMilliseconds: root.controller.snapshotStalenessMilliseconds
            providerHealth: root.controller.providerHealth
            providerError: root.controller.providerError
            onRetryRequested: root.controller.requestLatestMarketSnapshot()
        }
    }

    Text {
        anchors.horizontalCenter: plotFrame.horizontalCenter
        y: plotFrame.y + plotFrame.height + 31
        text: "个股序号 / 股票代码（共 " + controller.marketSnapshotModel.count + " 只）"
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
    }

    Row {
        x: 20; y: parent.height - 118; height: 36; spacing: 8
        PpButton { theme: root.theme; text: "− 缩小"; visualWidth: 72; available: false; toolTip: "首版固定全市场范围" }
        PpButton { theme: root.theme; text: "全部"; visualWidth: 82; active: true }
        PpButton { theme: root.theme; text: "＋ 放大"; visualWidth: 72; available: false; toolTip: "首版固定全市场范围" }
    }

    Rectangle {
        x: 110; y: parent.height - 84
        width: Math.max(300, parent.width - 286); height: 12; radius: 6
        color: theme.track
        Rectangle { anchors.fill: parent; radius: 6; color: root.theme.cyan; opacity: 0.32 }
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 20
        y: parent.height - 91; width: 140; height: 24
        horizontalAlignment: Text.AlignRight
        text: "1 — " + controller.marketSnapshotModel.count
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10
    }

    Text {
        x: 290; y: parent.height - 51
        text: "已缓存 " + summary.validCount + " 个有效点 · 无效 " + summary.invalidCount
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10
    }
    Row {
        x: Math.min(640, parent.width - 540)
        y: parent.height - 61; height: 36; spacing: 0
        Repeater {
            model: [
                {name: "主板", color: root.theme.cyan},
                {name: "深市主板", color: root.theme.blue},
                {name: "创业板", color: root.theme.purple},
                {name: "科创板", color: root.theme.yellow},
                {name: "北交所", color: root.theme.green}
            ]
            delegate: Item {
                required property var modelData
                width: 104; height: 36
                Rectangle { x: 0; anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: modelData.color }
                Text { x: 13; anchors.verticalCenter: parent.verticalCenter; text: modelData.name; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            }
        }
    }

    Text {
        x: 20; anchors.bottom: parent.bottom; anchors.bottomMargin: 10
        text: "悬停查看最近点 · 单击固定选择 · 导出/全屏/双击多窗口暂未实现"
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9
    }
}
