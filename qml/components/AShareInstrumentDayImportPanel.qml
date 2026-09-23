// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    FileDialog {
        id: csvDialog
        title: "选择由交易所公开信息整理的停复牌事实 CSV"
        nameFilters: ["CSV 文件 (*.csv)", "所有文件 (*)"]
        onAccepted: root.controller.setAShareInstrumentDayImportFile(
                        String(selectedFile))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 9

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 960 ? 3 : 1
            columnSpacing: 12
            rowSpacing: 7

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 280
                spacing: 4
                Text {
                    text: "1. 显式选择规范 CSV（选择不会读取内容）"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: fileField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        readOnly: true
                        text: root.controller.aShareInstrumentDayImportFilePath
                        placeholderText: "symbol,name,venue,trading_date,state,reason"
                        color: root.theme.textPrimary
                        placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        selectByMouse: true; activeFocusOnTab: true
                        Accessible.name: "停复牌事实 CSV 路径"
                        Accessible.description: "仅显示用户选择的本地文件；选择本身不会读取文件"
                        background: Rectangle {
                            radius: 6; color: root.theme.bgCanvas
                            border.width: fileField.activeFocus ? 2 : 1
                            border.color: fileField.activeFocus
                                          ? root.theme.cyan : root.theme.border
                        }
                    }
                    PpButton {
                        theme: root.theme; text: "浏览"; visualWidth: 64
                        available: !root.controller.aShareInstrumentDayImportRunning
                        toolTip: "选择本地 CSV；只有点击导入才读取"
                        onClicked: csvDialog.open()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 220
                spacing: 4
                Text {
                    text: "2. 声明官方来源"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                ComboBox {
                    id: sourceBox
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    model: ["SSE", "SZSE"]
                    currentIndex: root.controller.aShareInstrumentDayImportSource === "SZSE" ? 1 : 0
                    Accessible.name: "停复牌事实官方来源"
                    Accessible.description: "SSE 为上海证券交易所，SZSE 为深圳证券交易所"
                    background: Rectangle {
                        radius: 6; color: root.theme.bgCanvas
                        border.width: sourceBox.activeFocus ? 2 : 1
                        border.color: sourceBox.activeFocus
                                      ? root.theme.cyan : root.theme.border
                    }
                    contentItem: Text {
                        leftPadding: 10; text: sourceBox.displayText
                        verticalAlignment: Text.AlignVCenter
                        color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 10
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 260
                spacing: 4
                Text {
                    text: "3. 公告号 / 导出日期 / 数据版本（必填）"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                TextField {
                    id: versionField
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: root.controller.aShareInstrumentDayImportSourceVersion
                    placeholderText: "例如 SSE-export-20260821"
                    color: root.theme.textPrimary
                    placeholderTextColor: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    selectByMouse: true; activeFocusOnTab: true
                    Accessible.name: "停复牌事实来源版本"
                    Accessible.description: "填写可追溯的公告号、导出日期或版本标识"
                    background: Rectangle {
                        radius: 6; color: root.theme.bgCanvas
                        border.width: versionField.activeFocus ? 2 : 1
                        border.color: versionField.activeFocus
                                      ? root.theme.cyan : root.theme.border
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: root.controller.aShareInstrumentDayImportCorrectedRecords > 0
                      ? "修订原因（必填）" : "修订原因（仅变更既有事实时必填）"
                color: root.controller.aShareInstrumentDayImportCorrectedRecords > 0
                       ? root.theme.yellow : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            TextField {
                id: revisionReasonField
                Layout.fillWidth: true; Layout.preferredHeight: 34
                placeholderText: "例如：交易所公告更正，经人工复核"
                color: root.theme.textPrimary
                placeholderTextColor: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true; activeFocusOnTab: true
                Accessible.name: "停复牌事实修订原因"
                Accessible.description: "预览发现既有事实变化时必须填写，随导入批次永久留存"
                background: Rectangle {
                    radius: 6; color: root.theme.bgCanvas
                    border.width: revisionReasonField.activeFocus ? 2 : 1
                    border.color: revisionReasonField.activeFocus
                                  ? root.theme.cyan
                                  : (root.controller.aShareInstrumentDayImportCorrectedRecords > 0
                                     && revisionReasonField.text.trim().length === 0
                                     ? root.theme.yellow : root.theme.border)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 9
            PpButton {
                theme: root.theme
                text: root.controller.aShareInstrumentDayImportRunning
                      ? "正在处理…" : "1. 预览差异"
                visualWidth: 118; primary: false
                available: !root.controller.aShareInstrumentDayImportRunning
                           && root.controller.aShareInstrumentDayImportFilePath.length > 0
                           && versionField.text.trim().length > 0
                toolTip: available
                         ? "逐行校验并与当前事实比较；不写库、不联网"
                         : "请选择 CSV、填写来源版本，或等待当前任务结束"
                onClicked: root.controller.previewAShareInstrumentDayImport(
                               sourceBox.currentText, versionField.text)
            }
            PpButton {
                theme: root.theme; text: "2. 确认事务导入"; visualWidth: 132
                primary: true
                available: !root.controller.aShareInstrumentDayImportRunning
                           && root.controller.aShareInstrumentDayImportPreviewReady
                           && root.controller.aShareInstrumentDayImportSource
                              === sourceBox.currentText
                           && root.controller.aShareInstrumentDayImportSourceVersion
                              === versionField.text.trim()
                           && (root.controller.aShareInstrumentDayImportCorrectedRecords === 0
                               || revisionReasonField.text.trim().length > 0)
                toolTip: available
                         ? "重新校验预览哈希后，事务写入不可变批次明细与当前事实"
                         : "请先完成同一文件、来源和版本的预览；修订既有事实还需填写原因"
                onClicked: root.controller.commitAShareInstrumentDayImport(
                               sourceBox.currentText, versionField.text,
                               revisionReasonField.text)
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 62
                available: root.controller.aShareInstrumentDayImportRunning
                toolTip: "取消解析；未提交事务的数据不会写入"
                onClicked: root.controller.cancelAShareInstrumentDayImport()
            }
            PpButton {
                theme: root.theme; text: "刷新审计记录"; visualWidth: 108
                available: !root.controller.aShareInstrumentDayImportRunning
                toolTip: "只读取 SQLite 审计记录，不读取 CSV、不联网"
                onClicked: root.controller.refreshAShareInstrumentDayImportHistory()
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.aShareInstrumentDayImportStatusText
                color: root.controller.aShareInstrumentDayImportRunning
                       ? root.theme.yellow : root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 900 ? 4 : 2
            columnSpacing: 12; rowSpacing: 3
            Text { Layout.fillWidth: true; text: "读取行 " + root.controller.aShareInstrumentDayImportLinesRead; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "有效事实 " + root.controller.aShareInstrumentDayImportValidRecords; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "本次写入 " + root.controller.aShareInstrumentDayImportWrittenRecords + (root.controller.aShareInstrumentDayImportReused ? "（已归档复用）" : ""); color: root.controller.aShareInstrumentDayImportWrittenRecords > 0 ? root.theme.green : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "范围 " + (root.controller.aShareInstrumentDayImportDateRange || "--"); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
            Text { Layout.fillWidth: true; text: "新增 " + root.controller.aShareInstrumentDayImportNewRecords; color: root.controller.aShareInstrumentDayImportNewRecords > 0 ? root.theme.green : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "不变 " + root.controller.aShareInstrumentDayImportUnchangedRecords; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "修订 " + root.controller.aShareInstrumentDayImportCorrectedRecords; color: root.controller.aShareInstrumentDayImportCorrectedRecords > 0 ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: root.controller.aShareInstrumentDayImportPreviewReady ? "预览可确认" : "尚未完成预览"; color: root.controller.aShareInstrumentDayImportPreviewReady ? root.theme.cyan : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: "SHA-256 " + (root.controller.aShareInstrumentDayImportContentSha256 || "--"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideMiddle; Accessible.name: text }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: root.controller.aShareInstrumentDayImportHistory.length > 0 ? "最近归档 " + root.controller.aShareInstrumentDayImportHistory[0].source + " · " + root.controller.aShareInstrumentDayImportHistory[0].sourceVersion + " · " + root.controller.aShareInstrumentDayImportHistory[0].recordCount + " 条" : "最近归档 --"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight }
        }

        Text {
            Layout.fillWidth: true
            visible: root.controller.aShareInstrumentDayImportError.length > 0
            text: root.controller.aShareInstrumentDayImportError
            color: root.theme.red
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap
            Accessible.name: "停复牌事实导入错误：" + text
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 44
            model: root.controller.aShareInstrumentDayImportDiagnostics
            clip: true; spacing: 2; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Text {
                required property var modelData
                width: ListView.view.width
                height: Math.max(18, implicitHeight)
                text: (modelData.lineNumber > 0 ? "行 " + modelData.lineNumber + " · " : "")
                      + modelData.reason
                color: root.theme.red
                font.family: root.theme.fontFamily; font.pixelSize: 9
                wrapMode: Text.Wrap; Accessible.name: text
            }
        }

        Text {
            Layout.fillWidth: true
            text: "边界：预览不写库；确认时重验 SHA-256。每批明细不可变，修订保留原因与前序批次；不自动联网，未导入项保持 unknown。"
            color: root.theme.yellow
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap; Accessible.name: text
        }
    }
}
