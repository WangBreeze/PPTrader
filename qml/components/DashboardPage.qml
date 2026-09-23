// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    required property bool pageActive
    property var liveQuote: ({})
    property int realtimePeriodSeconds: 60
    property int historyPeriodSeconds: 300
    property bool showRawTickSeries: false
    signal backtestWorkspaceRequested()

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
            root.controller.requestRealtimeMinuteSeriesPeriod(
                root.controller.selectedLiveSymbol, root.realtimePeriodSeconds)
    }

    function requestHistory() {
        if (root.controller.selectedBacktestSymbol.length > 0)
            root.controller.requestLocalCandleSeriesPeriod(
                root.controller.selectedBacktestSymbol, root.historyPeriodSeconds,
                0, 3000, 800)
    }

    function requestRawTicks() {
        if (root.controller.selectedBacktestSymbol.length > 0)
            root.controller.requestLocalTickSeries(
                root.controller.selectedBacktestSymbol, tickDateField.text, 100000)
    }

    Component.onCompleted: {
        refreshLiveQuote()
        if (root.controller.selectedBacktestPeriodSeconds > 0)
            root.historyPeriodSeconds = root.controller.selectedBacktestPeriodSeconds
        root.realtimePeriodSeconds = root.controller.realtimeMinutePeriodSeconds
        requestRealtimeMinute()
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
            text: "实时图以免费 1 分钟行情派生常用周期且仅驻留内存 · 回测继续使用独立下载的历史 Tick 数据"
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
                        showWorkspaceActions: true
                        onBacktestWorkspaceRequested: root.backtestWorkspaceRequested()
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
                        text: "实时 " + root.controller.realtimeMinutePeriodOptions[
                                  realtimePeriodCombo.currentIndex >= 0
                                  ? realtimePeriodCombo.currentIndex : 0].label + " K 线"
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
                        ComboBox {
                            id: realtimePeriodCombo
                            width: 82; height: 32
                            model: root.controller.realtimeMinutePeriodOptions
                            textRole: "label"; valueRole: "periodSeconds"
                            currentIndex: {
                                const options = root.controller.realtimeMinutePeriodOptions
                                for (let index = 0; index < options.length; ++index) {
                                    if (Number(options[index].periodSeconds)
                                            === root.realtimePeriodSeconds) return index
                                }
                                return 0
                            }
                            Accessible.name: "实时 K 线常用周期"
                            onActivated: function(index) {
                                root.realtimePeriodSeconds = Number(
                                    root.controller.realtimeMinutePeriodOptions[index].periodSeconds)
                                root.controller.setRealtimeMinutePeriod(
                                    root.realtimePeriodSeconds)
                                if (root.controller.realtimeMinuteCount === 0)
                                    root.requestRealtimeMinute()
                            }
                        }
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
                        theme: root.theme
                        model: root.pageActive ? root.controller.realtimeMinuteCandleModel : null
                        dynamicMode: true
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
                        text: root.showRawTickSeries ? "历史逐笔成交（非周期）" : "历史 Tick 派生 K 线"
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 14; font.weight: Font.Bold
                    }
                    Text {
                        x: 18; y: 38; width: parent.width - 36
                        text: root.controller.selectedBacktestInstrumentTitle.length > 0
                              ? root.controller.selectedBacktestInstrumentTitle + " · "
                                + (root.showRawTickSeries
                                   ? root.controller.localTickSeriesStatusText
                                   : root.controller.localCandleSeriesStatusText)
                              : "尚未选择本地历史品种"
                        color: root.theme.textMuted
                        elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                    }
                    Row {
                        x: 18; y: 62; height: 34; spacing: 8
                        PpButton {
                            theme: root.theme; text: "周期 K 线"; visualWidth: 78
                            active: !root.showRawTickSeries
                            toolTip: "从历史 Tick 按指定周期派生 OHLCV"
                            onClicked: { root.showRawTickSeries = false; root.requestHistory() }
                        }
                        PpButton {
                            theme: root.theme; text: "逐笔成交"; visualWidth: 78
                            active: root.showRawTickSeries
                            toolTip: "非周期序列：横轴为成交事件顺序，保留真实时间与 B/S/M 方向"
                            onClicked: { root.showRawTickSeries = true; root.requestRawTicks() }
                        }
                        Text { visible: !root.showRawTickSeries; anchors.verticalCenter: parent.verticalCenter; text: "周期（秒）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        TextField {
                            id: historyPeriodField; width: 92; height: 32; text: String(root.historyPeriodSeconds)
                            visible: !root.showRawTickSeries
                            validator: IntValidator { bottom: 1; top: 86400 }
                            color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                            Accessible.name: "历史 K 线周期秒数"
                            background: Rectangle { radius: 5; color: root.theme.bgInput; border.color: historyPeriodField.activeFocus ? root.theme.cyan : root.theme.border }
                        }
                        PpButton {
                            theme: root.theme; text: "应用周期"; visualWidth: 82; primary: true
                            visible: !root.showRawTickSeries
                            available: !root.controller.localCandleSeriesLoading
                            toolTip: "优先复用周期缓存；原始 Tick 改变后自动重建"
                            onClicked: { root.historyPeriodSeconds = Number(historyPeriodField.text); root.requestHistory() }
                        }
                        TextField {
                            id: tickDateField; width: 104; height: 32
                            visible: root.showRawTickSeries
                            placeholderText: "最新交易日"
                            color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                            Accessible.name: "逐笔成交交易日，留空读取最新交易日"
                            background: Rectangle { radius: 5; color: root.theme.bgInput; border.color: tickDateField.activeFocus ? root.theme.cyan : root.theme.border }
                        }
                        PpButton {
                            theme: root.theme; text: "加载逐笔"; visualWidth: 82; primary: true
                            visible: root.showRawTickSeries
                            available: !root.controller.localTickSeriesLoading
                            toolTip: "只读取 SQLite 原始 Tick；日期留空时读取最新交易日，不访问网络"
                            onClicked: root.requestRawTicks()
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: root.showRawTickSeries ? (root.controller.localTickSeriesRawCount > 0 ? "原始 " + root.controller.localTickSeriesRawCount + " 笔" : "原始 -- 笔") : (root.controller.localCandleSeriesTotalBars > 0 ? "共 " + root.controller.localCandleSeriesTotalBars + " 根" : "共 -- 根"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                    CandleSeriesChart {
                        x: 18; y: 102; width: parent.width - 36; height: parent.height - 118
                        visible: !root.showRawTickSeries
                        theme: root.theme
                        model: root.pageActive ? root.controller.localCandleSeriesModel : null
                        loading: root.controller.localCandleSeriesLoading
                        statusText: root.controller.localCandleSeriesStatusText
                        errorText: root.controller.localCandleSeriesError
                        canLoadOlderData: root.controller.localCandleSeriesHasOlderData
                        olderDataLoading: root.controller.localCandleSeriesLoading
                        onOlderDataRequested: root.controller.requestOlderLocalCandleSeries()
                    }
                    TickSeriesChart {
                        x: 18; y: 102; width: parent.width - 36; height: parent.height - 118
                        visible: root.showRawTickSeries
                        theme: root.theme
                        seriesModel: root.pageActive ? root.controller.localTickSeriesModel : null
                        loadingState: root.controller.localTickSeriesLoading
                        statusText: root.controller.localTickSeriesStatusText
                        errorText: root.controller.localTickSeriesError
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
        function onLiveInstrumentChanged() { root.refreshLiveQuote(); root.requestRealtimeMinute() }
        function onMarketSnapshotChanged() { root.refreshLiveQuote() }
        function onSnapshotQueryStateChanged() { root.refreshLiveQuote() }
    }
    Connections {
        target: root.controller
        function onBacktestInstrumentChanged() {
            if (root.controller.selectedBacktestPeriodSeconds > 0)
                root.historyPeriodSeconds = root.controller.selectedBacktestPeriodSeconds
            if (!root.visible) return
            if (root.showRawTickSeries) root.requestRawTicks()
            else root.requestHistory()
        }
    }
}
