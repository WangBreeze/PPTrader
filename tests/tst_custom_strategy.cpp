#include "app/CustomStrategyWorkspacePolicy.h"
#include "models/CustomStrategyListModel.h"
#include "services/CustomStrategyPreviewService.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CustomStrategyValidator.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace pp {

class CustomStrategyTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesRestrictedSchemaAndRejectsUnsafeInputs();
    void persistsImmutableVersionsAndExposesModelRoles();
    void calculatesIndicatorsWithExplicitWarmupAndMissingValues();
    void compilesEveryWhitelistedIndicator();
    void v2SupportsAliasedSeriesAndNewIndicators();
    void v2SupportsTopLevelSeries();
    void v1RejectsV2OnlySyntax();
    void nextOpenSignalFillPrice();
    void buildsPreviewSeriesAndSignals();
    void previewUsesOnlyLocalTickDerivedCandles();
    void compilesCrossConditionsAndResetsDeterministically();
    void validatesCompilesAndExecutesPositionRiskRules();
    void enforcesWorkspaceRunAndResultIdentity();
};

static QJsonObject indicator(const QString &name, const QJsonValue &period)
{
    return {{QStringLiteral("indicator"), name}, {QStringLiteral("period"), period}};
}

static QJsonObject indicatorWithAlias(const QString &name, int period,
                                      const QString &alias)
{
    return {{QStringLiteral("indicator"), name},
            {QStringLiteral("period"), period},
            {QStringLiteral("alias"), alias}};
}

static QJsonObject parameterReference(const QString &key)
{
    return {{QStringLiteral("parameter"), key}};
}

static QJsonObject validDocument()
{
    const QJsonArray parameters {
        QJsonObject{{QStringLiteral("key"), QStringLiteral("fastPeriod")},
                    {QStringLiteral("displayName"), QStringLiteral("快速周期")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("defaultValue"), 5},
                    {QStringLiteral("minimumValue"), 2},
                    {QStringLiteral("maximumValue"), 120},
                    {QStringLiteral("step"), 1}},
        QJsonObject{{QStringLiteral("key"), QStringLiteral("slowPeriod")},
                    {QStringLiteral("displayName"), QStringLiteral("慢速周期")},
                    {QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("defaultValue"), 20},
                    {QStringLiteral("minimumValue"), 3},
                    {QStringLiteral("maximumValue"), 250},
                    {QStringLiteral("step"), 1}}
    };
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("strategyId"), QStringLiteral("custom_sma_filter")},
            {QStringLiteral("version"), 1},
            {QStringLiteral("name"), QStringLiteral("自定义均线过滤")},
            {QStringLiteral("description"), QStringLiteral("仅用于回测的版本化示例")},
            {QStringLiteral("author"), QStringLiteral("Qt Test")},
            {QStringLiteral("market"), QStringLiteral("A股")},
            {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
            {QStringLiteral("createdAt"), QStringLiteral("2026-08-08T00:00:00.000Z")},
            {QStringLiteral("updatedAt"), QStringLiteral("2026-08-08T00:00:00.000Z")},
            {QStringLiteral("parameters"), parameters},
            {QStringLiteral("entry"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("crossesAbove")},
                {QStringLiteral("left"), indicator(QStringLiteral("SMA"), parameterReference(QStringLiteral("fastPeriod")))},
                {QStringLiteral("right"), indicator(QStringLiteral("SMA"), parameterReference(QStringLiteral("slowPeriod")))}}},
            {QStringLiteral("exit"), QJsonObject{
                {QStringLiteral("op"), QStringLiteral("crossesBelow")},
                {QStringLiteral("left"), indicator(QStringLiteral("SMA"), parameterReference(QStringLiteral("fastPeriod")))},
                {QStringLiteral("right"), indicator(QStringLiteral("SMA"), parameterReference(QStringLiteral("slowPeriod")))}}}};
}

void CustomStrategyTest::validatesRestrictedSchemaAndRejectsUnsafeInputs()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(validator.validate(validDocument(), &definition, &error));
    QCOMPARE(definition.strategyId, QStringLiteral("custom_sma_filter"));
    QCOMPARE(definition.executionScope, QStringLiteral("backtest_only"));
    QCOMPARE(definition.toVariantMap().value(QStringLiteral("executable")).toBool(), true);
    QVERIFY(error.isEmpty());
    QCOMPARE(validator.schemaDescriptor().value(QStringLiteral("stage")).toString(),
             QStringLiteral("backtest_ready"));

    QJsonObject unknownIndicator = validDocument();
    QJsonObject entry = unknownIndicator.value(QStringLiteral("entry")).toObject();
    entry.insert(QStringLiteral("left"), indicator(QStringLiteral("FUTURE_CLOSE"), 5));
    unknownIndicator.insert(QStringLiteral("entry"), entry);
    QVERIFY(!validator.validate(unknownIndicator, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("未知指标")));

    QJsonObject futureField = validDocument();
    entry = futureField.value(QStringLiteral("entry")).toObject();
    entry.insert(QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("futureClose")}});
    futureField.insert(QStringLiteral("entry"), entry);
    QVERIFY(!validator.validate(futureField, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("未来字段")));

    QJsonObject unsafeScope = validDocument();
    unsafeScope.insert(QStringLiteral("executionScope"), QStringLiteral("automatic_trading"));
    QVERIFY(!validator.validate(unsafeScope, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("backtest_only")));

    QJsonObject invalidParameter = validDocument();
    QJsonArray parameterItems = invalidParameter.value(QStringLiteral("parameters")).toArray();
    QJsonObject firstParameter = parameterItems.at(0).toObject();
    firstParameter.insert(QStringLiteral("defaultValue"), 999);
    parameterItems.replace(0, firstParameter);
    invalidParameter.insert(QStringLiteral("parameters"), parameterItems);
    QVERIFY(!validator.validate(invalidParameter, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("范围")));

    QVERIFY(!validator.parseAndValidate(QStringLiteral("{not-json"), nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("JSON 无效")));
}

void CustomStrategyTest::persistsImmutableVersionsAndExposesModelRoles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("custom-strategy.db"));
    CustomStrategyValidator validator;
    CustomStrategyDefinition versionOne;
    QString error;
    QVERIFY(validator.validate(validDocument(), &versionOne, &error));

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QVERIFY(store.insertCustomStrategy(versionOne));
        QVERIFY(store.insertCustomStrategy(versionOne));

        QJsonObject conflictingDocument = validDocument();
        conflictingDocument.insert(QStringLiteral("name"), QStringLiteral("同版本被修改"));
        CustomStrategyDefinition conflict;
        QVERIFY(validator.validate(conflictingDocument, &conflict, &error));
        QVERIFY(!store.insertCustomStrategy(conflict));

        QJsonObject nextDocument = validDocument();
        nextDocument.insert(QStringLiteral("version"), 2);
        nextDocument.insert(QStringLiteral("updatedAt"), QStringLiteral("2026-08-09T00:00:00.000Z"));
        CustomStrategyDefinition versionTwo;
        QVERIFY(validator.validate(nextDocument, &versionTwo, &error));
        QVERIFY(store.insertCustomStrategy(versionTwo));

        const QVector<CustomStrategyDefinition> loaded = store.loadCustomStrategies();
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).version, 2);
        QCOMPARE(loaded.at(1).version, 1);

        CustomStrategyListModel model;
        QSignalSpy countSpy(&model, &CustomStrategyListModel::countChanged);
        model.loadDefinitions(loaded);
        QCOMPARE(model.count(), 2);
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(model.indexOf(QStringLiteral("custom_sma_filter")), 0);
        QCOMPARE(model.indexOf(QStringLiteral("custom_sma_filter"), 1), 1);
        QCOMPARE(model.get(0).value(QStringLiteral("latest")).toBool(), true);
        QCOMPARE(model.get(1).value(QStringLiteral("latest")).toBool(), false);
        QCOMPARE(model.get(0).value(QStringLiteral("stage")).toString(), QStringLiteral("backtest_ready"));
        QCOMPARE(model.get(0).value(QStringLiteral("executable")).toBool(), true);
        QVERIFY(model.get(-1).isEmpty());

        const QString selectedStrategyId = QStringLiteral("custom_sma_filter");
        const int selectedVersion = 1;
        CustomStrategyDefinition versionThree = versionTwo;
        versionThree.version = 3;
        QVector<CustomStrategyDefinition> refreshed = loaded;
        refreshed.append(versionThree);
        model.loadDefinitions(refreshed);
        QCOMPARE(model.indexOf(selectedStrategyId, selectedVersion), 2);
        QCOMPARE(model.get(model.indexOf(selectedStrategyId, selectedVersion))
                     .value(QStringLiteral("version")).toInt(), selectedVersion);
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(databasePath));
    QCOMPARE(reopened.loadCustomStrategies().size(), 2);
}

void CustomStrategyTest::enforcesWorkspaceRunAndResultIdentity()
{
    const QVariantMap invalidDefinition =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            false, false, {}, 0, 0, nullptr, 10, false);
    QVERIFY(!invalidDefinition.value(QStringLiteral("available")).toBool());
    QVERIFY(invalidDefinition.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("选择并校验")));

    const QVariantMap notExecutable =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            true, false, QStringLiteral("custom_fixture"), 1, 3,
            nullptr, 10, false);
    QVERIFY(!notExecutable.value(QStringLiteral("available")).toBool());
    QVERIFY(notExecutable.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("backtest_ready")));

    const QVariantMap missingDataset =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            true, true, QStringLiteral("custom_fixture"), 1, 3,
            nullptr, 10, false);
    QVERIFY(!missingDataset.value(QStringLiteral("available")).toBool());
    QVERIFY(missingDataset.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("SQLite")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("workspace.db"))));
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");
    for (int row = 0; row < 12; ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 7, 1).addDays(row),
                                     QTime(7, 0), Qt::UTC);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = 100.0 + row;
        candle.volume = 1000.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        QVERIFY(store.appendCandle(candle));
    }
    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(store.loadRecentCandles(instrument, 86400, 10).size(), 10);

    const QVariantMap insufficient =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            true, true, QStringLiteral("custom_fixture"), 1, 3,
            &catalog.first(), 20, false);
    QVERIFY(!insufficient.value(QStringLiteral("available")).toBool());
    QVERIFY(insufficient.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("仅 12 根")));

    const QVariantMap available =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            true, true, QStringLiteral("custom_fixture"), 1, 3,
            &catalog.first(), 10, false);
    QVERIFY(available.value(QStringLiteral("available")).toBool());
    QCOMPARE(available.value(QStringLiteral("symbol")).toString(),
             QStringLiteral("600519"));
    QCOMPARE(available.value(QStringLiteral("frequency")).toString(),
             QStringLiteral("day"));

    LocalHistoryDataset remoteOnly = catalog.first();
    remoteOnly.source = QStringLiteral("mootdx");
    const QVariantMap remoteRejected =
        CustomStrategyWorkspacePolicy::localRunAvailability(
            true, true, QStringLiteral("custom_fixture"), 1, 3,
            &remoteOnly, 10, false);
    QVERIFY(!remoteRejected.value(QStringLiteral("available")).toBool());
    QVERIFY(remoteRejected.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("历史 Tick")));

    const QVariantMap result {{QStringLiteral("strategyId"),
                               QStringLiteral("custom_fixture")},
                              {QStringLiteral("strategyVersion"), 2}};
    QVERIFY(CustomStrategyWorkspacePolicy::resultMatches(
        result, QStringLiteral("custom_fixture"), 2));
    QVERIFY(!CustomStrategyWorkspacePolicy::resultMatches(
        result, QStringLiteral("custom_fixture"), 1));
}

void CustomStrategyTest::calculatesIndicatorsWithExplicitWarmupAndMissingValues()
{
    QVector<CompiledCustomIndicator> indicators;
    indicators.append({CustomIndicatorKind::Sma, 3});
    indicators.append({CustomIndicatorKind::Ema, 3});
    indicators.append({CustomIndicatorKind::Rsi, 2});
    CompiledCustomIndicator macd;
    macd.kind = CustomIndicatorKind::Macd;
    macd.fastPeriod = 2;
    macd.slowPeriod = 3;
    macd.signalPeriod = 2;
    macd.output = QStringLiteral("histogram");
    indicators.append(macd);
    CompiledCustomIndicator bollinger;
    bollinger.kind = CustomIndicatorKind::Bollinger;
    bollinger.period = 3;
    bollinger.standardDeviations = 2.0;
    bollinger.output = QStringLiteral("upper");
    indicators.append(bollinger);
    indicators.append({CustomIndicatorKind::Atr, 2});
    indicators.append({CustomIndicatorKind::VolumeSma, 2});

    CustomIndicatorRuntime runtime(indicators);
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    for (int row = 1; row <= 4; ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 1, row), QTime(7, 0), Qt::UTC);
        candle.open = candle.close = row;
        candle.high = row + 1.0;
        candle.low = std::max(0.1, row - 1.0);
        candle.volume = row * 100.0;
        runtime.append(candle);
        if (row < 3) QVERIFY(!runtime.value(0));
    }
    QCOMPARE(*runtime.value(0), 3.0);
    QCOMPARE(*runtime.value(1), 3.0);
    QCOMPARE(*runtime.value(2), 100.0);
    QVERIFY(runtime.value(3).has_value());
    const double expectedUpper = 3.0 + std::sqrt(2.0 / 3.0) * 2.0;
    QVERIFY(std::abs(*runtime.value(4) - expectedUpper) < 1.0e-9);
    QCOMPARE(*runtime.value(5), 2.0);
    QCOMPARE(*runtime.value(6), 350.0);

    Candle missing;
    missing.instrument = instrument;
    missing.startTime = QDateTime(QDate(2026, 1, 5), QTime(7, 0), Qt::UTC);
    missing.close = 0.0;
    missing.high = 0.0;
    missing.low = 0.0;
    missing.volume = 500.0;
    runtime.append(missing);
    QVERIFY(!runtime.value(0));
    QVERIFY(!runtime.value(1));
    QVERIFY(!runtime.value(2));
    QVERIFY(!runtime.value(3));
    QVERIFY(!runtime.value(4));
    QVERIFY(!runtime.value(5));
    QVERIFY(runtime.value(6).has_value());
}

void CustomStrategyTest::compilesCrossConditionsAndResetsDeterministically()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("crossesAbove")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 100.0}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("crossesBelow")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 100.0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(validator.validate(document, &definition, &error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 2);

    CustomBacktestStrategy strategy(*plan);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    const QVector<double> closes {99.0, 0.0, 101.0, 99.0, 101.0, 99.0};
    for (int row = 0; row < closes.size(); ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 2, 1), QTime(7, 0), Qt::UTC).addDays(row);
        candle.open = candle.high = candle.low = candle.close = closes.at(row);
        candle.volume = 1000.0;
        strategy.onCandle(candle);
    }
    QCOMPARE(signalSpy.count(), 2);
    QCOMPARE(signalSpy.at(0).at(0).value<TradeSignal>().side, SignalSide::Buy);
    QCOMPARE(signalSpy.at(1).at(0).value<TradeSignal>().side, SignalSide::Sell);
    const QDateTime firstSignalTime = signalSpy.at(0).at(0).value<TradeSignal>().timestamp;
    QCOMPARE(firstSignalTime, QDateTime(QDate(2026, 2, 5), QTime(7, 0), Qt::UTC));

    strategy.reset();
    signalSpy.clear();
    for (int row = 0; row < closes.size(); ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 2, 1), QTime(7, 0), Qt::UTC).addDays(row);
        candle.open = candle.high = candle.low = candle.close = closes.at(row);
        candle.volume = 1000.0;
        strategy.onCandle(candle);
    }
    QCOMPARE(signalSpy.count(), 2);
    QCOMPARE(signalSpy.at(0).at(0).value<TradeSignal>().timestamp, firstSignalTime);

    const auto invalidRuntimeParameter = compiler.compile(definition,
        {{QStringLiteral("unknown"), 1}}, &error);
    QVERIFY(!invalidRuntimeParameter.has_value());
    QVERIFY(error.contains(QStringLiteral("不存在")));
}

void CustomStrategyTest::compilesEveryWhitelistedIndicator()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    QJsonArray conditions;
    const auto greaterThanZero = [&](const QJsonObject &left) {
        return QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                           {QStringLiteral("left"), left},
                           {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}};
    };
    conditions.append(greaterThanZero(indicator(QStringLiteral("SMA"), 3)));
    conditions.append(greaterThanZero(indicator(QStringLiteral("EMA"), 3)));
    conditions.append(greaterThanZero(indicator(QStringLiteral("RSI"), 2)));
    conditions.append(greaterThanZero(QJsonObject{
        {QStringLiteral("indicator"), QStringLiteral("MACD")},
        {QStringLiteral("fastPeriod"), 2}, {QStringLiteral("slowPeriod"), 3},
        {QStringLiteral("signalPeriod"), 2},
        {QStringLiteral("output"), QStringLiteral("histogram")}}));
    conditions.append(greaterThanZero(QJsonObject{
        {QStringLiteral("indicator"), QStringLiteral("BOLLINGER")},
        {QStringLiteral("period"), 3}, {QStringLiteral("standardDeviations"), 2.0},
        {QStringLiteral("output"), QStringLiteral("middle")}}));
    conditions.append(greaterThanZero(indicator(QStringLiteral("ATR"), 2)));
    conditions.append(greaterThanZero(indicator(QStringLiteral("VOLUME_SMA"), 2)));
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), conditions}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->indicators.size(), 7);
    QCOMPARE(plan->minimumWarmupBars, 4);

    QJsonObject invalidMacd = document;
    QJsonObject entry = invalidMacd.value(QStringLiteral("entry")).toObject();
    QJsonArray invalidConditions = entry.value(QStringLiteral("conditions")).toArray();
    QJsonObject comparison = invalidConditions.at(3).toObject();
    QJsonObject macd = comparison.value(QStringLiteral("left")).toObject();
    macd.insert(QStringLiteral("fastPeriod"), 10);
    macd.insert(QStringLiteral("slowPeriod"), 3);
    comparison.insert(QStringLiteral("left"), macd);
    invalidConditions.replace(3, comparison);
    entry.insert(QStringLiteral("conditions"), invalidConditions);
    invalidMacd.insert(QStringLiteral("entry"), entry);
    QVERIFY(!validator.validate(invalidMacd, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("MACD")));
}

void CustomStrategyTest::v2SupportsAliasedSeriesAndNewIndicators()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("schemaVersion"), 2);
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QJsonArray{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("momentum")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.8}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("vwap")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("ppflow")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), indicatorWithAlias(QStringLiteral("ROC"), 14, QStringLiteral("momentum"))},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), -100.0}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("lt")},
                        {QStringLiteral("left"), QJsonObject{
                            {QStringLiteral("indicator"), QStringLiteral("VWAP")},
                            {QStringLiteral("alias"), QStringLiteral("vwap")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("field"), QStringLiteral("high")}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QJsonObject{
                            {QStringLiteral("indicator"), QStringLiteral("PP_FLOW")},
                            {QStringLiteral("alias"), QStringLiteral("ppflow")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), -1.0e12}}}}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("momentum")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    QCOMPARE(definition.schemaVersion, 2);

    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->indicators.size(), 3);
    QCOMPARE(plan->minimumWarmupBars, 15);
    bool foundRoc = false;
    bool foundVwap = false;
    bool foundPpFlow = false;
    for (const CompiledCustomIndicator &indicator : plan->indicators) {
        if (indicator.kind == CustomIndicatorKind::Roc) {
            foundRoc = true;
            QCOMPARE(indicator.period, 14);
            QCOMPARE(indicator.alias, QStringLiteral("momentum"));
        } else if (indicator.kind == CustomIndicatorKind::Vwap) {
            foundVwap = true;
            QCOMPARE(indicator.alias, QStringLiteral("vwap"));
        } else if (indicator.kind == CustomIndicatorKind::PpFlow) {
            foundPpFlow = true;
            QCOMPARE(indicator.alias, QStringLiteral("ppflow"));
        }
    }
    QVERIFY(foundRoc && foundVwap && foundPpFlow);

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    auto candle = [&](const QDate &date, const QTime &time, double close) {
        Candle value;
        value.instrument = instrument;
        value.startTime = QDateTime(date, time, Qt::UTC);
        value.open = value.close = close;
        value.high = close + 1.0;
        value.low = close - 1.0;
        value.volume = 1000.0;
        return value;
    };

    CustomBacktestStrategy strategy(*plan);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);
    for (int row = 0; row < 40; ++row) {
        strategy.onCandle(candle(QDate(2026, 4, 1),
                                 QTime(1, 0).addSecs(row * 900), 100.0 + row));
    }
    QCOMPARE(signalSpy.count(), 1);
    QCOMPARE(signalSpy.at(0).at(0).value<TradeSignal>().side, SignalSide::Buy);

    QVector<CompiledCustomIndicator> runtimeIndicators;
    runtimeIndicators.append({CustomIndicatorKind::Roc, 3});
    CompiledCustomIndicator vwap;
    vwap.kind = CustomIndicatorKind::Vwap;
    CompiledCustomIndicator ppFlow;
    ppFlow.kind = CustomIndicatorKind::PpFlow;
    runtimeIndicators.append(vwap);
    runtimeIndicators.append(ppFlow);
    CustomIndicatorRuntime runtime(runtimeIndicators);
    double expectedPvt = 0.0;
    for (int row = 1; row <= 6; ++row) {
        const double close = 9.0 + row;
        runtime.append(candle(QDate(2026, 4, 1),
                              QTime(1, 0).addSecs((row - 1) * 900), close));
        if (row > 1) expectedPvt += (close - (8.0 + row)) / (8.0 + row) * 1000.0;
    }
    QVERIFY(runtime.value(0).has_value());
    QVERIFY(std::abs(*runtime.value(0) - 25.0) < 1.0e-9);
    QVERIFY(runtime.value(1).has_value());
    QVERIFY(std::abs(*runtime.value(1) - 12.5) < 1.0e-9);
    QVERIFY(runtime.value(2).has_value());
    QVERIFY(std::abs(*runtime.value(2) - expectedPvt) < 1.0e-6);
    runtime.append(candle(QDate(2026, 4, 2), QTime(7, 0), 100.0));
    QVERIFY(runtime.value(1).has_value());
    QVERIFY(std::abs(*runtime.value(1) - 100.0) < 1.0e-9);
}

void CustomStrategyTest::v1RejectsV2OnlySyntax()
{
    CustomStrategyValidator validator;
    QString error;

    QJsonObject aliasDocument = validDocument();
    QJsonObject entry = aliasDocument.value(QStringLiteral("entry")).toObject();
    QJsonObject sma = entry.value(QStringLiteral("left")).toObject();
    sma.insert(QStringLiteral("alias"), QStringLiteral("fast"));
    entry.insert(QStringLiteral("left"), sma);
    aliasDocument.insert(QStringLiteral("entry"), entry);
    QVERIFY(!validator.validate(aliasDocument, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("schemaVersion=2")));

    QJsonObject rocDocument = validDocument();
    entry = rocDocument.value(QStringLiteral("entry")).toObject();
    entry.insert(QStringLiteral("left"), indicator(QStringLiteral("ROC"), 14));
    rocDocument.insert(QStringLiteral("entry"), entry);
    QVERIFY(!validator.validate(rocDocument, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("未知指标")));

    QJsonObject v2 = validDocument();
    v2.insert(QStringLiteral("schemaVersion"), 2);
    v2.insert(QStringLiteral("parameters"), QJsonArray{});
    v2.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("left"), indicatorWithAlias(QStringLiteral("SMA"), 5, QStringLiteral("fast"))},
        {QStringLiteral("right"), indicatorWithAlias(QStringLiteral("SMA"), 10, QStringLiteral("fast"))}});
    QVERIFY(!validator.validate(v2, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("指标别名重复")));

    v2.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("missing")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});
    QVERIFY(!validator.validate(v2, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("未定义的指标别名")));
}

void CustomStrategyTest::v2SupportsTopLevelSeries()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("schemaVersion"), 2);
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("series"), QJsonArray{
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("fast")},
                    {QStringLiteral("indicator"), QStringLiteral("SMA")},
                    {QStringLiteral("period"), 20}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("slow")},
                    {QStringLiteral("indicator"), QStringLiteral("SMA")},
                    {QStringLiteral("period"), 60}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("momentum")},
                    {QStringLiteral("indicator"), QStringLiteral("ROC")},
                    {QStringLiteral("period"), 14}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("vwap")},
                    {QStringLiteral("indicator"), QStringLiteral("VWAP")}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("ppflow")},
                    {QStringLiteral("indicator"), QStringLiteral("PP_FLOW")}}});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QJsonArray{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("crossesAbove")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("fast")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("series"), QStringLiteral("slow")}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("momentum")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.8}}}}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("momentum")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->indicators.size(), 5);
    QCOMPARE(plan->minimumWarmupBars, 61);

    QJsonObject duplicate = document;
    QJsonArray series = duplicate.value(QStringLiteral("series")).toArray();
    series.append(QJsonObject{{QStringLiteral("alias"), QStringLiteral("fast")},
                              {QStringLiteral("indicator"), QStringLiteral("EMA")},
                              {QStringLiteral("period"), 5}});
    duplicate.insert(QStringLiteral("series"), series);
    QVERIFY(!validator.validate(duplicate, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("指标别名重复")));

    QJsonObject v1WithSeries = validDocument();
    v1WithSeries.insert(QStringLiteral("series"), QJsonArray{});
    QVERIFY(!validator.validate(v1WithSeries, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("schemaVersion=2")));
}

void CustomStrategyTest::nextOpenSignalFillPrice()
{
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    auto candle = [&](const QDate &date, const QTime &time, double open, double close) {
        Candle value;
        value.instrument = instrument;
        value.startTime = QDateTime(date, time, Qt::UTC);
        value.open = open;
        value.close = close;
        value.high = std::max(open, close) + 1.0;
        value.low = std::min(open, close) - 1.0;
        value.volume = 1000.0;
        return value;
    };
    auto makeDocument = [](const QString &priceMode) {
        QJsonObject document = validDocument();
        document.insert(QStringLiteral("schemaVersion"), 2);
        document.insert(QStringLiteral("parameters"), QJsonArray{});
        document.insert(QStringLiteral("signal"), QJsonObject{
            {QStringLiteral("priceMode"), priceMode}});
        document.insert(QStringLiteral("entry"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("gt")},
            {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
            {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 100.0}}}});
        document.insert(QStringLiteral("exit"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("lt")},
            {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
            {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 50.0}}}});
        return document;
    };

    CustomStrategyValidator validator;
    CustomStrategyCompiler compiler;
    QString error;

    CustomStrategyDefinition nextOpenDefinition;
    QVERIFY2(validator.validate(makeDocument(QStringLiteral("nextOpen")),
                                &nextOpenDefinition, &error), qPrintable(error));
    const auto nextOpenPlan = compiler.compile(nextOpenDefinition, {}, &error);
    QVERIFY2(nextOpenPlan.has_value(), qPrintable(error));
    CustomBacktestStrategy nextOpenStrategy(*nextOpenPlan);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy nextOpenSpy(&nextOpenStrategy, &Strategy::signalGenerated);
    const QDate date(2026, 5, 4);
    nextOpenStrategy.onCandle(candle(date, QTime(2, 0), 96.0, 98.0));
    nextOpenStrategy.onCandle(candle(date, QTime(2, 5), 99.0, 102.0));
    nextOpenStrategy.onCandle(candle(date, QTime(2, 10), 105.0, 110.0));
    QCOMPARE(nextOpenSpy.count(), 1);
    const TradeSignal nextOpenSignal =
        nextOpenSpy.at(0).at(0).value<TradeSignal>();
    QCOMPARE(nextOpenSignal.price, 105.0);
    QCOMPARE(nextOpenSignal.timestamp,
             QDateTime(date, QTime(2, 10), Qt::UTC));

    CustomStrategyDefinition triggerCloseDefinition;
    QVERIFY2(validator.validate(makeDocument(QStringLiteral("triggerClose")),
                                &triggerCloseDefinition, &error), qPrintable(error));
    const auto triggerClosePlan = compiler.compile(triggerCloseDefinition, {}, &error);
    QVERIFY2(triggerClosePlan.has_value(), qPrintable(error));
    CustomBacktestStrategy triggerCloseStrategy(*triggerClosePlan);
    QSignalSpy triggerCloseSpy(&triggerCloseStrategy, &Strategy::signalGenerated);
    triggerCloseStrategy.onCandle(candle(date, QTime(2, 0), 96.0, 98.0));
    triggerCloseStrategy.onCandle(candle(date, QTime(2, 5), 99.0, 102.0));
    QCOMPARE(triggerCloseSpy.count(), 1);
    const TradeSignal triggerCloseSignal =
        triggerCloseSpy.at(0).at(0).value<TradeSignal>();
    QCOMPARE(triggerCloseSignal.price, 102.0);

    QJsonObject v1WithSignal = validDocument();
    v1WithSignal.insert(QStringLiteral("signal"), QJsonObject{
        {QStringLiteral("priceMode"), QStringLiteral("nextOpen")}});
    QVERIFY(!validator.validate(v1WithSignal, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("schemaVersion=2")));
}

void CustomStrategyTest::buildsPreviewSeriesAndSignals()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("schemaVersion"), 2);
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("and")},
        {QStringLiteral("conditions"), QJsonArray{
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), indicatorWithAlias(QStringLiteral("ROC"), 14, QStringLiteral("momentum"))},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.8}}}},
            QJsonObject{{QStringLiteral("op"), QStringLiteral("gt")},
                        {QStringLiteral("left"), QJsonObject{
                            {QStringLiteral("indicator"), QStringLiteral("PP_FLOW")},
                            {QStringLiteral("alias"), QStringLiteral("ppflow")}}},
                        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("series"), QStringLiteral("momentum")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    QVector<Candle> candles;
    for (int row = 0; row < 40; ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 4, 1),
                                     QTime(1, 0).addSecs(row * 900), Qt::UTC);
        candle.open = candle.close = 100.0 + row;
        candle.high = candle.close + 1.0;
        candle.low = candle.close - 1.0;
        candle.volume = 1000.0;
        candles.append(candle);
    }

    const auto preview = CustomStrategyPreviewService::buildPreview(
        definition, {}, candles, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesAliases.size(), 2);
    QCOMPARE(preview.seriesLabels.size(), 2);
    QCOMPARE(preview.bars.size(), 40);
    QVERIFY(!preview.signalEvents.isEmpty());
    QCOMPARE(preview.signalEvents.first().side, 0);
    QVERIFY(preview.signalEvents.first().barIndex >= 14);
    QCOMPARE(preview.bars.last().seriesValues.size(), 2);
    QVERIFY(preview.bars.last().seriesValues.at(0).has_value());
    QVERIFY(preview.bars.last().seriesValues.at(1).has_value());
}

void CustomStrategyTest::previewUsesOnlyLocalTickDerivedCandles()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(validDocument(), &definition, &error), qPrintable(error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("preview-source.db"))));

    const Instrument instrument {QStringLiteral("600519"), QStringLiteral("Kweichow Moutai"),
                                 Market::AShare, QStringLiteral("SSE")};
    QVector<Candle> candles;
    const QDateTime rawStart(QDate(2026, 8, 1), QTime(1, 30), Qt::UTC);
    for (int index = 0; index < 6; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = rawStart.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = 100.0 + index;
        candle.volume = 1000.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    const QDateTime providerStart(QDate(2026, 8, 20), QTime(1, 30), Qt::UTC);
    for (int index = 0; index < 3; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = providerStart.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = 900.0 + index;
        candle.volume = 2000.0;
        candle.source = QStringLiteral("mootdx_daily_fixture");
        candles.append(candle);
    }
    QVERIFY(store.upsertCandles(candles));

    const QVector<Candle> genericRecent = store.loadRecentCandles(instrument, 86400, 3);
    QCOMPARE(genericRecent.size(), 3);
    QVERIFY(genericRecent.first().close >= 900.0);

    const auto preview = CustomStrategyPreviewService::buildLocalTickPreview(
        definition, {}, store, instrument, 86400, 3, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.bars.size(), 3);
    QCOMPARE(preview.bars.first().close, 103.0);
    QCOMPARE(preview.bars.last().close, 105.0);

    const Instrument providerOnly {QStringLiteral("000001"), QStringLiteral("Ping An Bank"),
                                   Market::AShare, QStringLiteral("SZSE")};
    Candle providerCandle;
    providerCandle.instrument = providerOnly;
    providerCandle.startTime = providerStart;
    providerCandle.periodSeconds = 86400;
    providerCandle.open = providerCandle.high = providerCandle.low = providerCandle.close = 10.0;
    providerCandle.volume = 1000.0;
    providerCandle.source = QStringLiteral("mootdx_daily_fixture");
    QVERIFY(store.upsertCandles({providerCandle}));

    error.clear();
    const auto missingPreview = CustomStrategyPreviewService::buildLocalTickPreview(
        definition, {}, store, providerOnly, 86400, 3, &error);
    QVERIFY(missingPreview.bars.isEmpty());
    QVERIFY(error.contains(QStringLiteral("raw_tick_cache_v1")));
    QVERIFY(error.contains(QStringLiteral("未访问网络")));
}

void CustomStrategyTest::validatesCompilesAndExecutesPositionRiskRules()
{
    CustomStrategyValidator validator;
    QString error;

    QJsonObject invalid = validDocument();
    invalid.insert(QStringLiteral("risk"), QJsonObject{{QStringLiteral("tPlusOne"), false}});
    QVERIFY(!validator.validate(invalid, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("tPlusOne")));

    invalid = validDocument();
    invalid.insert(QStringLiteral("position"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("fixedPercent")},
        {QStringLiteral("percent"), 1.5}});
    QVERIFY(!validator.validate(invalid, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("position.percent")));

    QJsonObject document = validDocument();
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("position"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("fixedPercent")},
        {QStringLiteral("percent"), 0.5}});
    document.insert(QStringLiteral("risk"), QJsonObject{
        {QStringLiteral("stopLossPercent"), 5.0},
        {QStringLiteral("takeProfitPercent"), 10.0},
        {QStringLiteral("maximumHoldingBars"), 20},
        {QStringLiteral("tPlusOne"), true}});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});

    CustomStrategyDefinition definition;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->rules.positionFraction, 0.5);
    QCOMPARE(plan->rules.stopLossPercent, 5.0);
    QCOMPARE(plan->rules.takeProfitPercent, 10.0);
    QCOMPARE(plan->rules.maximumHoldingBars, 20);
    QVERIFY(plan->rules.tPlusOne);
    QCOMPARE(plan->rules.toVariantMap().value(QStringLiteral("positionFraction")).toDouble(), 0.5);

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;
    auto candle = [&](const QDate &date, const QTime &time, double close) {
        Candle value;
        value.instrument = instrument;
        value.startTime = QDateTime(date, time, Qt::UTC);
        value.open = value.high = value.low = value.close = close;
        value.volume = 1000.0;
        return value;
    };

    CustomBacktestStrategy strategy(*plan);
    QCOMPARE(strategy.positionFraction(), 0.5);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);
    strategy.onCandle(candle(QDate(2026, 3, 2), QTime(2, 0), 100.0));
    strategy.onCandle(candle(QDate(2026, 3, 2), QTime(2, 5), 90.0));
    QCOMPARE(signalSpy.count(), 1);
    strategy.onCandle(candle(QDate(2026, 3, 3), QTime(1, 35), 110.0));
    QCOMPARE(signalSpy.count(), 2);
    const TradeSignal delayedExit = signalSpy.at(1).at(0).value<TradeSignal>();
    QCOMPARE(delayedExit.side, SignalSide::Sell);
    QVERIFY(delayedExit.reason.contains(QStringLiteral("止损")));
    QVERIFY(delayedExit.reason.contains(QStringLiteral("T+1")));

    strategy.reset();
    signalSpy.clear();
    strategy.onCandle(candle(QDate(2026, 3, 2), QTime(2, 0), 100.0));
    strategy.onCandle(candle(QDate(2026, 3, 3), QTime(1, 35), 111.0));
    QCOMPARE(signalSpy.count(), 2);
    QVERIFY(signalSpy.at(1).at(0).value<TradeSignal>().reason.contains(QStringLiteral("止盈")));

    QJsonObject holdingDocument = document;
    holdingDocument.insert(QStringLiteral("risk"), QJsonObject{
        {QStringLiteral("maximumHoldingBars"), 2},
        {QStringLiteral("tPlusOne"), true}});
    CustomStrategyDefinition holdingDefinition;
    QVERIFY2(validator.validate(holdingDocument, &holdingDefinition, &error), qPrintable(error));
    const auto holdingPlan = compiler.compile(holdingDefinition, {}, &error);
    QVERIFY2(holdingPlan.has_value(), qPrintable(error));
    CustomBacktestStrategy holdingStrategy(*holdingPlan);
    QSignalSpy holdingSpy(&holdingStrategy, &Strategy::signalGenerated);
    holdingStrategy.onCandle(candle(QDate(2026, 3, 2), QTime(2, 0), 100.0));
    holdingStrategy.onCandle(candle(QDate(2026, 3, 3), QTime(2, 0), 100.0));
    QCOMPARE(holdingSpy.count(), 1);
    holdingStrategy.onCandle(candle(QDate(2026, 3, 4), QTime(2, 0), 100.0));
    QCOMPARE(holdingSpy.count(), 2);
    QVERIFY(holdingSpy.at(1).at(0).value<TradeSignal>().reason.contains(QStringLiteral("最大持仓")));

    QJsonObject defaultsDocument = validDocument();
    CustomStrategyDefinition defaultsDefinition;
    QVERIFY(validator.validate(defaultsDocument, &defaultsDefinition, &error));
    const auto defaultsPlan = compiler.compile(defaultsDefinition, {}, &error);
    QVERIFY2(defaultsPlan.has_value(), qPrintable(error));
    QCOMPARE(defaultsPlan->rules.positionFraction, 1.0);
    QCOMPARE(defaultsPlan->rules.stopLossPercent, 0.0);
    QCOMPARE(defaultsPlan->rules.maximumHoldingBars, 0);
    QVERIFY(defaultsPlan->rules.tPlusOne);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CustomStrategyTest)

#include "tst_custom_strategy.moc"
