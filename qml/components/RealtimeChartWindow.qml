// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    required property var theme
    required property var controller
    required property var sessionObject
    required property int windowOrdinal
    required property int anchorPositionX
    required property int anchorPositionY
    property bool closeRequested: false
    property var instrumentRows: []

    width: 1600
    height: 1000
    minimumWidth: 1120
    minimumHeight: 760
    visible: true
    transientParent: null
    flags: Qt.Window
    color: root.theme.bgCanvas
    title: "PPTrader · 实时行情 · "
           + (root.sessionObject ? root.sessionObject.instrumentTitle : "--")

    function refreshInstrumentRows() {
        if (!root.sessionObject) {
            root.instrumentRows = []
        } else if (searchField.text.trim().length === 0) {
            root.instrumentRows = root.sessionObject.recentInstruments
        } else {
            root.instrumentRows = root.controller.searchRealtimeChartWindowInstruments(
                        root.sessionObject.windowId, searchField.text, 50)
        }
    }

    function selectInstrument(code) {
        if (!root.sessionObject || code.length !== 6) return
        if (root.controller.setRealtimeChartWindowInstrument(
                    root.sessionObject.windowId, code)) {
            searchField.text = ""
            root.refreshInstrumentRows()
        }
    }

    Component.onCompleted: {
        x = root.anchorPositionX + (root.windowOrdinal % 4) * 34
        y = root.anchorPositionY + (root.windowOrdinal % 4) * 28
        root.refreshInstrumentRows()
        requestActivate()
    }

    onClosing: function(close) {
        if (!root.closeRequested && root.sessionObject) {
            root.closeRequested = true
            root.controller.closeRealtimeChartWindow(root.sessionObject.windowId)
        }
        close.accepted = true
    }

    Connections {
        target: root.sessionObject
        function onRecentInstrumentsChanged() {
            if (searchField.text.trim().length === 0)
                root.refreshInstrumentRows()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.bgCanvas

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: root.theme.bgBar
                border.color: root.theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 12
                    spacing: 9

                    Rectangle {
                        Layout.preferredWidth: 11
                        Layout.preferredHeight: 11
                        radius: 6
                        color: root.theme.red
                    }
                    Rectangle {
                        Layout.preferredWidth: 11
                        Layout.preferredHeight: 11
                        radius: 6
                        color: root.theme.yellow
                    }
                    Rectangle {
                        Layout.preferredWidth: 11
                        Layout.preferredHeight: 11
                        radius: 6
                        color: root.theme.green
                    }
                    Text {
                        Layout.leftMargin: 8
                        Layout.fillWidth: true
                        text: "实时行情 · "
                              + (root.sessionObject ? root.sessionObject.symbol : "--")
                              + " · 图表工作区"
                        color: root.theme.textPrimary
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: root.sessionObject && root.sessionObject.lastBarTime.toString().length > 0
                              ? Qt.formatDateTime(root.sessionObject.lastBarTime,
                                                  "yyyy-MM-dd HH:mm") : "行情时刻 --"
                        color: root.sessionObject && !root.sessionObject.stale
                               ? root.theme.green : root.theme.yellow
                        font.family: root.theme.fontFamily
                        font.pixelSize: 10
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.preferredWidth: Math.min(300, Math.max(248, root.width * 0.19))
                    Layout.fillHeight: true
                    color: root.theme.bgBar
                    border.color: root.theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: "品种列表"
                                color: root.theme.textPrimary
                                font.family: root.theme.fontFamily
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }
                            Text {
                                text: "仅本窗口"
                                color: root.theme.cyan
                                font.family: root.theme.fontFamily
                                font.pixelSize: 9
                            }
                        }

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            Layout.preferredHeight: 38
                            placeholderText: "搜索 A 股代码或名称"
                            color: root.theme.textPrimary
                            placeholderTextColor: root.theme.textMuted
                            font.family: root.theme.fontFamily
                            font.pixelSize: 11
                            activeFocusOnTab: true
                            Accessible.name: "本窗口品种搜索"
                            Accessible.description: "搜索只影响当前实时行情子窗口，不改变主页面或策略页品种"
                            onTextChanged: root.refreshInstrumentRows()
                            onAccepted: {
                                if (root.instrumentRows.length > 0)
                                    root.selectInstrument(root.instrumentRows[0].code)
                            }
                            background: Rectangle {
                                radius: 6
                                color: root.theme.bgInput
                                border.width: searchField.activeFocus ? 2 : 1
                                border.color: searchField.activeFocus
                                              ? root.theme.cyan : root.theme.border
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: searchField.text.trim().length > 0
                                  ? "搜索结果 " + root.instrumentRows.length
                                  : "当前与最近品种"
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                        }

                        ListView {
                            id: instrumentList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 4
                            model: root.instrumentRows
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Rectangle {
                                id: instrumentRow
                                required property var modelData
                                width: instrumentList.width
                                height: 50
                                radius: 7
                                color: root.sessionObject
                                       && instrumentRow.modelData.code === root.sessionObject.symbol
                                       ? root.theme.bgSelected
                                       : instrumentMouse.containsMouse
                                         ? root.theme.bgRaised : "transparent"
                                border.width: root.sessionObject
                                              && instrumentRow.modelData.code === root.sessionObject.symbol
                                              ? 1 : 0
                                border.color: root.theme.cyan

                                Column {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 20
                                    spacing: 2
                                    Text {
                                        width: parent.width
                                        text: instrumentRow.modelData.code + "  "
                                              + (instrumentRow.modelData.name || "--")
                                        color: root.theme.textPrimary
                                        elide: Text.ElideRight
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        width: parent.width
                                        text: instrumentRow.modelData.venue || "A股"
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 9
                                    }
                                }
                                MouseArea {
                                    id: instrumentMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.selectInstrument(
                                                   instrumentRow.modelData.code)
                                }
                                Accessible.role: Accessible.ListItem
                                Accessible.name: instrumentRow.modelData.code + " "
                                                 + (instrumentRow.modelData.name || "")
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 76
                            radius: 7
                            color: root.theme.bgInput
                            border.color: root.theme.border
                            Column {
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 3
                                Text {
                                    width: parent.width
                                    text: root.sessionObject
                                          ? root.sessionObject.instrumentTitle : "--"
                                    color: root.theme.textPrimary
                                    elide: Text.ElideRight
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                }
                                Text {
                                    width: parent.width
                                    text: root.sessionObject ? root.sessionObject.statusText : "--"
                                    color: root.sessionObject && root.sessionObject.errorText.length > 0
                                           ? root.theme.red : root.theme.textMuted
                                    elide: Text.ElideRight
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 8
                                }
                                Text {
                                    width: parent.width
                                    text: root.sessionObject
                                          ? "请求 " + root.sessionObject.requestId
                                            + " / 已解析 " + root.sessionObject.resolvedRequestId
                                          : "请求 --"
                                    color: root.theme.textMuted
                                    font.family: root.theme.fontFamily
                                    font.pixelSize: 8
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.theme.bgCanvas

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 60
                            color: root.theme.bgPanel
                            border.color: root.theme.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 18
                                anchors.rightMargin: 18
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: "图表列表（实时）"
                                        color: root.theme.textPrimary
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 14
                                        font.weight: Font.Bold
                                    }
                                    Text {
                                        text: "最多 10 张 · 向下滚动 · 同一窗口品种"
                                        color: root.theme.textMuted
                                        font.family: root.theme.fontFamily
                                        font.pixelSize: 9
                                    }
                                }
                                PpButton {
                                    theme: root.theme
                                    text: root.sessionObject && root.sessionObject.loading
                                          ? "刷新中" : "立即刷新"
                                    visualWidth: 82
                                    available: root.sessionObject
                                               && !root.sessionObject.loading
                                    toolTip: "只刷新本窗口的腾讯 1 分钟内存行情"
                                    onClicked: root.controller.refreshRealtimeChartWindow(
                                                   root.sessionObject.windowId)
                                }
                                PpButton {
                                    theme: root.theme
                                    text: "＋ 添加图表"
                                    visualWidth: 108
                                    primary: true
                                    available: root.sessionObject
                                               && root.sessionObject.chartPaneCount < 10
                                    toolTip: available
                                             ? "默认依次添加 5、10、15、30、60 分钟图表"
                                             : "当前已达到 10 张图表上限"
                                    onClicked: root.controller.addRealtimeChart(
                                                   root.sessionObject.windowId, 0)
                                }
                            }
                        }

                        ListView {
                            id: chartList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.margins: 12
                            clip: true
                            spacing: 12
                            model: root.sessionObject
                                   ? root.sessionObject.chartPaneModel : null
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: chartCard
                                required property string chartId
                                required property int periodSeconds
                                required property string periodLabel
                                required property var chartModel
                                required property int pointCount
                                width: chartList.width
                                height: chartList.count === 1
                                        ? Math.max(560, chartList.height - 4) : 440
                                radius: 9
                                color: root.theme.bgPanel
                                border.color: root.theme.border

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 7

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        spacing: 8
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text {
                                                Layout.fillWidth: true
                                                text: (root.sessionObject
                                                       ? root.sessionObject.instrumentTitle : "--")
                                                      + " · " + chartCard.periodLabel
                                                color: root.theme.textPrimary
                                                elide: Text.ElideRight
                                                font.family: root.theme.fontFamily
                                                font.pixelSize: 12
                                                font.weight: Font.Bold
                                            }
                                            Text {
                                                text: chartCard.pointCount + " 根 · "
                                                      + (root.sessionObject
                                                         ? root.sessionObject.source : "--")
                                                      + " · LIVE"
                                                color: root.sessionObject && !root.sessionObject.stale
                                                       ? root.theme.green : root.theme.textMuted
                                                font.family: root.theme.fontFamily
                                                font.pixelSize: 8
                                            }
                                        }
                                        ComboBox {
                                            id: periodBox
                                            Layout.preferredWidth: 112
                                            Layout.preferredHeight: 34
                                            model: [
                                                { label: "1 分钟", seconds: 60 },
                                                { label: "5 分钟", seconds: 300 },
                                                { label: "10 分钟", seconds: 600 },
                                                { label: "15 分钟", seconds: 900 },
                                                { label: "30 分钟", seconds: 1800 },
                                                { label: "60 分钟", seconds: 3600 }
                                            ]
                                            textRole: "label"
                                            valueRole: "seconds"
                                            currentIndex: {
                                                for (let i = 0; i < model.length; ++i) {
                                                    if (model[i].seconds === chartCard.periodSeconds)
                                                        return i
                                                }
                                                return 0
                                            }
                                            onActivated: root.controller.setRealtimeChartPeriod(
                                                             root.sessionObject.windowId,
                                                             chartCard.chartId,
                                                             currentValue)
                                            Accessible.name: "图表周期"
                                            Accessible.description: "设置本张图表的周期"
                                        }
                                        PpButton {
                                            theme: root.theme
                                            text: "移除"
                                            visualWidth: 62
                                            danger: true
                                            available: root.sessionObject
                                                       && root.sessionObject.chartPaneCount > 1
                                            toolTip: available
                                                     ? "移除本张图表"
                                                     : "至少保留一张实时图表"
                                            onClicked: root.controller.removeRealtimeChart(
                                                           root.sessionObject.windowId,
                                                           chartCard.chartId)
                                        }
                                    }

                                    CandleSeriesChart {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        theme: root.theme
                                        model: chartCard.chartModel
                                        dynamicMode: true
                                        loading: root.sessionObject
                                                 ? root.sessionObject.loading : false
                                        statusText: root.sessionObject
                                                    ? root.sessionObject.statusText : ""
                                        errorText: root.sessionObject
                                                   ? root.sessionObject.errorText : ""
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: 14
                            Layout.rightMargin: 14
                            Layout.bottomMargin: 7
                            text: root.sessionObject && root.sessionObject.chartPaneError.length > 0
                                  ? root.sessionObject.chartPaneError
                                  : root.controller.realtimeChartWindowError.length > 0
                                    ? root.controller.realtimeChartWindowError
                                    : "实时数据仅用于显示并驻留内存；新增图表复用同一份 1 分钟源数据，不写行情数据库。"
                            color: (root.sessionObject
                                    && root.sessionObject.chartPaneError.length > 0)
                                   || root.controller.realtimeChartWindowError.length > 0
                                   ? root.theme.red : root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                            Accessible.name: text
                        }
                    }
                }
            }
        }
    }
}
