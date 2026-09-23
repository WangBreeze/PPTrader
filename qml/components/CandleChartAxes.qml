// qmllint disable unqualified
import QtQuick

Item {
    id: root
    required property var theme
    required property var chartItem

    function volumeLabel(value) {
        const numeric = Number(value)
        if (!isFinite(numeric) || numeric <= 0) return "--"
        if (numeric >= 100000000)
            return (numeric / 100000000).toFixed(2) + "亿"
        if (numeric >= 10000)
            return (numeric / 10000).toFixed(1) + "万"
        return numeric.toFixed(0)
    }

    visible: root.chartItem.visibleCount > 0

    Repeater {
        model: root.chartItem.priceAxisTicks
        delegate: Item {
            required property var modelData
            x: root.chartItem.x
            y: root.chartItem.y
               + Number(modelData.position) * root.chartItem.height
                 * root.chartItem.pricePlotRatio
            width: root.chartItem.width
            height: 1

            Rectangle {
                width: parent.width
                height: 1
                color: root.theme.gridY
                opacity: 0.8
            }
            Text {
                x: parent.width + 6
                y: -height / 2
                text: modelData.label
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
            }
        }
    }

    Repeater {
        model: root.chartItem.timeAxisTicks
        delegate: Item {
            required property var modelData
            x: root.chartItem.x
               + Number(modelData.position) * root.chartItem.width
            y: root.chartItem.y
            width: 1
            height: root.chartItem.height

            Rectangle {
                width: 1
                height: parent.height
                color: root.theme.gridY
                opacity: 0.55
            }
            Text {
                x: Math.max(root.chartItem.x - parent.x,
                            Math.min(root.chartItem.x + root.chartItem.width
                                     - parent.x - width, -width / 2))
                y: parent.height + 3
                text: modelData.label
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
            }
        }
    }

    Rectangle {
        visible: root.chartItem.showVolume
        x: root.chartItem.x
        y: root.chartItem.y
           + root.chartItem.height * root.chartItem.volumePlotTop
        width: root.chartItem.width
        height: 1
        color: root.theme.border
    }
    Text {
        visible: root.chartItem.showVolume
        x: root.chartItem.x + 4
        y: root.chartItem.y
           + root.chartItem.height * root.chartItem.volumePlotTop + 3
        text: "量 " + root.volumeLabel(root.chartItem.maximumVolume)
        color: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 8
    }

    Rectangle {
        visible: root.chartItem.showRsi
        x: root.chartItem.x
        y: root.chartItem.y + root.chartItem.height * root.chartItem.rsiPlotTop
        width: root.chartItem.width
        height: 1
        color: root.theme.border
    }
    Repeater {
        model: root.chartItem.showRsi ? [30, 50, 70] : []
        delegate: Item {
            required property var modelData
            x: root.chartItem.x
            y: root.chartItem.y + root.chartItem.height
               * (root.chartItem.rsiPlotTop
                  + (100 - Number(modelData)) / 100
                    * root.chartItem.rsiPlotRatio)
            width: root.chartItem.width
            height: 1
            Rectangle {
                width: parent.width
                height: 1
                color: Number(parent.modelData) === 50
                       ? root.theme.border : root.theme.gridY
                opacity: Number(parent.modelData) === 50 ? 0.8 : 0.55
            }
            Text {
                x: parent.width + 6
                y: -height / 2
                text: String(parent.modelData)
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
            }
        }
    }
    Text {
        visible: root.chartItem.showRsi
        x: root.chartItem.x + 4
        y: root.chartItem.y + root.chartItem.height * root.chartItem.rsiPlotTop + 3
        text: "RSI " + root.chartItem.rsiPeriod
        color: root.theme.yellow
        font.family: root.theme.fontFamily
        font.pixelSize: 8
    }

    Rectangle {
        visible: root.chartItem.showMacd
        x: root.chartItem.x
        y: root.chartItem.y + root.chartItem.height * root.chartItem.macdPlotTop
        width: root.chartItem.width
        height: 1
        color: root.theme.border
    }
    Repeater {
        model: root.chartItem.showMacd ? root.chartItem.macdAxisTicks : []
        delegate: Item {
            required property var modelData
            x: root.chartItem.x
            y: root.chartItem.y + root.chartItem.height
               * (root.chartItem.macdPlotTop
                  + Number(modelData.position) * root.chartItem.macdPlotRatio)
            width: root.chartItem.width
            height: 1
            Rectangle {
                width: parent.width
                height: 1
                color: root.theme.gridY
                opacity: 0.55
            }
            Text {
                x: parent.width + 6
                y: -height / 2
                text: modelData.label
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
            }
        }
    }
    Text {
        visible: root.chartItem.showMacd
        x: root.chartItem.x + 4
        y: root.chartItem.y + root.chartItem.height * root.chartItem.macdPlotTop + 3
        text: "MACD " + root.chartItem.macdFastPeriod + "/"
              + root.chartItem.macdSlowPeriod + "/"
              + root.chartItem.macdSignalPeriod
        color: root.theme.cyan
        font.family: root.theme.fontFamily
        font.pixelSize: 8
    }
}
