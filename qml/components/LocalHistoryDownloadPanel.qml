// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    function localEntry() {
        const period = frequencyBox.currentValue === "day" ? 86400
                     : frequencyBox.currentValue === "60m" ? 3600
                     : frequencyBox.currentValue === "30m" ? 1800
                     : frequencyBox.currentValue === "15m" ? 900
                     : frequencyBox.currentValue === "5m" ? 300 : 60
        const catalog = controller.localHistoryDataCatalog
        for (let index = 0; index < catalog.length; ++index) {
            if (catalog[index].symbol === controller.historyDownloadSymbol
                    && catalog[index].periodSeconds === period)
                return catalog[index]
        }
        return ({ candleCount: 0, firstTime: undefined, lastTime: undefined })
    }

    function startDownload() {
        controller.downloadAndStoreHistoryAdjusted(
            frequencyBox.currentValue, Number(targetBarsField.text),
            adjustmentBox.currentValue)
    }

    function providerSummary() {
        const capabilities = controller.historicalProviderCapabilities
        const labels = []
        for (let index = 0; index < capabilities.length; ++index) {
            const item = capabilities[index]
            labels.push(item.displayName + (item.configured ? " 已就绪" : " 未配置"))
        }
        return labels.length > 0 ? labels.join(" · ") : "尚无可用 Provider 声明"
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        Rectangle {
            Layout.preferredWidth: Math.max(330, parent.width * 0.38)
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            Text { x: 12; y: 11; text: "1. 选择下载品种（A 股）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
            TextField {
                id: searchField
                x: 12; y: 38; width: parent.width - 24; height: 36
                text: root.controller.stockSearchText
                placeholderText: "输入代码或名称；选择只影响下载目标"
                color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                activeFocusOnTab: true
                Accessible.name: "搜索历史数据下载品种"
                Accessible.description: "搜索本地 A 股股票池，不会开始网络请求"
                onTextEdited: root.controller.stockSearchText = text
                onAccepted: {
                    if (root.controller.searchModel.resultCount > 0) {
                        const first = root.controller.searchModel.get(0)
                        root.controller.selectHistoryDownloadInstrument(first.code)
                    }
                }
                background: Rectangle {
                    readonly property bool keyboardFocus: searchField.activeFocus
                        && searchField.focusReason !== Qt.MouseFocusReason
                        && searchField.focusReason !== Qt.OtherFocusReason
                    radius: 6; color: root.theme.bgCanvas
                    border.width: keyboardFocus ? 2 : 1
                    border.color: keyboardFocus ? root.theme.cyan : root.theme.border
                }
            }
            Text { x: 12; y: 82; width: parent.width - 24; text: "下载目标：" + (root.controller.historyDownloadInstrumentTitle || "未选择"); color: root.controller.historyDownloadSymbol.length > 0 ? root.theme.cyan : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 10; elide: Text.ElideRight }
            ListView {
                x: 12; y: 108; width: parent.width - 24; height: parent.height - 120
                model: root.controller.searchModel
                clip: true; spacing: 3; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Control {
                    id: resultRow
                    required property string code
                    required property string name
                    required property string market
                    width: ListView.view.width; height: 32
                    activeFocusOnTab: true; focusPolicy: Qt.TabFocus
                    Accessible.role: Accessible.ListItem
                    Accessible.name: "选择下载 " + code + " " + name
                    Accessible.description: "只更新历史数据下载目标，不改变研究、实时或本地回测选择"
                    function activate() { root.controller.selectHistoryDownloadInstrument(code) }
                    Keys.onReturnPressed: function(event) { activate(); event.accepted = true }
                    Keys.onSpacePressed: function(event) { activate(); event.accepted = true }
                    MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; onPressed: resultRow.forceActiveFocus(Qt.MouseFocusReason); onClicked: resultRow.activate() }
                    background: Rectangle { radius: 5; color: rowMouse.containsMouse || resultRow.activeFocus ? root.theme.bgSelected : root.theme.bgRaised; border.width: resultRow.visualFocus ? 2 : 1; border.color: resultRow.visualFocus ? root.theme.cyan : root.theme.border }
                    contentItem: Row {
                        x: 8; width: parent.width - 16; spacing: 8
                        Text { width: 62; text: resultRow.code; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9; anchors.verticalCenter: parent.verticalCenter }
                        Text { width: Math.max(80, resultRow.width - 160); text: resultRow.name; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: resultRow.market; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: Math.max(270, parent.width * 0.25)
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            Column {
                x: 12; y: 11; width: parent.width - 24; spacing: 9
                Text { text: "2. 设置周期与目标根数"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                Row {
                    width: parent.width; height: 36; spacing: 6
                    ComboBox {
                        id: frequencyBox; width: (parent.width - 6) * 0.56; height: 36
                        model: [{ label: "日线", value: "day" }, { label: "60 分钟", value: "60m" }, { label: "30 分钟", value: "30m" }, { label: "15 分钟", value: "15m" }, { label: "5 分钟", value: "5m" }, { label: "1 分钟", value: "1m" }]
                        textRole: "label"; valueRole: "value"; currentIndex: 0
                        Accessible.name: "下载 K 线周期"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: frequencyBox.activeFocus ? 2 : 1; border.color: frequencyBox.activeFocus ? root.theme.cyan : root.theme.border }
                        contentItem: Text { leftPadding: 10; text: frequencyBox.displayText; verticalAlignment: Text.AlignVCenter; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                    }
                    ComboBox {
                        id: adjustmentBox; width: (parent.width - 6) * 0.44; height: 36
                        model: [{ label: "不复权", value: "none" },
                                { label: "前复权", value: "qfq" },
                                { label: "后复权", value: "hfq" }]
                        textRole: "label"; valueRole: "value"; currentIndex: 0
                        Accessible.name: "历史日线复权方式"
                        Accessible.description: "复权数据只路由到明确声明支持的 Provider"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: adjustmentBox.activeFocus ? 2 : 1; border.color: adjustmentBox.activeFocus ? root.theme.cyan : root.theme.border }
                        contentItem: Text { leftPadding: 10; text: adjustmentBox.displayText; verticalAlignment: Text.AlignVCenter; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                }
                TextField {
                    id: targetBarsField; width: parent.width; height: 36
                    text: "500"; validator: IntValidator { bottom: 2; top: 5000 }
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                    placeholderText: "目标根数 2–5000"
                    Accessible.name: "目标 K 线根数"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: targetBarsField.activeFocus ? 2 : 1; border.color: targetBarsField.activeFocus ? root.theme.cyan : root.theme.border }
                }
                readonly property var cached: root.localEntry()
                Text { width: parent.width; text: "当前缓存：" + parent.cached.candleCount + " 根"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                Text { width: parent.width; text: "数据缺口：" + Math.max(0, Number(targetBarsField.text) - parent.cached.candleCount) + " 根"; color: Number(targetBarsField.text) > parent.cached.candleCount ? root.theme.yellow : root.theme.green; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                Text { width: parent.width; text: parent.cached.candleCount > 0 ? Qt.formatDateTime(parent.cached.firstTime, "yyyy-MM-dd HH:mm") + " → " + Qt.formatDateTime(parent.cached.lastTime, "yyyy-MM-dd HH:mm") : "可用范围：--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                PpButton {
                    theme: root.theme; text: root.controller.historyDownloadRunning ? "正在获取…" : "下载并保存本地数据"
                    visualWidth: 176; primary: true
                    available: !root.controller.historyDownloadRunning
                               && root.controller.historyDownloadSymbol.length > 0
                               && targetBarsField.acceptableInput
                    toolTip: available ? "显式检查缓存；不足时访问现有历史 Provider 并写入 SQLite"
                                       : "请选择 A 股并输入有效根数，或等待当前请求结束"
                    onClicked: root.startDownload()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.minimumWidth: 300
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border
            Column {
                x: 12; y: 11; width: parent.width - 24; spacing: 9
                Text { text: "3. 独立请求状态"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                Text { width: parent.width; text: root.controller.historyDownloadRunning ? "请求进行中 · 版本 " + root.controller.historyDownloadRequestVersion : (root.controller.historyDownloadStatusText || "尚未发起显式请求"); color: root.controller.historyDownloadRunning ? root.theme.yellow : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; wrapMode: Text.Wrap; Accessible.name: text }
                Rectangle { width: parent.width; height: 1; color: root.theme.border }
                Text { width: parent.width; text: "参数：" + (root.controller.historyDownloadSymbol || "--") + " · " + root.controller.historyDownloadFrequency + " · " + root.controller.historyDownloadAdjustment + " · " + root.controller.historyDownloadTargetBars + " 根"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                Text { width: parent.width; text: "实际来源：" + (root.controller.historyDownloadSource || "--"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { width: parent.width; text: "路由：" + (root.controller.historyDownloadProvider || "--") + (root.controller.historyDownloadFallbackUsed ? " · 已回退" : ""); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                Text { width: parent.width; text: root.providerSummary(); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideRight; Accessible.name: "历史数据源能力：" + text }
                Text { width: parent.width; text: "本次入库：" + root.controller.historyDownloadStoredCount + " 根 · 当前可用：" + root.controller.historyDownloadAvailableCount + " 根"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                Text { width: parent.width; text: "dataVersion：" + (root.controller.historyDownloadDataVersion || "--"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideMiddle; Accessible.name: text }
                Text { width: parent.width; text: root.controller.historyDownloadAvailableCount > 0 ? "范围：" + Qt.formatDateTime(root.controller.historyDownloadFirstTime, "yyyy-MM-dd HH:mm") + " → " + Qt.formatDateTime(root.controller.historyDownloadLastTime, "yyyy-MM-dd HH:mm") : "范围：--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                Text { width: parent.width; visible: root.controller.historyDownloadError.length > 0; text: root.controller.historyDownloadError; color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text; Accessible.description: "历史数据显式下载错误；已有缓存不会被删除" }
                Text { width: parent.width; text: "刷新目录、切换本地回测品种和打开页面均不会访问网络。"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text }
            }
        }
    }
}
