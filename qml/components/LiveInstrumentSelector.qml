// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    property string query: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: "实时品种"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: root.controller.selectedLiveInstrumentTitle.length > 0
                  ? root.controller.selectedLiveInstrumentTitle : "尚未选择 A 股"
            color: root.controller.selectedLiveSymbol.length > 0
                   ? root.theme.cyan : root.theme.textMuted
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
        TextField {
            id: liveSearch
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            placeholderText: "搜索代码或名称"
            color: root.theme.textPrimary
            placeholderTextColor: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            activeFocusOnTab: true
            Accessible.name: "搜索实时 A 股品种"
            Accessible.description: "输入本地股票池中的代码或名称，回车选择首项"
            onTextEdited: {
                root.query = text
                root.controller.searchStocks(text)
            }
            onAccepted: {
                if (root.controller.searchModel.resultCount > 0) {
                    const first = root.controller.searchModel.get(0)
                    if (root.controller.selectLiveInstrument(first.code)) {
                        clear()
                        root.query = ""
                        root.controller.searchStocks("")
                    }
                }
            }
            background: Rectangle {
                readonly property bool keyboardFocus: liveSearch.activeFocus
                    && liveSearch.focusReason !== Qt.MouseFocusReason
                    && liveSearch.focusReason !== Qt.OtherFocusReason
                radius: 6
                color: root.theme.bgInput
                border.width: keyboardFocus ? 2 : 1
                border.color: keyboardFocus ? root.theme.cyan : root.theme.border
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.query.length > 0
                  ? "本地股票池匹配 " + root.controller.searchModel.resultCount + " 项"
                  : "仅切换主页实时观察，不改变研究品种"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 9
        }
        ListView {
            id: liveResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length > 0
            model: root.controller.searchModel
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Accessible.name: "实时品种搜索结果"

            delegate: Control {
                id: resultRow
                required property string code
                required property string name
                required property string market
                width: ListView.view.width
                height: 34
                activeFocusOnTab: true
                focusPolicy: Qt.TabFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: code + " " + name
                Accessible.description: "选择为主页实时品种"
                Accessible.focusable: true

                function activate() {
                    if (root.controller.selectLiveInstrument(resultRow.code)) {
                        liveSearch.clear()
                        root.query = ""
                        root.controller.searchStocks("")
                    }
                }

                Keys.onReturnPressed: function(event) {
                    resultRow.activate(); event.accepted = true
                }
                Keys.onSpacePressed: function(event) {
                    resultRow.activate(); event.accepted = true
                }
                MouseArea {
                    id: resultMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: resultRow.forceActiveFocus(Qt.MouseFocusReason)
                    onClicked: resultRow.activate()
                }
                background: Rectangle {
                    radius: 5
                    color: resultMouse.containsMouse || resultRow.activeFocus
                           ? root.theme.bgSelected : root.theme.bgInput
                    border.width: resultRow.visualFocus ? 2 : 1
                    border.color: resultRow.visualFocus ? root.theme.cyan : root.theme.border
                }
                contentItem: RowLayout {
                    spacing: 8
                    Text {
                        Layout.preferredWidth: 58
                        text: resultRow.code; color: root.theme.cyan
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: resultRow.name; color: root.theme.textPrimary
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                    }
                }
            }
        }
        Item { Layout.fillHeight: !liveResults.visible }
    }
}
