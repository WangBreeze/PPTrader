// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property var theme
    required property var controller
    property bool busy: false
    property string draftDate: Qt.formatDateTime(new Date(), "yyyy-MM-dd")
    property string draftTime: Qt.formatDateTime(new Date(), "HH:mm")
    readonly property bool wrappedLayout: width < 1560
    readonly property real timelineX: wrappedLayout ? width - 520 : 1040
    readonly property real timelineTitleY: wrappedLayout ? 78 : 10
    readonly property real timelineControlY: wrappedLayout ? 93 : 31
    readonly property real timelineLabelY: wrappedLayout ? 120 : 56
    readonly property real actionY: wrappedLayout ? 92 : 30

    height: wrappedLayout ? 148 : 92
    radius: 9
    color: theme.bgPanel
    border.color: theme.border

    function setQuickTime(value) {
        draftTime = value
        timeField.text = value
    }

    function setCurrentDraft() {
        var now = new Date()
        draftDate = Qt.formatDateTime(now, "yyyy-MM-dd")
        draftTime = Qt.formatDateTime(now, "HH:mm")
        dateField.text = draftDate
        timeField.text = draftTime
    }

    function applyDraft() {
        if (busy)
            return
        draftDate = dateField.text.trim()
        draftTime = timeField.text.trim()
        root.controller.requestMarketSnapshotAt(root.draftDate + " " + root.draftTime + ":00")
    }

    Text {
        x: 20; y: 10; text: "选择观察时刻"; color: theme.textPrimary
        font.family: theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
    }
    Text {
        x: 570; y: 10; text: "快捷时刻"; color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
    }
    Text {
        x: root.timelineX; y: root.timelineTitleY; text: "分时定位"; color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Medium
    }

    Text { x: 20; y: 43; text: "日期"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10 }
    TextField {
        id: dateField
        x: 72; y: 30; width: 148; height: 38
        text: root.draftDate
        color: theme.textPrimary
        font.family: theme.fontFamily; font.pixelSize: 12
        selectByMouse: true
        activeFocusOnTab: true
        Accessible.name: "日期"
        Accessible.description: "输入观察日期，格式为年-月-日"
        background: Rectangle { radius: 7; color: root.theme.bgInput; border.color: dateField.activeFocus ? root.theme.cyan : root.theme.border }
    }
    Rectangle {
        x: dateField.x - 3; y: dateField.y - 3
        width: dateField.width + 6; height: dateField.height + 6; radius: 9
        visible: dateField.activeFocus
                 && dateField.focusReason !== Qt.MouseFocusReason
                 && dateField.focusReason !== Qt.OtherFocusReason
        color: "transparent"
        border.width: 1; border.color: root.theme.cyan
    }
    Text { x: 238; y: 43; text: "时刻"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 10 }
    TextField {
        id: timeField
        x: 290; y: 30; width: 132; height: 38
        text: root.draftTime
        color: theme.cyan
        font.family: theme.fontFamily; font.pixelSize: 13; font.weight: Font.Bold
        selectByMouse: true
        activeFocusOnTab: true
        Accessible.name: "时刻"
        Accessible.description: "输入观察时刻，格式为小时冒号分钟"
        background: Rectangle { radius: 7; color: root.theme.bgInput; border.color: timeField.activeFocus ? root.theme.cyan : root.theme.border }
    }
    Rectangle {
        x: timeField.x - 3; y: timeField.y - 3
        width: timeField.width + 6; height: timeField.height + 6; radius: 9
        visible: timeField.activeFocus
                 && timeField.focusReason !== Qt.MouseFocusReason
                 && timeField.focusReason !== Qt.OtherFocusReason
        color: "transparent"
        border.width: 1; border.color: root.theme.cyan
    }
    Rectangle {
        x: 432; y: 30; width: 112; height: 38; radius: 7
        color: theme.bgInput; border.color: theme.border
        Text { anchors.centerIn: parent; text: "Asia/Shanghai"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 10 }
    }

    Row {
        x: 570; y: 31; height: 36; spacing: 8
        PpButton { theme: root.theme; text: "开盘 09:30"; visualWidth: 94; visualHeight: 30; onClicked: root.setQuickTime("09:30") }
        PpButton { theme: root.theme; text: "10:30"; visualWidth: 70; visualHeight: 30; active: root.draftTime === "10:30"; onClicked: root.setQuickTime("10:30") }
        PpButton { theme: root.theme; text: "午后 14:00"; visualWidth: 96; visualHeight: 30; onClicked: root.setQuickTime("14:00") }
        PpButton { theme: root.theme; text: "收盘 15:00"; visualWidth: 96; visualHeight: 30; onClicked: root.setQuickTime("15:00") }
        PpButton { theme: root.theme; text: "当前"; visualWidth: 64; visualHeight: 30; onClicked: root.setCurrentDraft() }
    }

    Slider {
        id: timeSlider
        x: root.timelineX; y: root.timelineControlY; width: 250; height: 34
        from: 0; to: 330
        stepSize: 5
        snapMode: Slider.SnapAlways
        activeFocusOnTab: true
        Accessible.name: "分时定位"
        Accessible.description: "当前 " + root.draftTime + "，左右键每次调整 5 分钟"
        value: {
            var parts = root.draftTime.split(":")
            if (parts.length !== 2) return 60
            return Math.max(0, Math.min(330, Number(parts[0]) * 60 + Number(parts[1]) - 570))
        }
        onMoved: {
            var total = 570 + Math.round(value / 5) * 5
            root.setQuickTime(String(Math.floor(total / 60)).padStart(2, "0") + ":"
                              + String(total % 60).padStart(2, "0"))
        }
        background: Rectangle { x: timeSlider.leftPadding; y: timeSlider.availableHeight / 2 - 2; width: timeSlider.availableWidth; height: 4; radius: 2; color: root.theme.track }
        handle: Rectangle { x: timeSlider.leftPadding + timeSlider.visualPosition * (timeSlider.availableWidth - width); y: timeSlider.availableHeight / 2 - height / 2; width: 14; height: 14; radius: 7; color: root.theme.cyan }
    }
    Rectangle {
        x: timeSlider.x - 3; y: timeSlider.y - 3
        width: timeSlider.width + 6; height: timeSlider.height + 6; radius: 8
        visible: timeSlider.visualFocus; color: "transparent"
        border.width: 1; border.color: root.theme.cyan
    }
    Text { x: root.timelineX; y: root.timelineLabelY; text: "09:30"; color: theme.textMuted; font.family: theme.fontFamily; font.pixelSize: 9 }
    Text {
        x: root.timelineX + 208; y: root.timelineLabelY; width: 42
        horizontalAlignment: Text.AlignRight
        text: "15:00"; color: theme.textMuted
        font.family: theme.fontFamily; font.pixelSize: 9
    }

    PpButton {
        x: root.wrappedLayout ? parent.width - 252 : 1308; y: root.actionY
        theme: root.theme; text: "▶"; visualWidth: 54; visualHeight: 38
        available: false; toolTip: "播放控制尚未实现"
    }
    PpButton {
        x: root.wrappedLayout ? parent.width - 186 : 1374; y: root.actionY
        theme: root.theme; text: root.busy ? "正在查询…" : "应用时刻并刷新"
        visualWidth: 166; visualHeight: 38; primary: true; available: !root.busy
        toolTip: root.busy ? "查询进行中，请等待当前请求完成" : "按所选日期和时刻查询快照"
        onClicked: root.applyDraft()
    }
}
