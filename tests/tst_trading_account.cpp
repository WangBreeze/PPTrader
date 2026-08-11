#include "services/SimulatedBrokerGateway.h"
#include "services/TradingAccountService.h"

#include <QSignalSpy>
#include <QTest>

namespace pp {

class TradingAccountTest final : public QObject {
    Q_OBJECT

private slots:
    void maintainsCashReservationsPositionsAndPnl();
    void rejectsOrdersThatExceedRiskLimits();
    void restoresAndCancelsActiveOrdersSafely();
    void handlesPartialFillsTimeInForceAndExpiry();
};

void TradingAccountTest::maintainsCashReservationsPositionsAndPnl()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    SimulatedBrokerGateway broker(0.001, 0.0);
    TradingAccountService account(&broker, 100000.0, 0.001, 0.0);
    QSignalSpy errorSpy(&account, &TradingAccountService::operationFailed);
    broker.login();

    Tick buyTick;
    buyTick.instrument = instrument;
    buyTick.timestamp = QDateTime(QDate::currentDate(), QTime(9, 30), Qt::LocalTime);
    buyTick.price = 100.0;
    buyTick.bid = 99.0;
    buyTick.ask = 100.0;
    account.updateMarketPrice(buyTick);

    OrderRequest buyRequest;
    buyRequest.instrument = instrument;
    buyRequest.side = OrderSide::Buy;
    buyRequest.type = OrderType::Market;
    buyRequest.quantity = 100.0;
    buyRequest.referencePrice = 100.0;
    buyRequest.createdAt = buyTick.timestamp;
    const QString buyOrderId = account.submitOrder(buyRequest);
    QVERIFY(!buyOrderId.isEmpty());
    QCOMPARE(account.orders().size(), 1);
    QVERIFY(account.orders().first().status == OrderStatus::Filled);
    QCOMPARE(account.executions().size(), 1);
    QCOMPARE(account.positions().size(), 1);
    QCOMPARE(account.positions().first().quantity, 100.0);
    QCOMPARE(account.positions().first().availableQuantity, 100.0);
    QVERIFY(qAbs(account.positions().first().averageCost - 100.1) < 0.000001);
    QVERIFY(qAbs(account.snapshot().cash - 89990.0) < 0.000001);
    QVERIFY(qAbs(account.snapshot().totalEquity - 99990.0) < 0.000001);
    QCOMPARE(account.snapshot().tradingDay, buyTick.timestamp.toLocalTime().date());
    QVERIFY(qAbs(account.snapshot().dayStartEquity - 100000.0) < 0.000001);
    QVERIFY(qAbs(account.snapshot().dailyPnl + 10.0) < 0.000001);

    OrderRequest waitingSell;
    waitingSell.instrument = instrument;
    waitingSell.side = OrderSide::Sell;
    waitingSell.type = OrderType::Limit;
    waitingSell.quantity = 100.0;
    waitingSell.limitPrice = 105.0;
    waitingSell.referencePrice = 99.0;
    waitingSell.createdAt = buyTick.timestamp;
    const QString cancelledOrderId = account.submitOrder(waitingSell);
    QVERIFY(!cancelledOrderId.isEmpty());
    QVERIFY(account.orders().last().status == OrderStatus::Accepted);
    QCOMPARE(account.positions().first().availableQuantity, 0.0);

    account.cancelOrder(cancelledOrderId);
    QVERIFY(account.orders().last().status == OrderStatus::Cancelled);
    QCOMPARE(account.positions().first().availableQuantity, 100.0);

    const QString sellOrderId = account.submitOrder(waitingSell);
    QVERIFY(!sellOrderId.isEmpty());
    Tick sellTick = buyTick;
    sellTick.timestamp = buyTick.timestamp.addSecs(60);
    sellTick.price = 106.0;
    sellTick.bid = 106.0;
    sellTick.ask = 106.0;
    account.updateMarketPrice(sellTick);

    QVERIFY(account.orders().last().status == OrderStatus::Filled);
    QCOMPARE(account.executions().size(), 2);
    QVERIFY(account.positions().isEmpty());
    QVERIFY(qAbs(account.snapshot().cash - 100579.4) < 0.000001);
    QVERIFY(qAbs(account.snapshot().totalEquity - 100579.4) < 0.000001);
    QVERIFY(qAbs(account.snapshot().realizedPnl - 579.4) < 0.000001);
    QVERIFY(qAbs(account.snapshot().totalCommission - 20.6) < 0.000001);
    QVERIFY(qAbs(account.snapshot().dailyPnl - 579.4) < 0.000001);

    OrderRequest invalidSell = waitingSell;
    invalidSell.quantity = 100.0;
    QVERIFY(account.submitOrder(invalidSell).isEmpty());
    QCOMPARE(errorSpy.count(), 1);
}

void TradingAccountTest::rejectsOrdersThatExceedRiskLimits()
{
    PreTradeRiskService risk;
    RiskLimits limits;
    limits.maxOrderValue = 10000.0;
    limits.maxPositionValue = 15000.0;
    limits.maxDailyLoss = 5000.0;
    limits.maxActiveOrders = 2;
    risk.setLimits(limits);

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;

    OrderRequest request;
    request.instrument = instrument;
    request.side = OrderSide::Buy;
    request.type = OrderType::Limit;
    request.quantity = 100.0;
    request.limitPrice = 101.0;

    AccountSnapshot account;
    account.initialEquity = 100000.0;
    account.totalEquity = 100000.0;
    RiskCheckResult result = risk.check(request, account, std::nullopt, 0);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("MAX_ORDER_VALUE"));

    request.limitPrice = 100.0;
    Position position;
    position.instrument = instrument;
    position.marketValue = 6000.0;
    result = risk.check(request, account, position, 0);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("MAX_POSITION_VALUE"));

    position.marketValue = 0.0;
    account.dayStartEquity = 100000.0;
    account.totalEquity = 100000.0;
    account.dailyPnl = -6000.0;
    result = risk.check(request, account, position, 0);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("MAX_DAILY_LOSS"));

    account.dailyPnl = 0.0;
    result = risk.check(request, account, position, 2);
    QVERIFY(!result.accepted);
    QCOMPARE(result.code, QStringLiteral("MAX_ACTIVE_ORDERS"));
}

void TradingAccountTest::restoresAndCancelsActiveOrdersSafely()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;

    TradingState state;
    state.hasAccountSnapshot = true;
    state.account.initialEquity = 100000.0;
    state.account.cash = 90000.0;
    state.account.frozenCash = 5000.0;
    state.account.totalEquity = 100000.0;

    Order order;
    order.orderId = QStringLiteral("PAPER-RESTORE");
    order.request.instrument = instrument;
    order.request.side = OrderSide::Sell;
    order.request.type = OrderType::Limit;
    order.request.quantity = 100.0;
    order.status = OrderStatus::Accepted;
    state.orders.append(order);

    Position position;
    position.instrument = instrument;
    position.quantity = 100.0;
    position.availableQuantity = 0.0;
    position.averageCost = 100.0;
    position.marketPrice = 100.0;
    position.marketValue = 10000.0;
    state.positions.append(position);

    SimulatedBrokerGateway broker;
    TradingAccountService account(&broker);
    account.restoreTradingState(state);

    QCOMPARE(account.orders().size(), 1);
    QVERIFY(account.orders().first().status == OrderStatus::Cancelled);
    QCOMPARE(account.snapshot().frozenCash, 0.0);
    QCOMPARE(account.positions().size(), 1);
    QCOMPARE(account.positions().first().availableQuantity, 100.0);
    QCOMPARE(account.snapshot().totalEquity, 100000.0);
}

void TradingAccountTest::handlesPartialFillsTimeInForceAndExpiry()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    SimulatedBrokerGateway broker(0.0, 0.0, 0.5);
    TradingAccountService account(&broker, 100000.0, 0.0, 0.0);
    broker.login();

    Tick tick;
    tick.instrument = instrument;
    tick.timestamp = QDateTime(QDate(2026, 8, 3), QTime(1, 30), Qt::UTC);
    tick.price = 100.0;
    tick.bid = 100.0;
    tick.ask = 100.0;
    tick.volume = 200.0;
    account.updateMarketPrice(tick);

    OrderRequest dayOrder;
    dayOrder.instrument = instrument;
    dayOrder.side = OrderSide::Buy;
    dayOrder.type = OrderType::Limit;
    dayOrder.timeInForce = TimeInForce::Day;
    dayOrder.quantity = 300.0;
    dayOrder.limitPrice = 100.0;
    dayOrder.referencePrice = 100.0;
    dayOrder.createdAt = tick.timestamp;
    const QString dayOrderId = account.submitOrder(dayOrder);
    QVERIFY(!dayOrderId.isEmpty());
    QVERIFY(account.orders().last().status == OrderStatus::PartiallyFilled);
    QCOMPARE(account.orders().last().filledQuantity, 100.0);
    QCOMPARE(account.positions().first().quantity, 100.0);
    QCOMPARE(account.snapshot().frozenCash, 20000.0);

    tick.timestamp = tick.timestamp.addSecs(1);
    account.updateMarketPrice(tick);
    QVERIFY(account.orders().last().status == OrderStatus::PartiallyFilled);
    QCOMPARE(account.orders().last().filledQuantity, 200.0);
    tick.timestamp = tick.timestamp.addSecs(1);
    account.updateMarketPrice(tick);
    QVERIFY(account.orders().last().status == OrderStatus::Filled);
    QCOMPARE(account.orders().last().filledQuantity, 300.0);
    QCOMPARE(account.snapshot().frozenCash, 0.0);

    OrderRequest iocOrder = dayOrder;
    iocOrder.timeInForce = TimeInForce::ImmediateOrCancel;
    iocOrder.quantity = 300.0;
    iocOrder.createdAt = tick.timestamp;
    const QString iocOrderId = account.submitOrder(iocOrder);
    QVERIFY(!iocOrderId.isEmpty());
    QVERIFY(account.orders().last().status == OrderStatus::Cancelled);
    QCOMPARE(account.orders().last().filledQuantity, 100.0);
    QCOMPARE(account.snapshot().frozenCash, 0.0);

    OrderRequest fokOrder = dayOrder;
    fokOrder.timeInForce = TimeInForce::FillOrKill;
    fokOrder.quantity = 300.0;
    fokOrder.createdAt = tick.timestamp;
    const int executionCountBeforeFok = account.executions().size();
    const QString fokOrderId = account.submitOrder(fokOrder);
    QVERIFY(!fokOrderId.isEmpty());
    QVERIFY(account.orders().last().status == OrderStatus::Cancelled);
    QCOMPARE(account.orders().last().filledQuantity, 0.0);
    QCOMPARE(account.executions().size(), executionCountBeforeFok);

    OrderRequest expiringOrder = dayOrder;
    expiringOrder.limitPrice = 90.0;
    expiringOrder.quantity = 100.0;
    expiringOrder.createdAt = tick.timestamp;
    expiringOrder.expiresAt = tick.timestamp.addSecs(30);
    const QString expiringOrderId = account.submitOrder(expiringOrder);
    QVERIFY(!expiringOrderId.isEmpty());
    QVERIFY(account.orders().last().status == OrderStatus::Accepted);
    QVERIFY(account.snapshot().frozenCash > 0.0);
    tick.timestamp = tick.timestamp.addSecs(31);
    account.updateMarketPrice(tick);
    QVERIFY(account.orders().last().status == OrderStatus::Cancelled);
    QCOMPARE(account.snapshot().frozenCash, 0.0);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::TradingAccountTest)

#include "tst_trading_account.moc"
