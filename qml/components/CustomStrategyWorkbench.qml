// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    property string selectedStrategyId: ""
    property int selectedVersion: 0
    property var validation: ({ valid: false, error: "尚未校验" })
    property var analysis: ({ executable: false })
    property var runAvailability: ({ available: false, error: "请先选择策略版本" })
    property string operationMessage: ""
    property bool editorDirty: false

    readonly property bool savedDefinitionMatches: validation.valid === true
        && selectedStrategyId.length > 0
        && validation.definition !== undefined
        && validation.definition.definitionJson
           === controller.exportCustomStrategyJson(selectedStrategyId, selectedVersion)
    readonly property bool canRun: savedDefinitionMatches
        && analysis.executable === true
        && runAvailability.available === true

    function refreshRunAvailability() {
        runAvailability = controller.customStrategyLocalRunAvailability(
            selectedStrategyId, selectedVersion, {})
    }

    function validateEditor() {
        validation = controller.validateCustomStrategyJson(jsonEditor.text)
        analysis = controller.analyzeCustomStrategyJson(jsonEditor.text, {})
        editorDirty = false
        operationMessage = validation.valid
            ? (analysis.executable ? "校验通过，可保存并用于严格本地回测"
                                   : analysis.error)
            : validation.error
        refreshRunAvailability()
        return validation.valid === true && analysis.executable === true
    }

    function loadDefinition(strategyId, version, row) {
        const exported = controller.exportCustomStrategyJson(strategyId, version)
        if (exported.length === 0) {
            operationMessage = "无法读取所选策略版本"
            return
        }
        selectedStrategyId = strategyId
        selectedVersion = version
        strategyList.currentIndex = row
        jsonEditor.text = exported
        validateEditor()
    }

    function saveNewVersion() {
        if (!validateEditor()) return
        const definition = validation.definition
        if (controller.customStrategyModel.indexOf(definition.strategyId,
                                                   definition.version) >= 0) {
            operationMessage = "该策略版本已存在；请递增 JSON 中的 version 后再保存"
            return
        }
        if (!controller.saveCustomStrategyJson(jsonEditor.text)) {
            operationMessage = controller.customStrategyError
            return
        }
        selectedStrategyId = definition.strategyId
        selectedVersion = definition.version
        const row = controller.customStrategyModel.indexOf(selectedStrategyId,
                                                           selectedVersion)
        loadDefinition(selectedStrategyId, selectedVersion, row)
        operationMessage = "新版本已保存并选中：" + selectedStrategyId
                         + " v" + selectedVersion
    }

    function runLocalBacktest() {
        if (!validateEditor() || !canRun) {
            operationMessage = savedDefinitionMatches
                ? runAvailability.error
                : "编辑内容必须与已保存且选中的策略版本完全一致"
            return
        }
        const taskId = controller.runSelectedLocalCustomStrategyBacktest(
            selectedStrategyId, selectedVersion, {})
        operationMessage = taskId > 0
            ? "已启动严格本地回测任务 " + taskId
            : controller.backtestError
        refreshRunAvailability()
    }

    Component.onCompleted: {
        if (controller.customStrategyModel.count > 0) {
            const item = controller.customStrategyModel.get(0)
            loadDefinition(item.strategyId, item.version, 0)
        } else {
            refreshRunAvailability()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 250
            Layout.minimumWidth: 220
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border

            Text {
                x: 12; y: 12; width: parent.width - 24
                text: "已保存版本（" + root.controller.customStrategyModel.count + "）"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily
                font.pixelSize: 11; font.weight: Font.Bold
            }
            ListView {
                id: strategyList
                x: 10; y: 40; width: parent.width - 20; height: parent.height - 52
                model: root.controller.customStrategyModel
                clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: count > 0
                Accessible.role: Accessible.List
                Accessible.name: "已保存的自定义策略版本"
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property int index
                    required property string strategyId
                    required property int version
                    required property string name
                    required property string executionScope
                    required property string stage
                    required property bool executable
                    required property bool latest
                    readonly property bool selected: root.selectedStrategyId === strategyId
                                                     && root.selectedVersion === version
                    width: ListView.view.width; height: 66; radius: 6
                    color: selected ? root.theme.bgSelected : root.theme.bgRaised
                    border.width: activeFocus || selected ? 2 : 1
                    border.color: activeFocus || selected ? root.theme.cyan : root.theme.border
                    activeFocusOnTab: true
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: name + "，版本 " + version
                    Accessible.description: stage + "，" + executionScope
                    Accessible.checkable: true; Accessible.checked: selected
                    Text { x: 9; y: 7; width: parent.width - 18; text: name + " · v" + parent.version + (parent.latest ? " · 最新" : ""); color: parent.selected ? root.theme.cyan : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold; elide: Text.ElideRight }
                    Text { x: 9; y: 27; width: parent.width - 18; text: parent.strategyId; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                    Text { x: 9; y: 45; width: parent.width - 18; text: parent.stage + " · " + (parent.executable ? "可回测" : "不可运行") + " · 仅历史回测"; color: parent.executable ? root.theme.green : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideRight }
                    MouseArea { anchors.fill: parent; onPressed: parent.forceActiveFocus(Qt.MouseFocusReason); onClicked: root.loadDefinition(parent.strategyId, parent.version, parent.index) }
                    Keys.onSpacePressed: function(event) { root.loadDefinition(strategyId, version, index); event.accepted = true }
                    Keys.onReturnPressed: function(event) { root.loadDefinition(strategyId, version, index); event.accepted = true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.minimumWidth: 430
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border

            Text {
                x: 12; y: 11; width: parent.width - 24
                text: "粘贴或编辑声明式 JSON"
                color: root.theme.textPrimary; font.family: root.theme.fontFamily
                font.pixelSize: 11; font.weight: Font.Bold
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: 12; y: 12
                text: "禁止脚本、动态 JS、命令和网络请求"
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8
            }
            Rectangle {
                x: 10; y: 38; width: parent.width - 20; height: parent.height - 92
                radius: 6; color: root.theme.bgCanvas
                border.width: jsonEditor.activeFocus ? 2 : 1
                border.color: jsonEditor.activeFocus ? root.theme.cyan : root.theme.border
                ScrollView {
                    anchors.fill: parent; anchors.margins: 4
                    TextArea {
                        id: jsonEditor
                        color: root.theme.textPrimary
                        selectionColor: root.theme.cyan
                        selectedTextColor: root.theme.bgCanvas
                        font.family: "Consolas"; font.pixelSize: 10
                        wrapMode: TextEdit.NoWrap; selectByMouse: true
                        background: null
                        Accessible.name: "自定义策略 JSON 编辑器"
                        Accessible.description: "只接受受限声明式 DSL；修改后必须重新校验并保存新版本"
                        onTextChanged: {
                            root.editorDirty = true
                            root.validation = ({ valid: false, error: "内容已修改，请重新校验" })
                            root.analysis = ({ executable: false })
                        }
                    }
                }
            }
            Row {
                x: 10; y: parent.height - 46; spacing: 8
                PpButton { theme: root.theme; text: "校验"; visualWidth: 78; toolTip: "校验 JSON、字段路径、指标和规则"; onClicked: root.validateEditor() }
                PpButton { theme: root.theme; text: "保存新版本"; visualWidth: 104; primary: true; toolTip: "写入不可变版本并刷新本地策略模型"; onClicked: root.saveNewVersion() }
                PpButton {
                    theme: root.theme; text: "本地回测"; visualWidth: 92
                    success: root.canRun; available: root.canRun
                    toolTip: root.canRun ? "只读取当前 SQLite 数据集，不访问网络"
                                             : (root.savedDefinitionMatches
                                                ? root.runAvailability.error
                                                : "请先校验并保存当前版本")
                    onClicked: root.runLocalBacktest()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 286
            Layout.minimumWidth: 250
            Layout.fillHeight: true
            radius: 8; color: root.theme.bgInput; border.color: root.theme.border

            Column {
                x: 12; y: 12; width: parent.width - 24; spacing: 9
                Text { width: parent.width; text: "校验与运行边界"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 11; font.weight: Font.Bold }
                Text { width: parent.width; text: root.selectedStrategyId.length > 0 ? root.selectedStrategyId + " · v" + root.selectedVersion : "未选择已保存版本"; color: root.selectedStrategyId.length > 0 ? root.theme.cyan : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 11; elide: Text.ElideRight }
                Text { width: parent.width; text: "执行范围：仅历史回测（backtest_only）"; color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text }
                Rectangle { width: parent.width; height: 1; color: root.theme.border }
                Text { width: parent.width; text: root.validation.valid ? "JSON：有效" : "JSON：无效/未校验"; color: root.validation.valid ? root.theme.green : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                Text { width: parent.width; text: root.analysis.executable ? "编译：backtest_ready" : "编译：不可运行"; color: root.analysis.executable ? root.theme.green : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 10 }
                Text { width: parent.width; text: root.savedDefinitionMatches ? "版本：与已保存定义一致" : "版本：未保存或内容已变化"; color: root.savedDefinitionMatches ? root.theme.green : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                Text { width: parent.width; text: root.controller.selectedBacktestDataAvailable ? "本地数据：" + root.controller.selectedBacktestInstrumentTitle + " · " + root.controller.selectedBacktestFrequency + " · " + root.controller.selectedBacktestCandleCount + " 根" : "本地数据：未选择"; color: root.controller.selectedBacktestDataAvailable ? root.theme.textPrimary : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap }
                Text { width: parent.width; text: root.runAvailability.available ? "运行检查：通过" : "运行检查：" + root.runAvailability.error; color: root.runAvailability.available ? root.theme.green : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text }
                Rectangle { width: parent.width; height: 1; color: root.theme.border }
                Text { width: parent.width; text: root.operationMessage.length > 0 ? root.operationMessage : root.validation.error; color: root.validation.valid ? root.theme.textMuted : root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text; Accessible.description: "自定义策略校验、保存或运行结果" }
                Text { width: parent.width; text: "Schema：" + (root.controller.customStrategySchema.stage || "--") + " · 指标白名单由 C++ 校验器固定"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8; wrapMode: Text.Wrap }
            }
        }
    }

    Connections {
        target: root.controller
        function onBacktestInstrumentChanged() { root.refreshRunAvailability() }
        function onBacktestConfigurationChanged() { root.refreshRunAvailability() }
        function onBacktestRunningChanged() { root.refreshRunAvailability() }
        function onCustomStrategyStateChanged() {
            if (root.selectedStrategyId.length === 0
                    && root.controller.customStrategyModel.count > 0) {
                const item = root.controller.customStrategyModel.get(0)
                root.loadDefinition(item.strategyId, item.version, 0)
                return
            }
            root.refreshRunAvailability()
        }
    }
}
