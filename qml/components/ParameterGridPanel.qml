// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 参数网格扫描：对自定义策略的参数候选值做笛卡尔积回测，按收益率降序展示。
Item {
    id: root
    required property var theme
    required property var controller

    function parseAxis(keyText, valuesText) {
        const key = keyText.trimmed()
        if (key.length === 0 || valuesText.trimmed().length === 0) return null
        const values = valuesText.split(",").map(function(s) { return Number(s.trimmed()) })
                                     .filter(function(n) { return !isNaN(n) })
        if (values.length === 0) return null
        return { key: key, values: values }
    }

    function runScan() {
        const axis1 = parseAxis(axis1Key.text, axis1Values.text)
        const axis2 = parseAxis(axis2Key.text, axis2Values.text)
        var axes = new Array()
        if (axis1) axes.push(axis1)
        if (axis2) axes.push(axis2)
        root.controller.runParameterGridBacktest(
            strategyIdField.text, Number(versionField.text) || 1,
            symbolField.text, Number(periodField.text) || 60,
            Number(barsField.text) || 300, axes)
    }

    component ScanField: TextField {
        height: 30
        color: root.theme.textPrimary
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        leftPadding: 8
        selectByMouse: true
        background: Rectangle {
            radius: 5; color: root.theme.bgCanvas
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    component FieldLabel: Text {
        color: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 9
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            text: "参数网格扫描"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: "对参数候选值做笛卡尔积回测，按收益率降序；只读取 raw_tick_cache_v1，不访问网络"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Column { spacing: 4; Layout.preferredWidth: 150
                FieldLabel { text: "策略 ID" }
                ScanField { id: strategyIdField; width: 150; placeholderText: "custom_xxx"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.preferredWidth: 60
                FieldLabel { text: "版本" }
                ScanField { id: versionField; width: 60; text: "1"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.preferredWidth: 100
                FieldLabel { text: "标的" }
                ScanField { id: symbolField; width: 100; text: "600519"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.preferredWidth: 70
                FieldLabel { text: "周期秒" }
                ScanField { id: periodField; width: 70; text: "60"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.preferredWidth: 70
                FieldLabel { text: "根数" }
                ScanField { id: barsField; width: 70; text: "300"; placeholderTextColor: root.theme.textMuted }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Column { spacing: 4; Layout.preferredWidth: 130
                FieldLabel { text: "轴 1 参数名" }
                ScanField { id: axis1Key; width: 130; placeholderText: "如 fastPeriod"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.fillWidth: true
                FieldLabel { text: "轴 1 候选值（逗号分隔）" }
                ScanField { id: axis1Values; Layout.fillWidth: true; placeholderText: "如 5,10,20"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.preferredWidth: 130
                FieldLabel { text: "轴 2 参数名" }
                ScanField { id: axis2Key; width: 130; placeholderText: "如 slowPeriod"; placeholderTextColor: root.theme.textMuted }
            }
            Column { spacing: 4; Layout.fillWidth: true
                FieldLabel { text: "轴 2 候选值（逗号分隔）" }
                ScanField { id: axis2Values; Layout.fillWidth: true; placeholderText: "如 20,40,60"; placeholderTextColor: root.theme.textMuted }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PpButton {
                Layout.preferredWidth: 96; Layout.preferredHeight: 30
                theme: root.theme; text: "开始扫描"; primary: true
                available: !root.controller.parameterGridRunning
                onClicked: root.runScan()
            }
            PpButton {
                Layout.preferredWidth: 72; Layout.preferredHeight: 30
                theme: root.theme; text: "取消"
                available: root.controller.parameterGridRunning
                onClicked: root.controller.cancelParameterGridBacktest()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.parameterGridRunning
                      ? "扫描中…"
                      : (root.controller.parameterGridResults.length > 0
                         ? "共 " + root.controller.parameterGridResults.length + " 组结果（按收益率降序）"
                         : "尚未扫描")
                color: root.controller.parameterGridRunning ? root.theme.yellow : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.controller.parameterGridError.length > 0
            text: root.controller.parameterGridError
            color: root.theme.red
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            model: root.controller.parameterGridResults
            clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property int index
                required property var parameters
                required property double totalReturnPercent
                required property double maxDrawdownPercent
                required property double sharpeRatio
                required property double winRatePercent
                required property int totalTrades
                width: ListView.view.width; height: 40; radius: 5
                color: index % 2 ? root.theme.bgRaised : "transparent"
                Text {
                    x: 8; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.5
                    text: "第 " + (index + 1) + " 名 · " + root.formatParameters(parameters)
                    color: root.theme.textPrimary
                    font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: (totalReturnPercent >= 0 ? "+" : "") + Number(totalReturnPercent).toFixed(2)
                          + "% · 回撤 " + Number(maxDrawdownPercent).toFixed(2)
                          + "% · " + totalTrades + " 笔"
                    color: totalReturnPercent >= 0 ? root.theme.green : root.theme.red
                    font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
            }
        }
    }

    function formatParameters(params) {
        if (!params) return "默认参数"
        const keys = Object.keys(params)
        if (keys.length === 0) return "默认参数"
        return keys.map(function(k) { return k + "=" + params[k] }).join(", ")
    }
}
