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

#include <limits>

namespace pp {

class CustomStrategyTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesRestrictedSchemaAndRejectsUnsafeInputs();
    void exposesIndicatorParameterSchemaForWizard();
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
    void executesNestedOrNotConditions();
    void validatesCompilesAndExecutesPositionRiskRules();
    void enforcesWorkspaceRunAndResultIdentity();
    void v3SupportsUniverseCrossIndicatorsAndSelection();
    void v3RejectsInvalidCrossSyntax();
    void fixedAmountPositionAndMaxPercentCompile();
    void wmaUsesRecentLinearWeightsAndRejectsInvalidPeriods();
    void biasTracksRelativeDistanceFromAverage();
    void cciUsesTypicalPriceMeanAbsoluteDeviation();
    void williamsRangeUsesNegativeScaleAndRewarmsAfterBadHighLow();
    void donchianIncludesCurrentBarAndKeepsOutputsIndependent();
    void obvAccumulatesSignedVolumeWithoutBridgingMissingBars();
    void mfiUsesTypicalMoneyFlowAndNeedsPeriodComparisons();
    void kdjUsesRecursiveSmoothingAndDistinctParameterIdentities();
    void expandedIndicatorOptionsRequireIntegerParameterReferences_data();
    void expandedIndicatorOptionsRequireIntegerParameterReferences();
    void expandedIndicatorsNeverPublishOverflowedCalculations_data();
    void expandedIndicatorsNeverPublishOverflowedCalculations();
    void expandedCatalogueDefaultsCompileInV2V3ButNotV1_data();
    void expandedCatalogueDefaultsCompileInV2V3ButNotV1();
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

static QJsonObject singleIndicatorDocument(const QJsonObject &operand)
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("schemaVersion"), 2);
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gt")},
        {QStringLiteral("left"), operand},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), -1000000.0}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0.0}}}});
    return document;
}

static Candle indicatorBar(double close, double high = 0.0, double low = 0.0,
                           double volume = 100.0)
{
    Candle candle;
    candle.instrument = {QStringLiteral("600519"), QStringLiteral("指标测试"),
                         Market::AShare, QStringLiteral("SSE")};
    candle.startTime = QDateTime(QDate(2026, 9, 1), QTime(7, 0), Qt::UTC);
    candle.periodSeconds = 86400;
    candle.open = candle.close = close;
    candle.high = high > 0.0 ? high : close;
    candle.low = low > 0.0 ? low : close;
    candle.volume = volume;
    return candle;
}

void CustomStrategyTest::wmaUsesRecentLinearWeightsAndRejectsInvalidPeriods()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    const QJsonObject operand = indicator(QStringLiteral("WMA"), 3);
    QVERIFY2(validator.validate(singleIndicatorDocument(operand), &definition, &error),
             qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 3);
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(indicatorBar(1.0));
    runtime.append(indicatorBar(2.0));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(3.0));
    QVERIFY(runtime.value(0));
    QVERIFY(std::abs(*runtime.value(0) - 2.333333333333333) < 1e-12);
    runtime.append(indicatorBar(4.0));
    QVERIFY(std::abs(*runtime.value(0) - 3.333333333333333) < 1e-12);
    runtime.append(indicatorBar(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(7.0));
    runtime.append(indicatorBar(7.0));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(7.0));
    QCOMPARE(*runtime.value(0), 7.0);
    runtime.reset();
    QCOMPARE(runtime.barCount(), 0);
    QVERIFY(!runtime.value(0));
    const auto preview = CustomStrategyPreviewService::buildPreview(
        definition, {}, {indicatorBar(1.0), indicatorBar(2.0), indicatorBar(3.0)}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("WMA3"));
    QVERIFY(std::abs(*preview.bars.last().seriesValues.first() - 2.333333333333333) < 1e-12);
    for (double bad : {1.0, 501.0, 2.5}) {
        QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("WMA"), bad)),
                                    nullptr, &error));
    }
    QJsonObject legacy = singleIndicatorDocument(operand);
    legacy.insert(QStringLiteral("schemaVersion"), 1);
    QVERIFY(!validator.validate(legacy, nullptr, &error));
}

void CustomStrategyTest::biasTracksRelativeDistanceFromAverage()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(singleIndicatorDocument(indicator(QStringLiteral("BIAS"), 3)),
                                &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 3);
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(indicatorBar(8.0));
    runtime.append(indicatorBar(10.0));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(12.0));
    QVERIFY(runtime.value(0));
    QVERIFY(std::abs(*runtime.value(0) - 20.0) < 1e-12);
    runtime.append(indicatorBar(8.0));
    QVERIFY(std::abs(*runtime.value(0) + 20.0) < 1e-12);
    runtime.append(indicatorBar(0.0));
    QVERIFY(!runtime.value(0));
    runtime.reset();
    for (int index = 0; index < 3; ++index) runtime.append(indicatorBar(10.0));
    QCOMPARE(*runtime.value(0), 0.0);
    const auto preview = CustomStrategyPreviewService::buildPreview(
        definition, {}, {indicatorBar(8.0), indicatorBar(10.0), indicatorBar(12.0)}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("BIAS3"));
    QVERIFY(std::abs(*preview.bars.last().seriesValues.first() - 20.0) < 1e-12);
    QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("BIAS"), 2.5)),
                                nullptr, &error));
}

void CustomStrategyTest::cciUsesTypicalPriceMeanAbsoluteDeviation()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(singleIndicatorDocument(indicator(QStringLiteral("CCI"), 3)),
                                &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 3);
    const QVector<Candle> bars {indicatorBar(2.0), indicatorBar(4.0),
                               indicatorBar(6.0), indicatorBar(8.0)};
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(bars[0]);
    runtime.append(bars[1]);
    QVERIFY(!runtime.value(0));
    runtime.append(bars[2]);
    QVERIFY(runtime.value(0));
    QVERIFY(std::abs(*runtime.value(0) - 100.0) < 1e-10);
    runtime.append(bars[3]);
    QVERIFY(std::abs(*runtime.value(0) - 100.0) < 1e-10);
    Candle malformed = indicatorBar(10.0);
    malformed.high = 9.0;
    runtime.append(malformed);
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(7.0));
    runtime.append(indicatorBar(7.0));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(7.0));
    QCOMPARE(*runtime.value(0), 0.0);
    runtime.reset();
    QVERIFY(!runtime.value(0));
    // 收盘价都为 10，但典型价为 10、12、14，防止误用收盘均值。
    runtime.append(indicatorBar(10.0, 11.0, 9.0));
    runtime.append(indicatorBar(10.0, 17.0, 9.0));
    runtime.append(indicatorBar(10.0, 23.0, 9.0));
    QVERIFY(std::abs(*runtime.value(0) - 100.0) < 1e-10);
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("CCI3"));
    QVERIFY(std::abs(*preview.bars.last().seriesValues.first() - 100.0) < 1e-10);
    QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("CCI"), 2.5)),
                                nullptr, &error));
}

void CustomStrategyTest::williamsRangeUsesNegativeScaleAndRewarmsAfterBadHighLow()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(singleIndicatorDocument(indicator(QStringLiteral("WR"), 3)),
                                &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 3);
    const QVector<Candle> bars {indicatorBar(2.0, 3.0, 1.0), indicatorBar(2.0, 3.0, 1.0),
                               indicatorBar(3.0, 3.0, 1.0), indicatorBar(1.0, 3.0, 1.0)};
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(bars[0]);
    runtime.append(bars[1]);
    QVERIFY(!runtime.value(0));
    runtime.append(bars[2]);
    QVERIFY(runtime.value(0));
    QCOMPARE(*runtime.value(0), 0.0);
    runtime.append(bars[3]);
    QCOMPARE(*runtime.value(0), -100.0);
    Candle malformed = indicatorBar(2.0, 3.0, 1.0);
    malformed.low = 4.0;
    runtime.append(malformed);
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(2.0));
    runtime.append(indicatorBar(2.0));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(2.0));
    QCOMPARE(*runtime.value(0), -50.0);
    runtime.reset();
    QCOMPARE(runtime.barCount(), 0);
    QVERIFY(!runtime.value(0));
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("WR3"));
    QCOMPARE(*preview.bars.last().seriesValues.first(), -100.0);
    QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("WR"), 1)),
                                nullptr, &error));
}

void CustomStrategyTest::donchianIncludesCurrentBarAndKeepsOutputsIndependent()
{
    const QStringList outputs {QStringLiteral("upper"), QStringLiteral("middle"), QStringLiteral("lower")};
    QJsonArray series;
    for (const QString &output : outputs) {
        auto operand = indicatorWithAlias(QStringLiteral("DONCHIAN"), 3, output);
        operand.insert(QStringLiteral("output"), output);
        series.append(operand);
    }
    QJsonObject document = singleIndicatorDocument(QJsonObject{{QStringLiteral("series"), QStringLiteral("upper")}});
    document.insert(QStringLiteral("series"), series);
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 3);
    QCOMPARE(plan->indicators.size(), 3);
    const QVector<Candle> bars {indicatorBar(10, 12, 8), indicatorBar(12, 14, 10),
                               indicatorBar(11, 13, 9), indicatorBar(9, 11, 7)};
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(bars[0]);
    runtime.append(bars[1]);
    QVERIFY(!runtime.value(0));
    runtime.append(bars[2]);
    QVERIFY(runtime.value(0));
    QCOMPARE(*runtime.value(0), 14.0);
    QCOMPARE(*runtime.value(1), 11.0);
    QCOMPARE(*runtime.value(2), 8.0);
    runtime.append(bars[3]);
    QCOMPARE(*runtime.value(0), 14.0);
    QCOMPARE(*runtime.value(1), 10.5);
    QCOMPARE(*runtime.value(2), 7.0);
    Candle malformed = indicatorBar(10);
    malformed.high = std::numeric_limits<double>::infinity();
    runtime.append(malformed);
    for (int index = 0; index < 3; ++index) QVERIFY(!runtime.value(index));
    runtime.reset();
    for (int index = 0; index < 3; ++index) runtime.append(indicatorBar(10));
    for (int index = 0; index < 3; ++index) QCOMPARE(*runtime.value(index), 10.0);
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels, outputs);
    QCOMPARE(*preview.bars.last().seriesValues.at(1), 10.5);
    auto invalid = indicator(QStringLiteral("DONCHIAN"), 3);
    invalid.insert(QStringLiteral("output"), QStringLiteral("previousUpper"));
    QVERIFY(!validator.validate(singleIndicatorDocument(invalid), nullptr, &error));
}

void CustomStrategyTest::obvAccumulatesSignedVolumeWithoutBridgingMissingBars()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    const QJsonObject operand {{QStringLiteral("indicator"), QStringLiteral("OBV")}};
    QVERIFY2(validator.validate(singleIndicatorDocument(operand), &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 1);
    const QVector<Candle> bars {indicatorBar(1, 0, 0, 100), indicatorBar(2, 0, 0, 200),
                               indicatorBar(1, 0, 0, 50), indicatorBar(1, 0, 0, 80),
                               indicatorBar(2, 0, 0, 0)};
    const QVector<double> expected {0, 200, 150, 150, 150};
    CustomIndicatorRuntime runtime(plan->indicators);
    for (int index = 0; index < bars.size(); ++index) {
        runtime.append(bars.at(index));
        QVERIFY(runtime.value(0));
        QCOMPARE(*runtime.value(0), expected.at(index));
    }
    runtime.append(indicatorBar(3, 0, 0, -1));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(4, 0, 0, 500));
    QCOMPARE(*runtime.value(0), 150.0);
    runtime.append(indicatorBar(3, 0, 0, 20));
    QCOMPARE(*runtime.value(0), 130.0);
    runtime.append(indicatorBar(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(10));
    QCOMPARE(*runtime.value(0), 130.0);
    runtime.reset();
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(10));
    QCOMPARE(*runtime.value(0), 0.0);
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("OBV"));
    QCOMPARE(*preview.bars.last().seriesValues.first(), 150.0);
    QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("OBV"), 3)),
                                nullptr, &error));
}

void CustomStrategyTest::mfiUsesTypicalMoneyFlowAndNeedsPeriodComparisons()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(singleIndicatorDocument(indicator(QStringLiteral("MFI"), 3)),
                                &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, 4);
    // 收盘均为 10；典型价 10、12、11、14，正流 2600、负流 1100。
    const QVector<Candle> bars {indicatorBar(10, 11, 9), indicatorBar(10, 17, 9),
                               indicatorBar(10, 14, 9), indicatorBar(10, 23, 9)};
    CustomIndicatorRuntime runtime(plan->indicators);
    for (int row = 0; row < 3; ++row) runtime.append(bars.at(row));
    QVERIFY(!runtime.value(0));
    runtime.append(bars.last());
    QVERIFY(runtime.value(0));
    QVERIFY(std::abs(*runtime.value(0) - 70.27027027027027) < 1e-10);
    runtime.append(indicatorBar(10, 0, 0, -1));
    QVERIFY(!runtime.value(0));
    runtime.reset();
    for (double price : {1.0, 2.0, 3.0, 4.0}) runtime.append(indicatorBar(price));
    QCOMPARE(*runtime.value(0), 100.0);
    runtime.reset();
    for (double price : {4.0, 3.0, 2.0, 1.0}) runtime.append(indicatorBar(price));
    QCOMPARE(*runtime.value(0), 0.0);
    runtime.reset();
    for (int row = 0; row < 4; ++row) runtime.append(indicatorBar(10));
    QCOMPARE(*runtime.value(0), 50.0);
    runtime.reset();
    for (double price : {1.0, 2.0, 3.0, 4.0}) runtime.append(indicatorBar(price, 0, 0, 0));
    QCOMPARE(*runtime.value(0), 50.0);
    Candle malformed = indicatorBar(10);
    malformed.low = 11;
    runtime.append(malformed);
    QVERIFY(!runtime.value(0));
    for (int row = 0; row < 3; ++row) runtime.append(indicatorBar(10));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(10));
    QCOMPARE(*runtime.value(0), 50.0);
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("MFI3"));
    QVERIFY(!preview.bars.at(2).seriesValues.first());
    QVERIFY(std::abs(*preview.bars.last().seriesValues.first() - 70.27027027027027) < 1e-10);
    QVERIFY(!validator.validate(singleIndicatorDocument(indicator(QStringLiteral("MFI"), 501)),
                                nullptr, &error));
}

void CustomStrategyTest::kdjUsesRecursiveSmoothingAndDistinctParameterIdentities()
{
    QJsonArray series;
    for (const QString &output : {QStringLiteral("k"), QStringLiteral("d"), QStringLiteral("j")}) {
        auto operand = indicatorWithAlias(QStringLiteral("KDJ"), 3, output);
        operand.insert(QStringLiteral("kPeriod"), 3);
        operand.insert(QStringLiteral("dPeriod"), 3);
        operand.insert(QStringLiteral("output"), output);
        series.append(operand);
    }
    auto fastK = series.first().toObject();
    fastK.insert(QStringLiteral("alias"), QStringLiteral("fastK"));
    fastK.insert(QStringLiteral("kPeriod"), 1);
    series.append(fastK);
    auto fastD = series.at(1).toObject();
    fastD.insert(QStringLiteral("alias"), QStringLiteral("fastD"));
    fastD.insert(QStringLiteral("dPeriod"), 1);
    series.append(fastD);
    QJsonObject document = singleIndicatorDocument(QJsonObject{{QStringLiteral("series"), QStringLiteral("k")}});
    document.insert(QStringLiteral("series"), series);
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->indicators.size(), 5);
    QCOMPARE(plan->minimumWarmupBars, 3);
    const QVector<Candle> bars {indicatorBar(2, 3, 1), indicatorBar(2, 3, 1),
                               indicatorBar(3, 3, 1), indicatorBar(1, 3, 1)};
    CustomIndicatorRuntime runtime(plan->indicators);
    runtime.append(bars[0]);
    runtime.append(bars[1]);
    QVERIFY(!runtime.value(0));
    runtime.append(bars[2]);
    QVERIFY(runtime.value(0));
    const QVector<double> first {66.66666666666667, 55.55555555555556, 88.88888888888889,
                                 100.0, 66.66666666666667};
    for (int index = 0; index < 5; ++index) {
        QVERIFY(std::abs(*runtime.value(index) - first.at(index)) < 1e-10);
    }
    runtime.append(bars[3]);
    QVERIFY(std::abs(*runtime.value(0) - 44.44444444444444) < 1e-10);
    QVERIFY(std::abs(*runtime.value(1) - 51.85185185185185) < 1e-10);
    QVERIFY(std::abs(*runtime.value(2) - 29.62962962962963) < 1e-10);
    for (int row = 0; row < 3; ++row) runtime.append(indicatorBar(2));
    // 平坦窗口继续递归，而不是重新把 K/D 种子设为 50。
    QVERIFY(std::abs(*runtime.value(0) - 48.353909465020576) < 1e-10);
    QVERIFY(std::abs(*runtime.value(1) - 48.90260631001372) < 1e-10);
    QVERIFY(std::abs(*runtime.value(2) - 47.25651577503429) < 1e-10);
    runtime.append(indicatorBar(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(2));
    runtime.append(indicatorBar(2));
    QVERIFY(!runtime.value(0));
    runtime.append(indicatorBar(2));
    for (int index = 0; index < 3; ++index) QCOMPARE(*runtime.value(index), 50.0);
    runtime.reset();
    for (int row = 0; row < 5; ++row) runtime.append(indicatorBar(3, 3, 1));
    QVERIFY(*runtime.value(2) > 100.0); // J 不截断。
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), QStringLiteral("k"));
    QVERIFY(std::abs(*preview.bars.last().seriesValues.at(2) - 29.62962962962963) < 1e-10);
    for (const QString &key : {QStringLiteral("kPeriod"), QStringLiteral("dPeriod")}) {
        for (double bad : {0.0, 101.0, 2.5}) {
            auto invalid = series.first().toObject();
            invalid.insert(key, bad);
            QVERIFY(!validator.validate(singleIndicatorDocument(invalid), nullptr, &error));
        }
    }
    auto invalid = series.first().toObject();
    invalid.insert(QStringLiteral("output"), QStringLiteral("rsv"));
    QVERIFY(!validator.validate(singleIndicatorDocument(invalid), nullptr, &error));
}

void CustomStrategyTest::expandedIndicatorOptionsRequireIntegerParameterReferences_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("option");
    for (const char *name : {"WMA", "BIAS", "CCI", "WR", "MFI", "DONCHIAN", "KDJ"}) {
        QTest::newRow(name) << QString::fromLatin1(name) << QStringLiteral("period");
    }
    QTest::newRow("KDJ-kPeriod") << QStringLiteral("KDJ") << QStringLiteral("kPeriod");
    QTest::newRow("KDJ-dPeriod") << QStringLiteral("KDJ") << QStringLiteral("dPeriod");
}

void CustomStrategyTest::expandedIndicatorOptionsRequireIntegerParameterReferences()
{
    QFETCH(QString, name);
    QFETCH(QString, option);
    QJsonObject operand = indicator(name, 3);
    if (name == QStringLiteral("DONCHIAN")) operand.insert(QStringLiteral("output"), QStringLiteral("middle"));
    if (name == QStringLiteral("KDJ")) {
        operand.insert(QStringLiteral("kPeriod"), 3);
        operand.insert(QStringLiteral("dPeriod"), 3);
        operand.insert(QStringLiteral("output"), QStringLiteral("k"));
    }
    operand.insert(option, parameterReference(QStringLiteral("n")));
    const int minimum = option == QStringLiteral("period") ? 2 : 1;
    const int maximum = option == QStringLiteral("period") ? 500 : 100;
    QJsonObject parameter {{QStringLiteral("key"), QStringLiteral("n")},
                           {QStringLiteral("displayName"), QStringLiteral("指标周期")},
                           {QStringLiteral("type"), QStringLiteral("real")},
                           {QStringLiteral("defaultValue"), 3},
                           {QStringLiteral("minimumValue"), minimum},
                           {QStringLiteral("maximumValue"), maximum},
                           {QStringLiteral("step"), 0.5}};
    QJsonObject document = singleIndicatorDocument(operand);
    document.insert(QStringLiteral("parameters"), QJsonArray{parameter});
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY(!validator.validate(document, nullptr, &error));
    parameter.insert(QStringLiteral("type"), QStringLiteral("integer"));
    parameter.insert(QStringLiteral("step"), 1);
    document.insert(QStringLiteral("parameters"), QJsonArray{parameter});
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), 4}}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->normalizedParameters.value(QStringLiteral("n")).toInt(), 4);
    if (option == QStringLiteral("period")) QCOMPARE(plan->indicators.first().period, 4);
    else if (option == QStringLiteral("kPeriod")) QCOMPARE(plan->indicators.first().kPeriod, 4);
    else QCOMPARE(plan->indicators.first().dPeriod, 4);
    QVERIFY(!CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), 3.5}}, &error));
    QVERIFY(CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), minimum}}, &error));
    QVERIFY(CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), maximum}}, &error));
    QVERIFY(!CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), minimum - 1}}, &error));
    QVERIFY(!CustomStrategyCompiler().compile(definition, {{QStringLiteral("n"), maximum + 1}}, &error));
    parameter.insert(QStringLiteral("maximumValue"), maximum + 1);
    document.insert(QStringLiteral("parameters"), QJsonArray{parameter});
    QVERIFY(!validator.validate(document, nullptr, &error));
}

void CustomStrategyTest::expandedIndicatorsNeverPublishOverflowedCalculations_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("scenario");
    for (const char *name : {"WMA", "BIAS", "CCI", "WR", "DONCHIAN", "KDJ", "OBV"}) {
        QTest::newRow(name) << QString::fromLatin1(name) << QStringLiteral("overflow");
    }
    QTest::newRow("MFI-product") << QStringLiteral("MFI") << QStringLiteral("product");
    QTest::newRow("MFI-total") << QStringLiteral("MFI") << QStringLiteral("total");
}

void CustomStrategyTest::expandedIndicatorsNeverPublishOverflowedCalculations()
{
    QFETCH(QString, name);
    QFETCH(QString, scenario);
    QJsonObject operand = indicator(name, name == QStringLiteral("MFI") ? 2 : 3);
    if (name == QStringLiteral("OBV")) operand.remove(QStringLiteral("period"));
    if (name == QStringLiteral("DONCHIAN")) operand.insert(QStringLiteral("output"), QStringLiteral("middle"));
    if (name == QStringLiteral("KDJ")) {
        operand.insert(QStringLiteral("kPeriod"), 3);
        operand.insert(QStringLiteral("dPeriod"), 3);
        operand.insert(QStringLiteral("output"), QStringLiteral("j"));
    }
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(CustomStrategyValidator().validate(singleIndicatorDocument(operand), &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    const double maximum = std::numeric_limits<double>::max();
    QVector<Candle> bars;
    if (name == QStringLiteral("MFI") && scenario == QStringLiteral("product")) {
        bars = {indicatorBar(1, 0, 0, 0), indicatorBar(2, 0, 0, maximum), indicatorBar(3, 0, 0, maximum)};
    } else if (name == QStringLiteral("MFI")) {
        bars = {indicatorBar(2, 0, 0, 0), indicatorBar(3, 0, 0, maximum / 4),
                indicatorBar(2, 0, 0, maximum / 4)};
    } else if (name == QStringLiteral("OBV")) {
        bars = {indicatorBar(1, 0, 0, 0), indicatorBar(2, 0, 0, maximum), indicatorBar(3, 0, 0, maximum)};
    } else if (name == QStringLiteral("WMA") || name == QStringLiteral("BIAS") || name == QStringLiteral("CCI")) {
        const double price = name == QStringLiteral("WMA") ? maximum / 4 : maximum;
        bars = {indicatorBar(price), indicatorBar(price), indicatorBar(price)};
    } else {
        Candle malformed = indicatorBar(10);
        malformed.high = std::numeric_limits<double>::infinity();
        bars = {indicatorBar(10), indicatorBar(10), malformed};
    }
    CustomIndicatorRuntime runtime(plan->indicators);
    for (const auto &bar : bars) runtime.append(bar);
    QVERIFY(!runtime.value(0));
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!preview.bars.last().seriesValues.first());
    if (name == QStringLiteral("OBV")) {
        runtime.append(indicatorBar(4, 0, 0, 0));
        QVERIFY(runtime.value(0));
        QCOMPARE(*runtime.value(0), maximum);
        runtime.append(indicatorBar(3, 0, 0, maximum));
        QCOMPARE(*runtime.value(0), 0.0);
    }
    runtime.reset();
    for (int row = 0; row < 4; ++row) runtime.append(indicatorBar(10));
    QVERIFY(runtime.value(0));
    QVERIFY(std::isfinite(*runtime.value(0)));
}

void CustomStrategyTest::expandedCatalogueDefaultsCompileInV2V3ButNotV1_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("period");
    QTest::addColumn<int>("warmup");
    QTest::addColumn<double>("flatValue");
    QTest::newRow("WMA") << QStringLiteral("WMA") << 14 << 14 << 10.0;
    QTest::newRow("BIAS") << QStringLiteral("BIAS") << 14 << 14 << 0.0;
    QTest::newRow("CCI") << QStringLiteral("CCI") << 20 << 20 << 0.0;
    QTest::newRow("WR") << QStringLiteral("WR") << 14 << 14 << -50.0;
    QTest::newRow("OBV") << QStringLiteral("OBV") << 0 << 1 << 0.0;
    QTest::newRow("MFI") << QStringLiteral("MFI") << 14 << 15 << 50.0;
    QTest::newRow("KDJ") << QStringLiteral("KDJ") << 9 << 9 << 50.0;
    QTest::newRow("DONCHIAN") << QStringLiteral("DONCHIAN") << 20 << 20 << 10.0;
}

void CustomStrategyTest::expandedCatalogueDefaultsCompileInV2V3ButNotV1()
{
    QFETCH(QString, name);
    QFETCH(int, period);
    QFETCH(int, warmup);
    QFETCH(double, flatValue);
    CustomStrategyValidator validator;
    const QVariantMap catalogue = validator.schemaDescriptor();
    QCOMPARE(catalogue.value(QStringLiteral("indicators")).toStringList().size(), 18);
    const auto schemas = catalogue.value(QStringLiteral("indicatorSchemas")).toList();
    QCOMPARE(schemas.size(), 18);
    QVariantMap schema;
    for (const auto &item : schemas) {
        if (item.toMap().value(QStringLiteral("name")).toString() == name) schema = item.toMap();
    }
    QVERIFY(!schema.isEmpty());
    QJsonObject operand {{QStringLiteral("indicator"), name}};
    for (const auto &item : schema.value(QStringLiteral("parameters")).toList()) {
        const auto parameter = item.toMap();
        operand.insert(parameter.value(QStringLiteral("key")).toString(),
                       QJsonValue::fromVariant(parameter.value(QStringLiteral("defaultValue"))));
    }
    QCOMPARE(operand.value(QStringLiteral("period")).toInt(), period);
    if (name == QStringLiteral("KDJ")) {
        QCOMPARE(operand.value(QStringLiteral("kPeriod")).toInt(), 3);
        QCOMPARE(operand.value(QStringLiteral("dPeriod")).toInt(), 3);
        QCOMPARE(operand.value(QStringLiteral("output")).toString(), QStringLiteral("k"));
    }
    if (name == QStringLiteral("DONCHIAN")) {
        QCOMPARE(operand.value(QStringLiteral("output")).toString(), QStringLiteral("middle"));
        QVERIFY(schema.value(QStringLiteral("displayName")).toString().contains(QStringLiteral("含当前 Bar")));
    }
    if (name == QStringLiteral("WR")) {
        QVERIFY(schema.value(QStringLiteral("displayName")).toString().contains(QStringLiteral("−100～0")));
    }
    CustomStrategyDefinition definition;
    QString error;
    QJsonObject document = singleIndicatorDocument(operand);
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    const auto plan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->minimumWarmupBars, warmup);
    QVector<Candle> bars(warmup, indicatorBar(10));
    const auto preview = CustomStrategyPreviewService::buildPreview(definition, {}, bars, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(preview.seriesLabels.first(), name + (period > 0 ? QString::number(period) : QString{}));
    QVERIFY(preview.bars.last().seriesValues.first());
    QVERIFY(std::abs(*preview.bars.last().seriesValues.first() - flatValue) < 1e-10);
    document.insert(QStringLiteral("schemaVersion"), 1);
    QVERIFY(!validator.validate(document, nullptr, &error));
    document.insert(QStringLiteral("schemaVersion"), 3);
    document.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600519"), QStringLiteral("000001")}}});
    document.insert(QStringLiteral("crossIndicators"), QJsonArray{QJsonObject{
        {QStringLiteral("alias"), QStringLiteral("rank")},
        {QStringLiteral("indicator"), QStringLiteral("RANK")}, {QStringLiteral("of"), operand}}});
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    const auto crossPlan = CustomStrategyCompiler().compile(definition, {}, &error);
    QVERIFY2(crossPlan.has_value(), qPrintable(error));
    QCOMPARE(crossPlan->indicators.size(), 1);
    QCOMPARE(crossPlan->crossIndicators.size(), 1);
    QCOMPARE(crossPlan->crossIndicators.first().indicatorIndex, 0);
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

void CustomStrategyTest::exposesIndicatorParameterSchemaForWizard()
{
    CustomStrategyValidator validator;
    const QVariantList schemas = validator.schemaDescriptor()
                                     .value(QStringLiteral("indicatorSchemas"))
                                     .toList();
    QCOMPARE(schemas.size(), 18);

    auto findSchema = [&schemas](const QString &name) {
        for (const QVariant &value : schemas) {
            const QVariantMap schema = value.toMap();
            if (schema.value(QStringLiteral("name")).toString() == name)
                return schema;
        }
        return QVariantMap{};
    };

    const QVariantMap macd = findSchema(QStringLiteral("MACD"));
    QCOMPARE(macd.value(QStringLiteral("displayName")).toString(),
             QStringLiteral("MACD"));
    const QVariantList macdParameters = macd.value(QStringLiteral("parameters")).toList();
    QCOMPARE(macdParameters.size(), 4);
    QCOMPARE(macdParameters.at(0).toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("fastPeriod"));
    QCOMPARE(macdParameters.at(0).toMap().value(QStringLiteral("defaultValue")).toInt(), 12);
    QCOMPARE(macdParameters.at(1).toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("slowPeriod"));
    QCOMPARE(macd.value(QStringLiteral("relations")).toList().first().toMap()
                 .value(QStringLiteral("op")).toString(), QStringLiteral("lt"));

    const QVariantMap bollinger = findSchema(QStringLiteral("BOLLINGER"));
    const QVariantList bollingerParameters = bollinger.value(QStringLiteral("parameters")).toList();
    QCOMPARE(bollingerParameters.at(2).toMap().value(QStringLiteral("choices")).toStringList(),
             QStringList({QStringLiteral("upper"), QStringLiteral("middle"),
                          QStringLiteral("lower")}));

    QCOMPARE(findSchema(QStringLiteral("VWAP")).value(QStringLiteral("parameters"))
                 .toList().size(), 0);
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

void CustomStrategyTest::executesNestedOrNotConditions()
{
    const auto comparison = [](const QString &field, const QString &op, double value) {
        return QJsonObject{
            {QStringLiteral("op"), op},
            {QStringLiteral("left"), QJsonObject{{QStringLiteral("field"), field}}},
            {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), value}}}};
    };

    QJsonObject document = validDocument();
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    const QJsonObject negatedVolume{
        {QStringLiteral("op"), QStringLiteral("not")},
        {QStringLiteral("condition"),
         comparison(QStringLiteral("volume"), QStringLiteral("gt"), 500.0)}};
    const QJsonArray entryConditions{
        comparison(QStringLiteral("close"), QStringLiteral("gt"), 10.0),
        negatedVolume};
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("or")},
        {QStringLiteral("conditions"), entryConditions}});
    document.insert(QStringLiteral("exit"),
                    comparison(QStringLiteral("close"), QStringLiteral("lt"), 0.0));

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    CustomBacktestStrategy strategy(*plan);
    qRegisterMetaType<TradeSignal>();
    QSignalSpy signalSpy(&strategy, &Strategy::signalGenerated);
    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.market = Market::AShare;

    const QVector<double> volumes{1000.0, 100.0};
    for (int row = 0; row < volumes.size(); ++row) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 8, 19), QTime(7, 0), Qt::UTC).addDays(row);
        candle.open = candle.high = candle.low = candle.close = 9.0;
        candle.volume = volumes.at(row);
        strategy.onCandle(candle);
    }

    QCOMPARE(signalSpy.count(), 1);
    QCOMPARE(signalSpy.first().first().value<TradeSignal>().side, SignalSide::Buy);
    QCOMPARE(signalSpy.first().first().value<TradeSignal>().timestamp,
             QDateTime(QDate(2026, 8, 20), QTime(7, 0), Qt::UTC));
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

void CustomStrategyTest::v3SupportsUniverseCrossIndicatorsAndSelection()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("schemaVersion"), 3);
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600519"),
                                               QStringLiteral("000001"),
                                               QStringLiteral("600036")}}});
    document.insert(QStringLiteral("crossIndicators"), QJsonArray{
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("mom_rank")},
                    {QStringLiteral("indicator"), QStringLiteral("RANK")},
                    {QStringLiteral("of"), QJsonObject{{QStringLiteral("indicator"), QStringLiteral("ROC")},
                                                       {QStringLiteral("period"), 20}}},
                    {QStringLiteral("ascending"), false}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("close_z")},
                    {QStringLiteral("indicator"), QStringLiteral("ZSCORE")},
                    {QStringLiteral("of"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}}},
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("peer_spread")},
                    {QStringLiteral("indicator"), QStringLiteral("PEER")},
                    {QStringLiteral("symbol"), QStringLiteral("600519")},
                    {QStringLiteral("field"), QStringLiteral("close")}}});
    document.insert(QStringLiteral("selection"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("topN")},
        {QStringLiteral("rankBy"), QStringLiteral("mom_rank")},
        {QStringLiteral("count"), 3}});
    document.insert(QStringLiteral("position"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("equalWeight")}});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gte")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("cross"), QStringLiteral("mom_rank")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 1}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("cross"), QStringLiteral("mom_rank")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 3}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->universeSymbols.size(), 3);
    QCOMPARE(plan->universeSymbols.first(), QStringLiteral("600519"));
    QCOMPARE(plan->crossIndicators.size(), 3);
    QCOMPARE(plan->selection.count, 3);
    QCOMPARE(plan->selection.rankByIndex, 0);
    // 条件里的 cross 操作数编译为 Cross 类型并指向横截面指标。
    bool hasCrossOperand = false;
    for (const CompiledCustomOperand &operand : plan->operands) {
        if (operand.kind == CustomOperandKind::Cross) hasCrossOperand = true;
    }
    QVERIFY(hasCrossOperand);
}

void CustomStrategyTest::v3RejectsInvalidCrossSyntax()
{
    CustomStrategyValidator validator;
    QString error;

    QJsonObject v1WithUniverse = validDocument();
    v1WithUniverse.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600519")}}});
    QVERIFY(!validator.validate(v1WithUniverse, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("schemaVersion=3")));

    QJsonObject v3NoUniverse = validDocument();
    v3NoUniverse.insert(QStringLiteral("schemaVersion"), 3);
    QVERIFY(!validator.validate(v3NoUniverse, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("universe")));

    QJsonObject v3 = validDocument();
    v3.insert(QStringLiteral("schemaVersion"), 3);
    v3.insert(QStringLiteral("parameters"), QJsonArray{});
    v3.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600519")}}});
    v3.insert(QStringLiteral("crossIndicators"), QJsonArray{
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("r")},
                    {QStringLiteral("indicator"), QStringLiteral("RANK")},
                    {QStringLiteral("of"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}}}});
    v3.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gte")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("cross"), QStringLiteral("undefined_alias")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 1}}}});
    v3.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("cross"), QStringLiteral("r")}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 3}}}});
    QVERIFY(!validator.validate(v3, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("横截面指标")));
}

void CustomStrategyTest::fixedAmountPositionAndMaxPercentCompile()
{
    QJsonObject document = validDocument();
    document.insert(QStringLiteral("position"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("fixedAmount")},
        {QStringLiteral("amount"), 50000}});
    document.insert(QStringLiteral("risk"), QJsonObject{
        {QStringLiteral("maxPositionPercent"), 30},
        {QStringLiteral("stopLossPercent"), 5},
        {QStringLiteral("tPlusOne"), true}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->rules.positionAmount, 50000.0);
    QCOMPARE(plan->rules.maxPositionPercent, 30.0);
    QCOMPARE(plan->rules.positionFraction, 1.0);

    QJsonObject badAmount = document;
    badAmount.insert(QStringLiteral("position"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("fixedAmount")},
        {QStringLiteral("amount"), 0}});
    QVERIFY(!validator.validate(badAmount, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("amount")));

    QJsonObject badPercent = document;
    badPercent.insert(QStringLiteral("risk"), QJsonObject{
        {QStringLiteral("maxPositionPercent"), 150}});
    QVERIFY(!validator.validate(badPercent, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("maxPositionPercent")));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CustomStrategyTest)

#include "tst_custom_strategy.moc"
