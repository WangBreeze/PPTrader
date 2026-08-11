// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    property var liveQuote: ({})
    property int historyPeriodSeconds: 300

    function refreshLiveQuote() {
        const row = root.controller.marketSnapshotModel.indexOfSymbol(
            root.controller.selectedLiveSymbol)
        root.liveQuote = row >= 0 ? root.controller.marketSnapshotModel.get(row) : ({})
    }

    function compactNumber(value) {
        const number = Number(value)
        if (!isFinite(number)) return "--"
        if (Math.abs(number) >= 100000000) return (number / 100000000).toFixed(2) + " 亿"
        if (Math.abs(number) >= 10000) return (number / 10000).toFixed(2) + " 万"
        return number.toFixed(0)
    }

    function requestRealtimeMinute() {
        if (root.controller.selectedLiveSymbol.length > 0)
            root.controller.requestRealtimeMinuteSeries(root.controller.selectedLiveSymbol)
    }

    function requestHistory() {
        if (root.controller.selectedBacktestSymbol.length > 0)
            root.controller.requestLocalCandleSeriesPeriod(
                root.controller.selectedBacktestSymbol, root.historyPeriodSeconds,
                0, 3000, 800)
    }

    Component.onCompleted: {
        refreshLiveQuote()
        if (root.controller.selectedBacktestPeriodSeconds > 0)
            root.historyPeriodSeconds = root.controller.selectedBacktestPeriodSeconds
        requestHistory()
    }

    Flickable {
        id: workspaceScroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, 680)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: workspaceScroll.contentHeight > workspaceScroll.height
                    ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Text {
            id: pageTitle
            x: 20; y: 14; width: parent.width - 40; height: 30
            text: "A 股双品种工作台"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: 23
            font.weight: Font.Bold
        }
        Text {
            x: 20; y: 46; width: parent.width - 40; height: 20
            text: "实时图使用免费 1 分钟行情且仅驻留内存 · 回测继续使用独立下载的历史 Tick 数据"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 10
        }

        RowLayout {
            id: workbench
            x: 20; y: 80
            width: parent.width - 40
            height: workspaceScroll.contentHeight - y - 18
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: workbench.width >= 1320 ? 318 : 278
                Layout.minimumWidth: 260
                Layout.maximumWidth: workbench.width >= 1320 ? 318 : 278
                Layout.fillHeight: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 270
                    radius: 9; color: root.theme.bgPanel; border.color: root.theme.border
                    LiveInstrumentSelector {
                        anchors.fill: parent; anchors.margins: 16
                        theme: root.theme; controller: root.controller
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 270
                    radius: 9; color: root.theme.bgPanel; border.color: root.theme.border
                    LocalBacktestSelector {
                        anchors.fill: parent; anchors.margins: 16
                        theme: root.theme; controller: root.controller
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.fillHeight: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 270
                    radius: 9; color: root.theme.bgPanel; border.color: root.theme.border

                    Text {
                        x: 18; y: 14; width: parent.width - 36
                        text: "实时 1 分钟 K 线"
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.Bold
                    }
                    Text {
                        x: 18; y: 38; width: parent.width - 36
                        text: root.controller.selectedLiveInstrumentTitle.length > 0
                              ? root.controller.selectedLiveInstrumentTitle + " · "
                                + root.controller.realtimeMinuteStatusText
                              : "未选择实时品种"
                        color: root.theme.textMuted
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                    }
                    Row {
                        x: 18; y: 62; height: 34; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "周期 1min"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold }
                        PpButton {
                            theme: root.theme; text: "刷新行情"; visualWidth: 82; primary: true
                            available: !root.controller.realtimeMinuteLoading
                            toolTip: "只读取当前选中品种的免费 1 分钟行情；不写历史数据库"
                            onClicked: root.requestRealtimeMinute()
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.controller.realtimeMinuteLastBarTime.toString().length > 0 ? Qt.formatDateTime(root.controller.realtimeMinuteLastBarTime, "yyyy-MM-dd HH:mm") : "时间 --"; color: root.controller.realtimeMinuteStale ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.controller.realtimeMinuteStale ? "休市/陈旧" : "已更新"; color: root.controller.realtimeMinuteStale ? root.theme.yellow : root.theme.green; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                    CandleSeriesChart {
                        x: 18; y: 102; width: parent.width - 36; height: parent.height - 118
                        theme: root.theme; model: root.controller.realtimeMinuteCandleModel
                        loading: root.controller.realtimeMinuteLoading
                        statusText: root.controller.realtimeMinuteStatusText
                        errorText: root.controller.realtimeMinuteError
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 270
                    radius: 9; color: root.theme.bgPanel; border.color: root.theme.border

                    Text {
                        x: 18; y: 14; width: parent.width - 36
                        text: "历史 Tick 派生 K 线"
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.Bold
                    }
                    Text {
                        x: 18; y: 38; width: parent.width - 36
                        text: root.controller.selectedBacktestInstrumentTitle.length > 0
                              ? root.controller.selectedBacktestInstrumentTitle + " · "
                                + root.controller.localCandleSeriesStatusText
                              : "尚未选择本地历史品种"
                        color: root.theme.textMuted
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                    }
                    Row {
                        x: 18; y: 62; height: 34; spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: "周期（秒）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            id: historyPeriodField; width: 92; height: 32; text: String(root.historyPeriodSeconds)
                            validator: IntValidator { bottom: 1; top: 86400 }
                            color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                            Accessible.name: "历史 K 线周期秒数"
                            background: Rectangle { radius: 5; color: root.theme.bgInput; border.color: historyPeriodField.activeFocus ? root.theme.cyan : root.theme.border }
                        }
                        PpButton {
                            theme: root.theme; text: "应用周期"; visualWidth: 82; primary: true
                            available: !root.controller.localCandleSeriesLoading
                            toolTip: "优先复用周期缓存；原始 Tick 改变后自动重建"
                            onClicked: { root.historyPeriodSeconds = Number(historyPeriodField.text); root.requestHistory() }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.controller.localCandleSeriesTotalBars > 0 ? "共 " + root.controller.localCandleSeriesTotalBars + " 根" : "共 -- 根"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                    CandleSeriesChart {
                        x: 18; y: 102; width: parent.width - 36; height: parent.height - 118
                        theme: root.theme; model: root.controller.localCandleSeriesModel
                        loading: root.controller.localCandleSeriesLoading
                        statusText: root.controller.localCandleSeriesStatusText
                        errorText: root.controller.localCandleSeriesError
                    }
                }
            }
        }
    }

    Connections {
        target: root.controller.marketSnapshotModel
        function onModelReset() { root.refreshLiveQuote() }
        function onCountChanged() { root.refreshLiveQuote() }
    }
    Connections {
        target: root.controller
        function onLiveInstrumentChanged() { root.refreshLiveQuote() }
        function onMarketSnapshotChanged() { root.refreshLiveQuote() }
        function onSnapshotQueryStateChanged() { root.refreshLiveQuote() }
    }
    Connections {
        target: root.controller
        function onBacktestInstrumentChanged() {
            if (root.controller.selectedBacktestPeriodSeconds > 0)
                root.historyPeriodSeconds = root.controller.selectedBacktestPeriodSeconds
            root.requestHistory()
        }
    }
}
