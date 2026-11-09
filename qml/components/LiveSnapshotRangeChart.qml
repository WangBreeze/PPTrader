// qmllint disable unqualified
import QtQuick

Item {
    id: root
    required property var theme
    property var quote: ({})
    property string snapshotError: ""
    property string snapshotHealthText: ""
    property bool providerHealthy: false

    readonly property bool hasRange: Number(quote.price) > 0
                                     && Number(quote.high) > 0
                                     && Number(quote.low) > 0
                                     && Number(quote.high) >= Number(quote.low)
    readonly property bool stale: Boolean(quote.stale)
    readonly property string stateText: snapshotError.length > 0
        ? "快照错误 · " + snapshotError
        : !hasRange ? "当前品种没有可用快照报价"
        : stale ? "报价已陈旧 · 图中仍显示最后真实快照"
        : !providerHealthy ? "Provider 当前不健康 · 图中为已保存快照"
        : "最新快照价位范围"

    Accessible.role: Accessible.Graphic
    Accessible.name: hasRange
        ? quote.symbol + " 最新快照价位图，最低 " + Number(quote.low).toFixed(2)
          + "，最高 " + Number(quote.high).toFixed(2)
          + "，最新 " + Number(quote.price).toFixed(2)
        : stateText

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.Image
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const left = 54
            const right = 34
            const top = 28
            const bottom = 34
            const plotWidth = Math.max(1, width - left - right)
            const middleY = top + Math.max(1, height - top - bottom) / 2

            ctx.strokeStyle = root.theme.gridY
            ctx.lineWidth = 1
            for (let line = 0; line < 4; ++line) {
                const y = top + (height - top - bottom) * line / 3
                ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(width - right, y); ctx.stroke()
            }
            if (!root.hasRange) return

            const low = Number(root.quote.low)
            const high = Number(root.quote.high)
            const open = Number(root.quote.open)
            const price = Number(root.quote.price)
            const range = Math.max(0.000001, high - low)
            const xFor = function(value) {
                return left + plotWidth * Math.max(0, Math.min(1, (value - low) / range))
            }

            ctx.strokeStyle = root.theme.track
            ctx.lineWidth = 8
            ctx.lineCap = "round"
            ctx.beginPath(); ctx.moveTo(left, middleY); ctx.lineTo(width - right, middleY); ctx.stroke()
            ctx.strokeStyle = root.theme.cyan
            ctx.lineWidth = 3
            ctx.beginPath(); ctx.moveTo(left, middleY); ctx.lineTo(xFor(price), middleY); ctx.stroke()

            if (isFinite(open) && open > 0) {
                ctx.strokeStyle = root.theme.yellow
                ctx.lineWidth = 2
                ctx.beginPath(); ctx.moveTo(xFor(open), middleY - 22)
                ctx.lineTo(xFor(open), middleY + 22); ctx.stroke()
            }
            ctx.fillStyle = price >= Number(root.quote.previousClose)
                            ? root.theme.green : root.theme.red
            ctx.beginPath(); ctx.arc(xFor(price), middleY, 7, 0, Math.PI * 2); ctx.fill()

            ctx.fillStyle = root.theme.textMuted
            ctx.font = "10px " + root.theme.canvasFontFamily
            ctx.textAlign = "left"
            ctx.fillText("低 " + low.toFixed(2), left, middleY + 36)
            ctx.textAlign = "right"
            ctx.fillText("高 " + high.toFixed(2), width - right, middleY + 36)
            if (isFinite(open) && open > 0) {
                ctx.textAlign = "center"
                ctx.fillStyle = root.theme.yellow
                ctx.fillText("开 " + open.toFixed(2), xFor(open), middleY - 30)
            }
            ctx.fillStyle = price >= Number(root.quote.previousClose)
                            ? root.theme.green : root.theme.red
            ctx.fillText("现 " + price.toFixed(2), xFor(price), middleY + 56)
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 4
        width: parent.width - 24
        horizontalAlignment: Text.AlignHCenter
        text: root.stateText
        color: root.snapshotError.length > 0 ? root.theme.red
               : root.stale || !root.providerHealthy ? root.theme.yellow
               : root.hasRange ? root.theme.textMuted : root.theme.textMuted
        elide: Text.ElideRight
        font.family: root.theme.fontFamily; font.pixelSize: 10
    }

    onQuoteChanged: canvas.requestPaint()
}
