// qmllint disable unqualified
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property var theme
    required property var controller
    property var summary: ({validCount: 0, minimum: 0, maximum: 0, average: 0, median: 0, top: [], boardCounts: ({})})
    property var selectedPoint: ({})
    property int boardMask: 31
    property bool autoRefresh: false
    signal boardMaskRequested(int mask)
    signal autoRefreshRequested(bool enabled)

    function money(value) {
        return Number(value) > 0 ? "¥ " + Number(value).toFixed(2) : "--"
    }

    radius: 10
    color: theme.bgPanel
    border.color: theme.border

    Text {
        x: 20; y: 11; text: "市场快照摘要"
        color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
    }
    PpButton {
        x: parent.width - 92; y: 5
        theme: root.theme; text: root.autoRefresh ? "自动刷新" : "已暂停"
        visualWidth: 72; visualHeight: 24; success: root.autoRefresh
        toolTip: "自动刷新仅表示每 5 秒查询已完成快照；行情源："
                 + (root.controller.activeRealtimeProviderName || "未选择")
                 + "，健康：" + root.controller.providerHealthText
                 + "，评分：" + root.controller.realtimeProviderScore
        onClicked: root.autoRefreshRequested(!root.autoRefresh)
    }
    Text {
        x: 146; y: 13; width: 68
        visible: controller.snapshotHealth === 2
        horizontalAlignment: Text.AlignRight
        text: "部分数据"
        color: theme.yellow
        font.family: theme.fontFamily; font.pixelSize: 9; font.weight: Font.Medium
    }
    Text {
        x: 20; y: 45; width: parent.width - 40; elide: Text.ElideRight
        text: "快照时刻  " + (controller.resolvedSnapshotTime.toString().length > 0
              ? Qt.formatDateTime(controller.resolvedSnapshotTime, "yyyy-MM-dd HH:mm:ss") : "--")
        color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10
    }

    Rectangle {
        x: 20; y: 80; width: parent.width - 40; height: 126; radius: 8
        color: theme.bgInput; border.color: theme.cyan
        Text {
            x: 14; y: 12
            text: root.selectedPoint && root.selectedPoint.symbol !== undefined
                  ? root.selectedPoint.name + "  " + root.selectedPoint.symbol : "当前未选择股票"
            color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold
        }
        Text {
            x: 14; y: 42
            text: root.selectedPoint && root.selectedPoint.price !== undefined
                  ? "¥ " + Number(root.selectedPoint.price).toFixed(2) : "--"
            color: theme.cyan; font.family: theme.fontFamily; font.pixelSize: 20; font.weight: Font.Bold
        }
        Text {
            x: 14; y: 76
            text: root.selectedPoint && root.selectedPoint.changePercent !== undefined
                  ? "涨跌幅  " + Number(root.selectedPoint.changePercent).toFixed(2) + "%" : "涨跌幅  --"
            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10
        }
        Text {
            x: 14; y: 98
            text: "批次  " + (controller.snapshotBatchId.length > 0 ? controller.snapshotBatchId : "--")
            width: parent.width - 28; elide: Text.ElideMiddle
            color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9
        }
    }

    Text {
        x: 20; y: 226; text: "板块筛选"
        color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold
    }
    PpButton {
        x: parent.width - 92; y: 214
        theme: root.theme; text: root.boardMask === 31 ? "清空" : "全选"
        visualWidth: 72; visualHeight: 24
        onClicked: root.boardMaskRequested(root.boardMask === 31 ? 0 : 31)
    }

    Repeater {
        model: [
            {name: "主板", bit: 1, color: root.theme.cyan},
            {name: "深市主板", bit: 2, color: root.theme.blue},
            {name: "创业板", bit: 4, color: root.theme.purple},
            {name: "科创板", bit: 8, color: root.theme.yellow},
            {name: "北交所", bit: 16, color: root.theme.green}
        ]
        delegate: Control {
            id: boardOption
            required property var modelData
            required property int index
            readonly property bool checked: (root.boardMask & modelData.bit) !== 0
            x: 20; y: 258 + index * 34; width: root.width - 40; height: 28
            activeFocusOnTab: true
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.CheckBox
            Accessible.name: modelData.name
            Accessible.description: "当前快照 "
                                    + (root.summary.boardCounts
                                       && root.summary.boardCounts[modelData.name] !== undefined
                                       ? root.summary.boardCounts[modelData.name] : 0) + " 只"
            Accessible.checkable: true
            Accessible.checked: checked
            Accessible.focusable: true
            Accessible.onToggleAction: root.boardMaskRequested(
                                           root.boardMask ^ boardOption.modelData.bit)
            Keys.onSpacePressed: function(event) {
                if (!event.isAutoRepeat) {
                    root.boardMaskRequested(root.boardMask ^ boardOption.modelData.bit)
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) {
                if (!event.isAutoRepeat) {
                    root.boardMaskRequested(root.boardMask ^ boardOption.modelData.bit)
                    event.accepted = true
                }
            }
            Rectangle {
                x: -3; y: -3; width: parent.width + 6; height: parent.height + 6
                radius: 6; visible: boardOption.visualFocus
                color: "transparent"; border.width: 1; border.color: root.theme.cyan
            }
            Rectangle {
                x: 0; y: 7; width: 14; height: 14; radius: 3
                color: boardOption.checked ? modelData.color : "transparent"
                border.color: modelData.color
                Text { anchors.centerIn: parent; text: boardOption.checked ? "✓" : ""; color: root.theme.bgCanvas; font.pixelSize: 9; font.weight: Font.Bold }
            }
            Rectangle { x: 26; y: 10; width: 8; height: 8; radius: 4; color: modelData.color }
            Text { x: 44; anchors.verticalCenter: parent.verticalCenter; text: modelData.name; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text {
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                text: root.summary.boardCounts && root.summary.boardCounts[modelData.name] !== undefined
                      ? root.summary.boardCounts[modelData.name] : 0
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
            }
            MouseArea {
                anchors.fill: parent
                onPressed: boardOption.forceActiveFocus(Qt.MouseFocusReason)
                onClicked: root.boardMaskRequested(root.boardMask ^ modelData.bit)
            }
        }
    }

    Text {
        x: 20; y: 446; text: "价格分布"
        color: theme.textPrimary; font.family: theme.fontFamily; font.pixelSize: 12; font.weight: Font.Bold
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 20; y: 447
        text: controller.snapshotHealth === 2 ? "基于部分数据" : "有效 " + summary.validCount
        color: controller.snapshotHealth === 2 ? theme.yellow : theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 9
    }
    Rectangle {
        x: 20; y: 480; width: parent.width - 40; height: 102; radius: 7
        color: theme.bgInput; border.color: theme.border
        Repeater {
            model: [
                {label: "最高", value: root.summary.maximum},
                {label: "中位", value: root.summary.median},
                {label: "平均", value: root.summary.average},
                {label: "最低", value: root.summary.minimum}
            ]
            delegate: Item {
                required property var modelData
                required property int index
                x: (index % 2) * parent.width / 2
                y: Math.floor(index / 2) * 50
                width: parent.width / 2; height: 50
                Text { x: 12; y: 8; text: modelData.label; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { x: 12; y: 25; text: root.money(modelData.value); color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Medium }
            }
        }
    }

    Text {
        x: 20; y: 603
        text: controller.snapshotHealth === 2 ? "高价股 Top 3 · 部分数据" : "高价股 Top 3"
        color: controller.snapshotHealth === 2 ? theme.yellow : theme.textPrimary
        font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
    }
    PpButton {
        x: parent.width - 102; y: 591
        theme: root.theme; text: "按快照价"; visualWidth: 82; visualHeight: 22
        available: false; toolTip: "排序方式固定为快照价，切换尚未实现"
    }
    Repeater {
        model: 3
        delegate: Item {
            required property int index
            x: 20; y: 628 + index * 20; width: root.width - 40; height: 18
            property var stock: root.summary.top && root.summary.top.length > index ? root.summary.top[index] : ({})
            Text { x: 0; text: String(index + 1).padStart(2, "0"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text { x: 30; width: 145; elide: Text.ElideRight; text: parent.stock.name !== undefined ? parent.stock.name : "--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text { anchors.right: parent.right; text: parent.stock.price !== undefined ? root.money(parent.stock.price) : "--"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
        }
    }
}
