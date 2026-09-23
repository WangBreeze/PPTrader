// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: window
    width: 1600
    height: 1000
    minimumWidth: 1120
    minimumHeight: 760
    visible: true
    title: "PPTrader · 量化交易工作台"
    color: theme.bgCanvas
    font.family: theme.fontFamily

    property var controller: tradingController
    readonly property var appTheme: theme
    property int lastStrategySubPage: 0
    property var realtimeChildWindows: ({})

    function activateWorkspace(index) {
        workspaceTabs.currentIndex = index
        applyWorkspaceRoute(index)
    }

    function showBatchBacktestPanel(ignored) {
        activateWorkspace(3)
        strategyPage.showBatchBacktestPanel()
    }

    function applyWorkspaceRoute(index) {
        if (index === 2) {
            strategyPage.subPage = window.lastStrategySubPage
        } else if (index === 3) {
            strategyPage.strategyMode = 0
            strategyPage.subPage = 0
        } else if (index === 4) {
            strategyPage.subPage = 5
        }
    }

    function syncRealtimeChartWindows() {
        const model = window.controller.realtimeChartWindowModel
        const activeIds = ({})
        for (let row = 0; row < model.count; ++row) {
            const entry = model.get(row)
            const windowId = entry.windowId
            activeIds[windowId] = true
            if (!window.realtimeChildWindows[windowId]) {
                const child = realtimeWindowComponent.createObject(null, {
                    "theme": window.appTheme,
                    "controller": window.controller,
                    "sessionObject": entry.sessionObject,
                    "windowOrdinal": row,
                    "anchorPositionX": window.x + 36,
                    "anchorPositionY": window.y + 42
                })
                if (child)
                    window.realtimeChildWindows[windowId] = child
            }
        }
        for (const windowId in window.realtimeChildWindows) {
            if (activeIds[windowId]) continue
            const child = window.realtimeChildWindows[windowId]
            delete window.realtimeChildWindows[windowId]
            if (child) {
                child.closeRequested = true
                child.destroy(1)
            }
        }
    }

    Component.onCompleted: window.syncRealtimeChartWindows()
    Component.onDestruction: {
        for (const windowId in window.realtimeChildWindows) {
            const child = window.realtimeChildWindows[windowId]
            if (child) child.destroy()
        }
        window.realtimeChildWindows = ({})
    }

    PPTheme { id: theme }

    AppTopBar {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        theme: window.appTheme
        controller: window.controller
    }

    TabBar {
        id: workspaceTabs
        x: window.width < 1280 ? 190 : 250
        y: 13
        width: 466
        height: 38
        spacing: 8
        currentIndex: 0
        background: Item {}
        onCurrentIndexChanged: window.applyWorkspaceRoute(currentIndex)

        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 0
            text: "主页"; width: 76
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 1
            text: "市场快照"; width: 90
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 2
            text: "指标与策略"; width: 104
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 3
            text: "回测任务"; width: 82
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 4
            text: "执行中心"; width: 82
        }
    }

    // 左侧二级分类抽屉：仅「指标与策略」显示；回测与执行已提升为一级入口。
    Rectangle {
        id: subNav
        width: workspaceTabs.currentIndex === 2 ? 318 : 0
        anchors.left: parent.left
        anchors.top: topBar.bottom
        anchors.bottom: statusBar.top
        color: theme.bgBar
        border.color: theme.border
        clip: true

        Text {
            x: 20; y: 20; text: "策略研究"
            color: theme.textPrimary
            font.family: theme.fontFamily; font.pixelSize: 15; font.weight: Font.Bold
        }
        Text {
            x: 20; y: 46; text: "指标与策略 · 二级导航"
            color: theme.textMuted
            font.family: theme.fontFamily; font.pixelSize: 10
        }

        Column {
            x: 12; y: 84; width: 294; spacing: 2

            Text {
                width: parent.width; height: 28
                text: "研究与构建"; color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
            SubNavItem { label: "回测研究"; target: 0 }
            SubNavItem { label: "自定义策略"; target: 1 }
            SubNavItem { label: "组合回测"; target: 2 }
            SubNavItem { label: "多股回测"; target: 3 }
            SubNavItem { label: "参数网格扫描"; target: 4 }

            Text {
                width: parent.width; height: 28
                text: "运行与治理"; color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
            SubNavItem { label: "插件管理"; target: 7 }

            Text {
                width: parent.width; height: 28
                text: "数据管理"; color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
            SubNavItem { label: "数据导入与同步"; target: 6 }
        }
    }

    StackLayout {
        id: workspaceStack
        anchors.left: subNav.right
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: statusBar.top
        currentIndex: workspaceTabs.currentIndex <= 1
                      ? workspaceTabs.currentIndex : 2

        DashboardPage {
            id: dashboardPage
            theme: window.appTheme
            controller: window.controller
            pageActive: workspaceStack.currentIndex === 0
            onBacktestWorkspaceRequested: window.activateWorkspace(3)
        }

        MarketSnapshotPage {
            id: snapshotPage
            theme: window.appTheme
            controller: window.controller
            pageActive: workspaceStack.currentIndex === 1
        }

        StrategyResearchPage {
            id: strategyPage
            theme: window.appTheme
            controller: window.controller
            pageHeading: workspaceTabs.currentIndex === 3 ? "回测任务"
                         : workspaceTabs.currentIndex === 4 ? "执行中心"
                         : "指标与策略"
            pageDescription: workspaceTabs.currentIndex === 3
                ? "严格使用 SQLite 历史 Tick 数据，统一查看任务、结果、成交与归档"
                : workspaceTabs.currentIndex === 4
                  ? "模拟执行、人工审批、Candle 回放与通知设置；不连接真实券商"
                  : "内置与自定义策略统一使用 SQLite 中的历史 Tick 派生 K 线；运行回测不会访问网络 Provider"
            pageContextText: workspaceTabs.currentIndex === 3
                ? "任务化回测入口复用既有引擎和结果模型，切换页面不会中断运行中的任务。"
                : workspaceTabs.currentIndex === 4
                  ? "执行中心当前仅覆盖模拟执行和审批；真实券商下单保持禁用。"
                  : ""
        }
    }

    Connections {
        target: window.controller
        function onInstrumentResearchRequested(symbol) {
            window.activateWorkspace(2)
        }
    }

    // 使用动态顶层 Window，而不是主窗口内嵌页面；每个窗口仍由 Controller 会话托管。
    Component {
        id: realtimeWindowComponent
        RealtimeChartWindow {}
    }

    Connections {
        target: window.controller
        function onRealtimeChartWindowsChanged() {
            window.syncRealtimeChartWindows()
        }
    }

    Connections {
        target: strategyPage
        function onSubPageChanged() {
            if (workspaceTabs.currentIndex === 2)
                window.lastStrategySubPage = strategyPage.subPage
        }
    }

    AppStatusBar {
        id: statusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        theme: window.appTheme
        controller: window.controller
        renderMilliseconds: snapshotPage.renderMilliseconds
    }

    // 二级导航菜单项：点击设置策略页 subPage 索引（仅改入口跳转，不改面板逻辑）。
    component SubNavItem: Rectangle {
        id: item
        property string label: ""
        property int target: 0
        readonly property bool active: strategyPage.subPage === target
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.description: "切换到" + label
        Accessible.focusable: true

        function activate() {
            if (item.target === 1 || item.target === 4)
                strategyPage.strategyMode = 1
            strategyPage.subPage = item.target
        }

        width: 294; height: 34; radius: 7
        color: active ? theme.bgSelected
                      : (itemHover.containsMouse ? theme.bgRaised : "transparent")
        border.width: item.activeFocus ? 2 : active ? 1 : 0
        border.color: item.activeFocus || active ? theme.cyan : "transparent"

        Keys.onReturnPressed: function(event) {
            item.activate()
            event.accepted = true
        }
        Keys.onSpacePressed: function(event) {
            item.activate()
            event.accepted = true
        }

        Text {
            x: 14; anchors.verticalCenter: parent.verticalCenter
            text: item.label
            color: item.active ? theme.cyan : theme.textPrimary
            font.family: theme.fontFamily
            font.pixelSize: 11
            font.weight: item.active ? Font.Bold : Font.Medium
        }
        MouseArea {
            id: itemHover
            anchors.fill: parent
            hoverEnabled: true
            onPressed: item.forceActiveFocus(Qt.MouseFocusReason)
            onClicked: item.activate()
            cursorShape: Qt.PointingHandCursor
        }
    }
}
