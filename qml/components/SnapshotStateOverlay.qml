import QtQuick

Item {
    id: root
    required property var theme
    property int health: 0
    property bool loading: false
    property string errorText: ""
    property real completeness: 0
    property real stalenessMilliseconds: 0
    property int providerHealth: 0
    property string providerError: ""
    signal retryRequested()

    readonly property bool blocking: loading || health === 0 || health === 4
    visible: loading || health !== 1

    Rectangle {
        anchors.fill: parent
        visible: root.blocking
        radius: 7
        color: Qt.rgba(14 / 255, 21 / 255, 30 / 255, 0.94)

        Column {
            anchors.centerIn: parent
            spacing: 8
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.loading ? "正在加载所选时刻快照…"
                     : root.health === 4 ? "快照查询失败"
                     : root.providerHealth === 4 ? "行情源不可用"
                     : "该时刻没有可用 A 股快照"
                color: root.health === 4 || root.providerHealth === 4
                       ? root.theme.red : root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.Bold
            }
            Text {
                width: 420
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.loading ? "保留时间选择，查询完成后显示实际命中批次"
                     : root.health === 0 && root.providerHealth === 4
                       && root.providerError.length > 0 ? root.providerError
                     : root.errorText.length > 0 ? root.errorText
                     : "请求时刻之前没有已完成快照"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 11
            }
            PpButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !root.loading
                theme: root.theme
                text: "重试"
                visualWidth: 82
                onClicked: root.retryRequested()
            }
        }
    }

    Rectangle {
        visible: !root.blocking && (root.health === 2 || root.health === 3)
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        height: 34
        radius: 6
        color: Qt.rgba(245 / 255, 196 / 255, 81 / 255, 0.12)
        border.color: root.theme.yellow

        Text {
            anchors.fill: parent
            anchors.leftMargin: 12
            verticalAlignment: Text.AlignVCenter
            text: root.health === 2
                  ? "部分数据 · 覆盖率 " + (root.completeness * 100).toFixed(1) + "%"
                  : "历史快照 · 陈旧 " + (root.stalenessMilliseconds / 1000).toFixed(1) + " 秒"
            color: root.theme.yellow
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }
}
