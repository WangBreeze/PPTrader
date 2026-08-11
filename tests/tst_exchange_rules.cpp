#include "services/DefaultExchangeRuleService.h"

#include <QTest>

namespace pp {

class ExchangeRulesTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesLotsPriceTicksSessionsAndPriceLimits();
};

void ExchangeRulesTest::validatesLotsPriceTicksSessionsAndPriceLimits()
{
    DefaultExchangeRuleService rules;
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    OrderRequest request;
    request.instrument = instrument;
    request.side = OrderSide::Buy;
    request.type = OrderType::Limit;
    request.quantity = 50.0;
    request.limitPrice = 100.0;

    ExchangeRuleContext context;
    context.evaluationTime = QDateTime(QDate(2026, 8, 3), QTime(1, 30), Qt::UTC);
    RiskCheckResult result = rules.validate(request, context);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("INVALID_LOT_SIZE"));

    request.quantity = 100.0;
    request.limitPrice = 100.005;
    result = rules.validate(request, context);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("INVALID_PRICE_TICK"));

    request.limitPrice = 111.0;
    context.previousClose = 100.0;
    result = rules.validate(request, context);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("PRICE_LIMIT"));

    request.instrument.symbol = QStringLiteral("300750");
    request.limitPrice = 119.0;
    result = rules.validate(request, context);
    QVERIFY(result.accepted);

    context.suspended = true;
    result = rules.validate(request, context);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("INSTRUMENT_SUSPENDED"));

    context.suspended = false;
    context.previousClose = 0.0;
    context.enforceTradingSession = true;
    context.evaluationTime = QDateTime(QDate(2026, 8, 2), QTime(1, 30), Qt::UTC);
    result = rules.validate(request, context);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("MARKET_CLOSED"));

    context.evaluationTime = QDateTime(QDate(2026, 8, 3), QTime(1, 30), Qt::UTC);
    result = rules.validate(request, context);
    QVERIFY(result.accepted);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::ExchangeRulesTest)

#include "tst_exchange_rules.moc"
