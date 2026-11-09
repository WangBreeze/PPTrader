// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Control {
    id: root
    property var theme
    property string text: ""
    property string toolTip: ""
    property bool active: false
    property bool primary: false
    property bool success: false
    property bool danger: false
    property bool available: true
    property real visualHeight: 28
    property real visualWidth: 86
    signal clicked()

    implicitWidth: visualWidth
    implicitHeight: Math.max(36, visualHeight)
    // 禁用态只降低按钮本体透明度；悬停说明必须保持完全不透明、可读。
    opacity: 1.0
    // 不要把 activeFocusOnTab / focusPolicy 在“已持有 active focus”的项上翻成 false：
    // Qt 6.11 会拒绝该赋值并打印
    //   QQuickItem: Cannot set activeFocusOnTab to false once item is the active focus item.
    // focusPolicy 走同一处检查，所以一个按钮翻一次会打印两条。这里保留“不可用即不可
    // Tab 聚焦”的语义，但在该项仍持有焦点时保持可聚焦，等焦点离开后再退出 Tab 链。
    // 也不能改成 enabled: available：禁用 Control 会连带禁用子 MouseArea，使禁用按钮
    // 的悬停说明失效，违背本组件“禁用态仍可读 tooltip”的要求。
    activeFocusOnTab: available || root.activeFocus
    focusPolicy: (available || root.activeFocus) ? Qt.TabFocus : Qt.NoFocus

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: toolTip
    Accessible.focusable: available
    Accessible.ignored: !available
    Accessible.onPressAction: if (root.available) root.clicked()

    Item {
        anchors.fill: parent
        enabled: root.available
        Accessible.role: Accessible.Button
        Accessible.name: root.text
        Accessible.description: root.toolTip
        Accessible.focusable: false
        Accessible.ignored: root.available
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.visualWidth + 6
        height: root.visualHeight + 6
        radius: 8
        visible: root.visualFocus
        color: "transparent"
        border.width: 1
        border.color: root.theme.cyan
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.visualWidth
        height: root.visualHeight
        opacity: root.available ? 1.0 : 0.45
        radius: 6
        color: root.success ? root.theme.bgSuccess
                            : root.danger ? root.theme.bgRaised
                            : root.primary ? root.theme.cyan
                            : root.active ? root.theme.bgSelected : root.theme.bgRaised
        border.width: 1
        border.color: root.success ? root.theme.green
                      : root.danger ? root.theme.red
                      : root.active || mouse.containsMouse
                        ? root.theme.cyan : root.theme.border

        Text {
            anchors.centerIn: parent
            text: root.text
            color: root.success ? root.theme.green
                   : root.danger ? root.theme.red
                   : root.primary ? root.theme.bgCanvas
                   : root.active ? root.theme.cyan : root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            font.weight: root.primary || root.active || root.success || root.danger
                         ? Font.Bold : Font.Medium
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onPressed: root.forceActiveFocus(Qt.MouseFocusReason)
        onClicked: if (root.available) root.clicked()
    }

    Keys.onSpacePressed: function(event) {
        if (root.available && !event.isAutoRepeat) {
            root.clicked()
            event.accepted = true
        }
    }
    Keys.onReturnPressed: function(event) {
        if (root.available && !event.isAutoRepeat) {
            root.clicked()
            event.accepted = true
        }
    }

    Rectangle {
        visible: mouse.containsMouse && root.toolTip.length > 0
        z: 20
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: 3
        width: tipText.implicitWidth + 16
        height: 26
        radius: 6
        opacity: 1.0
        color: root.theme.bgBar
        border.width: 1
        border.color: root.theme.cyan
        Text {
            id: tipText
            anchors.centerIn: parent
            text: root.toolTip
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: 10
        }
    }
}
