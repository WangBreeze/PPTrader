#include "app/TradingController.h"
#include "app/WindowPlacement.h"
#include "domain/MarketTypes.h"
#include "models/CandleListModel.h"

#include <QApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QUrl>
#include <QWindow>
#include <QtGlobal>

int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PPTrader"));
    QCoreApplication::setApplicationName(QStringLiteral("PPTrader"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    qRegisterMetaType<pp::Instrument>("pp::Instrument");
    qRegisterMetaType<pp::Tick>("pp::Tick");
    qRegisterMetaType<pp::Candle>("pp::Candle");
    qRegisterMetaType<pp::TradeSignal>("pp::TradeSignal");
    qRegisterMetaType<pp::CandlePoint>("pp::CandlePoint");

    pp::TradingController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tradingController"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/PPTrader/qml/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // 多屏窗口放置：恢复到上次关闭时所在屏幕与几何；关闭时记录屏幕与几何。
    if (QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().first())) {
        const pp::WindowPlacementData placement = pp::WindowPlacement::load();
        QVector<pp::ScreenInfo> screens;
        for (QScreen *screen : QGuiApplication::screens()) {
            screens.append({screen->name(), screen->availableGeometry()});
        }
        const QRect geometry = pp::WindowPlacement::resolve(
            placement, screens, QSize(1600, 1000));
        window->setGeometry(geometry);

        QObject::connect(&app, &QCoreApplication::aboutToQuit, window, [window]() {
            QScreen *screen = window->screen();
            pp::WindowPlacement::save(screen ? screen->name() : QString{},
                                      window->geometry());
        });
    }

    return app.exec();
}
