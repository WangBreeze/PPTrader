#include "services/PortfolioBacktestEngine.h"

#include <QDate>
#include <QHash>
#include <QTest>
#include <QTimeZone>

#include <cmath>

namespace pp {

namespace {

Instrument instrumentFor(const QString &symbol)
{
    Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("XSHG");
    return instrument;
}

QVector<Candle> seriesFor(const QString &symbol,
                          const QVector<QDate> &days,
                          const QVector<double> &closes)
{
    QVector<Candle> candles;
    static const QTimeZone shanghai("Asia/Shanghai");
    for (int index = 0; index < days.size(); ++index) {
        Candle candle;
        candle.instrument = instrumentFor(symbol);
        candle.startTime = QDateTime(days.at(index), QTime(15, 0), shanghai)
                               .toUTC();
        candle.periodSeconds = 60;
        candle.open = candle.high = candle.low = candle.close = closes.at(index);
        candle.volume = 100000.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    return candles;
}

PortfolioDefinition equalWeightDefinition(const QString &first,
                                          const QString &second)
{
    PortfolioDefinition definition;
    definition.portfolioId = QStringLiteral("portfolio_test");
    definition.name = QStringLiteral("测试组合");
    definition.legs = {
        {instrumentFor(first), 0.0},
        {instrumentFor(second), 0.0}
    };
    definition.periodSeconds = 60;
    definition.initialCapital = 1000000.0;
    definition.commissionRate = 0.0;
    definition.slippageRate = 0.0;
    definition.lotSize = 100;
    return definition;
}

} // namespace

class PortfolioBacktestTest final : public QObject {
    Q_OBJECT

private slots:
    void equalWeightRebalancesToTarget();
    void commissionAndSlippageAreCharged();
    void sellBeforeBuyInvariantHolds();
    void missingDayUsesLastPrice();
    void missingDayDoesNotAdvanceRebalanceClock();
    void invalidDefinitionsRejected();
    void weightsRespectMaximumLegWeight();
};

void PortfolioBacktestTest::equalWeightRebalancesToTarget()
{
    const QVector<QDate> days{QDate(2026, 1, 5), QDate(2026, 1, 6),
                              QDate(2026, 1, 7)};
    // 腿 A 持续上涨、腿 B 横盘：每日再平衡应恢复 50/50。
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             days, {100.0, 120.0, 144.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             days, {100.0, 100.0, 100.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002")),
        series);
    QVERIFY(!result.equityCurve.isEmpty());
    QCOMPARE(result.trades.size(), 6); // Day1 建仓 2 笔 + Day2/3 再平衡各 2 笔
    const PortfolioBar &last = result.equityCurve.last();
    QVERIFY(last.positions.size() == 2);
    QVERIFY(std::abs(last.positions.at(0).weight - 0.5) < 0.02);
    QVERIFY(std::abs(last.positions.at(1).weight - 0.5) < 0.02);
    QVERIFY(last.equity > 1000000.0);
    QVERIFY(result.summary.totalReturnPercent > 0.0);
    QVERIFY(result.summary.maxDrawdownPercent >= 0.0);
}

void PortfolioBacktestTest::commissionAndSlippageAreCharged()
{
    const QVector<QDate> days{QDate(2026, 1, 5)};
    PortfolioDefinition definition =
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002"));
    definition.commissionRate = 0.001;
    definition.slippageRate = 0.0005;
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             days, {100.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             days, {100.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(definition, series);
    QVERIFY(!result.equityCurve.isEmpty());
    QCOMPARE(result.trades.size(), 2);
    QVERIFY(result.summary.totalCommission > 0.0);
    QVERIFY(result.equityCurve.last().equity < 1000000.0);
    for (const PortfolioTrade &trade : result.trades) {
        QVERIFY(trade.price != 100.0); // 滑点改变成交价
        QVERIFY(trade.commission > 0.0);
    }
}

void PortfolioBacktestTest::sellBeforeBuyInvariantHolds()
{
    const QVector<QDate> days{QDate(2026, 1, 5), QDate(2026, 1, 6)};
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             days, {100.0, 200.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             days, {100.0, 50.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002")),
        series);
    // 同一调仓日先卖后买：每个调仓日内最后一笔 sell 必须先于第一笔 buy。
    QHash<QDateTime, int> firstBuy;
    QHash<QDateTime, int> lastSell;
    for (int index = 0; index < result.trades.size(); ++index) {
        const PortfolioTrade &trade = result.trades.at(index);
        if (trade.side == QStringLiteral("sell")) {
            lastSell.insert(trade.timestamp, index);
        } else if (!firstBuy.contains(trade.timestamp)) {
            firstBuy.insert(trade.timestamp, index);
        }
    }
    for (auto it = firstBuy.constBegin(); it != firstBuy.constEnd(); ++it) {
        const auto sell = lastSell.constFind(it.key());
        if (sell == lastSell.constEnd()) continue;
        QVERIFY(sell.value() < it.value());
    }
    QVERIFY(result.trades.size() >= 4);
}

void PortfolioBacktestTest::missingDayUsesLastPrice()
{
    const QVector<QDate> fullDays{QDate(2026, 1, 5), QDate(2026, 1, 6),
                                  QDate(2026, 1, 7)};
    const QVector<QDate> shortDays{QDate(2026, 1, 5), QDate(2026, 1, 7)};
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             fullDays, {100.0, 110.0, 121.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             shortDays, {100.0, 100.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002")),
        series);
    // 2026-01-06 不是共同交易日，但仍以腿 B 的最近收盘价输出估值点。
    QCOMPARE(result.equityCurve.size(), 3);
    QCOMPARE(result.trades.size(), 4); // 1/5 建仓 2 笔 + 1/7 再平衡 2 笔
    static const QTimeZone shanghai("Asia/Shanghai");
    const PortfolioBar &missingDay = result.equityCurve.at(1);
    QCOMPARE(missingDay.timestamp.toTimeZone(shanghai).date(), QDate(2026, 1, 6));
    QCOMPARE(missingDay.equity, 1050000.0);
    QCOMPARE(missingDay.positions.at(0).lastPrice, 110.0);
    QVERIFY(!missingDay.positions.at(0).priceStale);
    QCOMPARE(missingDay.positions.at(1).lastPrice, 100.0);
    QVERIFY(missingDay.positions.at(1).priceStale);
    const PortfolioBar &last = result.equityCurve.last();
    QCOMPARE(last.positions.at(1).lastPrice, 100.0);
    QVERIFY(!last.positions.at(1).priceStale);
    for (const PortfolioTrade &trade : result.trades) {
        QVERIFY(trade.timestamp.toTimeZone(shanghai).date() != QDate(2026, 1, 6));
    }
}

void PortfolioBacktestTest::missingDayDoesNotAdvanceRebalanceClock()
{
    const QVector<QDate> fullDays{QDate(2026, 1, 5), QDate(2026, 1, 6),
                                  QDate(2026, 1, 7), QDate(2026, 1, 8)};
    const QVector<QDate> shortDays{QDate(2026, 1, 5), QDate(2026, 1, 7),
                                   QDate(2026, 1, 8)};
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             fullDays, {100.0, 120.0, 144.0, 172.8})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             shortDays, {100.0, 100.0, 100.0})}};
    PortfolioDefinition definition =
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002"));
    definition.rebalanceEveryTradingDays = 2;
    const PortfolioResult result = PortfolioBacktestEngine::run(definition, series);

    QCOMPARE(result.equityCurve.size(), 4);
    QCOMPARE(result.trades.size(), 4); // 1/5 建仓；第二个调仓点是第三个共同交易日 1/8。
    static const QTimeZone shanghai("Asia/Shanghai");
    for (int index = 2; index < result.trades.size(); ++index) {
        QCOMPARE(result.trades.at(index).timestamp.toTimeZone(shanghai).date(),
                 QDate(2026, 1, 8));
    }
}

void PortfolioBacktestTest::invalidDefinitionsRejected()
{
    PortfolioDefinition definition =
        equalWeightDefinition(QStringLiteral("600001"),
                              QStringLiteral("600002"));
    definition.initialCapital = -1.0;
    QString error;
    PortfolioResult result = PortfolioBacktestEngine::run(definition, {}, &error);
    QVERIFY(result.equityCurve.isEmpty());
    QVERIFY(!error.isEmpty());

    definition = equalWeightDefinition(QStringLiteral("600001"),
                                       QStringLiteral("600002"));
    definition.legs[0].targetWeight = 0.8;
    definition.legs[1].targetWeight = 0.4; // 合计 1.2 > 1
    result = PortfolioBacktestEngine::run(definition, {}, &error);
    QVERIFY(result.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("权重")));

    definition = equalWeightDefinition(QStringLiteral("600001"),
                                       QStringLiteral("600002"));
    const QHash<QString, QVector<Candle>> partial{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             {QDate(2026, 1, 5)}, {100.0})}};
    result = PortfolioBacktestEngine::run(definition, partial, &error);
    QVERIFY(result.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("缺少 K 线")));
}

void PortfolioBacktestTest::weightsRespectMaximumLegWeight()
{
    const QVector<QDate> days{QDate(2026, 1, 5)};
    PortfolioDefinition definition;
    definition.portfolioId = QStringLiteral("portfolio_cap");
    definition.periodSeconds = 60;
    definition.initialCapital = 1000000.0;
    definition.maximumLegWeight = 0.6;
    definition.legs = {
        {instrumentFor(QStringLiteral("600001")), 0.0},
        {instrumentFor(QStringLiteral("600002")), 0.0},
        {instrumentFor(QStringLiteral("600003")), 0.0}
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             days, {100.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             days, {100.0})},
        {QStringLiteral("600003"), seriesFor(QStringLiteral("600003"),
                                             days, {100.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(definition, series);
    QVERIFY(!result.equityCurve.isEmpty());
    QCOMPARE(result.trades.size(), 3);
    const PortfolioBar &bar = result.equityCurve.first();
    for (const PortfolioPosition &position : bar.positions) {
        QVERIFY(position.weight <= 0.6 + 1e-9);
    }
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::PortfolioBacktestTest)

#include "tst_portfolio_backtest.moc"
