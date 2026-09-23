// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 自定义策略运行时 + 第三方 C++ 插件 + 模拟回放/审批/通知。
// 所有行为只通过 TradingController；页面不扫描目录、不读 SQLite、不触发网络行情。
Item {
    id: root
    required property var theme
    required property var controller

    readonly property bool compactLayout: width < 980
    readonly property var runtimeCatalog: controller.customStrategyExecutionCatalog
    readonly property var loadedPlugins: controller.loadedStrategyPlugins
    property var scanResults: []
    property string actionMessage: ""
    property string actionError: ""
    property bool desktopEnabled: false
    property bool webhookEnabled: false
    property string webhookUrl: ""
    property bool notifySignal: true
    property bool notifyApproval: true
    property bool notifyOrderSubmitted: true
    property bool notifyExecutionRejected: true
    property bool notifyIntentExpired: true

    signal requestRefresh()

    function sequence(value) {
        if (!value || value.length === undefined) return []
        const result = []
        for (let i = 0; i < value.length; ++i) result.push(value[i])
        return result
    }

    function activeRuntime() {
        const rows = sequence(root.runtimeCatalog)
        for (let i = 0; i < rows.length; ++i) {
            if (Number(rows[i].state) === 2) return rows[i]
        }
        return null
    }

    function loadedPluginAt(index) {
        const rows = sequence(root.loadedPlugins)
        return index >= 0 && index < rows.length ? rows[index] : null
    }

    function isLoadedFile(fileName) {
        const rows = sequence(root.loadedPlugins)
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].fileName === fileName) return true
        }
        return false
    }

    function refreshScan() {
        root.actionError = ""
        root.scanResults = root.controller.strategyPluginScan()
        root.actionMessage = "插件目录扫描完成：" + root.scanResults.length + " 个候选"
    }

    function reloadPluginParameters() {
        pluginParameterModel.clear()
        const plugin = root.loadedPluginAt(loadedPluginCombo.currentIndex)
        if (!plugin) return
        const schemas = sequence(root.controller.strategyPluginParameterSchema(plugin.pluginId))
        for (let i = 0; i < schemas.length; ++i) {
            const schema = schemas[i]
            pluginParameterModel.append({
                parameterKey: String(schema.key || ""),
                parameterLabel: String(schema.displayName || schema.key || "参数"),
                parameterType: String(schema.type || "string"),
                parameterValue: String(schema.defaultValue === undefined ? "" : schema.defaultValue),
                minimumValue: schema.minimumValue,
                maximumValue: schema.maximumValue
            })
        }
    }

    function activateLoadedPlugin() {
        root.actionError = ""
        const plugin = root.loadedPluginAt(loadedPluginCombo.currentIndex)
        if (!plugin) {
            root.actionError = "请先加载并选择第三方插件"
            return false
        }
        const strategyId = pluginStrategyId.text.trim()
        const symbol = pluginSymbol.text.trim()
        if (strategyId.length === 0 || !/^\d{6}$/.test(symbol)) {
            root.actionError = "策略 ID 不能为空，A 股代码必须为 6 位数字"
            return false
        }
        const config = { strategyId: strategyId, symbol: symbol, name: symbol }
        for (let row = 0; row < pluginParameterModel.count; ++row) {
            const item = pluginParameterModel.get(row)
            let value = item.parameterValue
            if (item.parameterType === "integer") value = Math.round(Number(value))
            else if (item.parameterType === "real") value = Number(value)
            else if (item.parameterType === "boolean") value = value === "true"
            config[item.parameterKey] = value
        }
        const ok = root.controller.activateStrategyPluginStrategy(plugin.pluginId, config)
        root.actionMessage = ok ? "第三方插件策略已激活：" + strategyId : ""
        root.actionError = ok ? "" : "第三方插件策略激活失败，请查看底部状态"
        return ok
    }

    function loadNotificationSettings() {
        const settings = root.controller.notificationSettings()
        if (!settings) return
        root.desktopEnabled = settings.desktopEnabled === true
        root.webhookEnabled = settings.webhookEnabled === true
        root.webhookUrl = settings.webhookUrl === undefined ? "" : settings.webhookUrl
        root.notifySignal = settings.notifySignal !== false
        root.notifyApproval = settings.notifyApproval !== false
        root.notifyOrderSubmitted = settings.notifyOrderSubmitted !== false
        root.notifyExecutionRejected = settings.notifyExecutionRejected !== false
        root.notifyIntentExpired = settings.notifyIntentExpired !== false
    }

    Component.onCompleted: {
        loadNotificationSettings()
        reloadPluginParameters()
    }

    Connections {
        target: root.controller
        function onStrategyPluginStateChanged() {
            root.reloadPluginParameters()
        }
        function onNotificationSettingsChanged() {
            root.loadNotificationSettings()
        }
    }

    component FormField: TextField {
        implicitHeight: 30
        color: root.theme.textPrimary
        selectionColor: root.theme.cyan
        selectedTextColor: root.theme.bgCanvas
        placeholderTextColor: root.theme.textMuted
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        leftPadding: 8
        selectByMouse: true
        background: Rectangle {
            radius: 5; color: root.theme.bgInput
            border.width: parent.activeFocus ? 2 : 1
            border.color: parent.activeFocus ? root.theme.cyan : root.theme.border
        }
    }

    component SectionLabel: Text {
        color: root.theme.textPrimary
        font.family: root.theme.fontFamily
        font.pixelSize: 13
        font.weight: Font.Bold
    }

    component DarkComboBox: ComboBox {
        id: darkCombo
        implicitHeight: 30
        font.family: root.theme.fontFamily
        font.pixelSize: 10
        background: Rectangle {
            radius: 5; color: root.theme.bgInput
            border.width: darkCombo.activeFocus ? 2 : 1
            border.color: darkCombo.activeFocus ? root.theme.cyan : root.theme.border
        }
        contentItem: Text {
            leftPadding: 8; rightPadding: 24
            text: darkCombo.displayText || "--"
            color: root.theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font: darkCombo.font
        }
    }

    component DarkCheckBox: CheckBox {
        id: darkCheck
        font.family: root.theme.fontFamily
        font.pixelSize: 9
        contentItem: Text {
            leftPadding: darkCheck.indicator.width + darkCheck.spacing
            text: darkCheck.text
            color: darkCheck.enabled ? root.theme.textPrimary : root.theme.textMuted
            verticalAlignment: Text.AlignVCenter
            font: darkCheck.font
        }
    }

    ListModel { id: pluginParameterModel }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 42
            SectionLabel { text: "自定义策略 · 运行时插件管理"; Layout.fillWidth: true; font.pixelSize: 17 }
            Rectangle {
                Layout.preferredWidth: 130; Layout.preferredHeight: 24; radius: 6
                color: root.controller.pendingStrategyIntentCount > 0 ? "#2B2414" : root.theme.bgRaised
                border.color: root.controller.pendingStrategyIntentCount > 0 ? root.theme.yellow : root.theme.border
                Text {
                    anchors.centerIn: parent
                    text: "审批队列 " + root.controller.pendingStrategyIntentCount + " 待处理"
                    color: root.controller.pendingStrategyIntentCount > 0 ? root.theme.yellow : root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
            }
            PpButton {
                theme: root.theme; text: "刷新状态"; visualWidth: 86
                toolTip: "刷新插件扫描和运行时状态，不访问网络"
                onClicked: root.refreshScan()
            }
        }

        Text {
            Layout.fillWidth: true
            text: "加载 / 激活 / 停用 / 卸载仅作用于本机模拟执行；第三方 DLL 必须通过 manifest、SHA-256 与 ABI 校验。"
            color: root.theme.textMuted; wrapMode: Text.Wrap
            font.family: root.theme.fontFamily; font.pixelSize: 10
        }

        Text {
            Layout.fillWidth: true; visible: root.actionMessage.length > 0 || root.actionError.length > 0
            text: root.actionError.length > 0 ? root.actionError : root.actionMessage
            color: root.actionError.length > 0 ? root.theme.red : root.theme.cyan
            wrapMode: Text.Wrap; font.family: root.theme.fontFamily; font.pixelSize: 9
            Accessible.name: text
        }

        Item {
            id: pluginBody
            objectName: "strategyPluginGrid"
            Layout.fillWidth: true; Layout.fillHeight: true

            Rectangle {
                objectName: "strategyPluginCatalogCard"
                x: 0; y: 0
                width: root.compactLayout ? parent.width : Math.min(430, parent.width)
                height: root.compactLayout ? 720 : parent.height
                radius: 10; color: root.theme.bgPanel; border.color: root.theme.border

                ScrollView {
                    anchors.fill: parent; anchors.margins: 14; clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.availableWidth; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: "声明式策略运行时"; Layout.fillWidth: true }
                            Text {
                                text: root.runtimeCatalog.length + " 项"
                                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "仅 executionScope=mock_execution 可加载；backtest_only 会明确禁用。"
                            color: root.theme.textMuted; wrapMode: Text.Wrap
                            font.family: root.theme.fontFamily; font.pixelSize: 9
                        }
                        Repeater {
                            model: root.runtimeCatalog
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true; Layout.preferredHeight: 112
                                radius: 7; color: root.theme.bgRaised; border.color: root.theme.border
                                Text {
                                    x: 10; y: 8; width: parent.width - 120
                                    text: modelData.name || modelData.strategyId
                                    color: root.theme.textPrimary; elide: Text.ElideRight
                                    font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold
                                }
                                Rectangle {
                                    anchors.right: parent.right; anchors.rightMargin: 10; y: 7
                                    width: 88; height: 20; radius: 5
                                    color: Number(modelData.state) === 2 ? root.theme.bgSuccess : root.theme.bgSelected
                                    border.color: Number(modelData.state) === 2 ? root.theme.green : root.theme.cyan
                                    Text {
                                        anchors.centerIn: parent; text: modelData.stateName
                                        color: Number(modelData.state) === 2 ? root.theme.green : root.theme.cyan
                                        font.family: root.theme.fontFamily; font.pixelSize: 8
                                    }
                                }
                                Text {
                                    x: 10; y: 31; width: parent.width - 20
                                    text: modelData.strategyId + " · v" + modelData.version
                                          + " · " + modelData.executionScope
                                    color: root.theme.textMuted; elide: Text.ElideRight
                                    font.family: root.theme.fontFamily; font.pixelSize: 8
                                }
                                FormField {
                                    id: runtimeSymbol
                                    x: 10; y: 55; width: 130
                                    text: modelData.symbol || ""
                                    placeholderText: "A 股 6 位代码"
                                    readOnly: Number(modelData.state) !== 0
                                    Accessible.name: "运行时策略标的代码"
                                }
                                PpButton {
                                    x: 148; y: 52; visualWidth: 98; visualHeight: 26
                                    theme: root.theme
                                    text: Number(modelData.state) === 0 ? "加载并激活" : "卸载"
                                    danger: Number(modelData.state) !== 0
                                    available: Number(modelData.state) !== 0
                                               || (modelData.runtimeEligible === true
                                                   && /^\d{6}$/.test(runtimeSymbol.text.trim()))
                                    toolTip: modelData.runtimeEligible === true
                                             ? (Number(modelData.state) === 0
                                                ? "加载并进入本机模拟执行" : "停用并卸载运行时实例")
                                             : modelData.unavailableReason
                                    onClicked: {
                                        root.actionError = ""
                                        const ok = Number(modelData.state) === 0
                                            ? root.controller.enableCustomStrategyExecution(
                                                modelData.strategyId, Number(modelData.version),
                                                runtimeSymbol.text.trim())
                                            : root.controller.disableCustomStrategyExecution(modelData.strategyId)
                                        root.actionMessage = ok ? "运行时状态已更新：" + modelData.strategyId : ""
                                        root.actionError = ok ? "" : "运行时操作失败，请查看底部状态"
                                    }
                                }
                                Text {
                                    x: 10; y: 88; width: parent.width - 20
                                    text: modelData.error || modelData.unavailableReason || "模拟执行配置由执行中心继续约束"
                                    color: modelData.error ? root.theme.red : root.theme.textMuted
                                    elide: Text.ElideRight; font.family: root.theme.fontFamily; font.pixelSize: 8
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true; visible: root.runtimeCatalog.length === 0
                            text: "尚无已保存的自定义策略版本。"
                            color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
                        }

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: "第三方 C++ 插件"; Layout.fillWidth: true }
                            PpButton { theme: root.theme; text: "扫描目录"; visualWidth: 78; onClicked: root.refreshScan() }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.strategyPluginDirectory
                            color: root.theme.textMuted; elide: Text.ElideMiddle
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                            Accessible.name: "第三方插件目录：" + text
                        }
                        Repeater {
                            model: root.scanResults
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true; Layout.preferredHeight: 52
                                radius: 6; color: root.theme.bgInput; border.color: root.theme.border
                                Text {
                                    x: 8; y: 7; width: parent.width - 110
                                    text: modelData.fileName
                                    color: root.theme.textPrimary; elide: Text.ElideMiddle
                                    font.family: root.theme.fontFamily; font.pixelSize: 9
                                }
                                Text {
                                    x: 8; y: 27; width: parent.width - 110
                                    text: modelData.manifestOk ? "manifest / SHA-256 通过" : modelData.error
                                    color: modelData.manifestOk ? root.theme.green : root.theme.red
                                    elide: Text.ElideRight; font.family: root.theme.fontFamily; font.pixelSize: 8
                                }
                                PpButton {
                                    anchors.right: parent.right; anchors.rightMargin: 7; y: 8
                                    theme: root.theme; text: root.isLoadedFile(modelData.fileName) ? "已加载" : "加载"
                                    visualWidth: 74; visualHeight: 26
                                    available: modelData.manifestOk === true && !root.isLoadedFile(modelData.fileName)
                                    toolTip: modelData.manifestOk ? "通过校验后加载 DLL，不自动创建策略" : "manifest 或 SHA-256 校验未通过"
                                    onClicked: {
                                        const ok = root.controller.loadStrategyPlugin(modelData.fileName)
                                        root.actionMessage = ok ? "第三方插件已加载：" + modelData.fileName : ""
                                        root.actionError = ok ? "" : "第三方插件加载失败"
                                    }
                                }
                            }
                        }

                        Text { text: "已加载插件与动态参数"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                        DarkComboBox {
                            id: loadedPluginCombo
                            objectName: "loadedPluginCombo"
                            Layout.fillWidth: true; implicitHeight: 30
                            model: root.loadedPlugins; textRole: "pluginName"
                            onActivated: root.reloadPluginParameters()
                            Accessible.name: "选择已加载第三方插件"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FormField { id: pluginStrategyId; objectName: "pluginStrategyId"; Layout.fillWidth: true; placeholderText: "新策略 ID"; Accessible.name: "第三方插件策略 ID" }
                            FormField { id: pluginSymbol; objectName: "pluginSymbol"; Layout.preferredWidth: 120; placeholderText: "A 股代码"; Accessible.name: "第三方插件标的" }
                        }
                        Repeater {
                            model: pluginParameterModel
                            delegate: RowLayout {
                                required property int index
                                required property string parameterKey
                                required property string parameterLabel
                                required property string parameterType
                                required property string parameterValue
                                Layout.fillWidth: true
                                Text {
                                    Layout.preferredWidth: 120; text: parameterLabel
                                    color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
                                }
                                FormField {
                                    Layout.fillWidth: true; text: parameterValue
                                    Accessible.name: "插件参数 " + parameterLabel
                                    onEditingFinished: pluginParameterModel.setProperty(index, "parameterValue", text)
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            PpButton {
                                theme: root.theme; text: "激活策略"; primary: true; visualWidth: 88
                                available: root.loadedPlugins.length > 0
                                toolTip: available ? "按动态参数创建本机模拟策略实例" : "请先扫描并加载通过校验的插件"
                                onClicked: root.activateLoadedPlugin()
                            }
                            PpButton {
                                theme: root.theme; text: "卸载插件"; danger: true; visualWidth: 88
                                available: root.loadedPlugins.length > 0
                                toolTip: available ? "先移除该 DLL 创建的策略，再卸载插件" : "没有已加载插件"
                                onClicked: {
                                    const plugin = root.loadedPluginAt(loadedPluginCombo.currentIndex)
                                    if (!plugin) return
                                    const ok = root.controller.unloadStrategyPlugin(plugin.pluginId)
                                    root.actionMessage = ok ? "第三方插件已卸载：" + plugin.pluginId : ""
                                    root.actionError = ok ? "" : "第三方插件卸载失败"
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                x: root.compactLayout ? 0 : 442
                y: root.compactLayout ? 732 : 0
                width: root.compactLayout ? parent.width : Math.max(0, parent.width - x)
                height: root.compactLayout ? 720 : parent.height
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 132
                    radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: "模拟回放"; Layout.fillWidth: true }
                            Text {
                                text: root.controller.customStrategyReplayRunning
                                      ? "回放中 " + root.controller.customStrategyReplayedBars + "/" + root.controller.customStrategyReplayTotalBars
                                      : "未运行"
                                color: root.controller.customStrategyReplayRunning ? root.theme.green : root.theme.textMuted
                                font.family: root.theme.fontFamily; font.pixelSize: 9
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            FormField { id: replayStrategyId; Layout.fillWidth: true; placeholderText: "已激活策略 ID"; Accessible.name: "回放策略 ID" }
                            FormField { id: replaySymbol; Layout.preferredWidth: 100; placeholderText: "A 股代码"; Accessible.name: "回放标的" }
                            FormField { id: replayPeriod; Layout.preferredWidth: 64; text: "60"; Accessible.name: "回放周期秒" }
                            FormField { id: replayBars; Layout.preferredWidth: 64; text: "240"; Accessible.name: "回放 K 线根数" }
                            FormField { id: replayInterval; Layout.preferredWidth: 72; text: "1000"; Accessible.name: "回放间隔毫秒" }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ProgressBar {
                                id: replayProgress
                                Layout.fillWidth: true; from: 0
                                to: Math.max(1, root.controller.customStrategyReplayTotalBars)
                                value: root.controller.customStrategyReplayedBars
                                Accessible.name: "模拟回放进度"
                                background: Rectangle {
                                    implicitHeight: 8; radius: 4
                                    color: root.theme.track
                                    border.color: root.theme.border
                                }
                                contentItem: Item {
                                    implicitHeight: 8
                                    Rectangle {
                                        width: replayProgress.visualPosition * parent.width
                                        height: parent.height; radius: 4
                                        color: root.theme.cyan
                                    }
                                }
                            }
                            PpButton {
                                theme: root.theme; text: "开始回放"; primary: true; visualWidth: 88
                                available: !root.controller.customStrategyReplayRunning
                                           && replayStrategyId.text.trim().length > 0
                                           && /^\d{6}$/.test(replaySymbol.text.trim())
                                toolTip: available ? "只读取本地 raw_tick_cache_v1 逐 Bar 回放" : "请输入已激活策略 ID 和 6 位 A 股代码"
                                onClicked: root.controller.startCustomStrategyReplay(
                                    replayStrategyId.text.trim(), replaySymbol.text.trim(),
                                    Number(replayPeriod.text), Number(replayBars.text), Number(replayInterval.text))
                            }
                            PpButton {
                                theme: root.theme; text: "停止"; danger: true; visualWidth: 64
                                available: root.controller.customStrategyReplayRunning
                                toolTip: available ? "停止当前本地模拟回放" : "当前没有运行中的回放"
                                onClicked: root.controller.stopCustomStrategyReplay()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 260
                    radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: "审批队列"; Layout.fillWidth: true }
                            Text { text: root.controller.pendingStrategyIntentCount + " 待处理 · 仅模拟账户"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            model: root.controller.strategyIntentModel
                            spacing: 4; clip: true; boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property string intentId
                                required property string strategyId
                                required property string symbol
                                required property string signalSide
                                required property real signalPrice
                                required property real quantity
                                required property string status
                                required property bool pending
                                required property string message
                                required property var createdAt
                                width: ListView.view.width; height: 42; radius: 5
                                color: pending ? root.theme.bgSelected : root.theme.bgInput
                                border.color: pending ? root.theme.yellow : root.theme.border
                                Text {
                                    x: 7; y: 5; width: parent.width - 200
                                    text: intentId + " · " + strategyId + " · " + symbol
                                    color: root.theme.textPrimary; elide: Text.ElideRight
                                    font.family: root.theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold
                                }
                                Text {
                                    x: 7; y: 23; width: parent.width - 200
                                    text: signalSide + " " + quantity.toFixed(0) + " 股 @ " + signalPrice.toFixed(2)
                                          + " · " + status + (message ? " · " + message : "")
                                    color: pending ? root.theme.yellow : root.theme.textMuted; elide: Text.ElideRight
                                    font.family: root.theme.fontFamily; font.pixelSize: 8
                                }
                                Row {
                                    anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                                    PpButton { theme: root.theme; text: "通过"; visualWidth: 54; visualHeight: 24; available: pending; toolTip: "通过后仍进入模拟账户风控与撮合"; onClicked: root.controller.approveStrategyIntent(intentId) }
                                    PpButton { theme: root.theme; text: "拒绝"; danger: true; visualWidth: 54; visualHeight: 24; available: pending; toolTip: "以插件管理页面人工拒绝原因驳回"; onClicked: root.controller.rejectStrategyIntent(intentId, "用户在插件管理页面拒绝") }
                                }
                            }
                            Text {
                                anchors.centerIn: parent; visible: root.controller.strategyIntentModel.count === 0
                                text: "暂无真实策略意向"; color: root.theme.textMuted
                                font.family: root.theme.fontFamily; font.pixelSize: 10
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: root.theme.bgPanel; border.color: root.theme.border
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 7
                        RowLayout {
                            Layout.fillWidth: true
                            SectionLabel { text: "通知设置"; Layout.fillWidth: true }
                            Text { text: "保存到本机运行时配置"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                        }
                        GridLayout {
                            Layout.fillWidth: true; columns: root.width < 1180 ? 2 : 4
                            columnSpacing: 10; rowSpacing: 2
                            DarkCheckBox { text: "桌面通知"; checked: root.desktopEnabled; onToggled: root.desktopEnabled = checked; Accessible.name: "启用桌面通知" }
                            DarkCheckBox { text: "Webhook 通知"; checked: root.webhookEnabled; onToggled: root.webhookEnabled = checked; Accessible.name: "启用 Webhook 通知" }
                            DarkCheckBox { text: "信号触发"; checked: root.notifySignal; onToggled: root.notifySignal = checked; Accessible.name: "通知信号触发事件" }
                            DarkCheckBox { text: "审批事件"; checked: root.notifyApproval; onToggled: root.notifyApproval = checked; Accessible.name: "通知审批事件" }
                            DarkCheckBox { text: "订单提交"; checked: root.notifyOrderSubmitted; onToggled: root.notifyOrderSubmitted = checked; Accessible.name: "通知模拟订单提交事件" }
                            DarkCheckBox { text: "执行拒绝"; checked: root.notifyExecutionRejected; onToggled: root.notifyExecutionRejected = checked; Accessible.name: "通知执行拒绝事件" }
                            DarkCheckBox { text: "意向过期"; checked: root.notifyIntentExpired; onToggled: root.notifyIntentExpired = checked; Accessible.name: "通知策略意向过期事件" }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FormField { id: webhookField; Layout.fillWidth: true; text: root.webhookUrl; placeholderText: "Webhook URL（可留空）"; Accessible.name: "Webhook URL" }
                            PpButton {
                                theme: root.theme; text: "保存设置"; primary: true; visualWidth: 88
                                toolTip: "写入本机 QSettings；不会把凭据写入项目"
                                onClicked: {
                                    root.webhookUrl = webhookField.text.trim()
                                    const ok = root.controller.configureNotifications(
                                        root.desktopEnabled, root.webhookEnabled, root.webhookUrl,
                                        root.notifySignal, root.notifyApproval,
                                        root.notifyOrderSubmitted, root.notifyExecutionRejected,
                                        root.notifyIntentExpired)
                                    root.actionMessage = ok ? "通知设置已保存" : ""
                                    root.actionError = ok ? "" : "通知设置保存失败"
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Webhook 只在用户启用后发送运行事件；本页面不测试或上传 URL。"
                            color: root.theme.textMuted; wrapMode: Text.Wrap
                            font.family: root.theme.fontFamily; font.pixelSize: 8
                        }
                    }
                }
            }
        }
    }
}
