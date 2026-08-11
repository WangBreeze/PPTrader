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

    StackLayout {
        id: workspaceStack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: topBar.bottom
        anchors.bottom: statusBar.top
        currentIndex: workspaceTabs.currentIndex

        DashboardPage {
            theme: window.appTheme
            controller: window.controller
        }

        StrategyResearchPage {
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
}
