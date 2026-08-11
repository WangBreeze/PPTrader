// qmllint disable unqualified
import QtQuick

Rectangle {
    id: root
    required property var theme
    required property var controller
    height: 64
    color: theme.bgBar
    border.color: theme.border

    Rectangle {
        x: 16; y: 15; width: 34; height: 34; radius: 8
        color: theme.cyan
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
    Text {
        x: 62; y: 34; text: "量化交易工作台"; color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 10
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 14
        height: 36
        spacing: 10

        PpButton {
            theme: root.theme
            visualWidth: 118
            text: root.controller.liveRunning ? "行情任务运行" : "行情任务停止"
            active: root.controller.liveRunning
            toolTip: "仅表示行情任务状态；当前路由："
                     + (root.controller.activeRealtimeProviderName || "未选择")
                     + "，切换次数：" + root.controller.realtimeFailoverCount
            onClicked: root.controller.liveRunning
                       ? root.controller.stopLive() : root.controller.startLive()
        }
        PpButton { theme: root.theme; visualWidth: 114; text: "A股 · 全市场" }
        PpButton { theme: root.theme; visualWidth: 82; text: "设置 ⋯"; available: false; toolTip: "设置页尚未实现" }
    }
}
