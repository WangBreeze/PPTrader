// qmllint disable unqualified
import QtQuick

Item {
    id: root
    required property var theme
    required property var model
    property string emptyText: "完成回测后显示权益曲线"
    readonly property bool hasData: root.model && root.model.count >= 2

    Accessible.role: Accessible.Graphic
    Accessible.name: hasData
        ? "真实回测权益曲线，共 " + root.model.count + " 个权益点"
        : root.emptyText

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.Image
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const left = 54
            const right = 14
            const top = 16
            const bottom = 28
            const plotWidth = Math.max(1, width - left - right)
            const plotHeight = Math.max(1, height - top - bottom)

            ctx.strokeStyle = root.theme.gridY
            ctx.lineWidth = 1
            for (let line = 0; line <= 4; ++line) {
                const y = top + plotHeight * line / 4
                ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke()
            }

            if (!root.hasData) return
            let minimum = Number.POSITIVE_INFINITY
            let maximum = Number.NEGATIVE_INFINITY
            for (let index = 0; index < root.model.count; ++index) {
                const value = Number(root.model.get(index).equity)
                if (isFinite(value)) {
                    minimum = Math.min(minimum, value)
                    maximum = Math.max(maximum, value)
                }
            }
            if (!isFinite(minimum) || !isFinite(maximum)) return
            const range = Math.max(1, maximum - minimum)

            ctx.strokeStyle = root.theme.cyan
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let row = 0; row < root.model.count; ++row) {
                const equity = Number(root.model.get(row).equity)
                const x = left + plotWidth * row / Math.max(1, root.model.count - 1)
                const y = top + plotHeight * (maximum - equity) / range
                if (row === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
            }
            ctx.stroke()

            ctx.fillStyle = root.theme.textMuted
            ctx.font = "10px " + root.theme.fontFamily
            ctx.textAlign = "right"
            ctx.fillText(maximum.toFixed(0), left - 7, top + 4)
            ctx.fillText(minimum.toFixed(0), left - 7, top + plotHeight)
            ctx.textAlign = "left"
            const first = root.model.get(0)
            const last = root.model.get(root.model.count - 1)
            ctx.fillText(Qt.formatDateTime(first.timestamp, "yyyy-MM-dd"), left, height - 7)
            ctx.textAlign = "right"
            ctx.fillText(Qt.formatDateTime(last.timestamp, "yyyy-MM-dd"), width - right, height - 7)
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Text {
        anchors.centerIn: parent
        visible: !root.hasData
        text: root.emptyText
        color: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 11
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true
        function onModelReset() { canvas.requestPaint() }
        function onRowsInserted() { canvas.requestPaint() }
        function onCountChanged() { canvas.requestPaint() }
    }
}
