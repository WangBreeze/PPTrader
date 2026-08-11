// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    readonly property bool fiveMinuteFile: formatBox.currentValue === 1
    readonly property var summary: controller.tickDataBatchSummary
    property bool skipCompleteExisting: true
    property string selectedRunId: ""

    function dateText(offsetDays) {
        const value = new Date()
        value.setDate(value.getDate() + offsetDays)
        return Qt.formatDate(value, "yyyy-MM-dd")
    }

    function preparePlan() {
        controller.prepareTickDataBatchImport(startDateField.text, endDateField.text,
                                              formatBox.currentValue,
                                              fiveMinuteFile ? 300 : periodBox.currentValue,
                                              volumeUnitBox.currentValue,
                                              root.skipCompleteExisting)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 1080 ? 5 : 3
            columnSpacing: 10; rowSpacing: 6

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 150; spacing: 3
                Text { text: "开始日期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                TextField {
                    id: startDateField; Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: root.dateText(-7); inputMask: "0000-00-00"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                    activeFocusOnTab: true; Accessible.name: "批量导入开始日期"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: startDateField.activeFocus ? 2 : 1; border.color: startDateField.activeFocus ? root.theme.cyan : root.theme.border }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 150; spacing: 3
                Text { text: "结束日期（最多 366 天）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                TextField {
                    id: endDateField; Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: root.dateText(0); inputMask: "0000-00-00"
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                    activeFocusOnTab: true; Accessible.name: "批量导入结束日期"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: endDateField.activeFocus ? 2 : 1; border.color: endDateField.activeFocus ? root.theme.cyan : root.theme.border }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 140; spacing: 3
                Text { text: "文件格式"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                ComboBox {
                    id: formatBox; Layout.fillWidth: true; Layout.preferredHeight: 34
                    model: [{ label: "逐笔无表头", value: 0 }, { label: "5min 有表头", value: 1 }]
                    textRole: "label"; valueRole: "value"; Accessible.name: "批量 CSV 格式"
                    onActivated: if (currentValue === 1) periodBox.currentIndex = 1
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: formatBox.activeFocus ? 2 : 1; border.color: formatBox.activeFocus ? root.theme.cyan : root.theme.border }
                    contentItem: Text { leftPadding: 8; text: formatBox.displayText; verticalAlignment: Text.AlignVCenter; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 130; spacing: 3
                Text { text: "聚合周期"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                ComboBox {
                    id: periodBox; Layout.fillWidth: true; Layout.preferredHeight: 34
                    model: [{ label: "1 分钟", value: 60 }, { label: "5 分钟", value: 300 }]
                    textRole: "label"; valueRole: "value"; enabled: !root.fiveMinuteFile
                    Accessible.name: "批量逐笔聚合周期"
                    Accessible.description: root.fiveMinuteFile ? "5min 文件固定为 5 分钟" : "逐笔可选 1 分钟或 5 分钟"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: periodBox.activeFocus ? 2 : 1; border.color: periodBox.activeFocus ? root.theme.cyan : root.theme.border }
                    contentItem: Text { leftPadding: 8; text: root.fiveMinuteFile ? "5 分钟（固定）" : periodBox.displayText; verticalAlignment: Text.AlignVCenter; color: periodBox.enabled ? root.theme.textPrimary : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 150; spacing: 3
                Text { text: "5min volume 单位"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                ComboBox {
                    id: volumeUnitBox; Layout.fillWidth: true; Layout.preferredHeight: 34
                    model: [{ label: "单位未知", value: 0 }, { label: "股（原值）", value: 1 }, { label: "手（×100）", value: 2 }]
                    textRole: "label"; valueRole: "value"; enabled: root.fiveMinuteFile
                    Accessible.name: "批量 5min 成交量单位"
                    Accessible.description: root.fiveMinuteFile ? "必须显式确认；系统不会猜测" : "逐笔固定按手转股"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: volumeUnitBox.activeFocus ? 2 : 1; border.color: volumeUnitBox.activeFocus ? root.theme.cyan : root.theme.border }
                    contentItem: Text { leftPadding: 8; text: root.fiveMinuteFile ? volumeUnitBox.displayText : "逐笔：手→股"; verticalAlignment: Text.AlignVCenter; color: volumeUnitBox.enabled ? root.theme.textPrimary : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            PpButton {
                theme: root.theme; text: "生成导入计划"; visualWidth: 116
                available: !root.controller.tickDataBatchRunning
                           && !root.controller.tickDataImportRunning
                           && root.controller.tickDataImportRootDirectory.length > 0
                           && root.controller.tickDataImportSymbol.length > 0
                           && (!root.fiveMinuteFile || volumeUnitBox.currentValue !== 0)
                toolTip: "显式检查日期对应文件的存在性和大小；不解析内容、不扫描整个目录"
                onClicked: root.preparePlan()
            }
            PpButton {
                theme: root.theme; text: "开始串行导入"; visualWidth: 116; primary: true
                available: root.controller.tickDataBatchPlanReady
                           && !root.controller.tickDataBatchRunning
                           && !root.controller.tickDataImportRunning
                toolTip: "按计划逐文件解析，每个交易日独立 SQLite 事务"
                onClicked: root.controller.startTickDataBatchImport()
            }
            PpButton {
                theme: root.theme; text: "取消"; visualWidth: 62
                available: root.controller.tickDataBatchRunning
                toolTip: "取消当前文件；未开始文件保留，可稍后继续"
                onClicked: root.controller.cancelTickDataBatchImport()
            }
            PpButton {
                theme: root.theme; text: "继续失败项"; visualWidth: 102
                available: root.controller.tickDataBatchPlanReady
                           && !root.controller.tickDataBatchRunning
                           && !root.controller.tickDataImportRunning
                           && root.summary.runId
                toolTip: "重新检查失败、取消或此前缺失的确定性文件；成功项不重复解析"
                onClicked: root.controller.resumeTickDataBatchImport()
            }
            PpButton {
                theme: root.theme; text: "完整日跳过"; visualWidth: 92
                active: root.skipCompleteExisting
                available: !root.controller.tickDataBatchRunning
                toolTip: root.skipCompleteExisting
                         ? "计划时跳过本地已达到 240/48 根参考值的日期"
                         : "计划会保留已有数据日期，并明确标记可能覆盖"
                onClicked: root.skipCompleteExisting = !root.skipCompleteExisting
            }
            Text {
                Layout.fillWidth: true
                text: (root.controller.tickDataImportInstrumentTitle || "未选择 A 股")
                      + " · " + (root.controller.tickDataImportRootDirectory ? "目录已确认" : "目录未确认")
                color: root.controller.tickDataImportRootDirectory.length > 0 ? root.theme.cyan : root.theme.yellow
                font.family: root.theme.fontFamily; font.pixelSize: 9; elide: Text.ElideRight
                Accessible.name: text
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 7; radius: 3
            color: root.theme.track
            Rectangle { width: parent.width * Math.max(0, Math.min(1, root.controller.tickDataBatchProgress)); height: parent.height; radius: 3; color: root.theme.cyan }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 14
            Text { Layout.fillWidth: true; text: root.controller.tickDataBatchStatusText || "尚未生成计划；页面打开不会检查目录"; color: root.controller.tickDataBatchRunning ? root.theme.yellow : root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap; Accessible.name: text }
            Text { text: root.controller.tickDataBatchRunning ? "当前 " + root.controller.tickDataBatchCurrentDate : "进度 " + (root.controller.tickDataBatchProgress * 100).toFixed(0) + "%"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
        }

        GridLayout {
            Layout.fillWidth: true; columns: width >= 920 ? 9 : 5; columnSpacing: 10
            Text { text: "总计 " + (root.summary.totalFiles || 0); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "完成 " + (root.summary.completedFiles || 0); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "成功 " + (root.summary.succeededFiles || 0); color: root.theme.green; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "失败 " + (root.summary.failedFiles || 0); color: root.theme.red; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "缺失 " + (root.summary.missingFiles || 0); color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "质量警告 " + (root.summary.qualityWarningFiles || 0); color: root.theme.yellow; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "跳过完整 " + (root.summary.skippedExistingFiles || 0); color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "可能覆盖 " + (root.summary.overlapFiles || 0); color: (root.summary.overlapFiles || 0) > 0 ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            Text { text: "写入/更新 " + (root.summary.writtenBars || 0) + " 根"; color: root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 9 }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 520
                radius: 7; color: root.theme.bgInput; border.color: root.theme.border
                Text { x: 10; y: 8; text: "文件计划（只显示确定性日期路径）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                ListView {
                    x: 10; y: 30; width: parent.width - 20; height: parent.height - 38
                    model: root.controller.tickDataBatchPlan
                    clip: true; spacing: 2; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 25; radius: 4
                        color: modelData.status === "running" ? root.theme.bgSelected : "transparent"
                        Row {
                            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 8
                            Text { width: 72; anchors.verticalCenter: parent.verticalCenter; text: modelData.tradingDate; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { width: 62; anchors.verticalCenter: parent.verticalCenter; text: modelData.status; color: modelData.status === "succeeded" ? root.theme.green : modelData.status === "failed" ? root.theme.red : modelData.status === "missing" ? root.theme.yellow : root.theme.cyan; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { width: 78; anchors.verticalCenter: parent.verticalCenter; text: modelData.fileSize > 0 ? (modelData.fileSize / 1024).toFixed(1) + " KB" : "--"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { width: 116; anchors.verticalCenter: parent.verticalCenter; text: "本地 " + modelData.existingBars + "/" + modelData.expectedBars; color: modelData.willOverwrite ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { width: 100; anchors.verticalCenter: parent.verticalCenter; text: modelData.willOverwrite ? "将 UPSERT 覆盖" : modelData.coverageStatus; color: modelData.willOverwrite ? root.theme.yellow : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                            Text { width: Math.max(80, parent.width - 500); anchors.verticalCenter: parent.verticalCenter; text: modelData.error || modelData.filePath; color: modelData.error && modelData.status !== "skipped_existing" ? root.theme.red : root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideMiddle }
                        }
                        Accessible.name: modelData.tradingDate + " " + modelData.status + " " + (modelData.error || "")
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: Math.max(260, parent.width * 0.27)
                Layout.fillHeight: true; radius: 7
                color: root.theme.bgInput; border.color: root.theme.border
                Text { x: 10; y: 8; text: "任务摘要与失败日恢复（不保存路径）"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold }
                Row {
                    x: 10; y: 28; width: parent.width - 20; height: 28; spacing: 5
                    TextField {
                        id: historySymbolFilter; width: 72; height: 28; placeholderText: "代码"
                        color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8
                        inputMask: "000000"; Accessible.name: "导入历史代码筛选"
                        background: Rectangle { radius: 4; color: root.theme.bgCanvas; border.color: historySymbolFilter.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    ComboBox {
                        id: historyStatusFilter; width: 112; height: 28
                        model: [{label: "全部状态", value: ""}, {label: "有错误", value: "completed_with_errors"}, {label: "已取消", value: "cancelled"}, {label: "已完成", value: "completed"}]
                        textRole: "label"; valueRole: "value"; Accessible.name: "导入历史状态筛选"
                        contentItem: Text { leftPadding: 6; text: historyStatusFilter.displayText; verticalAlignment: Text.AlignVCenter; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 8 }
                        background: Rectangle { radius: 4; color: root.theme.bgCanvas; border.color: historyStatusFilter.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    PpButton {
                        theme: root.theme; text: "筛选"; visualWidth: 54; visualHeight: 28
                        toolTip: "只查询 SQLite 批次摘要，不检查目录或访问网络"
                        onClicked: {
                            root.selectedRunId = ""
                            root.controller.refreshTickDataImportRunHistory(
                                        historySymbolFilter.text,
                                        historyStatusFilter.currentValue)
                        }
                    }
                }
                ListView {
                    x: 10; y: 62; width: parent.width - 20; height: parent.height - 104
                    model: root.controller.tickDataImportRunHistory
                    clip: true; spacing: 4; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool selected: root.selectedRunId === modelData.runId
                        width: ListView.view.width; height: 38; radius: 4
                        color: selected ? root.theme.bgSelected : "transparent"
                        border.width: activeFocus || selected ? 1 : 0
                        border.color: root.theme.cyan
                        activeFocusOnTab: true; Accessible.role: Accessible.ListItem
                        Accessible.name: summaryText.text
                        Text {
                            id: summaryText; anchors.fill: parent; anchors.margins: 4
                            text: modelData.symbol + " · " + modelData.startDate + " → " + modelData.endDate
                                  + "\n" + modelData.status + " · 成功 " + modelData.succeededFiles
                                  + " · 失败 " + modelData.failedFiles + " · 缺失 " + modelData.missingFiles
                            color: modelData.failedFiles > 0 || modelData.missingFiles > 0 ? root.theme.yellow : root.theme.textMuted
                            font.family: root.theme.fontFamily; font.pixelSize: 8; elide: Text.ElideRight
                        }
                        MouseArea { anchors.fill: parent; onClicked: { parent.forceActiveFocus(); root.selectedRunId = parent.modelData.runId } }
                        Keys.onSpacePressed: function(event) { root.selectedRunId = modelData.runId; event.accepted = true }
                        Keys.onReturnPressed: function(event) { root.selectedRunId = modelData.runId; event.accepted = true }
                    }
                }
                PpButton {
                    x: 10; y: parent.height - 36; theme: root.theme
                    text: "载入失败日期"; visualWidth: 104; visualHeight: 28
                    available: root.selectedRunId.length > 0
                               && !root.controller.tickDataBatchRunning
                               && root.controller.tickDataImportRootDirectory.length > 0
                    toolTip: available
                             ? "从 SQLite 恢复逐日状态；成功日期保留，路径按当前确认目录重建"
                             : "先选择历史任务并重新确认本地根目录"
                    onClicked: root.controller.restoreTickDataBatchRetryPlan(
                                   root.selectedRunId, root.skipCompleteExisting)
                }
            }
        }

        Text {
            Layout.fillWidth: true; visible: root.controller.tickDataBatchError.length > 0
            text: root.controller.tickDataBatchError; color: root.theme.red
            font.family: root.theme.fontFamily; font.pixelSize: 9; wrapMode: Text.Wrap
            Accessible.name: text
        }
    }
}
