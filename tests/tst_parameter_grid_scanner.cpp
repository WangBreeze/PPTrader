#include "services/ParameterGridScanner.h"
#include "services/BacktestEngine.h"
#include "strategy/CustomBacktestStrategy.h"
#include <QSignalSpy>
#include "services/RawTickParameterSensitivityScanner.h"
#include "strategy/CustomStrategyValidator.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QTest>

namespace pp {

class ParameterGridScannerTest : public QObject {
    Q_OBJECT

private slots:
    void expandGridComputesCartesianProduct();
    void scanRunsAllCombinationsAndSorts();
    void scanRejectsTooManyCombinations();
    void scanReportsEmptyGrid();
    void nextOpenUsesActualOpenInEngineAndScan();
    void candleTPlusOneMatchesEngineAndScan_data();
    void candleTPlusOneMatchesEngineAndScan();
    void rawTickSensitivitySelectsOnTrainingAndEvaluatesHeldOutTicks();
    void rawTickSensitivityNestedWalkForwardKeepsOuterFoldsIsolated();
    void rawTickSensitivityReportsOutOfSamplePayoffRatio();
    void rawTickSensitivityRejectsTamperedOuterFoldIdentity();
};

namespace {

QJsonObject gridDocument()
{
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("strategyId"), QStringLiteral("custom_grid_threshold")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("name"), QStringLiteral("网格阈值")},
            {QStringLiteral("author"), QStringLiteral("Qt Test")},
            {QStringLiteral("market"), QStringLiteral("A股")},
            {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-08-15T00:00:00.000Z")},
            {QStringLiteral("updatedAt"), QStringLiteral("2026-08-15T00:00:00.000Z")},
            {QStringLiteral("parameters"), QJsonArray{
                QJsonObject{{QStringLiteral("key"), QStringLiteral("buyLevel")},
                            {QStringLiteral("displayName"), QStringLiteral("买入阈值")},
                            {QStringLiteral("type"), QStringLiteral("real")},
                            {QStringLiteral("defaultValue"), 100},
                            {QStringLiteral("minimumValue"), 90},
                            {QStringLiteral("maximumValue"), 110},
                            {QStringLiteral("step"), 10}},
                QJsonObject{{QStringLiteral("key"), QStringLiteral("sellLevel")},
                            {QStringLiteral("displayName"), QStringLiteral("卖出阈值")},
                            {QStringLiteral("type"), QStringLiteral("real")},
                            {QStringLiteral("defaultValue"), 90},
                            {QStringLiteral("minimumValue"), 80},
                            {QStringLiteral("maximumValue"), 100},
                            {QStringLiteral("step"), 10}}}},
            {QStringLiteral("entry"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("gt")},
                {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
                {QStringLiteral("right"), QJsonObject{{QStringLiteral("parameter"), QStringLiteral("buyLevel")}}}}},
            {QStringLiteral("exit"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("lt")},
                {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
                {QStringLiteral("right"), QJsonObject{{QStringLiteral("parameter"), QStringLiteral("sellLevel")}}}}}};
}

CustomStrategyDefinition validatedDefinition(QString *error)
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QJsonObject document = gridDocument();
    validator.validate(document, &definition, error);
    return definition;
}

QVector<Candle> makeCandles()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    const QVector<double> closes {85.0, 90.0, 95.0, 105.0, 115.0, 120.0, 95.0, 85.0};
    QVector<Candle> candles;
    for (int index = 0; index < closes.size(); ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 8, 17), QTime(9, 30), Qt::UTC).addDays(index);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = closes.at(index);
        candle.volume = 1000.0;
        candles.append(candle);
    }
    return candles;
}

BacktestConfig makeConfig()
{
    BacktestConfig config;
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    config.instrument = instrument;
    config.periodSeconds = 86400;
    config.initialCapital = 1000000.0;
    config.commissionRate = 0.0;
    config.slippageRate = 0.0;
    config.lotSize = 100;
    return config;
}

QVector<RawTradeTick> makeRawSensitivityTicks()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("上交所");
    const QVector<QDate> dates {
        QDate(2026, 8, 17), QDate(2026, 8, 18), QDate(2026, 8, 19),
        QDate(2026, 8, 20), QDate(2026, 8, 21), QDate(2026, 8, 24)};
    QVector<RawTradeTick> ticks;
    const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    for (int dayIndex = 0; dayIndex < dates.size(); ++dayIndex) {
        for (int tickIndex = 0; tickIndex < 8; ++tickIndex) {
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = dates.at(dayIndex);
            tick.timestamp = QDateTime(dates.at(dayIndex), QTime(9, 30), shanghai)
                                 .addSecs(tickIndex).toUTC();
            tick.sourceLine = tickIndex + 1;
            const double direction = dayIndex < 4 ? 1.0 : -1.0;
            tick.price = 100.0 + dayIndex * 2.0 + direction * tickIndex * 0.4;
            tick.volumeShares = 100.0;
            tick.amount = tick.price * tick.volumeShares;
            ticks.append(tick);
        }
    }
    return ticks;
}

QVector<RawTradeTick> makeNestedRawSensitivityTicks()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("上交所");
    const QVector<QDate> dates {
        QDate(2026, 8, 11), QDate(2026, 8, 12), QDate(2026, 8, 13),
        QDate(2026, 8, 14), QDate(2026, 8, 17), QDate(2026, 8, 18),
        QDate(2026, 8, 19), QDate(2026, 8, 20)};
    QVector<RawTradeTick> ticks;
    const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    for (int dayIndex = 0; dayIndex < dates.size(); ++dayIndex) {
        for (int tickIndex = 0; tickIndex < 8; ++tickIndex) {
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = dates.at(dayIndex);
            tick.timestamp = QDateTime(dates.at(dayIndex), QTime(9, 30), shanghai)
                                 .addSecs(tickIndex).toUTC();
            tick.sourceLine = tickIndex + 1;
            const double direction = dayIndex % 3 == 0 ? -1.0 : 1.0;
            tick.price = 100.0 + dayIndex + direction * tickIndex * 0.4;
            tick.volumeShares = 100.0;
            tick.amount = tick.price * tick.volumeShares;
            ticks.append(tick);
        }
    }
    return ticks;
}

} // namespace

void ParameterGridScannerTest::expandGridComputesCartesianProduct()
{
    const QVector<ParameterGridAxis> axes {
        {QStringLiteral("a"), {1.0, 2.0}},
        {QStringLiteral("b"), {10.0, 20.0, 30.0}},
    };
    const auto combos = ParameterGridScanner::expandGrid(axes);
    QCOMPARE(combos.size(), 6);
    QCOMPARE(combos.at(0).value(QStringLiteral("a")).toDouble(), 1.0);
    QCOMPARE(combos.at(0).value(QStringLiteral("b")).toDouble(), 10.0);
    QCOMPARE(combos.at(5).value(QStringLiteral("a")).toDouble(), 2.0);
    QCOMPARE(combos.at(5).value(QStringLiteral("b")).toDouble(), 30.0);
}

void ParameterGridScannerTest::scanRunsAllCombinationsAndSorts()
{
    QString error;
    const CustomStrategyDefinition definition = validatedDefinition(&error);
    QVERIFY2(!definition.strategyId.isEmpty(), qPrintable(error));

    const QVector<ParameterGridAxis> axes {
        {QStringLiteral("buyLevel"), {100.0, 110.0}},
        {QStringLiteral("sellLevel"), {90.0, 80.0}},
    };
    const auto results = ParameterGridScanner::scan(
        definition, axes, makeCandles(), makeConfig(), 512, nullptr, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(results.size(), 4);

    // 按收益率降序。
    for (int index = 1; index < results.size(); ++index) {
        QVERIFY(results.at(index - 1).summary.totalReturnPercent
                >= results.at(index).summary.totalReturnPercent);
    }
    // 最优组合应为 buyLevel=100, sellLevel=90（买入更早、卖出及时，亏损最小）。
    QCOMPARE(results.first().parameters.value(QStringLiteral("buyLevel")).toDouble(), 100.0);
    QCOMPARE(results.first().parameters.value(QStringLiteral("sellLevel")).toDouble(), 90.0);
    QVERIFY(results.first().summary.totalTrades > 0);
}

void ParameterGridScannerTest::nextOpenUsesActualOpenInEngineAndScan()
{
    QJsonObject doc = gridDocument();
    doc.insert(QStringLiteral("schemaVersion"), 2);
    doc.insert(QStringLiteral("signal"), QJsonObject{{QStringLiteral("priceMode"), QStringLiteral("nextOpen")}});
    doc.insert(QStringLiteral("risk"), QJsonObject{{QStringLiteral("stopLossPercent"), 4.0}});
    QString error;
    CustomStrategyDefinition definition;
    QVERIFY2(CustomStrategyValidator().validate(doc, &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    auto bars = makeCandles().mid(0, 3);
    const double opens[] = {101, 105, 100};
    const double closes[] = {101, 110, 100};
    for (int i = 0; i < 3; ++i) {
        bars[i].open = opens[i]; bars[i].close = closes[i];
        bars[i].high = std::max(opens[i], closes[i]);
        bars[i].low = std::min(opens[i], closes[i]);
    }
    auto config = makeConfig();
    config.initialCapital = 100000;
    config.commissionRate = 0.001;
    config.strategyId = plan->strategyId;
    config.strategyVersion = plan->version;
    config.strategyParameters = plan->normalizedParameters;
    config.ruleSnapshot = plan->rules.toVariantMap();
    BacktestEngine engine;
    QSignalSpy finished(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, bars, std::make_unique<CustomBacktestStrategy>(*plan)));
    QTRY_COMPARE(finished.size(), 1);
    const auto result = engine.result();
    QCOMPARE(result.trades.size(), 1);
    QCOMPARE(result.trades.first().entryTime, bars[1].startTime);
    QCOMPARE(result.trades.first().entryPrice, 105.0);
    QCOMPARE(result.trades.first().quantity, 900.0);
    QCOMPARE(result.trades.first().exitPrice, 100.0);
    QVERIFY(result.trades.first().exitReason.contains(QStringLiteral("止损")));
    QVERIFY(std::abs(result.summary.totalCommission - 184.5) < 1e-8);
    QVERIFY(std::abs(result.summary.finalEquity - 95315.5) < 1e-8);
    const auto scanned = ParameterGridScanner::scan(definition, {}, bars, config, 512, nullptr, &error);
    QCOMPARE(scanned.size(), 1);
    QCOMPARE(scanned.first().summary.finalEquity, result.summary.finalEquity);
    QCOMPARE(scanned.first().summary.totalTrades, result.summary.totalTrades);
}

void ParameterGridScannerTest::candleTPlusOneMatchesEngineAndScan_data()
{
    QTest::addColumn<bool>("daily");
    QTest::addColumn<bool>("olderPosition");
    QTest::newRow("intraday-purchase") << false << false;
    QTest::newRow("daily-final-purchase") << true << false;
    QTest::newRow("prior-day-purchase") << false << true;
}

void ParameterGridScannerTest::candleTPlusOneMatchesEngineAndScan()
{
    QFETCH(bool, daily);
    QFETCH(bool, olderPosition);
    QString error;
    const auto definition = validatedDefinition(&error);
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY(plan.has_value());
    auto bars = makeCandles().mid(0, 3);
    for (int i = 0; i < bars.size(); ++i) {
        const double price = daily && i < 2 ? 90 : 105;
        bars[i].open = bars[i].high = bars[i].low = bars[i].close = price;
        bars[i].periodSeconds = daily ? 86400 : 60;
        if (!daily) bars[i].startTime = bars[0].startTime.addSecs(i * 60)
            .addDays(olderPosition && i > 0 ? 1 : 0);
    }
    auto config = makeConfig();
    config.initialCapital = 100000;
    config.commissionRate = 0.001;
    config.periodSeconds = daily ? 86400 : 60;
    config.strategyId = plan->strategyId;
    config.strategyVersion = plan->version;
    config.strategyParameters = plan->normalizedParameters;
    config.ruleSnapshot = plan->rules.toVariantMap();
    BacktestEngine engine;
    QSignalSpy finished(&engine, &BacktestEngine::finished);
    QVERIFY(engine.start(config, bars, std::make_unique<CustomBacktestStrategy>(*plan)));
    QTRY_COMPARE(finished.size(), 1);
    const auto result = engine.result();
    QCOMPARE(result.trades.size(), olderPosition ? 1 : 0);
    QCOMPARE(result.finalPosition.quantity, olderPosition ? 0.0 : 900.0);
    QVERIFY(std::abs(result.summary.finalEquity - (olderPosition ? 99811.0 : 99905.5)) < 1e-7);
    const auto scanned = ParameterGridScanner::scan(definition, {}, bars, config, 512, nullptr, &error);
    QCOMPARE(scanned.size(), 1);
    QCOMPARE(scanned.first().summary.finalEquity, result.summary.finalEquity);
    QCOMPARE(scanned.first().summary.totalTrades, result.summary.totalTrades);
}

void ParameterGridScannerTest::scanRejectsTooManyCombinations()
{
    QString error;
    const CustomStrategyDefinition definition = validatedDefinition(&error);
    const QVector<ParameterGridAxis> axes {
        {QStringLiteral("buyLevel"), {100.0, 110.0, 120.0}},
        {QStringLiteral("sellLevel"), {90.0, 80.0, 70.0}},
    };
    const auto results = ParameterGridScanner::scan(
        definition, axes, makeCandles(), makeConfig(), 8, nullptr, &error);
    QVERIFY(results.isEmpty());
    QVERIFY(error.contains(QStringLiteral("组合过多")));
}

void ParameterGridScannerTest::scanReportsEmptyGrid()
{
    QString error;
    const CustomStrategyDefinition definition = validatedDefinition(&error);
    const auto results = ParameterGridScanner::scan(
        definition, {}, makeCandles(), makeConfig(), 512, nullptr, &error);
    // 空轴 → 笛卡尔积只有 1 个空组合，用默认参数回测一次。
    QCOMPARE(results.size(), 1);
}

void ParameterGridScannerTest::rawTickSensitivitySelectsOnTrainingAndEvaluatesHeldOutTicks()
{
    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05, 5.0}}};
    request.validationTradingDays = 2;
    request.maximumCombinations = 16;
    request.baseConfig = makeConfig();
    request.baseConfig.dataMode = BacktestDataMode::RawTicks;
    request.baseConfig.periodSeconds = 0;
    request.baseConfig.frequency = QStringLiteral("tick");

    QString error;
    const auto result = RawTickParameterSensitivityScanner::scan(
        request, makeRawSensitivityTicks(), nullptr, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->trainingStartDate, QDate(2026, 8, 17));
    QCOMPARE(result->trainingEndDate, QDate(2026, 8, 20));
    QCOMPARE(result->validationStartDate, QDate(2026, 8, 21));
    QCOMPARE(result->validationEndDate, QDate(2026, 8, 24));
    QCOMPARE(result->trainingActualTicks, 32);
    QCOMPARE(result->validationActualTicks, 16);
    QCOMPARE(result->candidates.size(), 2);
    QCOMPARE(result->selectedParameters.value(
                 QStringLiteral("thresholdPercent")).toDouble(), 0.05);
    QVERIFY(result->trainingDataVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(result->validationDataVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(result->trainingDataVersion != result->validationDataVersion);
    QVERIFY(result->candidateSetSha256.startsWith(QStringLiteral("sha256:")));
    QVERIFY(result->identitySha256.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(result->selectionCriterion,
             QStringLiteral("training_return_desc_drawdown_asc_parameters_v1"));
    QVERIFY(result->researchOnly);
}

void ParameterGridScannerTest::rawTickSensitivityNestedWalkForwardKeepsOuterFoldsIsolated()
{
    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05, 5.0}}};
    request.validationTradingDays = 2;
    request.walkForwardFoldCount = 3;
    request.maximumCombinations = 16;
    request.baseConfig = makeConfig();
    request.baseConfig.dataMode = BacktestDataMode::RawTicks;
    request.baseConfig.periodSeconds = 0;
    request.baseConfig.frequency = QStringLiteral("tick");

    QString error;
    const auto result = RawTickParameterSensitivityScanner::scan(
        request, makeNestedRawSensitivityTicks(), nullptr, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->analysisMode, QStringLiteral("nested_walk_forward"));
    QCOMPARE(result->walkForwardFoldCount, 3);
    QCOMPARE(result->folds.size(), 3);

    QCOMPARE(result->folds.at(0).trainingStartDate, QDate(2026, 8, 11));
    QCOMPARE(result->folds.at(0).trainingEndDate, QDate(2026, 8, 12));
    QCOMPARE(result->folds.at(0).validationStartDate, QDate(2026, 8, 13));
    QCOMPARE(result->folds.at(0).validationEndDate, QDate(2026, 8, 14));
    QCOMPARE(result->folds.at(1).trainingEndDate, QDate(2026, 8, 14));
    QCOMPARE(result->folds.at(1).validationStartDate, QDate(2026, 8, 17));
    QCOMPARE(result->folds.at(1).validationEndDate, QDate(2026, 8, 18));
    QCOMPARE(result->folds.at(2).trainingEndDate, QDate(2026, 8, 18));
    QCOMPARE(result->folds.at(2).validationStartDate, QDate(2026, 8, 19));
    QCOMPARE(result->folds.at(2).validationEndDate, QDate(2026, 8, 20));

    for (const RawTickParameterSensitivityFold &fold : result->folds) {
        QVERIFY(fold.trainingEndDate < fold.validationStartDate);
        QCOMPARE(fold.candidates.size(), 2);
        QVERIFY(!fold.selectedParameters.isEmpty());
        QVERIFY(fold.trainingDataVersion.startsWith(QStringLiteral("sha256:")));
        QVERIFY(fold.validationDataVersion.startsWith(QStringLiteral("sha256:")));
        QVERIFY(fold.trainingDataVersion != fold.validationDataVersion);
    }
    QCOMPARE(result->positiveValidationFoldCount
             + result->nonPositiveValidationFoldCount, 3);
    QVERIFY(result->parameterSelectionVariantCount >= 1);
    QVERIFY(result->identitySha256.startsWith(QStringLiteral("sha256:")));
    QVERIFY(result->researchOnly);
}

void ParameterGridScannerTest::rawTickSensitivityReportsOutOfSamplePayoffRatio()
{
    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 10},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05}}};
    request.validationTradingDays = 2;
    request.maximumCombinations = 4;
    request.baseConfig = makeConfig();
    request.baseConfig.dataMode = BacktestDataMode::RawTicks;
    request.baseConfig.periodSeconds = 0;
    request.baseConfig.frequency = QStringLiteral("tick");
    request.datasetVersion = QStringLiteral("sha256:") + QString(64, 'd');

    QVector<RawTickDayFingerprint> days;
    for (int index = 0; index < 4; ++index) {
        RawTickDayFingerprint day;
        day.tradingDate = QDate(2026, 8, 17).addDays(index);
        day.rawCount = 10;
        days.append(day);
    }
    const auto runner = [](BacktestConfig config, const QDate &firstDate,
                           const QDate &lastDate, QString *)
        -> std::optional<BacktestResult> {
        BacktestResult result;
        result.config = config;
        result.config.actualBars = 10 * (firstDate.daysTo(lastDate) + 1);
        result.config.dataVersion = QStringLiteral("sha256:")
            + QString(64, firstDate == QDate(2026, 8, 17) ? 'a' : 'b');
        result.summary.initialCapital = config.initialCapital;
        result.summary.finalEquity = config.initialCapital;
        if (firstDate == QDate(2026, 8, 19)) {
            const QVector<double> netPnls {300.0, 600.0, -100.0, -200.0};
            for (double netPnl : netPnls) {
                BacktestTrade trade;
                trade.netPnl = netPnl;
                result.trades.append(trade);
            }
            result.summary.totalTrades = 4;
            result.summary.winningTrades = 2;
            result.summary.losingTrades = 2;
        }
        return result;
    };

    QString error;
    const auto result = RawTickParameterSensitivityScanner::scanSegments(
        request, days, runner, nullptr, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    QCOMPARE(result->folds.size(), 1);
    const RawTickParameterSensitivityFold &fold = result->folds.first();
    QCOMPARE(fold.validationWinningTrades, 2);
    QCOMPARE(fold.validationLosingTrades, 2);
    QCOMPARE(fold.validationGrossProfit, 900.0);
    QCOMPARE(fold.validationGrossLossAbsolute, 300.0);
    QCOMPARE(fold.validationAverageWinningNetPnl, 450.0);
    QCOMPARE(fold.validationAverageLosingNetPnlAbsolute, 150.0);
    QVERIFY(fold.validationPayoffRatioAvailable);
    QCOMPARE(fold.validationPayoffRatio, 3.0);
    QVERIFY(result->validationPayoffRatioAvailable);
    QCOMPARE(result->validationPayoffRatio, 3.0);
    const auto restoredFold = RawTickParameterSensitivityFold::fromVariantMap(
        fold.toVariantMap());
    QVERIFY(restoredFold.has_value());
    QVERIFY(restoredFold->validationPayoffRatioAvailable);
    QCOMPARE(restoredFold->validationPayoffRatio, 3.0);
    QVariantMap tamperedFold = fold.toVariantMap();
    tamperedFold.insert(QStringLiteral("validationPayoffRatio"), 99.0);
    QVERIFY(!RawTickParameterSensitivityFold::fromVariantMap(
        tamperedFold).has_value());
    RawTickParameterSensitivityResult archivedResult = *result;
    archivedResult.resultId = QStringLiteral("payoff-ratio-fixture");
    archivedResult.completedAt = QDateTime(
        QDate(2026, 8, 29), QTime(6, 7), Qt::UTC);
    QVariantMap tamperedResult = archivedResult.toVariantMap();
    tamperedResult.insert(QStringLiteral("validationPayoffRatio"), 99.0);
    QVERIFY(!RawTickParameterSensitivityResult::fromVariantMap(
        tamperedResult).has_value());
}

void ParameterGridScannerTest::rawTickSensitivityRejectsTamperedOuterFoldIdentity()
{
    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05, 5.0}}};
    request.validationTradingDays = 2;
    request.walkForwardFoldCount = 3;
    request.maximumCombinations = 16;
    request.baseConfig = makeConfig();
    request.baseConfig.dataMode = BacktestDataMode::RawTicks;
    request.datasetVersion = QStringLiteral("sha256:") + QString(64, 'd');

    QString error;
    auto result = RawTickParameterSensitivityScanner::scan(
        request, makeNestedRawSensitivityTicks(), nullptr, &error);
    QVERIFY2(result.has_value(), qPrintable(error));
    result->resultId = QStringLiteral("nested-tamper-fixture");
    result->completedAt = QDateTime(QDate(2026, 8, 29), QTime(6, 7), Qt::UTC);
    QVariantMap record = result->toVariantMap();
    QVariantList folds = record.value(QStringLiteral("folds")).toList();
    QVariantMap firstFold = folds.first().toMap();
    firstFold.insert(QStringLiteral("trainingDataVersion"),
                     QStringLiteral("not-a-sha256"));
    folds[0] = firstFold;
    record.insert(QStringLiteral("folds"), folds);

    QVERIFY(!RawTickParameterSensitivityResult::fromVariantMap(record).has_value());
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::ParameterGridScannerTest)

#include "tst_parameter_grid_scanner.moc"
