#include "services/BacktestEngine.h"
#include "models/BacktestTradeListModel.h"
#include "models/EquityCurveListModel.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <cmath>

namespace pp {

class BacktestEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void producesDeterministicTradesAndEquityCurve();
    void exposesStableStrategyCatalogAndParameterSchema();
    void runsEveryRegisteredStrategy();
    void rejectsUnknownInvalidAndInsufficientConfigurations();
    void exposesQmlResultModelCountAndRows();
    void runsVersionedExternalStrategyWithReproducibleSnapshots();
};

class FixedFractionTestStrategy final : public BacktestStrategy {
public:
    QString id() const override { return QStringLiteral("custom_engine_contract"); }
    QString displayName() const override { return QStringLiteral("外部策略契约测试"); }
    int minimumWarmupBars() const override { return 1; }
    double positionFraction() const override { return 0.5; }
    int strategyVersion() const override { return 7; }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("period"), 5}};
    }
    QVariantMap ruleSnapshot() const override
    {
        return {{QStringLiteral("positionFraction"), 0.5},
                {QStringLiteral("tPlusOne"), true}};
    }
    void onTick(const Tick &) override {}
    void onCandle(const Candle &candle) override
    {
        ++m_bar;
        if (m_bar != 1 && m_bar != 3) return;
        TradeSignal signal;
        signal.instrument = candle.instrument;
        signal.timestamp = candle.startTime;
        signal.side = m_bar == 1 ? SignalSide::Buy : SignalSide::Sell;
        signal.price = candle.close;
        signal.reason = m_bar == 1 ? QStringLiteral("测试进入") : QStringLiteral("测试退出");
        emit signalGenerated(signal);
    }
    void reset() override { m_bar = 0; }

private:
    int m_bar = 0;
};

static Instrument testInstrument()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");
    return instrument;
}

static QVector<Candle> waveCandles(int count)
{
    const Instrument instrument = testInstrument();
    QVector<Candle> candles;
    const QDateTime firstTime(QDate(2025, 1, 2), QTime(7, 0), Qt::UTC);
    for (int index = 0; index < count; ++index) {
        const double close = 100.0 + std::sin(index * 0.22) * 18.0 + index * 0.01;
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = firstTime.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = close;
        candle.high = close + 1.0;
        candle.low = close - 1.0;
        candle.close = close;
        candle.volume = 10000.0;
        candles.append(candle);
    }
    return candles;
}

void BacktestEngineTest::producesDeterministicTradesAndEquityCurve()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    QVector<double> closes;
    for (int index = 0; index < 20; ++index) {
        closes.append(120.0 - index);
    }
    for (int index = 0; index < 15; ++index) {
        closes.append(100.0 + index * 3.0);
    }
    for (int index = 0; index < 15; ++index) {
        closes.append(145.0 - index * 4.0);
    }

    QVector<Candle> candles;
    const QDateTime firstTime(QDate(2026, 1, 2), QTime(7, 0), Qt::UTC);
    for (int index = 0; index < closes.size(); ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = firstTime.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = closes.at(index);
        candle.high = closes.at(index) + 1.0;
        candle.low = closes.at(index) - 1.0;
        candle.close = closes.at(index);
        candle.volume = 10000.0;
        candle.source = QStringLiteral("test");
        candles.append(candle);
    }

    BacktestConfig config;
    config.instrument = instrument;
    config.frequency = QStringLiteral("day");
    config.periodSeconds = 86400;
    config.requestedBars = candles.size();
    config.initialCapital = 1000000.0;
    config.commissionRate = 0.0003;
    config.slippageRate = 0.0002;
    config.lotSize = 100;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);

    QVERIFY(engine.start(config, candles));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(engine.progress(), 1.0);
    QCOMPARE(engine.result().equityCurve.size(), candles.size());
    QVERIFY(!engine.result().trades.isEmpty());
    QCOMPARE(engine.result().summary.totalTrades, engine.result().trades.size());
    QVERIFY(engine.result().summary.finalEquity > 0.0);
    QVERIFY(engine.result().summary.totalCommission > 0.0);

    const double firstFinalEquity = engine.result().summary.finalEquity;
    const int firstTradeCount = engine.result().summary.totalTrades;
    QVERIFY(engine.start(config, candles));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 5000);
    QCOMPARE(engine.result().summary.finalEquity, firstFinalEquity);
    QCOMPARE(engine.result().summary.totalTrades, firstTradeCount);
}

void BacktestEngineTest::exposesStableStrategyCatalogAndParameterSchema()
{
    BacktestStrategyRegistry registry;
    const QVariantList catalog = registry.catalog();
    QCOMPARE(catalog.size(), 4);
    QCOMPARE(catalog.at(0).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("sma_crossover"));
    QCOMPARE(catalog.at(1).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("rsi_mean_reversion"));
    QCOMPARE(catalog.at(2).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("macd_trend"));
    QCOMPARE(catalog.at(3).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("bollinger_mean_reversion"));
    for (const QVariant &entry : catalog) {
        const QVariantMap descriptor = entry.toMap();
        QVERIFY(!descriptor.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(descriptor.value(QStringLiteral("minimumWarmupBars")).toInt() > 0);
        QVERIFY(!descriptor.value(QStringLiteral("parameters")).toList().isEmpty());
    }

    QVariantMap normalized;
    QString error;
    QVERIFY(registry.normalizeParameters(QStringLiteral("sma_crossover"), {},
                                         &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("fastPeriod")).toInt(), 5);
    QCOMPARE(normalized.value(QStringLiteral("slowPeriod")).toInt(), 20);
    QVERIFY(error.isEmpty());
}

void BacktestEngineTest::runsEveryRegisteredStrategy()
{
    const QVector<Candle> candles = waveCandles(240);
    const QStringList strategyIds = {
        QStringLiteral("sma_crossover"),
        QStringLiteral("rsi_mean_reversion"),
        QStringLiteral("macd_trend"),
        QStringLiteral("bollinger_mean_reversion")
    };
    for (int index = 0; index < strategyIds.size(); ++index) {
        BacktestConfig config;
        config.taskId = QString::number(index + 1);
        config.strategyId = strategyIds.at(index);
        config.instrument = testInstrument();
        config.requestedBars = candles.size();

        BacktestEngine engine;
        QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
        QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
        QVERIFY2(engine.start(config, candles), qPrintable(strategyIds.at(index)));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(errorSpy.count(), 0);
        QCOMPARE(engine.result().config.taskId, config.taskId);
        QCOMPARE(engine.result().config.strategyId, config.strategyId);
        QVERIFY(!engine.result().config.strategyParameters.isEmpty());
        QVERIFY2(!engine.result().trades.isEmpty(), qPrintable(strategyIds.at(index)));
        QCOMPARE(engine.result().equityCurve.size(), candles.size());
    }
}

void BacktestEngineTest::rejectsUnknownInvalidAndInsufficientConfigurations()
{
    const QVector<Candle> candles = waveCandles(60);
    BacktestConfig config;
    config.instrument = testInstrument();
    config.strategyId = QStringLiteral("missing");
    BacktestEngine engine;
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
    QVERIFY(!engine.start(config, candles));
    QCOMPARE(errorSpy.count(), 1);

    config.strategyId = QStringLiteral("sma_crossover");
    config.strategyParameters = {{QStringLiteral("fastPeriod"), 30},
                                 {QStringLiteral("slowPeriod"), 10}};
    QVERIFY(!engine.start(config, candles));
    QCOMPARE(errorSpy.count(), 2);

    config.strategyId = QStringLiteral("macd_trend");
    config.strategyParameters.clear();
    QVERIFY(!engine.start(config, waveCandles(20)));
    QCOMPARE(errorSpy.count(), 3);

    config.strategyId = QStringLiteral("sma_crossover");
    config.instrument.market = Market::Crypto;
    QVERIFY(!engine.start(config, candles));
    QCOMPARE(errorSpy.count(), 4);
}

void BacktestEngineTest::exposesQmlResultModelCountAndRows()
{
    EquityCurveListModel equityModel;
    QCOMPARE(equityModel.count(), 0);
    QSignalSpy equityCountSpy(&equityModel, &EquityCurveListModel::countChanged);
    EquityPoint point;
    point.timestamp = QDateTime(QDate(2026, 8, 8), QTime(7, 0), Qt::UTC);
    point.equity = 1012345.0;
    point.cash = 512345.0;
    point.positionValue = 500000.0;
    point.drawdownPercent = 1.25;
    equityModel.addPoint(point);
    QCOMPARE(equityModel.count(), 1);
    QCOMPARE(equityCountSpy.count(), 1);
    QCOMPARE(equityModel.get(0).value(QStringLiteral("equity")).toDouble(), point.equity);
    QVERIFY(equityModel.get(-1).isEmpty());
    equityModel.clear();
    QCOMPARE(equityModel.count(), 0);
    QCOMPARE(equityCountSpy.count(), 2);

    BacktestTradeListModel tradeModel;
    QSignalSpy tradeCountSpy(&tradeModel, &BacktestTradeListModel::countChanged);
    BacktestTrade trade;
    trade.instrument = testInstrument();
    trade.entryTime = point.timestamp;
    trade.exitTime = point.timestamp.addDays(2);
    trade.entryPrice = 100.0;
    trade.exitPrice = 110.0;
    trade.quantity = 100.0;
    trade.netPnl = 980.0;
    trade.returnPercent = 9.8;
    tradeModel.addTrade(trade);
    QCOMPARE(tradeModel.count(), 1);
    QCOMPARE(tradeCountSpy.count(), 1);
    const QVariantMap row = tradeModel.get(0);
    QCOMPARE(row.value(QStringLiteral("symbol")).toString(), QStringLiteral("600519"));
    QCOMPARE(row.value(QStringLiteral("profitable")).toBool(), true);
    QVERIFY(tradeModel.get(1).isEmpty());
}

void BacktestEngineTest::runsVersionedExternalStrategyWithReproducibleSnapshots()
{
    QVector<Candle> candles = waveCandles(8);
    candles[0].close = candles[0].open = candles[0].high = candles[0].low = 100.0;

    BacktestConfig config;
    config.taskId = QStringLiteral("custom-1");
    config.strategyId = QStringLiteral("custom_engine_contract");
    config.strategyVersion = 7;
    config.strategyParameters = {{QStringLiteral("period"), 5}};
    config.ruleSnapshot = {{QStringLiteral("positionFraction"), 0.5},
                           {QStringLiteral("tPlusOne"), true}};
    config.instrument = testInstrument();
    config.periodSeconds = 86400;
    config.requestedBars = candles.size();
    config.initialCapital = 1000000.0;
    config.commissionRate = 0.0;
    config.slippageRate = 0.0;
    config.lotSize = 100;

    BacktestEngine first;
    QSignalSpy firstFinished(&first, &BacktestEngine::finished);
    QVERIFY(first.start(config, candles, std::make_unique<FixedFractionTestStrategy>()));
    QTRY_COMPARE_WITH_TIMEOUT(firstFinished.count(), 1, 5000);
    QCOMPARE(first.result().config.strategyVersion, 7);
    QCOMPARE(first.result().config.ruleSnapshot, config.ruleSnapshot);
    QVERIFY(first.result().config.dataVersion.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(first.result().config.dataVersion.size(), 71);
    QCOMPARE(first.result().trades.size(), 1);
    QCOMPARE(first.result().trades.first().quantity, 5000.0);

    BacktestEngine second;
    QSignalSpy secondFinished(&second, &BacktestEngine::finished);
    QVector<Candle> reversed = candles;
    std::reverse(reversed.begin(), reversed.end());
    QVERIFY(second.start(config, reversed, std::make_unique<FixedFractionTestStrategy>()));
    QTRY_COMPARE_WITH_TIMEOUT(secondFinished.count(), 1, 5000);
    QCOMPARE(second.result().config.dataVersion, first.result().config.dataVersion);
    QCOMPARE(second.result().summary.finalEquity, first.result().summary.finalEquity);

    QVector<Candle> changed = candles;
    changed.last().close += 0.01;
    BacktestEngine changedEngine;
    QSignalSpy changedFinished(&changedEngine, &BacktestEngine::finished);
    QVERIFY(changedEngine.start(config, changed,
                                std::make_unique<FixedFractionTestStrategy>()));
    QTRY_COMPARE_WITH_TIMEOUT(changedFinished.count(), 1, 5000);
    QVERIFY(changedEngine.result().config.dataVersion
            != first.result().config.dataVersion);

    BacktestConfig unsafe = config;
    unsafe.strategyVersion = 0;
    BacktestEngine rejected;
    QSignalSpy errorSpy(&rejected, &BacktestEngine::errorOccurred);
    QVERIFY(!rejected.start(unsafe, candles,
                            std::make_unique<FixedFractionTestStrategy>()));
    QCOMPARE(errorSpy.count(), 1);

    unsafe = config;
    unsafe.ruleSnapshot.insert(QStringLiteral("positionFraction"), 0.75);
    QVERIFY(!rejected.start(unsafe, candles,
                            std::make_unique<FixedFractionTestStrategy>()));
    QCOMPARE(errorSpy.count(), 2);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::BacktestEngineTest)

#include "tst_backtest_engine.moc"
