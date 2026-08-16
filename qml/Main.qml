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
        width: 436
        height: 38
        spacing: 8
        currentIndex: 0
        background: Item {}

        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 0
            text: "主页面"; width: 104
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 1
            text: "指标与策略"; width: 132
        }
        WorkspaceTabButton {
            theme: window.appTheme; tabBar: workspaceTabs; tabIndex: 2
            text: "全市场时刻股价"; width: 184
        }
    }

    // 左侧二级分类抽屉：仅「指标与策略」tab 显示；主页/全市场无二级分类则收起。
    Rectangle {
        id: subNav
        width: workspaceTabs.currentIndex === 1 ? 318 : 0
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
                text: "策略执行"; color: theme.textMuted
                font.family: theme.fontFamily; font.pixelSize: 10; font.weight: Font.Bold
                verticalAlignment: Text.AlignVCenter
            }
            SubNavItem { label: "执行与审批"; target: 5 }

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
        currentIndex: workspaceTabs.currentIndex

        DashboardPage {
            theme: window.appTheme
            controller: window.controller
        }

        StrategyResearchPage {
            id: strategyPage
            theme: window.appTheme
            controller: window.controller
        }

        MarketSnapshotPage {
            id: snapshotPage
            theme: window.appTheme
            controller: window.controller
        }
    }

    Connections {
        target: window.controller
        function onInstrumentResearchRequested(symbol) {
            workspaceTabs.currentIndex = 1
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
        width: 294; height: 34; radius: 7
        color: active ? theme.bgSelected
                      : (itemHover.hovered ? theme.bgRaised : "transparent")
        border.width: active ? 1 : 0
        border.color: active ? theme.cyan : "transparent"

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
            onClicked: strategyPage.subPage = item.target
            cursorShape: Qt.PointingHandCursor
        }
    }
}
