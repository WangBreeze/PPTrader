// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    property string strategyJson: defaultStrategyJson()

    function defaultStrategyJson() {
        return JSON.stringify({
            schemaVersion: 3,
            strategyId: "custom_cross_momentum",
            version: 1,
            name: "横截面动量轮动",
            description: "按 20 日动量排名选前 3 只",
            author: "本地用户",
            market: "A股",
            executionScope: "backtest_only",
            createdAt: "2026-08-15T00:00:00.000Z",
            parameters: [],
            universe: { mode: "explicit", symbols: ["600519", "000001", "600036"] },
            crossIndicators: [
                { alias: "mom_rank", indicator: "RANK",
                  of: { indicator: "ROC", period: 20 }, ascending: false }
            ],
            selection: { mode: "topN", rankBy: "mom_rank", count: 3, ascending: true },
            position: { type: "equalWeight" },
            entry: { op: "gte", left: { cross: "mom_rank" }, right: { value: 1 } },
            exit: { op: "lt", left: { cross: "mom_rank" }, right: { value: 3 } }
        }, null, 2)
    }

    function runCrossSectional() {
        root.controller.runCrossSectionalBacktest(
            strategyField.text, Math.max(2, Number(barCountField.text) || 200))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Text {
            text: "多股横截面回测"
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
        }
        Text {
            Layout.fillWidth: true
            text: "同一时刻 n 只股票对齐价格参与计算 · 横截面选股/轮动 · 只读取 raw_tick_cache_v1"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 9
        }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "回测根数"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            TextField {
                id: barCountField
                Layout.preferredWidth: 80; Layout.preferredHeight: 28
                text: "200"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.width: parent.activeFocus ? 2 : 1
                    border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 180
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ScrollView {
                anchors.fill: parent; anchors.margins: 6
                TextArea {
                    id: strategyField
                    text: root.strategyJson
                    color: root.theme.textPrimary
                    font.family: "Consolas"; font.pixelSize: 10
                    selectByMouse: true
                    wrapMode: TextEdit.NoWrap
                    background: Item {}
                    Accessible.name: "多股策略 DSL v3 JSON"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            PpButton {
                theme: root.theme; text: "运行多股回测"; primary: true; visualWidth: 140
                available: !root.controller.crossSectionalBacktestRunning
                           && !root.controller.batchBacktestRunning
                           && !root.controller.backtestRunning
                           && !root.controller.portfolioBacktestRunning
                toolTip: available ? "按 DSL v3 横截面选股运行多股回测"
                                    : "请等待其他回测结束"
                onClicked: root.runCrossSectional()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 76
                available: root.controller.crossSectionalBacktestRunning
                onClicked: root.controller.cancelCrossSectionalBacktest()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.crossSectionalBacktestStatusText
                color: root.controller.crossSectionalBacktestRunning ? root.theme.yellow : root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.crossSectionalBacktestError.length > 0
            text: root.controller.crossSectionalBacktestError
            color: root.theme.red; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 150
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            BacktestEquityChart {
                anchors.fill: parent; anchors.margins: 8
                theme: root.theme
                model: root.controller.crossSectionalEquityCurveModel
            }
        }
        RowLayout {
            Layout.fillWidth: true
            readonly property var config: root.controller.crossSectionalResultConfiguration
            Text {
                Layout.fillWidth: true
                text: config && config.finalEquity
                      ? "最终权益 " + Number(config.finalEquity).toFixed(2)
                        + " · 收益 " + Number(config.totalReturnPercent).toFixed(2) + "%"
                        + " · 回撤 " + Number(config.maxDrawdownPercent).toFixed(2) + "%"
                        + " · Sharpe " + Number(config.sharpeRatio).toFixed(3)
                        + " · 手续费 " + Number(config.totalCommission).toFixed(2)
                        + " · 调仓 " + config.totalTrades + " 笔"
                      : "尚未运行多股回测"
                color: root.theme.textPrimary; elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 96
            radius: 7; color: root.theme.bgInput; border.color: root.theme.border
            clip: true
            ListView {
                anchors.fill: parent; anchors.margins: 6
                model: root.controller.crossSectionalTradeModel
                spacing: 3
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                header: Text {
                    height: 16
                    text: "调仓成交（" + root.controller.crossSectionalTradeModel.count + "）"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                }
                delegate: Rectangle {
                    required property string symbol
                    required property string side
                    required property double quantity
                    required property double price
                    required property double commission
                    required property double amount
                    required property var timestamp
                    required property string reason
                    width: ListView.view.width
                    height: 22
                    radius: 4
                    color: root.theme.bgCanvas
                    border.color: root.theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 64
                            text: symbol
                            color: root.theme.textPrimary
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 30
                            text: side === "buy" ? "买" : "卖"
                            color: side === "buy" ? root.theme.red : root.theme.cyan
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 76
                            text: quantity + " 股 @" + Number(price).toFixed(2)
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.preferredWidth: 76
                            text: "金额 " + Number(amount).toFixed(0) + " · 费 " + Number(commission).toFixed(2)
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                        Text {
                            Layout.fillWidth: true
                            text: reason + " · " + Qt.formatDateTime(timestamp, "MM-dd HH:mm")
                            color: root.theme.textMuted; elide: Text.ElideRight
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                    }
                }
            }
        }
    }
}
