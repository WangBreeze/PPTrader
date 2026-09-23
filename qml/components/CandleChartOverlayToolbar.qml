// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import PPTrader 1.0

Row {
    id: root
    required property var theme
    readonly property bool volumeLayerEnabled:
        ChartIndicatorPreferences.volumeLayerEnabled
    readonly property bool movingAverageLayerEnabled:
        ChartIndicatorPreferences.movingAverageLayerEnabled
    readonly property bool bollingerLayerEnabled:
        ChartIndicatorPreferences.bollingerLayerEnabled
    readonly property bool rsiLayerEnabled:
        ChartIndicatorPreferences.rsiLayerEnabled
    readonly property bool macdLayerEnabled:
        ChartIndicatorPreferences.macdLayerEnabled
    readonly property int movingAverageFastPeriod:
        ChartIndicatorPreferences.movingAverageFastPeriod
    readonly property int movingAverageSlowPeriod:
        ChartIndicatorPreferences.movingAverageSlowPeriod
    readonly property int bollingerPeriod: ChartIndicatorPreferences.bollingerPeriod
    readonly property real bollingerStandardDeviations:
        ChartIndicatorPreferences.bollingerStandardDeviations
    readonly property int rsiPeriod: ChartIndicatorPreferences.rsiPeriod
    readonly property int macdFastPeriod: ChartIndicatorPreferences.macdFastPeriod
    readonly property int macdSlowPeriod: ChartIndicatorPreferences.macdSlowPeriod
    readonly property int macdSignalPeriod: ChartIndicatorPreferences.macdSignalPeriod
    spacing: 4

    PpButton {
        theme: root.theme
        text: "成交量"
        active: root.volumeLayerEnabled
        visualWidth: 50
        visualHeight: 22
        toolTip: root.volumeLayerEnabled ? "隐藏成交量图层" : "显示成交量图层"
        Accessible.name: root.volumeLayerEnabled ? "隐藏成交量图层" : "显示成交量图层"
        onClicked: ChartIndicatorPreferences.volumeLayerEnabled =
                       !ChartIndicatorPreferences.volumeLayerEnabled
    }
    PpButton {
        theme: root.theme
        text: "MA " + root.movingAverageFastPeriod
              + "/" + root.movingAverageSlowPeriod
        active: root.movingAverageLayerEnabled
        visualWidth: 58
        visualHeight: 22
        toolTip: root.movingAverageLayerEnabled
                 ? "隐藏参数化移动平均线"
                 : "显示参数化移动平均线"
        Accessible.name: root.movingAverageLayerEnabled
                         ? "隐藏移动平均线"
                         : "显示移动平均线"
        onClicked: ChartIndicatorPreferences.movingAverageLayerEnabled =
                       !ChartIndicatorPreferences.movingAverageLayerEnabled
    }
    PpButton {
        theme: root.theme
        text: "BOLL " + root.bollingerPeriod
        active: root.bollingerLayerEnabled
        visualWidth: 48
        visualHeight: 22
        toolTip: root.bollingerLayerEnabled
                 ? "隐藏参数化布林带"
                 : "显示参数化布林带"
        Accessible.name: root.bollingerLayerEnabled
                         ? "隐藏布林带"
                         : "显示布林带"
        onClicked: ChartIndicatorPreferences.bollingerLayerEnabled =
                       !ChartIndicatorPreferences.bollingerLayerEnabled
    }
    PpButton {
        theme: root.theme
        text: "RSI"
        active: root.rsiLayerEnabled
        visualWidth: 44
        visualHeight: 22
        toolTip: root.rsiLayerEnabled
                 ? "隐藏 RSI 独立副图"
                 : "显示 RSI 独立副图"
        Accessible.name: root.rsiLayerEnabled
                         ? "隐藏 RSI 独立副图"
                         : "显示 RSI 独立副图"
        onClicked: ChartIndicatorPreferences.rsiLayerEnabled =
                       !ChartIndicatorPreferences.rsiLayerEnabled
    }
    PpButton {
        theme: root.theme
        text: "MACD"
        active: root.macdLayerEnabled
        visualWidth: 50
        visualHeight: 22
        toolTip: root.macdLayerEnabled
                 ? "隐藏 MACD 独立副图"
                 : "显示 MACD 独立副图"
        Accessible.name: root.macdLayerEnabled
                         ? "隐藏 MACD 独立副图"
                         : "显示 MACD 独立副图"
        onClicked: ChartIndicatorPreferences.macdLayerEnabled =
                       !ChartIndicatorPreferences.macdLayerEnabled
    }
    PpButton {
        theme: root.theme
        text: "参数"
        visualWidth: 46
        visualHeight: 22
        toolTip: "调整 MA、BOLL、RSI 与 MACD 周期"
        Accessible.name: "打开图表指标参数"
        onClicked: parameterPopup.open()
    }

    Popup {
        id: parameterPopup
        x: 0
        y: root.height + 4
        width: 360
        height: parameterColumn.implicitHeight + 20
        padding: 10
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 7
            color: root.theme.bgRaised
            border.color: root.theme.border
        }
        Column {
            id: parameterColumn
            width: parent.width
            spacing: 7
            Text {
                text: "指标周期参数"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
            Row {
                spacing: 6
                Text { width: 76; text: "MA 快 / 慢"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                SpinBox {
                    id: maFastSpin
                    width: 76; height: 28; from: 1
                    to: Math.max(from, maSlowSpin.value - 1)
                    value: root.movingAverageFastPeriod
                    editable: true
                    Accessible.name: "移动平均快周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setMovingAveragePeriods(
                                         value, maSlowSpin.value)
                }
                SpinBox {
                    id: maSlowSpin
                    width: 76; height: 28; from: maFastSpin.value + 1; to: 500
                    value: root.movingAverageSlowPeriod
                    editable: true
                    Accessible.name: "移动平均慢周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setMovingAveragePeriods(
                                         maFastSpin.value, value)
                }
            }
            Row {
                spacing: 6
                Text { width: 76; text: "BOLL 周期/σ"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                SpinBox {
                    id: bollingerPeriodSpin
                    width: 76; height: 28; from: 2; to: 500
                    value: root.bollingerPeriod
                    editable: true
                    Accessible.name: "布林带周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setBollingerParameters(
                                         value, bollingerDeviationSpin.value / 10)
                }
                SpinBox {
                    id: bollingerDeviationSpin
                    width: 76; height: 28; from: 1; to: 100
                    value: Math.round(root.bollingerStandardDeviations * 10)
                    editable: true
                    textFromValue: function(value) { return (value / 10).toFixed(1) }
                    valueFromText: function(text) { return Math.round(Number(text) * 10) }
                    Accessible.name: "布林带标准差倍数"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setBollingerParameters(
                                         bollingerPeriodSpin.value, value / 10)
                }
            }
            Row {
                spacing: 6
                Text { width: 76; text: "RSI 周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                SpinBox {
                    width: 76; height: 28; from: 2; to: 500
                    value: root.rsiPeriod
                    editable: true
                    Accessible.name: "RSI 周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.rsiPeriod = value
                }
            }
            Row {
                spacing: 6
                Text { width: 76; text: "MACD 快/慢/信号"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                SpinBox {
                    id: macdFastSpin
                    width: 70; height: 28; from: 1
                    to: Math.max(from, macdSlowSpin.value - 1)
                    value: root.macdFastPeriod
                    editable: true
                    Accessible.name: "MACD 快周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setMacdPeriods(
                                         value, macdSlowSpin.value,
                                         macdSignalSpin.value)
                }
                SpinBox {
                    id: macdSlowSpin
                    width: 70; height: 28; from: macdFastSpin.value + 1; to: 500
                    value: root.macdSlowPeriod
                    editable: true
                    Accessible.name: "MACD 慢周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setMacdPeriods(
                                         macdFastSpin.value, value,
                                         macdSignalSpin.value)
                }
                SpinBox {
                    id: macdSignalSpin
                    width: 70; height: 28; from: 1; to: 500
                    value: root.macdSignalPeriod
                    editable: true
                    Accessible.name: "MACD 信号周期"
                    palette.base: root.theme.bgInput
                    palette.text: root.theme.textPrimary
                    palette.button: root.theme.bgPanel
                    palette.buttonText: root.theme.textPrimary
                    onValueModified: ChartIndicatorPreferences.setMacdPeriods(
                                         macdFastSpin.value,
                                         macdSlowSpin.value, value)
                }
            }
            PpButton {
                theme: root.theme
                text: "恢复默认参数"
                visualWidth: 104
                visualHeight: 24
                toolTip: "恢复 MA(5,20)、BOLL(20,2)、RSI(14)、MACD(12,26,9) 和默认图层"
                Accessible.name: "恢复图表指标默认参数"
                onClicked: ChartIndicatorPreferences.resetDefaults()
            }
        }
    }
}
