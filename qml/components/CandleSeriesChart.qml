// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import PPTrader 1.0

Control {
    id: root
    required property var theme
    required property var model
    property bool loading: false
    property string statusText: ""
    property string errorText: ""
    property bool dynamicMode: false
    property bool canLoadOlderData: false
    property bool olderDataLoading: false
    property var hoverPoint: ({})
    property real crosshairX: 0
    property real crosshairY: 0
    property bool crosshairLocked: false
    property string actionFeedback: ""
    readonly property string hoverIndicatorText: indicatorSummary(root.hoverPoint)
    signal olderDataRequested()

    function indicatorSummary(point) {
        const parts = []
        if (overlayToolbar.movingAverageLayerEnabled) {
            if (isFinite(Number(point.movingAverageFast)))
                parts.push("MA" + point.movingAverageFastPeriod + " "
                           + Number(point.movingAverageFast).toFixed(2))
            if (isFinite(Number(point.movingAverageSlow)))
                parts.push("MA" + point.movingAverageSlowPeriod + " "
                           + Number(point.movingAverageSlow).toFixed(2))
        }
        if (overlayToolbar.bollingerLayerEnabled
                && isFinite(Number(point.bollingerUpper))) {
            parts.push("BOLL" + point.bollingerPeriod + " "
                       + Number(point.bollingerUpper).toFixed(2)
                       + "/" + Number(point.bollingerMiddle).toFixed(2)
                       + "/" + Number(point.bollingerLower).toFixed(2))
        }
        if (overlayToolbar.rsiLayerEnabled && isFinite(Number(point.rsi)))
            parts.push("RSI" + point.rsiPeriod + " " + Number(point.rsi).toFixed(2))
        if (overlayToolbar.macdLayerEnabled && isFinite(Number(point.macd)))
            parts.push("MACD " + Number(point.macd).toFixed(3)
                       + "/" + Number(point.macdSignal).toFixed(3)
                       + "/" + Number(point.macdHistogram).toFixed(3))
        return parts.join(" · ")
    }

    function copyCurrentBar() {
        if (Object.keys(root.hoverPoint).length === 0
                || !chart.copyNearestToClipboard(root.crosshairX)) {
            root.actionFeedback = "没有可复制的 K 线"
        } else {
            root.actionFeedback = "当前 K 线已复制"
        }
        feedbackTimer.restart()
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Graphic
    Accessible.name: "真实 K 线图"
    Accessible.description: dynamicMode
                            ? "动态 K 线：在最新位置自动跟随，拖到历史区域后暂停跟随；滚轮缩放，Shift 加滚轮平移，拖动右侧价格轴缩放，单击锁定十字线，Ctrl+C 复制，End 返回最新"
                            : "滚轮缩放，拖动平移，拖动右侧价格轴缩放，单击锁定十字线，Ctrl+C 复制，双击复位，Escape 取消锁定"
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.crosshairLocked = false
            root.hoverPoint = ({})
            event.accepted = true
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            chart.zoomAt(chart.width / 2, 1.2)
            event.accepted = true
        } else if (event.key === Qt.Key_Minus) {
            chart.zoomAt(chart.width / 2, 1 / 1.2)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            chart.panByPixels(chart.width / Math.max(1, chart.visibleCount) * 5)
            event.accepted = true
        } else if (event.key === Qt.Key_Right) {
            chart.panByPixels(-chart.width / Math.max(1, chart.visibleCount) * 5)
            event.accepted = true
        } else if (event.key === Qt.Key_0 || event.key === Qt.Key_Home) {
            chart.resetViewport()
            chart.resetPriceRange()
            event.accepted = true
        } else if (event.key === Qt.Key_End && root.dynamicMode) {
            chart.scrollToLatest()
            event.accepted = true
        } else if (event.key === Qt.Key_C
                   && (event.modifiers & Qt.ControlModifier) !== 0) {
            root.copyCurrentBar()
            event.accepted = true
        }
    }

    background: Rectangle {
        radius: 7
        color: root.theme.bgInput
        border.color: root.activeFocus ? root.theme.cyan : root.theme.border
        border.width: root.activeFocus ? 2 : 1
    }

    CandleChartAxes {
        anchors.fill: parent
        theme: root.theme
        chartItem: chart
    }

    LocalCandleSeriesItem {
        id: chart
        anchors.fill: parent
        anchors.margins: 12
        anchors.rightMargin: 58
        anchors.bottomMargin: 24
        model: root.model
        showVolume: ChartIndicatorPreferences.volumeLayerEnabled
        showMovingAverages: ChartIndicatorPreferences.movingAverageLayerEnabled
        showBollingerBands: ChartIndicatorPreferences.bollingerLayerEnabled
        showRsi: ChartIndicatorPreferences.rsiLayerEnabled
        showMacd: ChartIndicatorPreferences.macdLayerEnabled
        movingAverageFastPeriod: ChartIndicatorPreferences.movingAverageFastPeriod
        movingAverageSlowPeriod: ChartIndicatorPreferences.movingAverageSlowPeriod
        bollingerPeriod: ChartIndicatorPreferences.bollingerPeriod
        bollingerStandardDeviations:
            ChartIndicatorPreferences.bollingerStandardDeviations
        rsiPeriod: ChartIndicatorPreferences.rsiPeriod
        macdFastPeriod: ChartIndicatorPreferences.macdFastPeriod
        macdSlowPeriod: ChartIndicatorPreferences.macdSlowPeriod
        macdSignalPeriod: ChartIndicatorPreferences.macdSignalPeriod
        risingColor: root.theme.red
        fallingColor: root.theme.green
        wickColor: root.theme.textMuted
    }

    MouseArea {
        id: pointerArea
        anchors.fill: chart
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: pressed && pressedButtons === Qt.LeftButton
                     ? Qt.ClosedHandCursor : Qt.CrossCursor
        property real previousX: 0
        property real pressedX: 0
        property real pressedY: 0
        property bool dragged: false

        function updateCrosshair(mouseX, mouseY) {
            if (root.crosshairLocked) return
            root.crosshairX = Math.max(0, Math.min(width, mouseX))
            root.crosshairY = Math.max(0, Math.min(height, mouseY))
            root.hoverPoint = chart.findNearest(root.crosshairX)
        }

        onPressed: function(mouse) {
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                updateCrosshair(mouse.x, mouse.y)
                chartMenu.popup()
                return
            }
            previousX = mouse.x
            pressedX = mouse.x
            pressedY = mouse.y
            dragged = false
            updateCrosshair(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if ((pressedButtons & Qt.LeftButton) !== 0) {
                if (Math.abs(mouse.x - pressedX) + Math.abs(mouse.y - pressedY) > 4)
                    dragged = true
                if (dragged) chart.panByPixels(mouse.x - previousX)
                previousX = mouse.x
            }
            updateCrosshair(mouse.x, mouse.y)
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.LeftButton && !dragged) {
                root.crosshairX = mouse.x
                root.crosshairY = mouse.y
                root.hoverPoint = chart.findNearest(mouse.x)
                root.crosshairLocked = !root.crosshairLocked
            }
            if (root.canLoadOlderData && !root.olderDataLoading
                    && chart.viewportAtLeftEdge
                    && chart.visibleCount < chart.pointCount)
                root.olderDataRequested()
        }
        onWheel: function(wheel) {
            if ((wheel.modifiers & Qt.ShiftModifier) !== 0)
                chart.panByPixels(wheel.angleDelta.y > 0 ? 56 : -56)
            else
                chart.zoomAt(wheel.x, wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2)
            updateCrosshair(wheel.x, wheel.y)
            wheel.accepted = true
        }
        onDoubleClicked: function(mouse) {
            chart.resetViewport()
            chart.resetPriceRange()
            root.crosshairLocked = false
            updateCrosshair(mouse.x, mouse.y)
        }
        onExited: if (!root.crosshairLocked) root.hoverPoint = ({})
    }

    MouseArea {
        id: priceAxisArea
        z: 4
        anchors.top: chart.top
        anchors.bottom: chart.bottom
        anchors.right: parent.right
        width: 58
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.SizeVerCursor
        preventStealing: true
        property real previousY: 0
        onPressed: function(mouse) {
            root.forceActiveFocus()
            previousY = mouse.y
        }
        onPositionChanged: function(mouse) {
            if ((pressedButtons & Qt.LeftButton) === 0) return
            const delta = mouse.y - previousY
            if (Math.abs(delta) >= 1) {
                chart.scalePriceRange(Math.exp(delta / 140))
                previousY = mouse.y
            }
        }
        onDoubleClicked: chart.resetPriceRange()
        Accessible.role: Accessible.Slider
        Accessible.name: "K 线价格轴缩放"
        Accessible.description: "上下拖动调整价格范围，双击恢复自动范围"
    }

    CandleChartOverlayToolbar {
        id: overlayToolbar
        z: 6
        anchors.left: chart.left
        anchors.leftMargin: 4
        anchors.top: chart.top
        anchors.topMargin: 2
        theme: root.theme
    }

    Rectangle {
        visible: root.dynamicMode && chart.pointCount > 0
        anchors.right: parent.right; anchors.rightMargin: 64
        anchors.top: parent.top; anchors.topMargin: 6
        width: dynamicStateText.implicitWidth + 14; height: 20; radius: 4
        color: root.theme.bgRaised
        border.color: chart.viewportAtRightEdge ? root.theme.green : root.theme.yellow
        Text {
            id: dynamicStateText
            anchors.centerIn: parent
            text: chart.viewportAtRightEdge ? "● 实时跟随" : "历史浏览"
            color: chart.viewportAtRightEdge ? root.theme.green : root.theme.yellow
            font.family: root.theme.fontFamily; font.pixelSize: 8
        }
    }

    PpButton {
        visible: root.dynamicMode && !chart.viewportAtRightEdge && chart.pointCount > 0
        anchors.right: parent.right; anchors.rightMargin: 64
        anchors.bottom: parent.bottom; anchors.bottomMargin: 28
        theme: root.theme
        text: "返回最新"
        visualWidth: 74; visualHeight: 24
        primary: true
        toolTip: "恢复右侧实时跟随；快捷键 End"
        onClicked: chart.scrollToLatest()
    }

    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
        x: chart.x + root.crosshairX
        y: chart.y
        width: 1; height: chart.height
        color: root.theme.textMuted; opacity: 0.65
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
                 && root.crosshairY <= chart.height * chart.pricePlotRatio
        x: chart.x
        y: chart.y + Math.min(root.crosshairY,
                              chart.height * chart.pricePlotRatio)
        width: chart.width; height: 1
        color: root.theme.textMuted; opacity: 0.65
    }

    Text {
        anchors.centerIn: parent
        visible: root.loading || chart.pointCount === 0
        text: root.loading ? "正在异步生成 K 线…"
              : root.errorText.length > 0 ? root.errorText
              : root.statusText.length > 0 ? root.statusText : "没有可用 K 线"
        color: root.errorText.length > 0 ? root.theme.red : root.theme.textMuted
        font.family: root.theme.fontFamily; font.pixelSize: 10
        wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
                 && root.crosshairY <= chart.height * chart.pricePlotRatio
        anchors.right: parent.right; anchors.rightMargin: 2
        y: Math.max(chart.y, Math.min(chart.y + chart.height * chart.pricePlotRatio - height,
                                     chart.y + Math.min(root.crosshairY,
                                         chart.height * chart.pricePlotRatio) - height / 2))
        width: 54; height: 18; radius: 3
        color: root.theme.bgRaised; border.color: root.theme.cyan
        Text {
            anchors.centerIn: parent
            text: chart.priceAt(Math.min(root.crosshairY,
                                        chart.height * chart.pricePlotRatio)).toFixed(2)
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 8
        }
    }

    Rectangle {
        visible: root.actionFeedback.length > 0
        anchors.left: chart.left; anchors.leftMargin: 8
        anchors.bottom: chart.bottom; anchors.bottomMargin: 8
        width: feedbackText.implicitWidth + 18; height: 24; radius: 5
        color: root.theme.bgRaised; border.color: root.theme.cyan
        Text {
            id: feedbackText
            anchors.centerIn: parent
            text: root.actionFeedback
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }
        Accessible.name: root.actionFeedback
    }
    Timer {
        id: feedbackTimer
        interval: 1600
        onTriggered: root.actionFeedback = ""
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
        x: Math.max(chart.x, Math.min(chart.x + chart.width - width,
                                     chart.x + root.crosshairX - width / 2))
        anchors.bottom: parent.bottom; anchors.bottomMargin: 3
        width: 106; height: 18; radius: 3
        color: root.theme.bgRaised; border.color: root.theme.cyan
        Text {
            anchors.centerIn: parent
            text: root.hoverPoint.timestamp
                  ? Qt.formatDateTime(root.hoverPoint.timestamp, "MM-dd HH:mm:ss") : "--"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 8
        }
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
        x: chart.x + 8; y: chart.y + 40; width: 340
        height: root.hoverIndicatorText.length > 0 ? 80 : 48
        radius: 6
        color: root.theme.bgRaised; border.color: root.crosshairLocked
                                              ? root.theme.yellow : root.theme.cyan
        Text {
            anchors.fill: parent; anchors.margins: 7
            text: root.hoverPoint.timestamp
                  ? (Qt.formatDateTime(root.hoverPoint.timestamp, "yyyy-MM-dd HH:mm:ss")
                     + "  O " + Number(root.hoverPoint.open).toFixed(2)
                     + " H " + Number(root.hoverPoint.high).toFixed(2)
                     + " L " + Number(root.hoverPoint.low).toFixed(2)
                     + " C " + Number(root.hoverPoint.close).toFixed(2)
                     + "\n量 " + Number(root.hoverPoint.volume).toFixed(0)
                     + (root.hoverIndicatorText.length > 0
                        ? "\n" + root.hoverIndicatorText : "")
                     + (root.crosshairLocked ? " · 已锁定" : ""))
                  : ""
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 8
            wrapMode: Text.Wrap
        }
    }

    Menu {
        id: chartMenu
        MenuItem {
            text: "复位图表"
            Accessible.name: "复位 K 线图缩放和平移"
            onTriggered: {
                chart.resetViewport()
                chart.resetPriceRange()
                root.crosshairLocked = false
                root.hoverPoint = ({})
            }
        }
        MenuItem {
            text: "复制当前 Bar"
            enabled: Object.keys(root.hoverPoint).length > 0
            Accessible.name: "复制当前 K 线 OHLCV 数据"
            onTriggered: root.copyCurrentBar()
        }
        MenuItem {
            text: "恢复自动价格范围"
            enabled: !chart.automaticPriceRange
            Accessible.name: "恢复自动价格范围"
            onTriggered: chart.resetPriceRange()
        }
        MenuItem {
            text: "返回最新"
            visible: root.dynamicMode
            enabled: !chart.viewportAtRightEdge
            Accessible.name: "返回最新 K 线并恢复实时跟随"
            onTriggered: chart.scrollToLatest()
        }
    }
}
