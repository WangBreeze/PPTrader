// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var theme
    required property var controller
    property string title: "搜索 A 股"

    Text {
        x: 0; y: 0; width: parent.width; height: 24
        text: root.title
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily
        font.pixelSize: 14
        font.weight: Font.Bold
    }

    TextField {
        id: searchField
        x: 0; y: 32; width: parent.width; height: 40
        text: root.controller.stockSearchText
        placeholderText: "输入股票代码或名称"
        color: root.theme.textPrimary
        placeholderTextColor: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 12
        activeFocusOnTab: true
        Accessible.name: "A股品种搜索"
        Accessible.description: "输入代码或名称，回车打开首个结果"
        onTextEdited: root.controller.stockSearchText = text
        onAccepted: {
            if (root.controller.searchModel.resultCount > 0) {
                var first = root.controller.searchModel.get(0)
                root.controller.openInstrumentResearch(first.code)
            }
        }
        background: Rectangle {
            readonly property bool keyboardFocus: searchField.activeFocus
                                                  && searchField.focusReason !== Qt.MouseFocusReason
                                                  && searchField.focusReason !== Qt.OtherFocusReason
            radius: 7
            color: root.theme.bgInput
            border.width: keyboardFocus ? 2 : 1
            border.color: keyboardFocus ? root.theme.cyan : root.theme.border
        }
    }

    Text {
        x: 0; y: 80; width: parent.width; height: 18
        text: root.controller.stockSearchText.length > 0
              ? "匹配 " + root.controller.searchModel.resultCount + " 条，最多显示 50 条"
              : "搜索结果来自本地 A 股股票池"
        color: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 9
    }

    ListView {
        id: resultList
        x: 0; y: 102; width: parent.width; height: Math.max(0, parent.height - 102)
        model: root.controller.searchModel
        clip: true
        spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Control {
            id: resultRow
            required property string code
            required property string name
            required property string market
            width: ListView.view.width
            height: 36
            activeFocusOnTab: true
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.ListItem
            Accessible.name: code + " " + name
            Accessible.description: market + "，打开指标与策略"
            Accessible.focusable: true

            function activate() {
                root.controller.openInstrumentResearch(resultRow.code)
            }

            Keys.onReturnPressed: function(event) {
                resultRow.activate()
                event.accepted = true
            }
            Keys.onSpacePressed: function(event) {
                resultRow.activate()
                event.accepted = true
            }
            MouseArea {
                id: resultMouse
                anchors.fill: parent
                hoverEnabled: true
                onPressed: resultRow.forceActiveFocus(Qt.MouseFocusReason)
                onClicked: resultRow.activate()
            }
            background: Rectangle {
                radius: 6
                color: resultMouse.containsMouse || resultRow.activeFocus
                       ? root.theme.bgSelected : root.theme.bgInput
                border.width: resultRow.visualFocus ? 2 : 1
                border.color: resultRow.visualFocus ? root.theme.cyan : root.theme.border
            }
            contentItem: Item {
                Row {
                    x: 10; width: parent.width - 20; height: parent.height
                    spacing: 10
                    Text {
                        width: 72; anchors.verticalCenter: parent.verticalCenter
                        text: resultRow.code; color: root.theme.cyan
                        font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                    }
                    Text {
                        width: Math.max(80, resultRow.width - 204)
                        anchors.verticalCenter: parent.verticalCenter
                        text: resultRow.name; elide: Text.ElideRight
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 11
                    }
                    Text {
                        width: 72; anchors.verticalCenter: parent.verticalCenter
                        text: resultRow.market; horizontalAlignment: Text.AlignRight
                        color: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                    }
                }
            }
        }
    }
}
