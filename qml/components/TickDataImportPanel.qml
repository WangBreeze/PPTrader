// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    readonly property bool fiveMinuteFile: formatBox.currentValue === 1

    function beginImport() {
        root.controller.startTickDataImport(dateField.text,
                                            formatBox.currentValue,
                                            root.fiveMinuteFile ? 300 : periodBox.currentValue,
                                            volumeUnitBox.currentValue)
    }

    FolderDialog {
        id: folderDialog
        title: "选择 TickData 根目录"
        onAccepted: {
            rootPathField.text = String(selectedFolder)
            root.controller.setTickDataImportRootDirectory(String(selectedFolder))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 1180 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 260
                spacing: 4
                Text { text: "1. 根目录（选择不会读取文件）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: rootPathField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: root.controller.tickDataImportRootDirectory
                        placeholderText: "本地磁盘或已挂载盘符的绝对目录"
                        color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        selectByMouse: true; activeFocusOnTab: true
                        Accessible.name: "TickData 根目录"
                        Accessible.description: "输入本地或已挂载的绝对目录；只有开始导入才读取 CSV"
                        onAccepted: root.controller.setTickDataImportRootDirectory(text)
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: rootPathField.activeFocus ? 2 : 1; border.color: rootPathField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    PpButton {
                        theme: root.theme; text: "应用"; visualWidth: 58
                        available: !root.controller.tickDataImportRunning
                                   && !root.controller.tickDataBatchRunning
                        toolTip: "仅保存目录，不扫描、不联网"
                        onClicked: root.controller.setTickDataImportRootDirectory(rootPathField.text)
                    }
                    PpButton {
                        theme: root.theme; text: "浏览"; visualWidth: 58
                        available: !root.controller.tickDataImportRunning
                                   && !root.controller.tickDataBatchRunning
                        toolTip: "选择本地或已挂载目录"
                        onClicked: folderDialog.open()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 220
                spacing: 4
                Text { text: "2. 单个 A 股代码"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: symbolField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: root.controller.tickDataImportSymbol
                        placeholderText: "例如 600519"
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{6}/ }
                        color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                        activeFocusOnTab: true; selectByMouse: true
                        Accessible.name: "CSV 导入 A 股代码"
                        Accessible.description: "只从已加载的 A 股股票池选择，不读取 CSV、不联网"
                        onAccepted: root.controller.selectTickDataImportInstrument(text)
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: symbolField.activeFocus ? 2 : 1; border.color: symbolField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    PpButton {
                        theme: root.theme; text: "选择"; visualWidth: 64
                        available: !root.controller.tickDataImportRunning
                                   && !root.controller.tickDataBatchRunning
                                   && symbolField.acceptableInput
                        toolTip: "确认单个 A 股代码；不会读取文件"
                        onClicked: root.controller.selectTickDataImportInstrument(symbolField.text)
                    }
                }
                Text { Layout.fillWidth: true; text: root.controller.tickDataImportInstrumentTitle || "尚未选择"; color: root.controller.tickDataImportSymbol.length > 0 ? root.theme.cyan : root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 200
                spacing: 4
                Text { text: "3. 交易日与文件格式"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: dateField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: root.controller.tickDataImportTradingDate || Qt.formatDate(new Date(), "yyyy-MM-dd")
                        placeholderText: "yyyy-MM-dd"
                        inputMask: "0000-00-00"
                        color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                        activeFocusOnTab: true
                        Accessible.name: "CSV 交易日"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: dateField.activeFocus ? 2 : 1; border.color: dateField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    ComboBox {
                        id: formatBox
                        Layout.preferredWidth: 110; Layout.preferredHeight: 34
                        model: [{ label: "逐笔（自动识别）", value: 0 }, { label: "5min 有表头", value: 1 }]
                        textRole: "label"; valueRole: "value"
                        Accessible.name: "TickData CSV 格式"
                        onActivated: if (currentValue === 1) periodBox.currentIndex = 1
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: formatBox.activeFocus ? 2 : 1; border.color: formatBox.activeFocus ? root.theme.cyan : root.theme.border }
                        contentItem: Text { leftPadding: 8; text: formatBox.displayText; verticalAlignment: Text.AlignVCenter; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 230
                spacing: 4
                Text { text: "4. 聚合周期与 volume 单位"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ComboBox {
                        id: periodBox
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        model: [{ label: "1 分钟", value: 60 }, { label: "5 分钟", value: 300 }]
                        textRole: "label"; valueRole: "value"
                        enabled: !root.fiveMinuteFile
                        Accessible.name: "逐笔聚合周期"
                        Accessible.description: root.fiveMinuteFile ? "5min 文件固定为 5 分钟" : "逐笔可聚合为 1 分钟或 5 分钟"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: periodBox.activeFocus ? 2 : 1; border.color: periodBox.activeFocus ? root.theme.cyan : root.theme.border }
                        contentItem: Text { leftPadding: 8; text: root.fiveMinuteFile ? "5 分钟（固定）" : periodBox.displayText; verticalAlignment: Text.AlignVCenter; color: periodBox.enabled ? root.theme.textPrimary : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                    ComboBox {
                        id: volumeUnitBox
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        model: [{ label: "单位未知", value: 0 }, { label: "股（原值）", value: 1 }, { label: "手（×100）", value: 2 }]
                        textRole: "label"; valueRole: "value"
                        enabled: root.fiveMinuteFile
                        Accessible.name: "5min 成交量单位"
                        Accessible.description: root.fiveMinuteFile ? "必须按文件来源显式确认，系统不会猜测" : "逐笔文件固定按手转股"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: volumeUnitBox.activeFocus ? 2 : 1; border.color: volumeUnitBox.activeFocus ? root.theme.cyan : root.theme.border }
                        contentItem: Text { leftPadding: 8; text: root.fiveMinuteFile ? volumeUnitBox.displayText : "逐笔：手→股"; verticalAlignment: Text.AlignVCenter; color: volumeUnitBox.enabled ? root.theme.textPrimary : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.theme.border }

        RowLayout {
            Layout.fillWidth: true; spacing: 10
            PpButton {
                theme: root.theme
                text: root.controller.tickDataImportRunning ? "正在逐行导入…" : "导入单代码单交易日"
                visualWidth: 174; primary: true
                available: !root.controller.tickDataImportRunning
                           && !root.controller.tickDataBatchRunning
                           && root.controller.tickDataImportRootDirectory.length > 0
                           && root.controller.tickDataImportSymbol.length > 0
                           && (!root.fiveMinuteFile || volumeUnitBox.currentValue !== 0)
                toolTip: available ? "此按钮是唯一会读取目标 CSV 的操作；不会联网"
                                   : "请确认目录、A 股代码及 5min volume 单位"
                onClicked: root.beginImport()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 62
                available: root.controller.tickDataImportRunning
                toolTip: "停止解析；取消的结果不会写入 SQLite"
                onClicked: root.controller.cancelTickDataImport()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.tickDataImportStatusText || "尚未开始；目录选择、catalog 浏览和回测品种选择都不会读取 CSV"
                color: root.controller.tickDataImportRunning ? root.theme.yellow : root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                wrapMode: Text.Wrap; Accessible.name: text
            }
        }

        GridLayout {
            Layout.fillWidth: true; columns: width >= 900 ? 4 : 2
            columnSpacing: 12; rowSpacing: 4
            Text { Layout.fillWidth: true; text: "读取行 " + root.controller.tickDataImportLinesRead; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "有效记录 " + root.controller.tickDataImportValidRecords + " · 跳过 " + root.controller.tickDataImportSkippedRecords; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "写入/更新 K 线 " + root.controller.tickDataImportWrittenBars; color: root.controller.tickDataImportWrittenBars > 0 ? root.theme.green : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "原始 Tick 入库 " + root.controller.tickDataImportRawTicksWritten; color: root.controller.tickDataImportRawTicksWritten > 0 ? root.theme.green : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "source " + (root.controller.tickDataImportSource || "--"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: root.controller.tickDataImportFirstTime ? "范围 " + Qt.formatDateTime(root.controller.tickDataImportFirstTime, "yyyy-MM-dd HH:mm") + " → " + Qt.formatDateTime(root.controller.tickDataImportLastTime, "yyyy-MM-dd HH:mm") + " UTC" : "范围 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: root.controller.tickDataImportFilePath ? "文件 " + root.controller.tickDataImportFilePath : "文件 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideMiddle }
        }

        Text {
            Layout.fillWidth: true
            visible: root.controller.tickDataImportError.length > 0
            text: root.controller.tickDataImportError
            color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap; Accessible.name: text
            Accessible.description: "CSV 导入错误；失败不会破坏已有本地 catalog"
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 56
            model: root.controller.tickDataImportDiagnostics
            clip: true; spacing: 2; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Text {
                required property var modelData
                width: ListView.view.width; height: Math.max(18, implicitHeight)
                text: "行 " + modelData.lineNumber + " · " + modelData.reason
                color: modelData.fatal ? root.theme.red : root.theme.yellow
                font.family: root.theme.fontFamily; font.pixelSize: 9
                wrapMode: Text.Wrap; Accessible.name: text
            }
        }

        Text {
            Layout.fillWidth: true
                text: "规则：逐笔支持五列 B/S/M 或中文六列表头，volume 按手×100 转股；5min volume 必须显式确认单位。"
            color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap; Accessible.name: text
        }
    }
}
