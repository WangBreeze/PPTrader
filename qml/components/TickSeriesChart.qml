// qmllint disable unqualified
import QtQuick
import PPTrader 1.0

Item {
    id: root
    required property var theme
    required property var seriesModel
    property bool loadingState: false
    property string statusText: ""
    property string errorText: ""
    property var hoverPoint: ({})
    property real dragLastX: 0
    property bool dragging: false
    activeFocusOnTab: visible
    Accessible.name: "逐笔成交事件序列图"
    Accessible.description: "横轴为成交事件顺序，滚轮缩放，拖拽平移，双击复位；暂不提供逐点键盘选择"

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: root.theme.bgInput
        border.color: root.activeFocus ? root.theme.cyan : root.theme.border
    }

    Canvas {
        id: grid
        anchors.fill: plot
        opacity: 0.55
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = root.theme.border
            ctx.lineWidth = 1
            for (let i = 1; i < 5; ++i) {
                const y = Math.round(height * i / 5) + 0.5
                ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
            }
            for (let i = 1; i < 8; ++i) {
                const x = Math.round(width * i / 8) + 0.5
                ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Item {
        id: plot
        x: 12; y: 12; width: parent.width - 24; height: parent.height - 34
        clip: true

        LocalTickSeriesItem {
            id: series
            anchors.fill: parent
            model: root.seriesModel
            lineColor: root.theme.cyan
            buyColor: root.theme.red
            sellColor: root.theme.green
            neutralColor: root.theme.textMuted
        }

        Rectangle {
            visible: Object.keys(root.hoverPoint).length > 0
            x: Math.max(4, Math.min(parent.width - width - 4, mouseArea.mouseX + 12))
            y: 5
            width: 236; height: 64; radius: 5
            color: root.theme.bgPanel
            border.color: root.theme.border
            opacity: 0.96
            Column {
                anchors.fill: parent; anchors.margins: 7; spacing: 2
                Text { text: root.hoverPoint.timestamp ? Qt.formatDateTime(root.hoverPoint.timestamp, "yyyy-MM-dd HH:mm:ss") + " · 第 " + (Number(root.hoverPoint.eventIndex) + 1) + " 笔" : ""; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { text: root.hoverPoint.price !== undefined ? "价格 " + Number(root.hoverPoint.price).toFixed(3) + " · " + root.hoverPoint.directionText : ""; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold }
                Text { text: root.hoverPoint.volumeShares !== undefined ? "成交 " + Number(root.hoverPoint.volumeShares).toLocaleString(Qt.locale("zh_CN"), "f", 0) + " 股" : ""; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            onPressed: function(mouse) { root.forceActiveFocus(); root.dragging = true; root.dragLastX = mouse.x }
            onPositionChanged: function(mouse) {
                if (root.dragging) { series.panByPixels(mouse.x - root.dragLastX); root.dragLastX = mouse.x }
                root.hoverPoint = series.findNearest(mouse.x)
            }
            onReleased: root.dragging = false
            onExited: { root.dragging = false; root.hoverPoint = ({}) }
            onDoubleClicked: series.resetViewport()
            onWheel: function(wheel) { series.zoomAt(wheel.x, wheel.angleDelta.y > 0 ? 1.35 : 0.74); wheel.accepted = true }
        }
    }

    Text {
        anchors.left: parent.left; anchors.leftMargin: 12
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
        text: series.pointCount > 0 ? "横轴：成交事件顺序 · 可见 " + series.visibleCount + " / " + series.pointCount + " 点" : ""
        color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8
    }

    Rectangle {
        anchors.centerIn: parent
        visible: root.loadingState || root.errorText.length > 0 || series.pointCount === 0
        width: Math.min(parent.width - 32, 430); height: 54; radius: 6
        color: root.theme.bgPanel; border.color: root.errorText.length > 0 ? root.theme.red : root.theme.border
        Text {
            anchors.fill: parent; anchors.margins: 8
            text: root.loadingState ? "正在读取本地逐笔成交…" : root.errorText.length > 0 ? root.errorText : root.statusText
            color: root.errorText.length > 0 ? root.theme.red : root.theme.textMuted
            wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            font.family: root.theme.fontFamily; font.pixelSize: 10
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Left) { series.panByPixels(28); event.accepted = true }
        else if (event.key === Qt.Key_Right) { series.panByPixels(-28); event.accepted = true }
        else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) { series.zoomAt(series.width / 2, 1.35); event.accepted = true }
        else if (event.key === Qt.Key_Minus) { series.zoomAt(series.width / 2, 0.74); event.accepted = true }
        else if (event.key === Qt.Key_Home) { series.resetViewport(); event.accepted = true }
    }
}
