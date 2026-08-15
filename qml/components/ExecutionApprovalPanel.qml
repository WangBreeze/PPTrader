// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 执行审批队列 + 通知设置：审批走 StrategyExecutionCoordinator（模拟账户，不接真实下单），
// 通知通道（桌面/Webhook）与逐事件过滤由 QSettings 持久化。
Item {
    id: root
    required property var theme
    required property var controller

    property bool desktopEnabled: false
    property bool webhookEnabled: false
    property string webhookUrl: ""
    property bool notifySignal: true
    property bool notifyApproval: true
    property bool notifyOrderSubmitted: true
    property bool notifyExecutionRejected: true
    property bool notifyIntentExpired: true

    Component.onCompleted: loadSettings()

    function loadSettings() {
        const s = root.controller.notificationSettings()
        if (!s || Object.keys(s).length === 0) return
        root.desktopEnabled = s.desktopEnabled === true
        root.webhookEnabled = s.webhookEnabled === true
        root.webhookUrl = s.webhookUrl !== undefined ? s.webhookUrl : ""
        root.notifySignal = s.notifySignal !== false
        root.notifyApproval = s.notifyApproval !== false
        root.notifyOrderSubmitted = s.notifyOrderSubmitted !== false
        root.notifyExecutionRejected = s.notifyExecutionRejected !== false
        root.notifyIntentExpired = s.notifyIntentExpired !== false
    }

    component ToggleRow: Row {
        id: toggleRow
        property string toggleLabel: ""
        property bool toggleChecked: false
        spacing: 8
        Rectangle {
            id: box
            width: 14; height: 14; radius: 3
            anchors.verticalCenter: toggleRow.verticalCenter
            color: toggleRow.toggleChecked ? root.theme.cyan : root.theme.bgInput
            border.width: 1
            border.color: toggleRow.toggleChecked ? root.theme.cyan : root.theme.border
            Text {
                anchors.centerIn: box
                text: "✓"; color: root.theme.bgCanvas
                font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                visible: toggleRow.toggleChecked
            }
        }
        Text {
            text: toggleRow.toggleLabel
            anchors.verticalCenter: toggleRow.verticalCenter
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 10
        }
        MouseArea {
            anchors.fill: toggleRow
            onClicked: toggleRow.toggleChecked = !toggleRow.toggleChecked
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 16

        // 左：执行审批队列
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 360
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            Text {
                x: 12; y: 10; text: "执行审批队列（" + root.controller.pendingStrategyIntentCount + " 待审批）"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily
                font.pixelSize: 12; font.weight: Font.Bold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 12; y: 12
                text: "模拟账户 · 不接真实下单"
                color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            ListView {
                x: 12; y: 38; width: parent.width - 24; height: parent.height - 50
                model: root.controller.strategyIntentModel
                clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property string intentId
                    required property string strategyId
                    required property string symbol
                    required property string signalSide
                    required property real signalPrice
                    required property real quantity
                    required property string status
                    required property bool pending
                    required property string message
                    required property var createdAt
                    width: ListView.view.width; height: pending ? 76 : 52; radius: 6
                    color: pending ? root.theme.bgSelected : root.theme.bgRaised
                    border.width: pending ? 1 : 0
                    border.color: pending ? root.theme.purple : "transparent"
                    Text {
                        x: 10; y: 7; width: parent.width - 180
                        text: strategyId + " · " + symbol + " · " + signalSide
                              + " " + quantity.toFixed(0) + " 股 @ " + signalPrice.toFixed(2)
                        color: root.theme.textPrimary; font.family: root.theme.fontFamily
                        font.pixelSize: 10; font.weight: Font.Bold; elide: Text.ElideRight
                    }
                    Text {
                        x: 10; y: 27; width: parent.width - 180
                        text: status + (message.length > 0 ? " · " + message : "")
                        color: pending ? root.theme.purple : root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 10; y: 9
                        text: createdAt ? Qt.formatDateTime(createdAt, "MM-dd HH:mm:ss") : "--"
                        color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
                    }
                    RowLayout {
                        anchors.right: parent.right; anchors.rightMargin: 8
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                        spacing: 6
                        visible: pending
                        PpButton {
                            Layout.preferredWidth: 60; Layout.preferredHeight: 22
                            theme: root.theme; text: "批准"; primary: true
                            onClicked: root.controller.approveStrategyIntent(intentId)
                        }
                        PpButton {
                            Layout.preferredWidth: 60; Layout.preferredHeight: 22
                            theme: root.theme; text: "拒绝"
                            onClicked: root.controller.rejectStrategyIntent(intentId, "用户在审批队列拒绝")
                        }
                    }
                }
            }
        }

        // 右：通知设置
        Rectangle {
            Layout.preferredWidth: 400; Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            Text {
                x: 12; y: 10; text: "通知设置"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily
                font.pixelSize: 12; font.weight: Font.Bold
            }
            Column {
                x: 14; y: 40; width: parent.width - 28; spacing: 10
                ToggleRow {
                    toggleLabel: "桌面托盘通知"
                    toggleChecked: root.desktopEnabled
                    onToggleCheckedChanged: root.desktopEnabled = toggleChecked
                }
                ToggleRow {
                    toggleLabel: "Webhook 通知"
                    toggleChecked: root.webhookEnabled
                    onToggleCheckedChanged: root.webhookEnabled = toggleChecked
                }
                TextField {
                    width: parent.width; height: 32
                    placeholderText: "Webhook URL（如 https://example.com/hook）"
                    text: root.webhookUrl
                    color: root.theme.textPrimary
                    placeholderTextColor: root.theme.textMuted
                    selectionColor: root.theme.cyan
                    selectedTextColor: root.theme.bgCanvas
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    leftPadding: 8; selectByMouse: true
                    onEditingFinished: root.webhookUrl = text
                    background: Rectangle {
                        radius: 5; color: root.theme.bgCanvas
                        border.width: parent.activeFocus ? 2 : 1
                        border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                    }
                }
                Text {
                    width: parent.width; text: "通知事件"
                    color: root.theme.textMuted; font.family: root.theme.fontFamily
                    font.pixelSize: 10; font.weight: Font.Bold
                }
                Flow {
                    width: parent.width; spacing: 14
                    ToggleRow { toggleLabel: "信号生成"; toggleChecked: root.notifySignal; onToggleCheckedChanged: root.notifySignal = toggleChecked }
                    ToggleRow { toggleLabel: "待审批"; toggleChecked: root.notifyApproval; onToggleCheckedChanged: root.notifyApproval = toggleChecked }
                    ToggleRow { toggleLabel: "订单提交"; toggleChecked: root.notifyOrderSubmitted; onToggleCheckedChanged: root.notifyOrderSubmitted = toggleChecked }
                    ToggleRow { toggleLabel: "执行拒绝"; toggleChecked: root.notifyExecutionRejected; onToggleCheckedChanged: root.notifyExecutionRejected = toggleChecked }
                    ToggleRow { toggleLabel: "审批过期"; toggleChecked: root.notifyIntentExpired; onToggleCheckedChanged: root.notifyIntentExpired = toggleChecked }
                }
            }
            PpButton {
                x: 14; anchors.bottom: parent.bottom; anchors.bottomMargin: 14
                visualWidth: 110
                theme: root.theme; text: "应用设置"; primary: true
                toolTip: "保存到本机 QSettings"
                onClicked: {
                    root.controller.configureNotifications(
                        root.desktopEnabled, root.webhookEnabled, root.webhookUrl,
                        root.notifySignal, root.notifyApproval, root.notifyOrderSubmitted,
                        root.notifyExecutionRejected, root.notifyIntentExpired)
                }
            }
        }
    }
}
