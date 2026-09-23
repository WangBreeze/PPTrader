// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    readonly property var importState: controller.aShareIndustryImportState || ({})
    readonly property bool busy: Boolean(importState.running)
    readonly property bool needsReason: Boolean(importState.requiresRevisionReason)

    FileDialog {
        id: csvDialog
        title: "选择本地 A 股行业分类 CSV"
        nameFilters: ["CSV 文件 (*.csv)", "所有文件 (*)"]
        onAccepted: root.controller.setAShareIndustryImportFile(String(selectedFile))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 9

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 960 ? 4 : (width >= 620 ? 2 : 1)
            columnSpacing: 10
            rowSpacing: 7

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 250
                spacing: 4
                Text {
                    text: "1. 显式选择文件（选择不会读取）"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: fileField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        readOnly: true
                        text: String(root.importState.filePath || "")
                        placeholderText: "本地磁盘或已挂载盘符中的 CSV"
                        color: root.theme.textPrimary
                        placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        selectByMouse: true; activeFocusOnTab: true
                        Accessible.name: "行业分类 CSV 路径"
                        Accessible.description: "只显示用户选择的绝对路径，选择本身不读取文件"
                        background: Rectangle {
                            radius: 6; color: root.theme.bgCanvas
                            border.width: fileField.activeFocus ? 2 : 1
                            border.color: fileField.activeFocus
                                          ? root.theme.cyan : root.theme.border
                        }
                    }
                    PpButton {
                        theme: root.theme; text: "浏览"; visualWidth: 62
                        available: !root.busy
                        toolTip: "选择本地 CSV；点击预览后才逐行读取"
                        onClicked: csvDialog.open()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 160; spacing: 4
                Text {
                    text: "2. 分类体系"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                TextField {
                    id: classificationField
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: String(root.importState.classification || "CSRC")
                    placeholderText: "例如 CSRC"
                    color: root.theme.textPrimary
                    placeholderTextColor: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    selectByMouse: true; activeFocusOnTab: true
                    Accessible.name: "行业分类体系"
                    Accessible.description: "填写数据文件使用的分类体系标识"
                    background: Rectangle {
                        radius: 6; color: root.theme.bgCanvas
                        border.width: classificationField.activeFocus ? 2 : 1
                        border.color: classificationField.activeFocus
                                      ? root.theme.cyan : root.theme.border
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 180; spacing: 4
                Text {
                    text: "3. 数据来源（必填）"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                TextField {
                    id: sourceField
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: String(root.importState.source || "")
                    placeholderText: "按实际文件填写，不预设官方来源"
                    color: root.theme.textPrimary
                    placeholderTextColor: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    selectByMouse: true; activeFocusOnTab: true
                    Accessible.name: "行业数据来源"
                    Accessible.description: "必须如实填写文件来源，界面不会推断或伪造官方身份"
                    background: Rectangle {
                        radius: 6; color: root.theme.bgCanvas
                        border.width: sourceField.activeFocus ? 2 : 1
                        border.color: sourceField.activeFocus
                                      ? root.theme.cyan : root.theme.border
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 190; spacing: 4
                Text {
                    text: "4. 来源版本（必填）"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                }
                TextField {
                    id: versionField
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: String(root.importState.sourceVersion || "")
                    placeholderText: "例如 export-20260822"
                    color: root.theme.textPrimary
                    placeholderTextColor: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 10
                    selectByMouse: true; activeFocusOnTab: true
                    Accessible.name: "行业数据来源版本"
                    Accessible.description: "填写可追溯的导出日期、公告号或版本标识"
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
            Layout.fillWidth: true; spacing: 8
            Text {
                text: root.needsReason ? "同版本修订原因（必填）" : "同版本修订原因（内容变化时必填）"
                color: root.needsReason ? root.theme.yellow : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            TextField {
                id: reasonField
                Layout.fillWidth: true; Layout.preferredHeight: 34
                placeholderText: "例如：来源文件更正，经人工复核"
                color: root.theme.textPrimary
                placeholderTextColor: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 10
                selectByMouse: true; activeFocusOnTab: true
                Accessible.name: "行业数据集修订原因"
                Accessible.description: "同一分类、来源和版本出现不同内容时必须填写并永久留存"
                background: Rectangle {
                    radius: 6; color: root.theme.bgCanvas
                    border.width: reasonField.activeFocus ? 2 : 1
                    border.color: reasonField.activeFocus ? root.theme.cyan
                                  : (root.needsReason && reasonField.text.trim().length === 0
                                     ? root.theme.yellow : root.theme.border)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            PpButton {
                theme: root.theme
                text: root.busy ? "正在处理…" : "1. 预览校验"
                visualWidth: 112
                available: !root.busy
                           && fileField.text.length > 0
                           && classificationField.text.trim().length > 0
                           && sourceField.text.trim().length > 0
                           && versionField.text.trim().length > 0
                toolTip: available
                         ? "后台逐行校验、计算双哈希并检查版本冲突；不写库"
                         : "请选择文件并完整填写分类体系、来源和版本"
                onClicked: root.controller.previewAShareIndustryImport(
                               classificationField.text,
                               sourceField.text, versionField.text)
            }
            PpButton {
                theme: root.theme; text: "2. 确认导入"; visualWidth: 112
                primary: true
                available: !root.busy && Boolean(root.importState.previewReady)
                           && String(root.importState.classification)
                              === classificationField.text.trim()
                           && String(root.importState.source)
                              === sourceField.text.trim()
                           && String(root.importState.sourceVersion)
                              === versionField.text.trim()
                           && (!root.needsReason
                               || reasonField.text.trim().length > 0)
                toolTip: available
                         ? "重新校验文件 SHA-256 后事务写入不可变数据集"
                         : "请先完成相同文件与身份的预览；同版本修订还需填写原因"
                onClicked: root.controller.commitAShareIndustryImport(
                               classificationField.text, sourceField.text,
                               versionField.text, reasonField.text)
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 62
                available: root.busy
                toolTip: "取消后台解析；未提交的数据不会写入"
                onClicked: root.controller.cancelAShareIndustryImport()
            }
            PpButton {
                theme: root.theme; text: "刷新审计"; visualWidth: 88
                available: !root.busy
                toolTip: "只读取 SQLite 历史，不读取 CSV、不联网"
                onClicked: root.controller.refreshAShareIndustryImportHistory()
            }
            Text {
                Layout.fillWidth: true
                text: String(root.importState.statusText || "尚未选择行业分类 CSV")
                color: root.busy ? root.theme.yellow : root.theme.textPrimary
                font.family: root.theme.fontFamily; font.pixelSize: 10
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 850 ? 4 : 2
            columnSpacing: 12; rowSpacing: 3
            Text { Layout.fillWidth: true; text: "读取行 " + Number(root.importState.linesRead || 0); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "有效成员 " + Number(root.importState.validRecords || 0); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: "本次写入 " + Number(root.importState.writtenRecords || 0) + (root.importState.reused ? "（复用）" : ""); color: Number(root.importState.writtenRecords || 0) > 0 ? root.theme.green : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.fillWidth: true; text: root.importState.previewReady ? "预览可确认" : "尚未完成预览"; color: root.importState.previewReady ? root.theme.cyan : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: "文件 SHA-256 " + String(root.importState.fileContentSha256 || "--"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideMiddle; Accessible.name: text }
            Text { Layout.columnSpan: 2; Layout.fillWidth: true; text: "数据集 SHA-256 " + String(root.importState.datasetContentSha256 || "--"); color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideMiddle; Accessible.name: text }
            Text {
                Layout.columnSpan: 4; Layout.fillWidth: true
                text: root.controller.aShareIndustryImportHistory.length > 0
                      ? "最近归档 " + root.controller.aShareIndustryImportHistory[0].classification
                        + " · " + root.controller.aShareIndustryImportHistory[0].source
                        + " · " + root.controller.aShareIndustryImportHistory[0].sourceVersion
                        + " · " + root.controller.aShareIndustryImportHistory[0].recordCount + " 条"
                      : "最近归档 --"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            visible: String(root.importState.error || "").length > 0
            text: String(root.importState.error || "")
            color: root.theme.red
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap
            Accessible.name: "行业数据集导入错误：" + text
        }

        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 36
            model: root.importState.diagnostics || []
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
            text: "边界：仅处理用户显式选择的本地/已挂载盘符文件；预览不写库，确认时重验文件哈希。分类来源由用户声明，导入成功不代表来源已获官方认证；不访问网络或 NAS。"
            color: root.theme.yellow
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap; Accessible.name: text
        }
    }
}
