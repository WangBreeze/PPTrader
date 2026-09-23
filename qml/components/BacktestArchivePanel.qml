// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Item {
    id: root
    required property var theme
    required property var controller
    readonly property var selectedSummary: archiveList.currentIndex >= 0
        ? controller.backtestArchiveModel.get(archiveList.currentIndex) : ({})
    readonly property bool rawTickArchiveReady:
        !!root.selectedSummary.resultId
        && root.controller.selectedBacktestArchiveId === root.selectedSummary.resultId
        && root.controller.backtestResultConfiguration.rawTickSnapshotAvailable === true
    readonly property bool rawTickBrowserOpened: rawTickBrowser.opened
    property string rawTickBrowserResultId: ""

    function openRawTickBrowser() {
        if (!root.rawTickArchiveReady)
            return false
        root.rawTickBrowserResultId = root.selectedSummary.resultId
        rawTickBrowser.open()
        root.controller.requestBacktestArchiveRawTickPage(
            root.rawTickBrowserResultId, 0, 256)
        return true
    }

    function restoreSelection() {
        const selected = controller.selectedBacktestArchiveId
        const row = selected.length > 0
            ? controller.backtestArchiveModel.indexOfResultId(selected) : -1
        if (row >= 0)
            archiveList.currentIndex = row
        else if (archiveList.currentIndex >= controller.backtestArchiveCount)
            archiveList.currentIndex = controller.backtestArchiveCount > 0 ? 0 : -1
    }

    Text {
        x: 0; y: 0; text: "已归档回测"
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
    }
    Text {
        anchors.right: parent.right; y: 3
        text: root.controller.backtestArchiveCount + " 条 · SQLite 本地只读恢复"
        color: root.theme.textMuted
        font.family: root.theme.fontFamily; font.pixelSize: 9
    }

    Rectangle {
        x: 0; y: 32; width: Math.max(420, parent.width - 340); height: parent.height - 32
        radius: 7; color: root.theme.bgInput; border.color: root.theme.border
        ListView {
            id: archiveList
            anchors.fill: parent; anchors.margins: 6
            model: root.controller.backtestArchiveModel
            clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
            currentIndex: -1; keyNavigationEnabled: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: archiveDelegate
                required property int index
                required property string resultId
                required property string taskId
                required property var completedAt
                required property string strategyId
                required property int strategyVersion
                required property string symbol
                required property string name
                required property string frequency
                required property int actualBars
                required property real totalReturnPercent
                required property int totalTrades
                width: ListView.view.width; height: 47
                activeFocusOnTab: true; highlighted: ListView.isCurrentItem
                onClicked: {
                    archiveList.currentIndex = index
                    rawTickBrowser.close()
                }
                Keys.onReturnPressed: {
                    archiveList.currentIndex = index
                    rawTickBrowser.close()
                }
                Keys.onEnterPressed: {
                    archiveList.currentIndex = index
                    rawTickBrowser.close()
                }
                Accessible.name: "归档回测 " + symbol + " " + strategyId
                    + "，完成于 " + Qt.formatDateTime(completedAt, "yyyy-MM-dd HH:mm:ss")
                Accessible.description: "按回车选择，再使用右侧按钮从 SQLite 加载"
                background: Rectangle {
                    radius: 5
                    color: archiveDelegate.highlighted ? root.theme.bgSelected
                                                       : (archiveDelegate.hovered ? root.theme.bgRaised : "transparent")
                    border.color: archiveDelegate.activeFocus ? root.theme.cyan : "transparent"
                    border.width: archiveDelegate.activeFocus ? 2 : 1
                }
                contentItem: Item {
                    Text {
                        x: 8; y: 5; width: parent.width - 178
                        text: symbol + " " + name + " · " + strategyId
                              + (strategyVersion > 0 ? " v" + strategyVersion : "")
                        color: root.theme.textPrimary; elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.DemiBold
                    }
                    Text {
                        x: 8; y: 25; width: parent.width - 178
                        text: Qt.formatDateTime(completedAt, "yyyy-MM-dd HH:mm:ss")
                              + " · " + frequency + " · " + actualBars + " 根 · 任务 " + taskId
                        color: root.theme.textMuted; elide: Text.ElideRight
                        font.family: root.theme.fontFamily; font.pixelSize: 8
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 8; y: 8
                        text: (totalReturnPercent >= 0 ? "+" : "")
                              + totalReturnPercent.toFixed(2) + "% · " + totalTrades + " 笔"
                        color: totalReturnPercent >= 0 ? root.theme.green : root.theme.red
                        font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: root.controller.backtestArchiveCount === 0
            text: "尚无归档结果\n成功完成回测后会自动事务化保存"
            horizontalAlignment: Text.AlignHCenter
            color: root.theme.textMuted
            font.family: root.theme.fontFamily; font.pixelSize: 10
            Accessible.name: text
        }
    }

    Rectangle {
        anchors.right: parent.right; y: 32; width: 324; height: parent.height - 32
        radius: 7; color: root.theme.bgInput; border.color: root.theme.border
        Text {
            x: 12; y: 10; width: parent.width - 24
            text: root.selectedSummary.resultId
                  ? root.selectedSummary.symbol + " · " + root.selectedSummary.strategyId
                  : "选择一条归档查看身份"
            color: root.theme.textPrimary; elide: Text.ElideRight
            font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
        }
        Text {
            x: 12; y: 36; width: parent.width - 24; height: 112
            text: root.selectedSummary.resultId
                ? "请求/实际：" + root.selectedSummary.requestedBars + "/" + root.selectedSummary.actualBars
                  + " 根\n结果 ID：" + root.selectedSummary.resultId
                  + "\ndataVersion：" + root.selectedSummary.dataVersion
                  + "\ndataset：" + (root.selectedSummary.datasetDayCount > 0
                      ? Qt.formatDate(root.selectedSummary.datasetFirstDate, "yyyy-MM-dd")
                        + " ~ " + Qt.formatDate(root.selectedSummary.datasetLastDate, "yyyy-MM-dd")
                        + " · " + root.selectedSummary.datasetDayCount + " 交易日 · "
                        + root.selectedSummary.datasetRawTickCount + " 原始 Tick"
                        + (root.selectedSummary.datasetGapDayCount > 0
                           ? " · 缺口 " + root.selectedSummary.datasetGapDayCount : "")
                        + " · " + (root.selectedSummary.datasetCalendarCoverageKnown
                                   ? root.selectedSummary.datasetCalendarSource
                                   : "日历未覆盖")
                      : "无数据集身份")
                : "归档加载只读取 SQLite，不联网，也不会重新运行策略。"
            color: root.theme.textMuted; wrapMode: Text.WrapAnywhere
            font.family: root.theme.fontFamily; font.pixelSize: 8
            Accessible.name: text
        }
        Text {
            x: 12; y: 152; width: parent.width - 24; height: 34
            visible: root.controller.backtestResultHistoricalSnapshot
                     && root.controller.selectedBacktestArchiveId === root.selectedSummary.resultId
            text: "历史快照，与当前策略、参数或数据缓存可能不同"
            color: root.theme.yellow; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
            Accessible.name: text
        }
        PpButton {
            x: 12; y: parent.height - 42
            theme: root.theme; text: root.controller.backtestArchiveLoading ? "加载中" : "加载所选结果"
            visualWidth: 116; primary: true
            available: !!root.selectedSummary.resultId && !root.controller.backtestRunning
                       && !root.controller.backtestArchiveLoading
            toolTip: available ? "在后台从 SQLite 恢复结果与不可变身份；原始 Tick 输入保持按需分页"
                               : "请先选择归档，且等待当前回测结束"
            Accessible.name: "加载所选归档回测"; Accessible.description: toolTip
            onClicked: root.controller.loadBacktestArchive(root.selectedSummary.resultId)
        }
        PpButton {
            x: 138; y: parent.height - 42
            theme: root.theme; text: "刷新列表"; visualWidth: 88
            available: !root.controller.backtestArchiveLoading
            toolTip: "仅刷新 SQLite 归档目录，不访问网络"
            Accessible.name: "刷新归档回测列表"
            onClicked: root.controller.refreshBacktestArchiveList()
        }
        PpButton {
            x: 232; y: parent.height - 42
            theme: root.theme; text: "逐笔浏览"; visualWidth: 80
            available: root.rawTickArchiveReady
                       && !root.controller.backtestArchiveRawTickPageLoading
            toolTip: root.rawTickArchiveReady
                ? "按页读取已归档的原始 Tick；每页 256 条，不访问网络"
                : "请先加载包含完整原始 Tick 快照的归档"
            Accessible.name: "浏览归档原始 Tick"
            Accessible.description: toolTip
            onClicked: root.openRawTickBrowser()
        }
    }

    Popup {
        id: rawTickBrowser
        objectName: "backtestArchiveRawTickBrowser"
        parent: Overlay.overlay
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: root.rawTickBrowserResultId = ""
        width: Math.min(parent ? parent.width - 32 : 960, 960)
        height: Math.min(parent ? parent.height - 32 : 600, 600)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        padding: 0
        background: Rectangle {
            radius: 10
            color: root.theme.bgPanel
            border.color: root.theme.border
        }
        Item {
            anchors.fill: parent
            Text {
                x: 16; y: 12; width: parent.width - 150
                text: "归档原始 Tick · " + root.rawTickBrowserResultId
                color: root.theme.textPrimary; elide: Text.ElideMiddle
                font.family: root.theme.fontFamily; font.pixelSize: 13
                font.weight: Font.Bold
                Accessible.name: text
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 16; y: 14
                text: root.controller.backtestArchiveRawTickPageLoading
                      ? "读取中…"
                      : (root.controller.backtestArchiveRawTickTotalCount > 0
                         ? (root.controller.backtestArchiveRawTickPageOffset + 1)
                           + "–" + Math.min(
                               root.controller.backtestArchiveRawTickPageOffset
                               + root.controller.backtestArchiveRawTickModel.count,
                               root.controller.backtestArchiveRawTickTotalCount)
                           + " / " + root.controller.backtestArchiveRawTickTotalCount
                         : "尚未读取")
                color: root.controller.backtestArchiveRawTickPageLoading
                       ? root.theme.cyan : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
                Accessible.name: text
            }
            Rectangle {
                x: 12; y: 42; width: parent.width - 24; height: 26
                radius: 4; color: root.theme.bgRaised
                Text { x: 8; width: 62; anchors.verticalCenter: parent.verticalCenter; text: "序号"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 72; width: 184; anchors.verticalCenter: parent.verticalCenter; text: "成交时间"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 260; width: 92; anchors.verticalCenter: parent.verticalCenter; text: "价格"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 356; width: 100; anchors.verticalCenter: parent.verticalCenter; text: "成交量（股）"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 460; width: 116; anchors.verticalCenter: parent.verticalCenter; text: "成交额"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 580; width: 58; anchors.verticalCenter: parent.verticalCenter; text: "方向"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 642; width: 80; anchors.verticalCenter: parent.verticalCenter; text: "源行"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                Text { x: 726; width: parent.width - 734; anchors.verticalCenter: parent.verticalCenter; text: "来源"; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
            }
            Rectangle {
                x: 12; y: 72; width: parent.width - 24; height: parent.height - 126
                radius: 5; color: root.theme.bgInput; border.color: root.theme.border
                ListView {
                    id: rawTickList
                    anchors.fill: parent; anchors.margins: 4
                    model: root.controller.backtestArchiveRawTickModel
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: ItemDelegate {
                        required property int index
                        required property var timestamp
                        required property real eventIndex
                        required property real price
                        required property real volumeShares
                        required property real amount
                        required property string direction
                        required property string directionText
                        required property real sourceLine
                        required property string source
                        width: ListView.view.width; height: 27
                        activeFocusOnTab: true
                        Accessible.name: "第 " + (eventIndex + 1) + " 笔，"
                            + directionText + "，价格 " + price
                            + "，成交量 " + volumeShares + " 股"
                        background: Rectangle {
                            color: index % 2 === 0 ? "transparent" : root.theme.bgRaised
                            border.color: parent.activeFocus ? root.theme.cyan : "transparent"
                            border.width: parent.activeFocus ? 1 : 0
                        }
                        contentItem: Item {
                            Text { x: 8; width: 62; anchors.verticalCenter: parent.verticalCenter; text: eventIndex + 1; color: root.theme.textPrimary; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 72; width: 184; anchors.verticalCenter: parent.verticalCenter; text: Qt.formatDateTime(timestamp, "yyyy-MM-dd HH:mm:ss.zzz"); color: root.theme.textPrimary; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 260; width: 92; anchors.verticalCenter: parent.verticalCenter; text: price.toFixed(4); color: root.theme.textPrimary; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 356; width: 100; anchors.verticalCenter: parent.verticalCenter; text: volumeShares.toFixed(0); color: root.theme.textPrimary; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 460; width: 116; anchors.verticalCenter: parent.verticalCenter; text: amount.toFixed(2); color: root.theme.textPrimary; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 580; width: 58; anchors.verticalCenter: parent.verticalCenter; text: direction; color: direction === "B" ? root.theme.green : (direction === "S" ? root.theme.red : root.theme.yellow); font.pixelSize: 9; font.family: root.theme.fontFamily; font.weight: Font.Bold }
                            Text { x: 642; width: 80; anchors.verticalCenter: parent.verticalCenter; text: sourceLine; color: root.theme.textMuted; font.pixelSize: 9; font.family: root.theme.fontFamily }
                            Text { x: 726; width: parent.width - 734; anchors.verticalCenter: parent.verticalCenter; text: source; color: root.theme.textMuted; elide: Text.ElideRight; font.pixelSize: 9; font.family: root.theme.fontFamily }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: root.controller.backtestArchiveRawTickModel.count === 0
                             && !root.controller.backtestArchiveRawTickPageLoading
                    text: root.controller.backtestArchiveRawTickPageError.length > 0
                          ? root.controller.backtestArchiveRawTickPageError
                          : "该页没有可显示的原始 Tick"
                    color: root.controller.backtestArchiveRawTickPageError.length > 0
                           ? root.theme.red : root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    Accessible.name: text
                }
            }
            Text {
                x: 16; y: parent.height - 45; width: parent.width - 324
                text: root.controller.backtestArchiveRawTickPageError
                visible: text.length > 0
                color: root.theme.red; elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 9
                Accessible.name: text
            }
            PpButton {
                x: parent.width - 300; y: parent.height - 48
                theme: root.theme; text: "上一页"; visualWidth: 82
                available: root.controller.backtestArchiveRawTickHasPrevious
                           && !root.controller.backtestArchiveRawTickPageLoading
                toolTip: available ? "读取前一页 256 条" : "当前已是第一页或正在读取"
                Accessible.name: "归档原始 Tick 上一页"
                onClicked: root.controller.requestBacktestArchiveRawTickPage(
                    root.controller.backtestArchiveRawTickPageResultId,
                    Math.max(0, root.controller.backtestArchiveRawTickPageOffset
                             - root.controller.backtestArchiveRawTickPageSize),
                    root.controller.backtestArchiveRawTickPageSize)
            }
            PpButton {
                x: parent.width - 212; y: parent.height - 48
                theme: root.theme; text: "下一页"; visualWidth: 82
                available: root.controller.backtestArchiveRawTickHasNext
                           && !root.controller.backtestArchiveRawTickPageLoading
                toolTip: available ? "读取后一页 256 条" : "当前已是最后一页或正在读取"
                Accessible.name: "归档原始 Tick 下一页"
                onClicked: root.controller.requestBacktestArchiveRawTickPage(
                    root.controller.backtestArchiveRawTickPageResultId,
                    root.controller.backtestArchiveRawTickPageOffset
                    + root.controller.backtestArchiveRawTickPageSize,
                    root.controller.backtestArchiveRawTickPageSize)
            }
            PpButton {
                x: parent.width - 124; y: parent.height - 48
                theme: root.theme; text: "关闭"; visualWidth: 82
                toolTip: "关闭逐笔归档浏览器"
                Accessible.name: "关闭归档原始 Tick 浏览器"
                onClicked: rawTickBrowser.close()
            }
        }
    }

    Text {
        x: Math.max(420, parent.width - 340) + 16; y: 4
        width: 320; visible: root.controller.backtestArchiveError.length > 0
                            || root.controller.backtestArchiveSaveError.length > 0
        text: root.controller.backtestArchiveError.length > 0
              ? root.controller.backtestArchiveError : root.controller.backtestArchiveSaveError
        color: root.theme.red; elide: Text.ElideRight
        font.family: root.theme.fontFamily; font.pixelSize: 9
        Accessible.name: text
    }

    Connections {
        target: root.controller.backtestArchiveModel
        function onModelReset() { root.restoreSelection() }
        function onCountChanged() { root.restoreSelection() }
    }
    Connections {
        target: root.controller
        function onBacktestArchiveStateChanged() {
            root.restoreSelection()
            if (rawTickBrowser.opened
                    && root.controller.selectedBacktestArchiveId
                       !== root.rawTickBrowserResultId) {
                rawTickBrowser.close()
            }
        }
    }

    Component.onCompleted: root.restoreSelection()
}
