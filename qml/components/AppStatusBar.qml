// qmllint disable unqualified
import QtQuick

Rectangle {
    id: root
    required property var theme
    required property var controller
    property real renderMilliseconds: -1
    height: 48
    color: theme.bgBar
    border.color: theme.border

    readonly property color healthColor: controller.providerHealth === 2 ? theme.green
                                          : controller.providerHealth === 4 ? theme.red : theme.yellow

    Rectangle {
        x: 16; y: 20; width: 8; height: 8; radius: 4
        color: root.healthColor
    }
    Text {
        x: 30; y: 10; width: Math.min(540, parent.width * 0.32); height: 28
        verticalAlignment: Text.AlignVCenter
        color: theme.textMuted
        elide: Text.ElideRight
        text: (controller.activeRealtimeProviderName || "无可用实时源")
              + (controller.realtimeFallbackUsed ? " [备用]" : "")
              + " · " + controller.providerHealthText
              + " · 评分 " + controller.realtimeProviderScore
              + " · 源覆盖 " + (controller.providerCompleteness * 100).toFixed(1) + "%"
              + " · 快照 " + controller.snapshotHealthText
        font.family: theme.fontFamily; font.pixelSize: 10
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 10; height: 28; verticalAlignment: Text.AlignVCenter
        color: theme.cyan
        text: controller.marketSnapshotModel.count + " 只 · "
              + (controller.resolvedSnapshotTime.toString().length > 0
                 ? Qt.formatDateTime(controller.resolvedSnapshotTime, "yyyy-MM-dd HH:mm:ss") : "--")
        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 30
        y: 10; height: 28; verticalAlignment: Text.AlignVCenter
        color: theme.textMuted
        text: "CPU -- · MEM -- · SG CPU "
              + (root.renderMilliseconds >= 0 ? root.renderMilliseconds.toFixed(1) + " ms" : "--")
        font.family: theme.fontFamily; font.pixelSize: 10
    }
}
