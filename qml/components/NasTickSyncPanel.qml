// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var theme
    required property var controller

    ColumnLayout {
        anchors.fill: parent
        spacing: 9

        GridLayout {
            Layout.fillWidth: true
            columns: width >= 1160 ? 4 : 2
            columnSpacing: 10; rowSpacing: 7

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 300; spacing: 4
                Text { text: "1. NAS 数据节点"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    TextField {
                        id: endpointField
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: root.controller.nasDataNodeEndpoint
                        placeholderText: "https://nas.example:8765/"
                        color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        selectByMouse: true; activeFocusOnTab: true
                        Accessible.name: "NAS 数据节点地址"
                        Accessible.description: "局域网默认要求 HTTPS；地址不能包含用户名或密码"
                        onAccepted: root.controller.configureNasDataNode(text)
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: endpointField.activeFocus ? 2 : 1; border.color: endpointField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    PpButton {
                        theme: root.theme; text: "应用"; visualWidth: 58
                        available: !root.controller.nasDataNodeLoading
                                   && !root.controller.nasDataNodeSyncRunning
                        toolTip: "只保存端点；令牌仅从 PPTRADER_NAS_TOKEN 读取"
                        onClicked: root.controller.configureNasDataNode(endpointField.text)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 170; spacing: 4
                Text { text: "2. A 股代码"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                TextField {
                    id: symbolField
                    Layout.fillWidth: true; Layout.preferredHeight: 34
                    text: root.controller.selectedLiveSymbol.length === 6
                          ? root.controller.selectedLiveSymbol : "600519"
                    validator: RegularExpressionValidator { regularExpression: /[0-9]{6}/ }
                    color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 10
                    selectByMouse: true; activeFocusOnTab: true
                    Accessible.name: "NAS Tick A 股代码"
                    background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: symbolField.activeFocus ? 2 : 1; border.color: symbolField.activeFocus ? root.theme.cyan : root.theme.border }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 240; spacing: 4
                Text { text: "3. catalog 日期范围（最多 367 天）"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 5
                    TextField {
                        id: startDateField; Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: Qt.formatDate(new Date(), "yyyy-MM-dd")
                        inputMask: "0000-00-00"; color: root.theme.textPrimary
                        font.family: root.theme.fontFamily; font.pixelSize: 9
                        Accessible.name: "NAS catalog 起始日期"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: startDateField.activeFocus ? 2 : 1; border.color: startDateField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                    TextField {
                        id: endDateField; Layout.fillWidth: true; Layout.preferredHeight: 34
                        text: startDateField.text; inputMask: "0000-00-00"
                        color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9
                        Accessible.name: "NAS catalog 结束日期"
                        background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: endDateField.activeFocus ? 2 : 1; border.color: endDateField.activeFocus ? root.theme.cyan : root.theme.border }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 210; spacing: 4
                Text { text: "4. 查询与同步"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    PpButton {
                        theme: root.theme; text: root.controller.nasDataNodeLoading ? "查询中" : "查询 catalog"
                        visualWidth: 104
                        available: !root.controller.nasDataNodeLoading
                                   && !root.controller.nasDataNodeSyncRunning
                                   && endpointField.text.length > 0
                        toolTip: "只查询指定代码与日期范围，不扫描整个 NAS"
                        onClicked: root.controller.requestNasTickCatalog(
                            symbolField.text, startDateField.text, endDateField.text)
                    }
                    PpButton {
                        theme: root.theme; text: "同步选中"
                        visualWidth: 86
                        available: !root.controller.nasDataNodeLoading
                                   && !root.controller.nasDataNodeSyncRunning
                                   && catalogBox.currentIndex >= 0
                        toolTip: "显式下载选中交易日，校验 SHA-256 后事务写入本地 SQLite"
                        onClicked: root.controller.syncNasTickDay(
                            symbolField.text, catalogBox.currentValue)
                    }
                    PpButton {
                        theme: root.theme; text: "同步全部"
                        visualWidth: 86
                        available: !root.controller.nasDataNodeLoading
                                   && !root.controller.nasDataNodeSyncRunning
                                   && root.controller.nasDataNodeCatalog.length > 0
                        toolTip: "显式串行同步当前 catalog 的全部交易日；单日失败不影响其他日期"
                        onClicked: root.controller.syncNasTickRange(symbolField.text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    PpButton {
                        theme: root.theme; text: "取消"; visualWidth: 68
                        available: root.controller.nasDataNodeSyncRunning
                        toolTip: "取消当前下载或导入；已成功日期保持不变"
                        onClicked: root.controller.cancelNasTickSync()
                    }
                    PpButton {
                        theme: root.theme; text: "重试未完成"; visualWidth: 96
                        available: !root.controller.nasDataNodeSyncRunning
                                   && root.controller.nasDataNodeSyncCanResume
                        toolTip: "只重试失败或取消的交易日，不重复成功日期"
                        onClicked: root.controller.resumeNasTickSync()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text {
                text: "多代码 manifest"
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
            }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 56
                clip: true
                TextArea {
                    id: manifestField
                    text: root.controller.selectedLiveSymbol.length === 6
                          ? root.controller.selectedLiveSymbol : "600519"
                    placeholderText: "显式输入 A 股代码，以空格、逗号或换行分隔；最多 6000 个"
                    wrapMode: TextEdit.WrapAnywhere
                    selectByMouse: true; activeFocusOnTab: true
                    color: root.theme.textPrimary; placeholderTextColor: root.theme.textMuted
                    font.family: root.theme.fontFamily; font.pixelSize: 9
                    Accessible.name: "NAS 受控 manifest A 股代码清单"
                    Accessible.description: "只有点击查询 manifest 才会联网，不扫描未列出的代码"
                    background: Rectangle {
                        radius: 6; color: root.theme.bgCanvas
                        border.width: manifestField.activeFocus ? 2 : 1
                        border.color: manifestField.activeFocus ? root.theme.cyan : root.theme.border
                    }
                }
            }
            PpButton {
                theme: root.theme; text: root.controller.nasDataNodeLoading ? "查询中" : "查询 manifest"
                visualWidth: 108
                available: !root.controller.nasDataNodeLoading
                           && !root.controller.nasDataNodeSyncRunning
                           && endpointField.text.length > 0
                           && manifestField.text.trim().length > 0
                toolTip: "只检查显式代码清单与最多 31 天日期范围"
                onClicked: root.controller.requestNasTickManifest(
                    manifestField.text, startDateField.text, endDateField.text)
            }
            PpButton {
                theme: root.theme; text: "同步 manifest"; visualWidth: 104
                available: !root.controller.nasDataNodeLoading
                           && !root.controller.nasDataNodeSyncRunning
                           && root.controller.nasDataNodeManifestSummary.entries > 0
                toolTip: "当前固定单文件串行导入；每个代码交易日独立事务"
                onClicked: root.controller.syncNasTickManifest()
            }
        }

        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Text { text: "可用交易日"; color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9 }
            ComboBox {
                id: catalogBox
                Layout.preferredWidth: 210; Layout.preferredHeight: 34
                model: root.controller.nasDataNodeCatalog
                textRole: "tradingDate"; valueRole: "tradingDate"
                Accessible.name: "NAS 可用 Tick 交易日"
                contentItem: Text { leftPadding: 10; verticalAlignment: Text.AlignVCenter; text: catalogBox.displayText || "--"; color: root.theme.textPrimary; font.family: root.theme.fontFamily; font.pixelSize: 9 }
                background: Rectangle { radius: 6; color: root.theme.bgCanvas; border.width: catalogBox.activeFocus ? 2 : 1; border.color: catalogBox.activeFocus ? root.theme.cyan : root.theme.border }
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.nasDataNodeStatusText
                      + (root.controller.nasDataNodeManifestSummary.requestedSymbols > 1
                         ? " · manifest "
                           + root.controller.nasDataNodeManifestSummary.availableSymbols
                           + "/" + root.controller.nasDataNodeManifestSummary.requestedSymbols
                           + " · " + root.controller.nasDataNodeManifestSummary.entries + " 文件"
                           + " · " + root.controller.nasDataNodeManifestSummary.elapsedMilliseconds + " ms"
                         : "")
                      + (root.controller.nasDataNodeSyncRunning
                         ? " · " + root.controller.nasDataNodeSyncCurrentSymbol
                           + " · " + root.controller.nasDataNodeSyncCurrentDate
                           + " · " + (root.controller.nasDataNodeSyncProgress * 100).toFixed(1) + "% · "
                           + root.controller.nasDataNodeReceivedBytes
                           + "/" + root.controller.nasDataNodeExpectedBytes + " bytes" : "")
                color: root.controller.nasDataNodeError.length > 0
                       ? root.theme.red : root.theme.textMuted
                font.family: root.theme.fontFamily; font.pixelSize: 9
                elide: Text.ElideRight
                Accessible.name: "NAS 数据同步状态"
                Accessible.description: text
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.controller.nasDataNodeError.length > 0
                  ? root.controller.nasDataNodeError
                  : "单代码 catalog 最多 367 天；多代码 manifest 最多 6000 个代码、31 天，固定串行事务导入。打开页面和选择日期不会自动联网。"
            color: root.controller.nasDataNodeError.length > 0 ? root.theme.red : root.theme.yellow
            font.family: root.theme.fontFamily; font.pixelSize: 9
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            PpButton {
                theme: root.theme; text: root.controller.nasDataNodeLoading ? "检查中" : "检查节点更新"
                visualWidth: 108
                available: !root.controller.nasDataNodeLoading
                           && !root.controller.nasDataNodeSyncRunning
                           && root.controller.nasDataNodeEndpoint.length > 0
                toolTip: "只读取 NAS 日期目录层变更摘要；不下载、不自动同步"
                onClicked: root.controller.requestNasNodeChanges()
            }
            PpButton {
                theme: root.theme; text: "刷新缓存统计"; visualWidth: 96
                available: !root.controller.nasDataNodeSyncRunning
                toolTip: "只扫描应用 NAS 暂存目录的日期/代码 CSV，不访问网络"
                onClicked: root.controller.refreshNasTickCacheSummary()
            }
            PpButton {
                theme: root.theme; text: "清理 30 天前"; visualWidth: 96
                available: !root.controller.nasDataNodeSyncRunning
                           && !root.controller.nasDataNodeLoading
                toolTip: "显式删除 NAS 暂存目录中超过 30 天的代码 CSV；不会删除 SQLite 数据"
                onClicked: root.controller.pruneNasTickCache(30)
            }
            Text {
                Layout.fillWidth: true
                text: {
                    const changes = root.controller.nasNodeChangesSummary
                    const changeText = changes && changes.totalDirectories
                        ? " · 节点 " + changes.totalDirectories + " 目录"
                          + (changes.changedDates && changes.changedDates.length > 0
                             ? " · 变更 " + changes.changedDates.length
                               + "（最近 "
                               + Qt.formatDateTime(
                                     new Date(changes.latestDirectoryAt), "MM-dd HH:mm")
                               + "）" : " · 无新变更")
                        : ""
                    return "暂存 " + (root.controller.nasDataNodeCacheSummary.files || 0)
                      + " 文件 · " + (root.controller.nasDataNodeCacheSummary.bytes || 0)
                      + " bytes · 本轮 SHA 复用 "
                      + (root.controller.nasDataNodeSyncSummary.cacheHits || 0)
                      + (root.controller.nasDataNodePlanRecovered ? " · 已恢复上次计划" : "")
                      + changeText
                }
                color: root.theme.textMuted; font.family: root.theme.fontFamily; font.pixelSize: 9
                elide: Text.ElideRight
                Accessible.name: "NAS 暂存缓存与恢复状态"
                Accessible.description: text
            }
        }
    }
}
