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
    property var hoverPoint: ({})
    property real crosshairX: 0
    property real crosshairY: 0
    property bool crosshairLocked: false
    signal olderDataRequested()

    activeFocusOnTab: true
    Accessible.role: Accessible.Graphic
    Accessible.name: "真实 K 线图"
    Accessible.description: "滚轮缩放，拖动平移，单击锁定十字线，双击复位，Escape 取消锁定"
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
            event.accepted = true
        }
    }

    background: Rectangle {
        radius: 7
        color: root.theme.bgInput
        border.color: root.activeFocus ? root.theme.cyan : root.theme.border
        border.width: root.activeFocus ? 2 : 1
    }

    Canvas {
        anchors.fill: chart
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = root.theme.gridY
            ctx.lineWidth = 1
            for (let row = 1; row < 5; ++row) {
                const y = row * height / 5
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            for (let column = 1; column < 8; ++column) {
                const x = column * width / 8
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
        }
    }

    LocalCandleSeriesItem {
        id: chart
        anchors.fill: parent
        anchors.margins: 12
        anchors.rightMargin: 58
        anchors.bottomMargin: 24
        model: root.model
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
            if (chart.viewportAtLeftEdge && chart.visibleCount < chart.pointCount)
                root.olderDataRequested()
        }
        onWheel: function(wheel) {
            chart.zoomAt(wheel.x, wheel.angleDelta.y > 0 ? 1.2 : 1 / 1.2)
            updateCrosshair(wheel.x, wheel.y)
            wheel.accepted = true
        }
        onDoubleClicked: function(mouse) {
            chart.resetViewport()
            root.crosshairLocked = false
            updateCrosshair(mouse.x, mouse.y)
        }
        onExited: if (!root.crosshairLocked) root.hoverPoint = ({})
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
        x: chart.x
        y: chart.y + root.crosshairY
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
    Text {
        anchors.right: parent.right; anchors.rightMargin: 7; anchors.top: chart.top
        text: chart.visibleCount > 0 ? chart.maximumPrice.toFixed(2) : "--"
        color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 7; anchors.bottom: chart.bottom
        text: chart.visibleCount > 0 ? chart.minimumPrice.toFixed(2) : "--"
        color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
        anchors.right: parent.right; anchors.rightMargin: 2
        y: Math.max(chart.y, Math.min(chart.y + chart.height - height,
                                     chart.y + root.crosshairY - height / 2))
        width: 54; height: 18; radius: 3
        color: root.theme.bgRaised; border.color: root.theme.cyan
        Text {
            anchors.centerIn: parent
            text: chart.priceAt(root.crosshairY).toFixed(2)
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 8
        }
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
            text: Qt.formatDateTime(root.hoverPoint.timestamp, "MM-dd HH:mm:ss")
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 8
        }
    }
    Rectangle {
        visible: Object.keys(root.hoverPoint).length > 0
        x: chart.x + 8; y: chart.y + 8; width: 250; height: 48; radius: 6
        color: root.theme.bgRaised; border.color: root.crosshairLocked
                                              ? root.theme.yellow : root.theme.cyan
        Text {
            anchors.fill: parent; anchors.margins: 7
            text: Qt.formatDateTime(root.hoverPoint.timestamp, "yyyy-MM-dd HH:mm:ss")
                  + "  O " + Number(root.hoverPoint.open).toFixed(2)
                  + " H " + Number(root.hoverPoint.high).toFixed(2)
                  + " L " + Number(root.hoverPoint.low).toFixed(2)
                  + " C " + Number(root.hoverPoint.close).toFixed(2)
                  + "\n量 " + Number(root.hoverPoint.volume).toFixed(0)
                  + (root.crosshairLocked ? " · 已锁定" : "")
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
                root.crosshairLocked = false
                root.hoverPoint = ({})
            }
        }
    }
}
