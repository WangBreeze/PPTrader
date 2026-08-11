// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import PPTrader 1.0

Control {
    id: root
    required property var theme
    required property var model
    property int boardMask: 31
    property bool logarithmic: true
    readonly property var summary: pointLayer.summary
    property var hoverPoint: ({})
    property var selectedPoint: ({})
    property bool pinned: false
    readonly property real renderMilliseconds: pointLayer.renderMilliseconds
    property string interactionNotice: ""
    signal instrumentActivated(string symbol)

    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Graphic
    Accessible.name: "全市场个股价格散点图"
    Accessible.description: root.hasDisplayPoint
                            ? root.displayPoint.name + " " + root.displayPoint.symbol
                              + "，价格 " + Number(root.displayPoint.price).toFixed(2)
                              + "。键盘逐点选择暂未实现，请使用鼠标悬停或单击。"
                            : "包含 " + pointLayer.pointCount
                              + " 个快照点。键盘逐点选择暂未实现，请使用鼠标悬停或单击。"
    Accessible.focusable: true

    function boardColor(board) {
        if (board === "主板") return root.theme.cyan
        if (board === "深市主板") return root.theme.blue
        if (board === "创业板") return root.theme.purple
        if (board === "科创板") return root.theme.yellow
        if (board === "北交所") return root.theme.green
        return root.theme.textMuted
    }

    function findNearest(mouseX, mouseY) {
        return pointLayer.findNearest(mouseX, mouseY, 10)
    }

    function pointPosition(point) {
        return pointLayer.positionForPoint(point)
    }

    function refreshPinnedSelection() {
        if (!pinned || !selectedPoint.symbol)
            return
        selectedPoint = pointLayer.pointBySymbol(selectedPoint.symbol)
        pinned = selectedPoint.symbol !== undefined
    }

    Canvas {
        id: gridCanvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.lineWidth = 1
            for (var gx = 1; gx < 7; ++gx) {
                ctx.strokeStyle = root.theme.gridX
                ctx.beginPath()
                ctx.moveTo(gx * width / 7, 0)
                ctx.lineTo(gx * width / 7, height)
                ctx.stroke()
            }
            var ticks = [1, 5, 20, 100, 500, 2000]
            for (var gy = 0; gy < ticks.length; ++gy) {
                var y = root.logarithmic
                      ? height - Math.log(ticks[gy]) / Math.log(2000) * height
                      : height - gy / (ticks.length - 1) * height
                ctx.strokeStyle = root.theme.gridY
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(width, y)
                ctx.stroke()
                ctx.fillStyle = root.theme.textMuted
                ctx.font = "9px 'Noto Sans SC'"
                ctx.fillText(String(ticks[gy]), 8, Math.max(11, y - 5))
            }
        }
    }

    CrossSectionScatterItem {
        id: pointLayer
        anchors.fill: parent
        model: root.model
        boardMask: root.boardMask
        logarithmic: root.logarithmic
        mainBoardColor: root.theme.cyan
        shenzhenMainColor: root.theme.blue
        growthBoardColor: root.theme.purple
        starMarketColor: root.theme.yellow
        bseColor: root.theme.green
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: 9
        visible: root.visualFocus
        color: "transparent"
        border.width: 1
        border.color: root.theme.cyan
    }

    readonly property var displayPoint: pinned ? selectedPoint : hoverPoint
    readonly property bool hasDisplayPoint: displayPoint && displayPoint.symbol !== undefined
    readonly property point displayPosition: hasDisplayPoint
                                              ? pointPosition(displayPoint) : Qt.point(0, 0)
    readonly property real displayX: displayPosition.x
    readonly property real displayY: displayPosition.y

    Rectangle {
        visible: root.hasDisplayPoint
        x: root.displayX
        y: 0
        width: 1
        height: parent.height
        color: root.theme.cyan
        opacity: 0.75
    }
    Rectangle {
        visible: root.hasDisplayPoint
        x: 0
        y: root.displayY
        width: parent.width
        height: 1
        color: root.theme.cyan
        opacity: 0.75
    }
    Rectangle {
        visible: root.hasDisplayPoint
        x: root.displayX - 5
        y: root.displayY - 5
        width: 10
        height: 10
        radius: 5
        color: root.boardColor(root.displayPoint.board)
        border.color: root.theme.textPrimary
        border.width: 1
    }

    Rectangle {
        id: tooltip
        visible: root.hasDisplayPoint
        z: 10
        width: 252
        height: 136
        radius: 8
        x: root.displayX + width + 18 > root.width
           ? root.displayX - width - 14 : root.displayX + 14
        y: Math.max(6, Math.min(root.height - height - 6, root.displayY - 24))
        color: root.theme.bgRaised
        border.color: root.theme.cyan

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 5
            Text {
                text: root.hasDisplayPoint
                      ? root.displayPoint.name + "  " + root.displayPoint.symbol : ""
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            Text {
                text: root.hasDisplayPoint
                      ? "¥ " + Number(root.displayPoint.price).toFixed(2) : "--"
                color: root.theme.cyan
                font.family: root.theme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Bold
            }
            Text {
                text: root.hasDisplayPoint
                      ? "涨跌 " + Number(root.displayPoint.changePercent).toFixed(2)
                        + "%  ·  成交量 "
                        + Number(root.displayPoint.volume).toLocaleString(
                            Qt.locale("zh_CN"), "f", 0) : ""
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 9
            }
            Text {
                text: root.hasDisplayPoint && root.displayPoint.snapshotTime
                      ? "快照 " + Qt.formatDateTime(root.displayPoint.snapshotTime,
                                                    "yyyy-MM-dd HH:mm:ss")
                      : "快照 --"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 9
            }
            Text {
                text: root.pinned ? "已固定选择 · 双击功能未实现" : "单击固定选择"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 9
            }
        }
    }

    Rectangle {
        visible: root.interactionNotice.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: noticeText.implicitWidth + 24
        height: 30
        radius: 6
        color: root.theme.bgRaised
        border.color: root.theme.yellow
        Text {
            id: noticeText
            anchors.centerIn: parent
            text: root.interactionNotice
            color: root.theme.yellow
            font.family: root.theme.fontFamily
            font.pixelSize: 10
        }
    }

    Timer {
        id: noticeTimer
        interval: 2200
        onTriggered: root.interactionNotice = ""
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPressed: root.forceActiveFocus(Qt.MouseFocusReason)
        onPositionChanged: function(mouse) {
            if (!root.pinned)
                root.hoverPoint = root.findNearest(mouse.x, mouse.y)
        }
        onExited: if (!root.pinned) root.hoverPoint = ({})
        onClicked: function(mouse) {
            var nearest = root.findNearest(mouse.x, mouse.y)
            if (nearest.symbol !== undefined) {
                root.selectedPoint = nearest
                root.pinned = true
                root.instrumentActivated(nearest.symbol)
            } else {
                root.selectedPoint = ({})
                root.pinned = false
            }
        }
        onDoubleClicked: function(mouse) {
            root.interactionNotice = "双击打开多窗口尚未实现"
            noticeTimer.restart()
        }
    }

    Connections {
        target: pointLayer
        function onSnapshotDataChanged() { root.refreshPinnedSelection() }
    }

    onLogarithmicChanged: gridCanvas.requestPaint()
    onWidthChanged: gridCanvas.requestPaint()
    onHeightChanged: gridCanvas.requestPaint()
}
