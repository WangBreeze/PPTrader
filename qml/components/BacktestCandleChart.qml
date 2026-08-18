// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Control {
    id: root
    required property var theme
    required property var model
    property string emptyText: "完成回测后显示 K 线与买卖点"
    readonly property bool hasData: root.model && root.model.count > 0

    readonly property int candleCount: root.model && root.model.count ? root.model.count : 0
    property int hoveredIndex: -1

    function signalAt(relativeX) {
        if (root.candleCount === 0 || !root.model) return null
        const step = plot.width / root.candleCount
        const index = Math.floor(relativeX / step)
        if (index < 0 || index >= root.candleCount) return null
        const item = root.model.get(index)
        if (item.hasSignal !== true) return null
        return { index: index, side: item.signalSide, reason: item.signalReason,
                 price: item.signalPrice, volume: item.volume, time: item.time }
    }

    Accessible.role: Accessible.Graphic
    Accessible.name: hasData
        ? "回测 K 线与买卖点，共 " + root.candleCount + " 根"
        : root.emptyText

    background: Rectangle {
        radius: 7
        color: root.theme.bgInput
        border.color: root.activeFocus ? root.theme.cyan : root.theme.border
        border.width: root.activeFocus ? 2 : 1
    }

    Canvas {
        id: plot
        anchors.fill: parent
        anchors.margins: 8
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const count = root.candleCount
            if (count === 0) return

            let minimum = Infinity
            let maximum = -Infinity
            const candles = []
            for (let index = 0; index < count; ++index) {
                const item = root.model.get(index)
                const o = Number(item.open)
                const h = Number(item.high)
                const l = Number(item.low)
                const c = Number(item.close)
                if (isFinite(h)) maximum = Math.max(maximum, h, o, c)
                if (isFinite(l)) minimum = Math.min(minimum, l, o, c)
                candles.push({ o: o, h: h, l: l, c: c,
                               hasSignal: item.hasSignal === true,
                               signalSide: item.signalSide,
                               signalPrice: Number(item.signalPrice) })
            }
            if (!isFinite(minimum) || !isFinite(maximum)) return
            if (maximum <= minimum) maximum = minimum + 1
            const span = maximum - minimum
            const yOf = v => (1 - (v - minimum) / span) * height
            const step = width / count
            const cw = Math.max(2, step * 0.55)

            ctx.strokeStyle = root.theme.gridY
            ctx.lineWidth = 1
            for (let line = 1; line < 5; ++line) {
                const gy = height * line / 5
                ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(width, gy); ctx.stroke()
            }

            for (let index = 0; index < count; ++index) {
                const bar = candles[index]
                const x = index * step + step / 2
                const rising = bar.c >= bar.o
                ctx.strokeStyle = rising ? root.theme.red : root.theme.green
                ctx.fillStyle = ctx.strokeStyle
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(x, yOf(bar.h))
                ctx.lineTo(x, yOf(bar.l))
                ctx.stroke()
                const top = yOf(Math.max(bar.o, bar.c))
                const body = Math.max(1, Math.abs(yOf(bar.o) - yOf(bar.c)))
                ctx.fillRect(x - cw / 2, top, cw, body)
            }

            for (let index = 0; index < count; ++index) {
                const bar = candles[index]
                if (!bar.hasSignal) continue
                const x = index * step + step / 2
                const buy = bar.signalSide === "买入"
                const price = isFinite(bar.signalPrice) && bar.signalPrice > 0
                    ? bar.signalPrice : (buy ? bar.l : bar.h)
                const y = yOf(price)
                ctx.fillStyle = buy ? root.theme.green : root.theme.red
                ctx.beginPath()
                ctx.arc(x, y, 7, 0, Math.PI * 2)
                ctx.fill()
                ctx.fillStyle = root.theme.bgCanvas
                ctx.font = "bold 9px sans-serif"
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(buy ? "B" : "S", x, y)
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    MouseArea {
        id: hoverArea
        anchors.fill: plot
        hoverEnabled: true
        onPositionChanged: function(mouse) {
            const info = root.signalAt(mouse.x)
            root.hoveredIndex = info ? info.index : -1
        }
        onExited: root.hoveredIndex = -1
    }

    Rectangle {
        id: tooltip
        visible: root.hoveredIndex >= 0
        readonly property var info: root.signalAt(
            plot.width / root.candleCount * (root.hoveredIndex + 0.5))
        x: Math.max(plot.x, Math.min(plot.x + plot.width - width,
                                     plot.x + plot.width / root.candleCount * (root.hoveredIndex + 0.5) - width / 2))
        y: Math.max(plot.y, Math.min(plot.y + plot.height - height, plot.y + 8))
        width: 232; height: 74; radius: 6
        color: root.theme.bgRaised; border.color: root.theme.cyan
        Column {
            anchors.fill: parent; anchors.margins: 8; spacing: 3
            Text {
                width: parent.width
                text: (tooltip.info && tooltip.info.side === "买入" ? "买入 · 进场" : "卖出 · 出场")
                      + "  @ " + (tooltip.info ? tooltip.info.price : "--")
                color: tooltip.info && tooltip.info.side === "买入" ? root.theme.green : root.theme.red
                font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
            }
            Text {
                width: parent.width
                text: "条件：" + (tooltip.info ? tooltip.info.reason : "--")
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: "成交量 " + (tooltip.info ? tooltip.info.volume : "--")
                      + " · " + (tooltip.info ? tooltip.info.time : "--")
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8
                elide: Text.ElideRight
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.hasData
        text: root.emptyText
        color: root.theme.textMuted
        font.family: root.theme.fontFamily; font.pixelSize: 11
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true
        function onModelReset() { plot.requestPaint() }
        function onRowsInserted() { plot.requestPaint() }
        function onCountChanged() { plot.requestPaint() }
    }
}
