// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller
    readonly property var importState: controller.authorizedFactorImportState || ({})
    readonly property bool busy: Boolean(importState.running)
    readonly property var researchState: controller.externalFactorResearchState || ({})
    readonly property bool researchBusy: Boolean(researchState.running)
    readonly property var preflightState: controller.externalFactorResearchPreflightState || ({})
    readonly property bool preflightBusy: Boolean(preflightState.running)
    readonly property var researchArchive: controller.selectedExternalFactorResearchArchive || ({})

    function datasetIsBound(datasetId) {
        const bindings = controller.selectedFactorCandidateDatasetBindings || []
        for (let index = 0; index < bindings.length; ++index) {
            if (String(bindings[index].datasetId || "") === String(datasetId))
                return true
        }
        return false
    }

    function preflightMatches(datasetId) {
        return Boolean(preflightState.ready)
                && String(preflightState.candidateId || "")
                   === String(controller.selectedFactorCandidateId || "")
                && Number(preflightState.candidateVersion || 0)
                   === Number(controller.selectedFactorCandidateVersion || 0)
                && String(preflightState.datasetId || "") === String(datasetId)
                && Number(preflightState.periodSeconds || 0)
                   === Number(researchPeriod.currentValue)
                && Number(preflightState.minimumPairs || 0) === 3
                && Number(preflightState.oneWayCostBps || 0) === 0
    }

    FileDialog {
        id: csvDialog
        title: "选择用户授权的外部因子 CSV"
        nameFilters: ["CSV 文件 (*.csv)", "所有文件 (*)"]
        onAccepted: root.controller.setAuthorizedFactorImportFile(
                        String(selectedFile))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 7

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 900 ? 4 : 2
            columnSpacing: 8
            rowSpacing: 6

            TextField {
                id: factorKeyField
                Layout.fillWidth: true
                placeholderText: "因子键，例如 quality.score"
                color: root.theme.textPrimary
                placeholderTextColor: root.theme.textMuted
                selectByMouse: true
                Accessible.name: "授权外部因子键"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.color: factorKeyField.activeFocus
                                  ? root.theme.cyan : root.theme.border
                }
            }
            TextField {
                id: sourceVersionField
                Layout.fillWidth: true
                placeholderText: "来源版本"
                color: root.theme.textPrimary
                placeholderTextColor: root.theme.textMuted
                selectByMouse: true
                Accessible.name: "授权数据来源版本"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.color: sourceVersionField.activeFocus
                                  ? root.theme.cyan : root.theme.border
                }
            }
            TextField {
                id: sourceReferenceField
                Layout.fillWidth: true
                placeholderText: "授权来源引用或研究备忘录"
                color: root.theme.textPrimary
                placeholderTextColor: root.theme.textMuted
                selectByMouse: true
                Accessible.name: "授权数据来源引用"
                background: Rectangle {
                    radius: 5; color: root.theme.bgInput
                    border.color: sourceReferenceField.activeFocus
                                  ? root.theme.cyan : root.theme.border
                }
            }
            PpButton {
                theme: root.theme
                text: "选择 CSV"
                visualWidth: 84
                available: !root.busy
                toolTip: "选择本地或已挂载盘符文件；选择本身不会读取"
                Accessible.name: "选择授权外部因子 CSV"
                onClicked: csvDialog.open()
            }
        }

        Text {
            Layout.fillWidth: true
            text: String(root.importState.filePath || "尚未选择文件")
            color: root.theme.textMuted
            elide: Text.ElideMiddle
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            PpButton {
                theme: root.theme
                text: "异步预览"
                visualWidth: 78
                available: !root.busy
                           && String(root.importState.filePath || "").length > 0
                           && root.controller.selectedFactorCandidateId.length > 0
                           && factorKeyField.text.trim().length > 0
                           && sourceVersionField.text.trim().length > 0
                           && sourceReferenceField.text.trim().length > 0
                toolTip: available
                         ? "逐行读取并校验，不写入 SQLite"
                         : "先选择已冻结候选、文件并填写三项数据身份"
                Accessible.name: "异步预览授权因子数据"
                onClicked: root.controller.previewAuthorizedFactorImport(
                               factorKeyField.text,
                               sourceVersionField.text,
                               sourceReferenceField.text)
            }
            PpButton {
                theme: root.theme
                text: "确认导入"
                visualWidth: 78
                available: !root.busy && Boolean(root.importState.previewReady)
                toolTip: available
                         ? "重新校验文件 SHA 后事务写入 SQLite"
                         : "必须先完成同一文件和元数据的预览"
                Accessible.name: "确认导入授权因子数据"
                onClicked: root.controller.commitAuthorizedFactorImport(
                               factorKeyField.text,
                               sourceVersionField.text,
                               sourceReferenceField.text)
            }
            PpButton {
                theme: root.theme
                text: "取消"
                visualWidth: 58
                available: root.busy
                toolTip: available ? "请求取消当前逐行任务" : "当前没有导入任务"
                Accessible.name: "取消授权因子导入"
                onClicked: root.controller.cancelAuthorizedFactorImport()
            }
            Text {
                Layout.fillWidth: true
                text: String(root.importState.statusText || "--")
                color: String(root.importState.error || "").length > 0
                       ? root.theme.red
                       : (root.busy ? root.theme.cyan : root.theme.textMuted)
                elide: Text.ElideRight
                font.family: root.theme.fontFamily
                font.pixelSize: 8
                Accessible.name: text
            }
            Text {
                text: "有效 " + Number(root.importState.validRecords || 0)
                      + " · 重复 " + Number(root.importState.duplicateRecords || 0)
                      + " · 写入 " + Number(root.importState.writtenRecords || 0)
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
                Accessible.name: text
            }
        }

        Text {
            Layout.fillWidth: true
            visible: String(root.importState.error || "").length > 0
            text: String(root.importState.error || "")
            color: root.theme.red
            wrapMode: Text.Wrap
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: "授权因子导入错误：" + text
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.externalFactorDatasetCatalog
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                required property var modelData
                width: 350
                height: ListView.view.height - 4
                radius: 6
                color: root.theme.bgInput
                border.color: root.theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 5
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: modelData.factorKey + " · " + modelData.sourceVersion
                            color: root.theme.textPrimary
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 9
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.recordCount + " 条 · "
                                  + modelData.symbolCount + " 标的 · "
                                  + modelData.timestampCount + " 时刻"
                            color: root.theme.textMuted
                            elide: Text.ElideRight
                            font.family: root.theme.fontFamily
                            font.pixelSize: 8
                        }
                    }
                    PpButton {
                        theme: root.theme
                        text: "绑定"
                        visualWidth: 52
                        available: root.controller.selectedFactorCandidateId.length > 0
                        toolTip: available
                                 ? "仅定义摘要一致的已冻结候选可绑定"
                                 : "请先选择已冻结候选版本"
                        Accessible.name: "绑定数据集 " + modelData.factorKey
                        onClicked: root.controller.bindSelectedExternalFactorDataset(
                                       modelData.datasetId)
                    }
                    PpButton {
                        theme: root.theme
                        text: "预检"
                        visualWidth: 52
                        available: !root.researchBusy && !root.preflightBusy
                                   && root.datasetIsBound(modelData.datasetId)
                        toolTip: available
                                 ? "异步核对全部标的的本地 Tick 派生完成 Bar；不归档"
                                 : (root.researchBusy || root.preflightBusy
                                    ? "已有外部因子任务正在运行"
                                    : "请先把该数据集绑定到当前已冻结候选")
                        Accessible.name: "预检数据集 " + modelData.factorKey + " 的本地数据覆盖"
                        onClicked: root.controller.preflightExternalFactorResearch(
                                       modelData.datasetId,
                                       Number(researchPeriod.currentValue), 3, 0.0)
                    }
                    PpButton {
                        theme: root.theme
                        text: "研究"
                        visualWidth: 52
                        available: !root.researchBusy && !root.preflightBusy
                                   && root.preflightMatches(modelData.datasetId)
                        toolTip: available
                                 ? "预检已通过；只读取本地 Tick 派生 K 线并归档"
                                 : "先对当前候选、数据集和收益周期完成预检"
                        Accessible.name: "运行数据集 " + modelData.factorKey + " 的外部因子研究"
                        onClicked: root.controller.startExternalFactorResearch(
                                       modelData.datasetId,
                                       Number(researchPeriod.currentValue), 3, 0.0)
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: String(root.preflightState.statusText || "--")
            color: String(root.preflightState.error || "").length > 0
                   ? root.theme.red
                   : (root.preflightBusy ? root.theme.cyan
                                         : (Boolean(root.preflightState.ready)
                                            ? root.theme.green : root.theme.textMuted))
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: "外部因子研究预检：" + text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            Text {
                text: "收益周期"
                color: root.theme.textMuted
                font.family: root.theme.fontFamily
                font.pixelSize: 8
            }
            ComboBox {
                id: researchPeriod
                Layout.preferredWidth: 96
                model: [
                    { label: "1 分钟", value: 60 },
                    { label: "5 分钟", value: 300 },
                    { label: "15 分钟", value: 900 },
                    { label: "30 分钟", value: 1800 }
                ]
                textRole: "label"
                valueRole: "value"
                Accessible.name: "外部因子研究收益 K 线周期"
                contentItem: Text {
                    leftPadding: 9
                    rightPadding: 22
                    text: researchPeriod.displayText
                    color: root.theme.textPrimary
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    font.family: root.theme.fontFamily
                    font.pixelSize: 9
                }
                indicator: Text {
                    x: researchPeriod.width - width - 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌄"
                    color: root.theme.textMuted
                    font.family: root.theme.fontFamily
                    font.pixelSize: 11
                }
                background: Rectangle {
                    radius: 5
                    color: root.theme.bgInput
                    border.width: researchPeriod.activeFocus ? 2 : 1
                    border.color: researchPeriod.activeFocus
                                  ? root.theme.cyan : root.theme.border
                }
            }
            PpButton {
                theme: root.theme
                text: "取消研究"
                visualWidth: 74
                available: root.researchBusy || root.preflightBusy
                toolTip: available ? "请求取消当前本地研究或预检任务" : "当前没有研究任务"
                Accessible.name: "取消外部因子研究"
                onClicked: root.controller.cancelExternalFactorResearch()
            }
            PpButton {
                theme: root.theme
                text: "刷新归档"
                visualWidth: 74
                available: !root.researchBusy
                toolTip: "只读取本地 SQLite 归档目录"
                Accessible.name: "刷新外部因子研究归档"
                onClicked: root.controller.refreshExternalFactorResearchArchives()
            }
            Text {
                Layout.fillWidth: true
                text: String(root.researchState.statusText || "--")
                color: String(root.researchState.error || "").length > 0
                       ? root.theme.red
                       : (root.researchBusy ? root.theme.cyan : root.theme.textMuted)
                elide: Text.ElideRight
                font.family: root.theme.fontFamily
                font.pixelSize: 8
                Accessible.name: text
            }
        }

        Text {
            Layout.fillWidth: true
            visible: String(root.researchState.error || "").length > 0
            text: String(root.researchState.error || "")
            color: root.theme.red
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: "外部因子研究错误：" + text
        }

        Text {
            Layout.fillWidth: true
            readonly property var summary: root.researchArchive.summary || ({})
            text: String(root.researchArchive.resultId || "").length > 0
                  ? "结果 " + String(root.researchArchive.resultId).slice(0, 8)
                    + " · " + Number(summary.evaluatedFrameCount || 0) + " 帧"
                    + " · IC " + Number(summary.meanIc || 0).toFixed(4)
                    + " · Rank IC " + Number(summary.meanRankIc || 0).toFixed(4)
                    + " · 本地收益身份 "
                    + Number((root.researchArchive.returnDatasetIdentities || []).length)
                  : "尚未选择外部因子研究结果"
            color: String(root.researchArchive.resultId || "").length > 0
                   ? root.theme.green : root.theme.textMuted
            elide: Text.ElideRight
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: text
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            orientation: ListView.Horizontal
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.externalFactorResearchArchiveCatalog
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: PpButton {
                required property var modelData
                theme: root.theme
                text: String(modelData.factorKey) + " · "
                      + Number(modelData.evaluatedFrameCount) + " 帧"
                visualWidth: 190
                available: !root.researchBusy
                toolTip: "从本地 SQLite 加载不可变历史研究快照；不会联网或重算"
                Accessible.name: "加载外部因子研究归档 " + String(modelData.resultId)
                onClicked: root.controller.loadExternalFactorResearchArchive(
                               modelData.resultId)
            }
        }

        Text {
            Layout.fillWidth: true
            text: "当前候选已绑定 "
                  + root.controller.selectedFactorCandidateDatasetBindings.length
                  + " 个不可变数据集 · 研究必须显式启动且仅使用本地 Tick 派生数据"
            color: root.theme.textMuted
            font.family: root.theme.fontFamily
            font.pixelSize: 8
            Accessible.name: text
        }
    }
}
