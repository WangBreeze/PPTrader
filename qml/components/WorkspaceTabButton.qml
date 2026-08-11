import QtQuick
import QtQuick.Controls

TabButton {
    id: root
    required property var theme
    required property var tabBar
    required property int tabIndex

    implicitHeight: 38
    focusPolicy: Qt.StrongFocus
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.description: "切换到" + text
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.focusable: true

    function focusTab(index) {
        var count = root.tabBar.count
        var target = (index + count) % count
        root.tabBar.currentIndex = target
        var item = root.tabBar.itemAt(target)
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason)
    }

    onClicked: {
        tabBar.currentIndex = tabIndex
        root.forceActiveFocus(Qt.MouseFocusReason)
    }
    Keys.onLeftPressed: function(event) {
        root.focusTab(root.tabIndex - 1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        root.focusTab(root.tabIndex + 1)
        event.accepted = true
    }
    Keys.onReturnPressed: function(event) {
        root.tabBar.currentIndex = root.tabIndex
        event.accepted = true
    }
    Keys.onSpacePressed: function(event) {
        root.tabBar.currentIndex = root.tabIndex
        event.accepted = true
    }

    contentItem: Text {
        text: root.text
        color: root.checked ? root.theme.cyan : root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 11
        font.weight: root.checked ? Font.Bold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 7
        color: root.checked ? root.theme.bgSelected
                            : root.hovered ? root.theme.bgRaised : "transparent"
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus || root.checked
                      ? root.theme.cyan : root.theme.border
    }
}
