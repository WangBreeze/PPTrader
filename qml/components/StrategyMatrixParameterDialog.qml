pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    required property var theme
    property string strategyToken: ""
    property var parameterSpecs: []
    property var draftValues: ({})
    property string validationError: ""
    signal saved(string token, var values)

    function configure(token, specs, values) {
        strategyToken = token
        parameterSpecs = []
        draftValues = Object.assign({}, values || ({}))
        parameterSpecs = specs || []
        validationError = ""
        open()
    }

    function displayValue(spec) {
        return String(draftValues[spec.key] === undefined
                      ? spec.defaultValue : draftValues[spec.key])
    }

    function changeValue(key, value) {
        const next = Object.assign({}, draftValues)
        next[key] = value
        draftValues = next
        validationError = ""
    }

    function saveValues() {
        const values = ({})
        for (const spec of parameterSpecs) {
            const raw = displayValue(spec).trim()
            const number = Number(raw)
            if (!raw || !isFinite(number) || number < Number(spec.minimumValue)
                || number > Number(spec.maximumValue)
                || (spec.type === "integer" && Math.floor(number) !== number)) {
                validationError = spec.displayName + " 需要填写 "
                    + spec.minimumValue + "–" + spec.maximumValue
                    + (spec.type === "integer" ? " 的整数" : " 的数值")
                return
            }
            values[spec.key] = number
        }
        saved(strategyToken, values)
        close()
    }

    parent: Overlay.overlay
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(parent ? parent.width - 32 : 600, 600)
    height: Math.min(parent ? parent.height - 32 : 530, 530)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 16
    background: Rectangle {
        radius: 10
        color: root.theme.bgPanel
        border.color: root.theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        Text {
            Layout.fillWidth: true
            text: "参数微调 · " + root.strategyToken
            color: root.theme.textPrimary
            font.family: root.theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: "每次保存生成本次回测使用的参数；批次启动后会冻结策略定义和参数。"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 8
                Repeater {
                    model: root.parameterSpecs
                    delegate: RowLayout {
                        id: parameterRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.preferredWidth: 140
                            text: parameterRow.modelData.displayName
                            color: root.theme.textPrimary
                            font.family: root.theme.fontFamily
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: root.displayValue(parameterRow.modelData)
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            Accessible.name: parameterRow.modelData.displayName
                            onTextEdited: root.changeValue(parameterRow.modelData.key, text)
                        }
                        Text {
                            Layout.preferredWidth: 145
                            text: parameterRow.modelData.minimumValue + "–"
                                + parameterRow.modelData.maximumValue
                            color: root.theme.textMuted
                            font.family: root.theme.fontFamily
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.validationError.length > 0
            text: root.validationError
            color: root.theme.red
            font.family: root.theme.fontFamily
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 72
                onClicked: root.close()
            }
            PpButton {
                theme: root.theme; text: "保存参数"; visualWidth: 92; primary: true
                onClicked: root.saveValues()
            }
        }
    }
}
