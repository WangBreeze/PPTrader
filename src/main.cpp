#include "app/TradingController.h"
#include "app/RealtimeMultiWindowAcceptanceCoordinator.h"
#include "app/VisibleBacktestLongrunCoordinator.h"
#include "app/WindowPlacement.h"
#include "domain/MarketTypes.h"
#include "models/CandleListModel.h"
#ifdef PPTRADER_CONTRACT_TESTS
#include "app/WizardContractProbe.h"
#endif

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QWindow>
#include <QtGlobal>

#include <algorithm>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>

namespace {

// 无人值守长稳验收不需要辅助技术客户端。Qt 6.4 Windows UIA Provider 在
// 外部工具持续枚举大型 QML 可访问树时会保留 COM 包装对象，干扰进程内存
// 口径；正常启动不安装该过滤器，完整保留无障碍能力。
class UiAutomationIsolationFilter final : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &, void *message,
                           qintptr *result) override
    {
        const auto *nativeMessage = static_cast<const MSG *>(message);
        if (!nativeMessage || nativeMessage->message != WM_GETOBJECT) return false;
        *result = 0;
        return true;
    }
};

} // namespace
#endif

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    std::unique_ptr<UiAutomationIsolationFilter> uiAutomationIsolation;
    if (qEnvironmentVariableIsSet("PPTRADER_ACCEPTANCE_DISABLE_ACCESSIBILITY")) {
        uiAutomationIsolation = std::make_unique<UiAutomationIsolationFilter>();
        app.installNativeEventFilter(uiAutomationIsolation.get());
    }
#endif
    app.setQuitOnLastWindowClosed(true);
    QCoreApplication::setOrganizationName(QStringLiteral("PPTrader"));
    QCoreApplication::setApplicationName(QStringLiteral("PPTrader"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const QStringList startupArguments = QCoreApplication::arguments();
    const bool exitProbe = startupArguments.contains(QStringLiteral("--exit-probe"))
        || startupArguments.contains(QStringLiteral("--exit-probe-native"));
    std::unique_ptr<QTemporaryDir> exitProbeStorage;
    if (startupArguments.contains(QStringLiteral("--exit-probe"))
        || startupArguments.contains(QStringLiteral("--exit-probe-native"))
        || startupArguments.contains(QStringLiteral("--headless-live-probe"))) {
        exitProbeStorage = std::make_unique<QTemporaryDir>();
        if (!exitProbeStorage->isValid()) return -2;
        qputenv("PPTRADER_DATABASE_PATH",
                exitProbeStorage->filePath(QStringLiteral("exit-probe.db"))
                    .toLocal8Bit());
    }

    qRegisterMetaType<pp::Instrument>("pp::Instrument");
    qRegisterMetaType<pp::Tick>("pp::Tick");
    qRegisterMetaType<pp::Candle>("pp::Candle");
    qRegisterMetaType<pp::TradeSignal>("pp::TradeSignal");
    qRegisterMetaType<pp::CandlePoint>("pp::CandlePoint");
#ifdef PPTRADER_CONTRACT_TESTS
    if (startupArguments.contains(QStringLiteral("--wizard-contract-probe")))
        return runWizardContractProbe();
    if (startupArguments.contains(QStringLiteral("--paper-panel-probe")))
        return runPaperStrategyContractProbe();
    if (startupArguments.contains(QStringLiteral("--paper-flow-acceptance")))
        return runPaperFlowAcceptance();
#endif

    auto controller = std::make_unique<pp::TradingController>();
    QObject::connect(&app, &QGuiApplication::lastWindowClosed,
                     &app, &QCoreApplication::quit);
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     controller.get(), [controllerPtr = controller.get()]() {
        controllerPtr->shutdown();
    });
    if (startupArguments.contains(QStringLiteral("--headless-live-probe"))) {
        bool validDuration = false;
        const int requestedSeconds = qEnvironmentVariableIntValue(
            "PPTRADER_HEADLESS_PROBE_SECONDS", &validDuration);
        const int durationSeconds = std::clamp(
            validDuration ? requestedSeconds : 180, 1, 3600);
        QTimer::singleShot(0, controller.get(),
                           &pp::TradingController::startLive);
        QTimer::singleShot(durationSeconds * 1000, &app,
                           &QCoreApplication::quit);
        const int exitCode = app.exec();
        controller.reset();
        return exitCode;
    }

    auto engine = std::make_unique<QQmlApplicationEngine>();
    engine->rootContext()->setContextProperty(QStringLiteral("tradingController"),
                                              controller.get());
    engine->load(QUrl(QStringLiteral("qrc:/PPTrader/qml/Main.qml")));

    if (engine->rootObjects().isEmpty()) {
        engine.reset();
        controller.reset();
        return -1;
    }

    QObject *rootObject = engine->rootObjects().first();
    QWindow *mainWindow = qobject_cast<QWindow *>(rootObject);
    std::unique_ptr<pp::VisibleBacktestLongrunCoordinator>
        visibleBacktestLongrun;
    std::unique_ptr<pp::RealtimeMultiWindowAcceptanceCoordinator>
        realtimeMultiWindowAcceptance;

    // Deterministic real-time acceptance entry point. It avoids attaching a UI
    // automation client merely to press the start button, so resource sampling
    // reflects the application rather than the accessibility inspection tool.
    if (startupArguments.contains(QStringLiteral("--start-live"))) {
        QTimer::singleShot(0, controller.get(), &pp::TradingController::startLive);
    }

    // 多屏窗口放置：恢复到上次关闭时所在屏幕与几何；关闭时记录屏幕与几何。
    if (QWindow *window = mainWindow) {
        // 某些 Windows 原生标题栏关闭路径会先隐藏最后一个 QML 窗口，
        // 但偶发不发出 lastWindowClosed。可见性变为 false 时直接排队退出，
        // 仍沿用 aboutToQuit 中的 TradingController::shutdown 顺序。
        QObject::connect(window, &QWindow::visibleChanged, &app,
                         [&app](bool visible) {
            if (!visible)
                QMetaObject::invokeMethod(&app, &QCoreApplication::quit,
                                          Qt::QueuedConnection);
        });

        const pp::WindowPlacementData placement = pp::WindowPlacement::load();
        QVector<pp::ScreenInfo> screens;
        for (QScreen *screen : QGuiApplication::screens()) {
            screens.append({screen->name(), screen->availableGeometry()});
        }
        const QRect geometry = pp::WindowPlacement::resolve(
            placement, screens, QSize(1600, 1000));
        window->setGeometry(geometry);

        // 退出探针只使用副屏（存在时），不覆盖用户保存的窗口位置。
        if (exitProbe && QGuiApplication::screens().size() > 1) {
            QScreen *probeScreen = QGuiApplication::screens().at(1);
            window->setScreen(probeScreen);
            window->setGeometry(QRect(probeScreen->availableGeometry().topLeft(),
                QSize(1120, 760).boundedTo(probeScreen->availableGeometry().size())));
        }

        QObject::connect(&app, &QCoreApplication::aboutToQuit, window, [window, exitProbe]() {
            if (exitProbe) return;
            QScreen *screen = window->screen();
            pp::WindowPlacement::save(screen ? screen->name() : QString{},
                                      window->geometry());
        });

        // Deterministic lifecycle probe used by scripts/pptrader-exit-probe.ps1.
        // It exercises the same QML window-close path without requiring UI automation.
        if (startupArguments.contains(QStringLiteral("--exit-probe"))) {
            QTimer::singleShot(300, window, [window]() { window->close(); });
        } else if (startupArguments.contains(QStringLiteral("--exit-probe-native"))) {
            QTimer::singleShot(300, window, [window]() {
#ifdef Q_OS_WIN
                const HWND handle = reinterpret_cast<HWND>(window->winId());
                ::PostMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
#else
                window->close();
#endif
            });
        }
    }

    if (startupArguments.contains(QStringLiteral("--backtest-longrun-visible"))) {
        pp::VisibleBacktestLongrunCoordinator::Options options;
        QString optionsError;
        if (!mainWindow
            || !pp::VisibleBacktestLongrunCoordinator::optionsFromEnvironment(
                &options, &optionsError)) {
            qCritical().noquote()
                << QStringLiteral("可见回测长稳配置无效：%1").arg(optionsError);
            engine.reset();
            controller.reset();
            return -3;
        }
        visibleBacktestLongrun =
            std::make_unique<pp::VisibleBacktestLongrunCoordinator>(
                controller.get(), rootObject, mainWindow, std::move(options));
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                         visibleBacktestLongrun.get(),
                         &pp::VisibleBacktestLongrunCoordinator::finishInterrupted);
        QTimer::singleShot(500, visibleBacktestLongrun.get(),
                           &pp::VisibleBacktestLongrunCoordinator::start);
    }

    if (startupArguments.contains(
            QStringLiteral("--realtime-multiwindow-acceptance"))) {
        pp::RealtimeMultiWindowAcceptanceCoordinator::Options options;
        QString optionsError;
        if (!pp::RealtimeMultiWindowAcceptanceCoordinator::optionsFromEnvironment(
                &options, &optionsError)) {
            qCritical().noquote()
                << QStringLiteral("双实时窗口验收配置无效：%1").arg(optionsError);
            engine.reset();
            controller.reset();
            return -4;
        }
        realtimeMultiWindowAcceptance =
            std::make_unique<pp::RealtimeMultiWindowAcceptanceCoordinator>(
                controller.get(), &app, std::move(options));
        QObject::connect(&app, &QCoreApplication::aboutToQuit,
                         realtimeMultiWindowAcceptance.get(),
                         &pp::RealtimeMultiWindowAcceptanceCoordinator::finishInterrupted);
        QTimer::singleShot(700, realtimeMultiWindowAcceptance.get(),
                           &pp::RealtimeMultiWindowAcceptanceCoordinator::start);
    }

    const int exitCode = app.exec();
    visibleBacktestLongrun.reset();
    realtimeMultiWindowAcceptance.reset();
    engine.reset();
    controller.reset();
    return exitCode;
}
