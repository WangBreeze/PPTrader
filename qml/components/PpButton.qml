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
    property bool available: true
    property real visualHeight: 28
    property real visualWidth: 86
    signal clicked()

    implicitWidth: visualWidth
    implicitHeight: Math.max(36, visualHeight)
    opacity: available ? 1.0 : 0.45
    activeFocusOnTab: available
    focusPolicy: available ? Qt.TabFocus : Qt.NoFocus

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
        radius: 6
        color: root.success ? root.theme.bgSuccess
                            : root.primary ? root.theme.cyan
                            : root.active ? root.theme.bgSelected : root.theme.bgRaised
        border.width: 1
        border.color: root.success ? root.theme.green
                      : root.active || mouse.containsMouse
                        ? root.theme.cyan : root.theme.border

        Text {
            anchors.centerIn: parent
            text: root.text
            color: root.success ? root.theme.green
                   : root.primary ? root.theme.bgCanvas
                   : root.active ? root.theme.cyan : root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            font.weight: root.primary || root.active || root.success
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
        color: root.theme.bgRaised
        border.color: root.theme.border
        Text {
            id: tipText
            anchors.centerIn: parent
            text: root.toolTip
            color: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 10
        }
    }
}
