// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    property bool showWorkspaceActions: false
    property bool allowArbitraryPeriod: !showWorkspaceActions
    signal backtestWorkspaceRequested()

    function syncIndex() {
        const catalog = root.controller.localBacktestDataCatalog
        let match = -1
        for (let index = 0; index < catalog.length; ++index) {
            if (catalog[index].symbol === root.controller.selectedBacktestSymbol
                    && catalog[index].periodSeconds
                       === root.controller.selectedBacktestPeriodSeconds) {
                match = index
                break
            }
        }
        datasetCombo.currentIndex = match
    }

    Component.onCompleted: syncIndex()

    ColumnLayout {
        anchors.fill: parent
        spacing: 9

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "回测品种 · 历史 Tick"
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.Bold
            }
            PpButton {
                theme: root.theme
                text: "刷新"
                visualWidth: 58; visualHeight: 26
                toolTip: "重新读取 SQLite 本地历史清单，不访问网络"
                onClicked: root.controller.refreshLocalBacktestDataCatalog()
            }
        }

        ComboBox {
            id: datasetCombo
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            model: root.controller.localBacktestDataCatalog
            textRole: "title"
            valueRole: "symbol"
            enabled: count > 0
            // 同 PpButton：不要在已持有 active focus 时把该属性翻成 false。
            activeFocusOnTab: enabled || datasetCombo.activeFocus
            Accessible.name: "选择本地回测品种和周期"
            Accessible.description: enabled
                ? "仅列出 SQLite 中由历史 Tick 聚合的数据集"
                : "本地历史数据库没有 Tick 派生数据"
            onActivated: function(index) {
                const entry = root.controller.localBacktestDataCatalog[index]
                root.controller.selectLocalBacktestInstrument(
                    entry.symbol, entry.frequency, 2)
                root.syncIndex()
            }
            contentItem: Text {
                leftPadding: 10; rightPadding: 26
                verticalAlignment: Text.AlignVCenter
                text: datasetCombo.currentIndex >= 0
                      ? root.controller.localBacktestDataCatalog[datasetCombo.currentIndex].title
                        + " · "
                        + root.controller.localBacktestDataCatalog[datasetCombo.currentIndex].frequency
                      : "没有历史 Tick 派生数据集"
                color: datasetCombo.enabled ? root.theme.textPrimary : root.theme.textMuted
                elide: Text.ElideRight
                font.family: root.theme.fontFamily; font.pixelSize: 10
            }
            background: Rectangle {
                readonly property bool keyboardFocus: datasetCombo.activeFocus
                    && datasetCombo.focusReason !== Qt.MouseFocusReason
                    && datasetCombo.focusReason !== Qt.OtherFocusReason
                radius: 6; color: root.theme.bgInput
                border.width: keyboardFocus ? 2 : 1
                border.color: keyboardFocus ? root.theme.cyan : root.theme.border
            }
            delegate: ItemDelegate {
                required property int index
                required property var modelData
                width: datasetCombo.width
                highlighted: datasetCombo.highlightedIndex === index
                text: modelData.title + " · " + modelData.frequency
                      + " · " + modelData.candleCount + " 根"
                      + (modelData.datasetVersion
                         ? " · " + modelData.datasetVersion.slice(7, 17)
                           + " · " + modelData.datasetDayCount + " 日"
                         : "")
                font.family: root.theme.fontFamily; font.pixelSize: 10
                Accessible.name: text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.allowArbitraryPeriod
            spacing: 8
            Text {
                text: "任意周期（秒）"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            TextField {
                id: arbitraryPeriodField
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                text: root.controller.selectedBacktestPeriodSeconds > 0
                      ? String(root.controller.selectedBacktestPeriodSeconds) : "60"
                validator: IntValidator { bottom: 1; top: 86400 }
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                Accessible.name: "历史 Tick 回测任意 K 线周期秒数"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.color: arbitraryPeriodField.activeFocus
                                  ? root.theme.cyan : root.theme.border
                }
            }
            PpButton {
                theme: root.theme
                text: "生成并选择"
                visualWidth: 96; visualHeight: 30
                primary: true
                available: root.controller.selectedBacktestSymbol.length > 0
                           && arbitraryPeriodField.acceptableInput
                           && !root.controller.localCandleSeriesLoading
                toolTip: available
                    ? "只从 SQLite 原始 Tick 异步生成该周期缓存并选中，不访问网络"
                    : "请先选择本地 Tick 品种并输入 1–86400 秒"
                onClicked: root.controller.prepareLocalBacktestPeriod(
                    root.controller.selectedBacktestSymbol,
                    Number(arbitraryPeriodField.text))
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.controller.selectedBacktestDataAvailable
                  ? root.controller.selectedBacktestInstrumentTitle
                  : "未选择可用的历史 Tick 数据"
            color: root.controller.selectedBacktestDataAvailable
                   ? root.theme.cyan : root.theme.textMuted
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 10; rowSpacing: 6
            Text { text: "周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: root.controller.selectedBacktestFrequency || "--"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text { text: "K 线"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text {
                text: root.controller.selectedBacktestDataAvailable
                      ? root.controller.selectedBacktestCandleCount + " 根" : "--"
                color: root.controller.selectedBacktestCandleCount >= root.controller.backtestBarCount
                       ? root.theme.textPrimary : root.theme.yellow
                font.family: root.theme.fontFamily; font.pixelSize: 10
            }
            Text { text: "首条"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: root.controller.selectedBacktestFirstTime.toString().length > 0 ? Qt.formatDateTime(root.controller.selectedBacktestFirstTime, "yyyy-MM-dd HH:mm") : "--"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text { text: "末条"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: root.controller.selectedBacktestLastTime.toString().length > 0 ? Qt.formatDateTime(root.controller.selectedBacktestLastTime, "yyyy-MM-dd HH:mm") : "--"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
            Text { text: "数据集"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text {
                text: {
                    const entry = datasetCombo.currentIndex >= 0
                        ? root.controller.localBacktestDataCatalog[datasetCombo.currentIndex] : null
                    return entry && entry.datasetVersion
                        ? entry.datasetVersion.slice(7, 17)
                          + " · " + entry.datasetDayCount + " 交易日"
                          + (entry.datasetGapDayCount > 0
                             ? " · 缺口 " + entry.datasetGapDayCount : "")
                        : "--"
                }
                color: root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                Accessible.name: "数据集身份摘要"
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.controller.backtestInstrumentSelectionError.length > 0
                     || (root.controller.selectedBacktestDataAvailable
                         && root.controller.selectedBacktestCandleCount
                            < root.controller.backtestBarCount)
            text: root.controller.backtestInstrumentSelectionError.length > 0
                  ? root.controller.backtestInstrumentSelectionError
                  : "本地仅 " + root.controller.selectedBacktestCandleCount
                    + " 根，当前回测配置需要 " + root.controller.backtestBarCount + " 根"
            wrapMode: Text.Wrap
            color: root.theme.yellow
            font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.showWorkspaceActions
            spacing: 8
            PpButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                theme: root.theme
                text: "打开回测任务"
                primary: true
                available: root.controller.selectedBacktestDataAvailable
                toolTip: available ? "进入任务化回测、结果和归档工作区"
                                   : "请先选择本地历史 Tick 数据集"
                onClicked: root.backtestWorkspaceRequested()
            }
            PpButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                theme: root.theme
                text: "回测多窗口"
                available: false
                toolTip: "本阶段已完成实时多窗口；回测多窗口需绑定严格本地 Tick 与归档身份，尚未接入"
            }
        }
        Item { Layout.fillHeight: true }
    }

    Connections {
        target: root.controller
        function onBacktestInstrumentChanged() {
            root.syncIndex()
            if (!arbitraryPeriodField.activeFocus
                    && root.controller.selectedBacktestPeriodSeconds > 0) {
                arbitraryPeriodField.text = String(
                    root.controller.selectedBacktestPeriodSeconds)
            }
        }
        function onLocalBacktestDataCatalogChanged() { root.syncIndex() }
    }
}
