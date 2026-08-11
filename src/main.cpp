#include "app/TradingController.h"
#include "domain/MarketTypes.h"
#include "models/CandleListModel.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
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

    return app.exec();
}
