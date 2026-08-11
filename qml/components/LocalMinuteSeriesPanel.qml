// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PPTrader 1.0

Item {
    id: root
    required property var theme
    required property var controller
    property int pageSize: 1000
    property int maximumPoints: 800
    property var hoverPoint: ({})
    readonly property var minuteCatalog: controller.localHistoryDataCatalog.filter(
        function(entry) { return entry.periodSeconds === 60 || entry.periodSeconds === 300 })
    readonly property var selectedDataset: datasetBox.currentIndex >= 0
        && datasetBox.currentIndex < minuteCatalog.length
        ? minuteCatalog[datasetBox.currentIndex] : ({})

    function requestPage(page) {
        if (!selectedDataset.symbol || controller.localCandleSeriesLoading)
            return
        controller.requestLocalCandleSeries(selectedDataset.symbol,
                                            selectedDataset.frequency,
                                            page, pageSize, maximumPoints)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ComboBox {
                id: datasetBox
                Layout.fillWidth: true
                Layout.minimumWidth: 280
                Layout.preferredHeight: 36
                model: root.minuteCatalog
                textRole: "title"
                enabled: count > 0 && !root.controller.localCandleSeriesLoading
                activeFocusOnTab: enabled
                Accessible.name: "选择本地分钟数据集"
                Accessible.description: "只列出 SQLite 已有的 1 分钟和 5 分钟数据，不读取原始 CSV，不访问网络"
                contentItem: Text {
                    leftPadding: 10; rightPadding: 28
                    verticalAlignment: Text.AlignVCenter
                    text: datasetBox.currentIndex >= 0
                          ? root.minuteCatalog[datasetBox.currentIndex].title + " · "
                            + root.minuteCatalog[datasetBox.currentIndex].frequency + " · "
                            + root.minuteCatalog[datasetBox.currentIndex].candleCount + " 根"
                          : "没有可用的本地分钟数据"
                    color: datasetBox.enabled ? root.theme.textPrimary : root.theme.textMuted
                    elide: Text.ElideRight
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                }
                background: Rectangle {
                    radius: 6; color: root.theme.bgInput
                    border.width: datasetBox.activeFocus ? 2 : 1
                    border.color: datasetBox.activeFocus ? root.theme.cyan : root.theme.border
                }
                delegate: ItemDelegate {
                    required property var modelData
                    width: datasetBox.width
                    text: modelData.title + " · " + modelData.frequency
                          + " · " + modelData.candleCount + " 根"
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    Accessible.name: text
                }
            }
            ComboBox {
                id: pageSizeBox
                Layout.preferredWidth: 116; Layout.preferredHeight: 36
                model: [{ label: "每页 500", value: 500 },
                        { label: "每页 1000", value: 1000 },
                        { label: "每页 3000", value: 3000 }]
                textRole: "label"; valueRole: "value"; currentIndex: 1
                Accessible.name: "分钟序列分页大小"
                onActivated: root.pageSize = currentValue
            }
            ComboBox {
                id: pointsBox
                Layout.preferredWidth: 126; Layout.preferredHeight: 36
                model: [{ label: "显示 400 点", value: 400 },
                        { label: "显示 800 点", value: 800 },
                        { label: "显示 1200 点", value: 1200 }]
                textRole: "label"; valueRole: "value"; currentIndex: 1
                Accessible.name: "图表最大显示点数"
                onActivated: root.maximumPoints = currentValue
            }
            PpButton {
                theme: root.theme; text: root.controller.localCandleSeriesLoading ? "读取中" : "加载本地序列"
                visualWidth: 122; primary: true
                available: root.minuteCatalog.length > 0 && !root.controller.localCandleSeriesLoading
                toolTip: available ? "异步读取 SQLite；不会访问网络或原始 CSV"
                                   : "没有本地分钟数据，或查询正在进行"
                onClicked: root.requestPage(0)
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 250
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            clip: true

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
                Connections {
                    target: root.theme
                    function onGridYChanged() { parent.requestPaint() }
                }
            }

            LocalCandleSeriesItem {
                id: chart
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 68
                anchors.topMargin: 18; anchors.bottomMargin: 34
                model: root.controller.localCandleSeriesModel
                risingColor: root.theme.red
                fallingColor: root.theme.green
                wickColor: root.theme.textMuted
                Accessible.role: Accessible.Chart
                Accessible.name: "本地分钟 K 线图"
                Accessible.description: "单个 Scene Graph 图元批量绘制；鼠标悬停可读取最近一根，暂不支持键盘逐根选择"
            }

            MouseArea {
                anchors.fill: chart
                hoverEnabled: true
                onPositionChanged: function(mouse) { root.hoverPoint = chart.findNearest(mouse.x) }
                onExited: root.hoverPoint = ({})
            }

            Column {
                anchors.right: parent.right; anchors.rightMargin: 8
                anchors.top: chart.top; spacing: 4
                Text { text: chart.pointCount > 0 ? chart.maximumPrice.toFixed(2) : "--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Item { width: 1; height: Math.max(0, chart.height - 34) }
                Text { text: chart.pointCount > 0 ? chart.minimumPrice.toFixed(2) : "--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            }

            Text {
                anchors.centerIn: parent
                visible: root.controller.localCandleSeriesLoading || chart.pointCount === 0
                text: root.controller.localCandleSeriesLoading ? "正在从 SQLite 读取…"
                      : root.controller.localCandleSeriesError.length > 0
                        ? root.controller.localCandleSeriesError
                        : "请选择已导入的 1min/5min 数据集并加载"
                color: root.controller.localCandleSeriesError.length > 0
                       ? root.theme.red : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 11
                Accessible.name: text
            }

            Rectangle {
                visible: Object.keys(root.hoverPoint).length > 0
                x: 20; y: 16; width: 270; height: 58; radius: 6
                color: root.theme.bgRaised; border.color: root.theme.cyan
                Text {
                    anchors.fill: parent; anchors.margins: 8
                    text: Qt.formatDateTime(root.hoverPoint.timestamp, "yyyy-MM-dd HH:mm")
                          + "  开 " + Number(root.hoverPoint.open).toFixed(2)
                          + "  高 " + Number(root.hoverPoint.high).toFixed(2)
                          + "  低 " + Number(root.hoverPoint.low).toFixed(2)
                          + "  收 " + Number(root.hoverPoint.close).toFixed(2)
                          + "\n成交量 " + Number(root.hoverPoint.volume).toFixed(0)
                          + (root.hoverPoint.sourceCount > 1
                             ? " · 聚合 " + root.hoverPoint.sourceCount + " 根" : "")
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily
                    font.pixelSize: 9; wrapMode: Text.Wrap
                }
            }

            Text {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                text: root.controller.localCandleSeriesFirstTime.toString().length > 0
                      ? Qt.formatDateTime(root.controller.localCandleSeriesFirstTime, "MM-dd HH:mm") : "--"
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 68
                anchors.bottom: parent.bottom; anchors.bottomMargin: 8
                text: root.controller.localCandleSeriesLastTime.toString().length > 0
                      ? Qt.formatDateTime(root.controller.localCandleSeriesLastTime, "MM-dd HH:mm") : "--"
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text {
                Layout.fillWidth: true
                text: root.controller.localCandleSeriesStatusText
                      + (root.controller.localCandleSeriesTotalBars > 0
                         ? " · 共 " + root.controller.localCandleSeriesTotalBars + " 根"
                           + " · 请求 " + root.controller.localCandleSeriesRequestId : "")
                color: root.controller.localCandleSeriesError.length > 0
                       ? root.theme.red : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                Accessible.name: text
            }
            PpButton {
                theme: root.theme; text: "更新一页"; visualWidth: 76
                available: root.controller.localCandleSeriesPage > 0
                           && !root.controller.localCandleSeriesLoading
                toolTip: available ? "读取时间更近的一页" : "当前已经是最新一页"
                onClicked: root.requestPage(root.controller.localCandleSeriesPage - 1)
            }
            Text {
                text: root.controller.localCandleSeriesPageCount > 0
                      ? "第 " + (root.controller.localCandleSeriesPage + 1) + " / "
                        + root.controller.localCandleSeriesPageCount + " 页" : "第 -- 页"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
            }
            PpButton {
                theme: root.theme; text: "更早一页"; visualWidth: 76
                available: root.controller.localCandleSeriesPage + 1
                           < root.controller.localCandleSeriesPageCount
                           && !root.controller.localCandleSeriesLoading
                toolTip: available ? "读取时间更早的一页" : "没有更早的数据"
                onClicked: root.requestPage(root.controller.localCandleSeriesPage + 1)
            }
        }
    }
}
