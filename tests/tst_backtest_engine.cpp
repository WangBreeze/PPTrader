#include "services/BacktestEngine.h"
#include "services/BoardPortfolioBacktestEngine.h"
#include "domain/RawTradeTypes.h"
#include "models/BacktestTradeListModel.h"
#include "models/CandleListModel.h"
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
    void runsRawTickBacktestOnlyWithCompatibleStrategy();
    void rawTickProgressIsReportedInBoundedChunks();
    void longRawTickBacktestKeepsBoundedDisplayEquityWithoutChangingSummary();
    void rawTickMomentumHonorsMinimumHoldAndSuppressesRepeatedSignals();
    void rawTickMomentumDefersSameDayExitUntilNextTradingDay();
    void rawTickMeanReversionUsesRollingTicksAndDefersExit();
    void rawTickVolumeBreakoutRequiresVolumeAndDefersExit();
    void rawTickDirectionalImbalanceUsesBSMVolumeAndDefersExit();
    void rawTickBoardContinuationUsesOnlyPastTicksAndTPlusOne();
    void boardPortfolioUsesOneCashBookAcrossSymbols();
    void rawTickBacktestKeepsFinalDayPurchaseAsOpenPosition();
    void runsEveryRegisteredStrategy();
    void rejectsUnknownInvalidAndInsufficientConfigurations();
    void exposesQmlResultModelCountAndRows();
    void candleResultModelRetainsConfiguredBacktestLimit();
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

void BacktestEngineTest::candleResultModelRetainsConfiguredBacktestLimit()
{
    CandleListModel model;
    const QVector<Candle> candles = waveCandles(5001);
    for (const Candle &candle : candles) {
        CandlePoint point;
        point.candle = candle;
        model.addPoint(point);
    }
    QCOMPARE(model.count(), 5000);
    QCOMPARE(model.rowCount(), 5000);
    QCOMPARE(model.get(0).value(QStringLiteral("timestamp")).toDateTime(),
             candles.at(1).startTime);
    QCOMPARE(model.get(4999).value(QStringLiteral("timestamp")).toDateTime(),
             candles.last().startTime);
    QVERIFY(model.get(5000).isEmpty());
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
    QCOMPARE(engine.result().equityObservationCount, qint64(candles.size()));
    QVERIFY(!engine.result().equityCurveSampled);
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
    QCOMPARE(catalog.size(), 10);
    QCOMPARE(catalog.at(0).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("sma_crossover"));
    QCOMPARE(catalog.at(1).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("rsi_mean_reversion"));
    QCOMPARE(catalog.at(2).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("macd_trend"));
    QCOMPARE(catalog.at(3).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("bollinger_mean_reversion"));
    QCOMPARE(catalog.at(4).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_momentum"));
    QCOMPARE(catalog.at(5).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_mean_reversion"));
    QCOMPARE(catalog.at(6).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_volume_breakout"));
    QCOMPARE(catalog.at(7).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_directional_imbalance"));
    QCOMPARE(catalog.at(8).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_second_board"));
    QCOMPARE(catalog.at(9).toMap().value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("tick_third_board"));
    const QVariantList rawParameters = catalog.at(4).toMap()
                                           .value(QStringLiteral("parameters")).toList();
    QCOMPARE(rawParameters.size(), 4);
    QCOMPARE(rawParameters.last().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("minimumHoldSeconds"));
    for (int index = 0; index < catalog.size(); ++index) {
        const QVariant &entry = catalog.at(index);
        const QVariantMap descriptor = entry.toMap();
        QVERIFY(!descriptor.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(descriptor.value(QStringLiteral("minimumWarmupBars")).toInt() > 0);
        QVERIFY(!descriptor.value(QStringLiteral("parameters")).toList().isEmpty());
        QCOMPARE(descriptor.value(QStringLiteral("supportsCandles")).toBool(), index < 4);
        QCOMPARE(descriptor.value(QStringLiteral("supportsRawTicks")).toBool(), index >= 4);
    }

    QVERIFY(registry.supportsDataMode(QStringLiteral("sma_crossover"),
                                      QStringLiteral("candles")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("sma_crossover"),
                                       QStringLiteral("raw_ticks")));
    QVERIFY(registry.supportsDataMode(QStringLiteral("tick_momentum"),
                                      QStringLiteral("raw_ticks")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("tick_momentum"),
                                       QStringLiteral("candles")));
    QVERIFY(registry.supportsDataMode(QStringLiteral("tick_mean_reversion"),
                                      QStringLiteral("raw_ticks")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("tick_mean_reversion"),
                                       QStringLiteral("candles")));
    QVERIFY(registry.supportsDataMode(QStringLiteral("tick_volume_breakout"),
                                      QStringLiteral("raw_ticks")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("tick_volume_breakout"),
                                       QStringLiteral("candles")));
    QVERIFY(registry.supportsDataMode(QStringLiteral("tick_directional_imbalance"),
                                      QStringLiteral("raw_ticks")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("tick_directional_imbalance"),
                                       QStringLiteral("candles")));
    QVERIFY(registry.supportsDataMode(QStringLiteral("tick_second_board"),
                                      QStringLiteral("raw_ticks")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("tick_third_board"),
                                       QStringLiteral("candles")));
    QVERIFY(!registry.supportsDataMode(QStringLiteral("missing"),
                                       QStringLiteral("candles")));

    QVariantMap normalized;
    QString error;
    QVERIFY(registry.normalizeParameters(QStringLiteral("sma_crossover"), {},
                                         &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("fastPeriod")).toInt(), 5);
    QCOMPARE(normalized.value(QStringLiteral("slowPeriod")).toInt(), 20);
    QVERIFY(error.isEmpty());

    QVERIFY(registry.normalizeParameters(QStringLiteral("tick_momentum"), {},
                                         &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("lookbackTicks")).toInt(), 20);
    QCOMPARE(normalized.value(QStringLiteral("thresholdPercent")).toDouble(), 0.2);
    QCOMPARE(normalized.value(QStringLiteral("cooldownSeconds")).toInt(), 30);
    QCOMPARE(normalized.value(QStringLiteral("minimumHoldSeconds")).toInt(), 60);
    QVERIFY(error.isEmpty());

    QVERIFY(registry.normalizeParameters(QStringLiteral("tick_mean_reversion"), {},
                                         &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("lookbackTicks")).toInt(), 50);
    QCOMPARE(normalized.value(QStringLiteral("entryDeviationPercent")).toDouble(), 0.3);
    QCOMPARE(normalized.value(QStringLiteral("exitDeviationPercent")).toDouble(), 0.0);
    QVERIFY(error.isEmpty());

    QVERIFY(registry.normalizeParameters(QStringLiteral("tick_volume_breakout"), {},
                                         &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("lookbackTicks")).toInt(), 50);
    QCOMPARE(normalized.value(QStringLiteral("breakoutPercent")).toDouble(), 0.1);
    QCOMPARE(normalized.value(QStringLiteral("breakdownPercent")).toDouble(), 0.1);
    QCOMPARE(normalized.value(QStringLiteral("volumeMultiplier")).toDouble(), 1.5);
    QVERIFY(error.isEmpty());

    QVERIFY(registry.normalizeParameters(
        QStringLiteral("tick_directional_imbalance"), {}, &normalized, &error));
    QCOMPARE(normalized.value(QStringLiteral("lookbackTicks")).toInt(), 50);
    QCOMPARE(normalized.value(QStringLiteral("entryImbalancePercent")).toDouble(), 30.0);
    QCOMPARE(normalized.value(QStringLiteral("exitImbalancePercent")).toDouble(), -10.0);
    QCOMPARE(normalized.value(
        QStringLiteral("minimumDirectionalCoveragePercent")).toDouble(), 50.0);
    QVERIFY(error.isEmpty());
    QVERIFY(!registry.normalizeParameters(
        QStringLiteral("tick_directional_imbalance"),
        {{QStringLiteral("entryImbalancePercent"), 20.0},
         {QStringLiteral("exitImbalancePercent"), 20.0}},
        &normalized, &error));
    QVERIFY(error.contains(QStringLiteral("退出失衡阈值")));
}

void BacktestEngineTest::runsRawTickBacktestOnlyWithCompatibleStrategy()
{
    QVector<RawTradeTick> ticks;
    const Instrument instrument = testInstrument();
    const QDate day(2026, 1, 5);
    const QDateTime first(QDate(2026, 1, 5), QTime(1, 30), Qt::UTC);
    for (int index = 0; index < 80; ++index) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = first.addSecs(index);
        tick.sourceLine = index + 1;
        tick.price = 100.0 + std::sin(index * 0.35) * 2.0;
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = index % 2 ? RawTradeDirection::Buy
                                   : RawTradeDirection::Sell;
        ticks.append(tick);
    }

    BacktestConfig config;
    config.taskId = QStringLiteral("raw-1");
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                 {QStringLiteral("thresholdPercent"), 0.2},
                                 {QStringLiteral("cooldownSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(engine.result().config.actualBars, ticks.size());
    QCOMPARE(engine.result().rawTicks.size(), ticks.size());
    QVERIFY(engine.result().candles.isEmpty());
    QVERIFY(engine.result().config.dataVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(engine.result().trades.isEmpty());
    QVERIFY(engine.result().finalPosition.quantity > 0.0);

    config.strategyId = QStringLiteral("sma_crossover");
    config.strategyParameters = {{QStringLiteral("fastPeriod"), 3},
                                 {QStringLiteral("slowPeriod"), 8}};
    QVERIFY(!engine.start(config, ticks));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.last().first().toString().contains(QStringLiteral("不支持")));
}

void BacktestEngineTest::rawTickProgressIsReportedInBoundedChunks()
{
    QVector<RawTradeTick> ticks;
    const Instrument instrument = testInstrument();
    const QDate day(2026, 1, 5);
    const QDateTime first(day, QTime(1, 30), Qt::UTC);
    ticks.reserve(600);
    for (int index = 0; index < 600; ++index) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = first.addMSecs(index);
        tick.sourceLine = index + 1;
        tick.price = 100.0 + std::sin(index * 0.1);
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = index % 2 ? RawTradeDirection::Buy
                                   : RawTradeDirection::Sell;
        ticks.append(tick);
    }

    BacktestConfig config;
    config.taskId = QStringLiteral("raw-chunked-progress");
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                 {QStringLiteral("thresholdPercent"), 0.2},
                                 {QStringLiteral("cooldownSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy progressSpy(&engine, &BacktestEngine::progressChanged);
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(engine.result().config.actualBars, 600);
    QCOMPARE(engine.result().equityCurve.size(), 600);
    QVERIFY(progressSpy.count() >= 4);
    QVERIFY(progressSpy.count() <= 10);
    QCOMPARE(progressSpy.last().first().toDouble(), 1.0);
}

void BacktestEngineTest::longRawTickBacktestKeepsBoundedDisplayEquityWithoutChangingSummary()
{
    constexpr int observationCount = 12000;
    QVector<RawTradeTick> ticks;
    const Instrument instrument = testInstrument();
    const QDate firstDay(2026, 1, 5);
    const QDateTime firstTimestamp(firstDay, QTime(1, 30), Qt::UTC);
    ticks.reserve(observationCount);
    for (int index = 0; index < observationCount; ++index) {
        const bool secondDay = index >= observationCount / 2;
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = secondDay ? firstDay.addDays(1) : firstDay;
        tick.timestamp = secondDay
            ? QDateTime(firstDay.addDays(1), QTime(1, 30), Qt::UTC)
                  .addMSecs(index - observationCount / 2)
            : firstTimestamp.addMSecs(index);
        tick.sourceLine = index + 1;
        tick.price = 100.0 + std::sin(index * 0.031) * 3.0
            + std::sin(index * 0.007) * 1.5;
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = index % 2 ? RawTradeDirection::Buy
                                   : RawTradeDirection::Sell;
        ticks.append(tick);
    }

    BacktestConfig config;
    config.taskId = QStringLiteral("raw-bounded-equity");
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                 {QStringLiteral("thresholdPercent"), 0.2},
                                 {QStringLiteral("cooldownSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);

    const BacktestResult &result = engine.result();
    QCOMPARE(result.equityObservationCount, qint64(observationCount));
    QVERIFY(result.equityCurveSampled);
    QVERIFY(result.equityCurve.size()
            <= BacktestEngine::RawTickDisplayEquityPointLimit);
    QCOMPARE(result.equityCurve.first().timestamp, ticks.first().timestamp);
    QCOMPARE(result.equityCurve.last().timestamp, ticks.last().timestamp);
    QVERIFY(std::any_of(result.equityCurve.cbegin(), result.equityCurve.cend(),
                        [&result](const EquityPoint &point) {
                            return qAbs(point.drawdownPercent
                                        - result.summary.maxDrawdownPercent) < 1e-12;
                        }));
    QVERIFY(qAbs(result.summary.finalEquity - 1037854.0741097186) < 1e-6);
    QVERIFY(qAbs(result.summary.totalReturnPercent - 3.7854074109718638) < 1e-10);
    QVERIFY(qAbs(result.summary.maxDrawdownPercent - 8.5329359883998084) < 1e-10);
    QVERIFY(qAbs(result.summary.sharpeRatio - 9.617352793367882) < 1e-10);
    QCOMPARE(result.summary.totalTrades, 1);

    BacktestConfig streamedConfig = config;
    streamedConfig.taskId = QStringLiteral("raw-bounded-equity-streamed");
    streamedConfig.datasetIdentity.instrument = instrument;
    streamedConfig.datasetIdentity.periodSeconds = 0;
    streamedConfig.datasetIdentity.days = {
        {firstDay, observationCount / 2, observationCount / 2,
         QStringLiteral("sha256:") + QString(64, '1'), 0, 0, false},
        {firstDay.addDays(1), observationCount / 2, observationCount,
         QStringLiteral("sha256:") + QString(64, '2'), 0, 0, false}
    };
    streamedConfig.datasetIdentity.datasetVersion =
        QStringLiteral("sha256:") + QString(64, '3');
    int offset = 0;
    BacktestEngine streamed;
    QSignalSpy streamedFinished(&streamed, &BacktestEngine::finished);
    QVERIFY(streamed.startStreamedRawTicks(
        streamedConfig, observationCount,
        [&ticks, &offset](QVector<RawTradeTick> *chunk, QString *) {
            constexpr int chunkSize = 777;
            const int remaining = ticks.size() - offset;
            if (remaining <= 0) {
                chunk->clear();
                return true;
            }
            *chunk = ticks.mid(offset, std::min(chunkSize, remaining));
            offset += chunk->size();
            return true;
        }));
    QTRY_COMPARE_WITH_TIMEOUT(streamedFinished.count(), 1, 10000);
    const BacktestResult &streamedResult = streamed.result();
    QCOMPARE(streamedResult.equityObservationCount, qint64(observationCount));
    QVERIFY(streamedResult.equityCurveSampled);
    QVERIFY(streamedResult.equityCurve.size()
            <= BacktestEngine::RawTickDisplayEquityPointLimit);
    QVERIFY(streamedResult.rawTicks.isEmpty());
    QVERIFY(streamedResult.rawTickSnapshotDeferred);
    QCOMPARE(streamedResult.config.dataVersion, result.config.dataVersion);
    QCOMPARE(streamedResult.equityCurve.first().timestamp, ticks.first().timestamp);
    QCOMPARE(streamedResult.equityCurve.last().timestamp, ticks.last().timestamp);
    QVERIFY(qAbs(streamedResult.summary.finalEquity
                 - result.summary.finalEquity) < 1e-6);
    QVERIFY(qAbs(streamedResult.summary.totalReturnPercent
                 - result.summary.totalReturnPercent) < 1e-10);
    QVERIFY(qAbs(streamedResult.summary.maxDrawdownPercent
                 - result.summary.maxDrawdownPercent) < 1e-10);
    QVERIFY(qAbs(streamedResult.summary.sharpeRatio
                 - result.summary.sharpeRatio) < 1e-10);
}

void BacktestEngineTest::rawTickMomentumHonorsMinimumHoldAndSuppressesRepeatedSignals()
{
    const Instrument instrument = testInstrument();
    const QDate day(2026, 1, 5);
    const QDateTime first(day, QTime(1, 30), Qt::UTC);
    const QVector<double> prices {100.0, 102.0, 104.0, 100.0, 98.0, 103.0, 97.0};
    QVector<RawTradeTick> ticks;
    ticks.reserve(prices.size());
    for (int index = 0; index < prices.size(); ++index) {
        RawTradeTick tick;
        tick.instrument = instrument;
        const bool nextTradingDay = index == prices.size() - 1;
        tick.tradingDate = nextTradingDay ? day.addDays(1) : day;
        tick.timestamp = nextTradingDay
            ? QDateTime(day.addDays(1), QTime(1, 30), Qt::UTC)
            : first.addSecs(index);
        tick.sourceLine = index + 1;
        tick.price = prices.at(index);
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = RawTradeDirection::Neutral;
        ticks.append(tick);
    }

    BacktestConfig config;
    config.taskId = QStringLiteral("raw-hold-guard");
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 1},
                                 {QStringLiteral("thresholdPercent"), 1.0},
                                 {QStringLiteral("cooldownSeconds"), 0},
                                 {QStringLiteral("minimumHoldSeconds"), 5}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.frequency = QStringLiteral("tick");
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(engine.result().trades.size(), 1);
    QCOMPARE(engine.result().chartSignals.size(), 2);
    QCOMPARE(engine.result().chartSignals.first().side, SignalSide::Buy);
    QCOMPARE(engine.result().chartSignals.last().side, SignalSide::Sell);
    QVERIFY(engine.result().trades.first().entryTime.secsTo(
                engine.result().trades.first().exitTime) >= 5);
}

void BacktestEngineTest::rawTickMomentumDefersSameDayExitUntilNextTradingDay()
{
    const Instrument instrument = testInstrument();
    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    QVector<RawTradeTick> ticks;
    const auto appendTick = [&](const QDate &day, const QTime &time,
                                double price, qint64 sourceLine) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, time, Qt::UTC);
        tick.sourceLine = sourceLine;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * tick.volumeShares;
        ticks.append(tick);
    };
    appendTick(firstDay, QTime(1, 30), 100.0, 1);
    appendTick(firstDay, QTime(1, 30, 1), 102.0, 2);
    appendTick(firstDay, QTime(1, 30, 2), 98.0, 3);
    appendTick(secondDay, QTime(1, 30), 97.0, 1);
    appendTick(secondDay, QTime(1, 30, 1), 97.0, 2);

    BacktestConfig config;
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 1},
                                 {QStringLiteral("thresholdPercent"), 1.0},
                                 {QStringLiteral("cooldownSeconds"), 0},
                                 {QStringLiteral("minimumHoldSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(engine.result().trades.size(), 1);
    QCOMPARE(engine.result().trades.first().entryTime.date(), firstDay);
    QCOMPARE(engine.result().trades.first().exitTime.date(), secondDay);
    QVERIFY(engine.result().trades.first().exitReason.contains(QStringLiteral("T+1")));
}

void BacktestEngineTest::rawTickMeanReversionUsesRollingTicksAndDefersExit()
{
    const Instrument instrument = testInstrument();
    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    QVector<RawTradeTick> ticks;
    const auto appendTick = [&](const QDate &day, const QTime &time,
                                double price, qint64 sourceLine) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, time, Qt::UTC);
        tick.sourceLine = sourceLine;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * tick.volumeShares;
        ticks.append(tick);
    };
    appendTick(firstDay, QTime(1, 30), 100.0, 1);
    appendTick(firstDay, QTime(1, 30, 1), 100.0, 2);
    appendTick(firstDay, QTime(1, 30, 2), 100.0, 3);
    appendTick(firstDay, QTime(1, 30, 3), 96.0, 4);
    appendTick(firstDay, QTime(1, 30, 4), 100.0, 5);
    appendTick(secondDay, QTime(1, 30), 101.0, 1);

    BacktestConfig config;
    config.strategyId = QStringLiteral("tick_mean_reversion");
    config.strategyParameters = {
        {QStringLiteral("lookbackTicks"), 3},
        {QStringLiteral("entryDeviationPercent"), 2.0},
        {QStringLiteral("exitDeviationPercent"), 0.0},
        {QStringLiteral("cooldownSeconds"), 0},
        {QStringLiteral("minimumHoldSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(engine.result().chartSignals.size(), 2);
    QCOMPARE(engine.result().trades.size(), 1);
    QCOMPARE(engine.result().trades.first().entryTime.date(), firstDay);
    QCOMPARE(engine.result().trades.first().exitTime.date(), secondDay);
    QVERIFY(engine.result().trades.first().entryReason.contains(QStringLiteral("均值偏离")));
    QVERIFY(engine.result().trades.first().exitReason.contains(QStringLiteral("T+1")));
    QCOMPARE(engine.result().finalPosition.quantity, 0.0);
}

void BacktestEngineTest::rawTickVolumeBreakoutRequiresVolumeAndDefersExit()
{
    const Instrument instrument = testInstrument();
    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    QVector<RawTradeTick> ticks;
    const auto appendTick = [&](const QDate &day, const QTime &time,
                                double price, double volume, qint64 sourceLine) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, time, Qt::UTC);
        tick.sourceLine = sourceLine;
        tick.price = price;
        tick.volumeShares = volume;
        tick.amount = price * volume;
        ticks.append(tick);
    };
    appendTick(firstDay, QTime(1, 30), 100.0, 100.0, 1);
    appendTick(firstDay, QTime(1, 30, 1), 100.0, 100.0, 2);
    appendTick(firstDay, QTime(1, 30, 2), 100.0, 100.0, 3);
    appendTick(firstDay, QTime(1, 30, 3), 102.0, 100.0, 4);
    appendTick(firstDay, QTime(1, 30, 4), 103.0, 300.0, 5);
    appendTick(firstDay, QTime(1, 30, 5), 98.0, 100.0, 6);
    appendTick(secondDay, QTime(1, 30), 97.0, 100.0, 1);

    BacktestConfig config;
    config.strategyId = QStringLiteral("tick_volume_breakout");
    config.strategyParameters = {
        {QStringLiteral("lookbackTicks"), 3},
        {QStringLiteral("breakoutPercent"), 0.5},
        {QStringLiteral("breakdownPercent"), 0.5},
        {QStringLiteral("volumeMultiplier"), 2.0},
        {QStringLiteral("cooldownSeconds"), 0},
        {QStringLiteral("minimumHoldSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(engine.result().chartSignals.size(), 2);
    QCOMPARE(engine.result().trades.size(), 1);
    QCOMPARE(engine.result().trades.first().entryTime, ticks.at(4).timestamp);
    QCOMPARE(engine.result().trades.first().exitTime.date(), secondDay);
    QVERIFY(engine.result().trades.first().entryReason.contains(QStringLiteral("放量突破")));
    QVERIFY(engine.result().trades.first().exitReason.contains(QStringLiteral("T+1")));
    QCOMPARE(engine.result().finalPosition.quantity, 0.0);
}

void BacktestEngineTest::rawTickDirectionalImbalanceUsesBSMVolumeAndDefersExit()
{
    const Instrument instrument = testInstrument();
    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    QVector<RawTradeTick> ticks;
    const auto appendTick = [&](const QDate &day, const QTime &time,
                                RawTradeDirection direction, double volume,
                                qint64 sourceLine) {
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, time, Qt::UTC);
        tick.sourceLine = sourceLine;
        tick.price = 100.0;
        tick.volumeShares = volume;
        tick.amount = tick.price * volume;
        tick.direction = direction;
        ticks.append(tick);
    };

    appendTick(firstDay, QTime(1, 30), RawTradeDirection::Neutral, 1000.0, 1);
    appendTick(firstDay, QTime(1, 30, 1), RawTradeDirection::Buy, 100.0, 2);
    appendTick(firstDay, QTime(1, 30, 2), RawTradeDirection::Buy, 100.0, 3);
    appendTick(firstDay, QTime(1, 30, 3), RawTradeDirection::Buy, 100.0, 4);
    appendTick(firstDay, QTime(1, 30, 4), RawTradeDirection::Sell, 100.0, 5);
    appendTick(firstDay, QTime(1, 30, 5), RawTradeDirection::Sell, 100.0, 6);
    appendTick(firstDay, QTime(1, 30, 6), RawTradeDirection::Sell, 100.0, 7);
    appendTick(secondDay, QTime(1, 30), RawTradeDirection::Neutral, 1000.0, 1);

    BacktestConfig config;
    config.strategyId = QStringLiteral("tick_directional_imbalance");
    config.strategyParameters = {
        {QStringLiteral("lookbackTicks"), 3},
        {QStringLiteral("entryImbalancePercent"), 50.0},
        {QStringLiteral("exitImbalancePercent"), -50.0},
        {QStringLiteral("minimumDirectionalCoveragePercent"), 50.0},
        {QStringLiteral("cooldownSeconds"), 0},
        {QStringLiteral("minimumHoldSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(engine.result().chartSignals.size(), 2);
    QCOMPARE(engine.result().trades.size(), 1);
    QCOMPARE(engine.result().trades.first().entryTime, ticks.at(3).timestamp);
    QCOMPARE(engine.result().trades.first().exitTime.date(), secondDay);
    QVERIFY(engine.result().trades.first().entryReason.contains(QStringLiteral("方向失衡")));
    QVERIFY(engine.result().trades.first().exitReason.contains(QStringLiteral("T+1")));
    QCOMPARE(engine.result().finalPosition.quantity, 0.0);
}

void BacktestEngineTest::rawTickBoardContinuationUsesOnlyPastTicksAndTPlusOne()
{
    Instrument instrument = testInstrument();
    instrument.symbol = QStringLiteral("600001");
    const QDate firstDay(2026, 7, 6);
    const auto runScenario = [&](int targetBoard, bool targetTouchesBeforeSignal) {
        QVector<RawTradeTick> ticks;
        const auto appendTick = [&](int dayOffset, const QTime &localTime,
                                    double price, double volume) {
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = firstDay.addDays(dayOffset);
            tick.timestamp = QDateTime(tick.tradingDate,
                                       localTime.addSecs(-8 * 3600), Qt::UTC);
            tick.sourceLine = ticks.size() + 1;
            tick.price = price;
            tick.volumeShares = volume;
            tick.amount = price * volume;
            ticks.append(tick);
        };
        appendTick(0, QTime(15, 0), 10.00, 500000);
        appendTick(1, QTime(9, 30), 10.30, 500000);
        appendTick(1, QTime(10, 0), 11.00, 500000);
        appendTick(1, QTime(15, 0), 11.00, 500000);
        if (targetBoard == 3) {
            appendTick(2, QTime(9, 30), 11.30, 500000);
            appendTick(2, QTime(10, 0), 12.10, 500000);
            appendTick(2, QTime(15, 0), 12.10, 500000);
        }
        const int targetOffset = targetBoard;
        const double priorClose = targetBoard == 2 ? 11.00 : 12.10;
        const double targetLimit = targetBoard == 2 ? 12.10 : 13.31;
        appendTick(targetOffset, QTime(9, 30), priorClose * 1.02, 500000);
        if (targetTouchesBeforeSignal)
            appendTick(targetOffset, QTime(9, 34), targetLimit, 500000);
        appendTick(targetOffset, QTime(9, 35), priorClose * 1.04, 500000);
        appendTick(targetOffset, QTime(9, 35, 1), priorClose * 1.05, 500000);
        appendTick(targetOffset, QTime(15, 0), targetLimit, 500000);
        appendTick(targetOffset + 1, QTime(9, 35), targetLimit * 1.01, 500000);
        appendTick(targetOffset + 1, QTime(9, 35, 1), targetLimit * 1.01, 500000);

        BacktestConfig config;
        config.strategyId = targetBoard == 2 ? QStringLiteral("tick_second_board")
                                             : QStringLiteral("tick_third_board");
        config.dataMode = BacktestDataMode::RawTicks;
        config.instrument = instrument;
        config.initialCapital = 1000000.0;
        BacktestEngine engine;
        QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
        QSignalSpy errorSpy(&engine, &BacktestEngine::errorOccurred);
        if (!engine.start(config, ticks)) return false;
        if (!finishedSpy.wait(5000) && finishedSpy.isEmpty()) return false;
        if (!errorSpy.isEmpty()) return false;
        const BacktestResult &result = engine.result();
        if (targetTouchesBeforeSignal) return result.chartSignals.isEmpty()
            && result.trades.isEmpty()
            && result.strategyEvaluationFacts.value(
                QStringLiteral("entryDayOutcomeKnownSignals")).toInt() == 0;
        if (result.chartSignals.size() != 2 || result.trades.size() != 1)
            return false;
        const BacktestTrade &trade = result.trades.first();
        return trade.entryTime == ticks.at(ticks.size() - 4).timestamp
            && trade.entryTime.toUTC().date() == firstDay.addDays(targetOffset)
            && trade.exitTime.toUTC().date() == firstDay.addDays(targetOffset + 1)
            && trade.quantity <= 5000.0
            && result.strategyEvaluationFacts.value(
                QStringLiteral("entryDayOutcomeKnownSignals")).toInt() == 1
            && result.strategyEvaluationFacts.value(
                QStringLiteral("entryDayClosedBoardSignals")).toInt() == 1
            && trade.exitReason.contains(QStringLiteral("封板命中"));
    };
    QVERIFY(runScenario(2, false));
    QVERIFY(runScenario(3, false));
    QVERIFY(runScenario(2, true));
}

void BacktestEngineTest::boardPortfolioUsesOneCashBookAcrossSymbols()
{
    const QDate firstDay(2026, 7, 6);
    QVector<RawTradeTick> ticks;
    for (const QString &symbol : {QStringLiteral("600001"),
                                  QStringLiteral("600002")}) {
        Instrument instrument = testInstrument();
        instrument.symbol = symbol;
        const auto append = [&](int dayOffset, QTime localTime, double price) {
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = firstDay.addDays(dayOffset);
            tick.timestamp = QDateTime(tick.tradingDate,
                                       localTime.addSecs(-8 * 3600), Qt::UTC);
            tick.sourceLine = ticks.size() + 1;
            tick.price = price;
            tick.volumeShares = 500000.0;
            tick.amount = price * tick.volumeShares;
            ticks.append(tick);
        };
        append(0, QTime(15, 0), 10.00);
        append(1, QTime(9, 30), 10.30);
        append(1, QTime(10, 0), 11.00);
        append(1, QTime(15, 0), 11.00);
        append(2, QTime(9, 30), 11.22);
        append(2, QTime(9, 35), 11.44);
        append(2, QTime(9, 35, 1), 11.55);
        append(2, QTime(15, 0), 12.10);
        append(3, QTime(9, 35), 12.22);
        append(3, QTime(9, 35, 1), 12.22);
    }
    std::sort(ticks.begin(), ticks.end(), [](const RawTradeTick &left,
                                            const RawTradeTick &right) {
        if (left.timestamp != right.timestamp) return left.timestamp < right.timestamp;
        return left.instrument.symbol < right.instrument.symbol;
    });
    BoardPortfolioConfig config;
    config.initialCapital = 100000.0;
    config.maximumHoldingSymbols = 1;
    BoardPortfolioBacktestEngine engine(config);
    QString error;
    for (const RawTradeTick &tick : ticks) {
        QVERIFY2(engine.accept(tick, &error), qPrintable(error));
    }
    const BoardPortfolioReport result = engine.finish();
    QCOMPARE(result.observedSymbols, 2);
    QCOMPARE(result.buySignals, 2);
    QCOMPARE(result.buys, 1);
    QCOMPARE(result.rejectedBuys, 1);
    QCOMPARE(result.rejectedForHoldingLimit, 1);
    QCOMPARE(result.sells, 1);
    QCOMPARE(result.maximumObservedHoldingSymbols, 1);
    QCOMPARE(result.openPositions.size(), 0);
    QCOMPARE(result.entryDayOutcomeKnownSignals, 1);
    QCOMPARE(result.entryDayClosedBoardSignals, 1);
    QCOMPARE(result.trades.size(), 2);
    QCOMPARE(result.trades.at(0).side, QStringLiteral("buy"));
    QCOMPARE(result.trades.at(1).side, QStringLiteral("sell"));
    QCOMPARE(result.trades.at(0).instrument.symbol,
             result.trades.at(1).instrument.symbol);
    QVERIFY(result.trades.at(1).timestamp > result.trades.at(0).timestamp);
    QVERIFY(result.cash > 0.0);

    BoardPortfolioConfig scopedConfig = config;
    scopedConfig.maximumHoldingSymbols = 2;
    scopedConfig.eligibleEntries.insert(
        QStringLiteral("600001|20260708|tick_second_board"));
    BoardPortfolioBacktestEngine scopedEngine(scopedConfig);
    for (const RawTradeTick &tick : ticks) {
        QVERIFY2(scopedEngine.accept(tick, &error), qPrintable(error));
    }
    const BoardPortfolioReport scoped = scopedEngine.finish();
    QCOMPARE(scoped.buySignals, 1);
    QCOMPARE(scoped.buys, 1);
    QCOMPARE(scoped.ignoredOutOfScopeSignals, 1);
    QCOMPARE(scoped.rejectedBuys, 0);
}

void BacktestEngineTest::rawTickBacktestKeepsFinalDayPurchaseAsOpenPosition()
{
    const Instrument instrument = testInstrument();
    const QDate day(2026, 1, 5);
    const QDateTime first(day, QTime(1, 30), Qt::UTC);
    const QVector<double> prices {100.0, 102.0, 98.0};
    QVector<RawTradeTick> ticks;
    for (int index = 0; index < prices.size(); ++index) {
        RawTradeTick tick;
        tick.instrument = instrument;
        // T+1 必须以 UTC 时间换算上海交易日；审计字段即使异常也不能放宽门禁。
        tick.tradingDate = day.addDays(1);
        tick.timestamp = first.addSecs(index);
        tick.sourceLine = index + 1;
        tick.price = prices.at(index);
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        ticks.append(tick);
    }

    BacktestConfig config;
    config.strategyId = QStringLiteral("tick_momentum");
    config.strategyParameters = {{QStringLiteral("lookbackTicks"), 1},
                                 {QStringLiteral("thresholdPercent"), 1.0},
                                 {QStringLiteral("cooldownSeconds"), 0},
                                 {QStringLiteral("minimumHoldSeconds"), 0}};
    config.dataMode = BacktestDataMode::RawTicks;
    config.instrument = instrument;
    config.initialCapital = 1000000.0;

    BacktestEngine engine;
    QSignalSpy finishedSpy(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, ticks));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QVERIFY(engine.result().trades.isEmpty());
    QVERIFY(engine.result().finalPosition.quantity > 0.0);
    QCOMPARE(engine.result().finalPosition.entryTime, ticks.at(1).timestamp);
    QVERIFY(engine.result().equityCurve.last().positionValue > 0.0);
    QCOMPARE(engine.result().summary.totalTrades, 0);
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
    QCOMPARE(first.result().chartSignals.size(), 2);
    QCOMPARE(first.result().chartSignals.at(0).timestamp, candles.at(0).startTime);
    QCOMPARE(first.result().chartSignals.at(0).side, SignalSide::Buy);
    QCOMPARE(first.result().chartSignals.at(0).reason, QStringLiteral("测试进入"));
    QCOMPARE(first.result().chartSignals.at(1).timestamp, candles.at(2).startTime);
    QCOMPARE(first.result().chartSignals.at(1).side, SignalSide::Sell);
    QCOMPARE(first.result().chartSignals.at(1).reason, QStringLiteral("测试退出"));

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
