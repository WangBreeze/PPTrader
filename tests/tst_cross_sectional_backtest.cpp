#include "strategy/CrossSectionalIndicatorRuntime.h"
#include "strategy/CrossSectionalCustomStrategy.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomStrategyValidator.h"
#include "domain/CustomStrategyTypes.h"
#include "services/CrossSectionalBacktestEngine.h"
#include "services/CrossSectionalBacktestService.h"
#include "services/AShareTradingCalendar.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

namespace pp {

class CrossSectionalBacktestTest final : public QObject {
    Q_OBJECT

private slots:
    void rankSortsDescendingWithStableTies();
    void zscoreUsesPopulationDeviation();
    void percentileMapsTopToLowest();
    void meanAndMedianScalars();
    void emptyInputReturnsEmpty();
    void strategySelectsTopNByCrossRank();
    void engineRebalancesToTopNSelection();
    void engineKeepsCashWhenMaximumLegWeightCapsSelection();
    void engineLimitsActiveLegsDeterministically();
    void industryConstraintRequiresCompleteSnapshot();
    void industryTargetWeightsAreCappedAndIdentified();
    void strategyFactorTargetExposureIsCappedByKeepingCash();
    void heldSymbolWithoutCurrentFactorIsReportedUnmeasured();
    void factorResearchUsesOnlyNextAlignedFrameReturns();
    void factorResearchProducesExpandingOutOfSampleWindows();
    void factorResearchIndustryDemeaningRequiresAndUsesSnapshot();
    void circuitBreakerStopsStrategyReentry();
    void absentLegDoesNotTradeAtForwardFilledPrice();
    void suspensionFactIsNotConfusedWithUnknownMissingQuote();
    void missingLegExitsOnFirstFreshQuoteAfterThreshold();
    void tPlusOneDefersSameDayRotationSell();
    void requestPolicyRequiresSamePeriodIdentityAndCapacity();
    void riskConfigurationValidationHasStableBoundaries();
    void serviceValidatesLifecycleAndRejectsMissingData();
};

static Candle makeBar(const QString &symbol, double close)
{
    Candle candle;
    candle.instrument.symbol = symbol;
    candle.startTime = QDateTime(QDate(2026, 8, 1), QTime(9, 30));
    candle.periodSeconds = 60;
    candle.open = candle.high = candle.low = candle.close = close;
    candle.volume = 1000.0;
    return candle;
}

static QJsonObject makeCrossV3Document()
{
    QJsonObject document;
    document.insert(QStringLiteral("schemaVersion"), 3);
    document.insert(QStringLiteral("strategyId"), QStringLiteral("custom_cross_test"));
    document.insert(QStringLiteral("version"), 1);
    document.insert(QStringLiteral("name"), QStringLiteral("横截面测试"));
    document.insert(QStringLiteral("author"), QStringLiteral("Qt Test"));
    document.insert(QStringLiteral("market"), QStringLiteral("A股"));
    document.insert(QStringLiteral("executionScope"), QStringLiteral("backtest_only"));
    document.insert(QStringLiteral("createdAt"), QStringLiteral("2026-08-15T00:00:00.000Z"));
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600001"),
                                               QStringLiteral("600002"),
                                               QStringLiteral("600003")}}});
    document.insert(QStringLiteral("crossIndicators"), QJsonArray{
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("close_rank")},
                    {QStringLiteral("indicator"), QStringLiteral("RANK")},
                    {QStringLiteral("of"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}}}});
    document.insert(QStringLiteral("selection"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("topN")},
        {QStringLiteral("rankBy"), QStringLiteral("close_rank")},
        {QStringLiteral("count"), 2},
        {QStringLiteral("ascending"), true}});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gte")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("value"), 1}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 1}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("value"), 1}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0}}}});
    return document;
}

static AShareIndustrySnapshot makeIndustrySnapshot(bool complete = true)
{
    AShareIndustrySnapshot snapshot;
    snapshot.datasetId = QStringLiteral("industry-cross-v1");
    snapshot.classification = QStringLiteral("test-classification");
    snapshot.source = QStringLiteral("test-fixture");
    snapshot.sourceVersion = QStringLiteral("fixture-v1");
    snapshot.datasetContentSha256 =
        QStringLiteral("sha256:") + QString(64, QLatin1Char('a'));
    snapshot.snapshotVersion =
        QStringLiteral("sha256:") + QString(64, QLatin1Char('b'));
    snapshot.asOfDate = QDate(2026, 8, 1);
    snapshot.expectedSymbols = 3;
    snapshot.resolvedSymbols = complete ? 3 : 2;
    snapshot.coverageComplete = complete;

    auto membership = [](const QString &symbol, const QString &industryCode) {
        AShareIndustryMembership value;
        value.instrument.symbol = symbol;
        value.instrument.name = QStringLiteral("测试股票%1").arg(symbol);
        value.instrument.market = Market::AShare;
        value.industryCode = industryCode;
        value.industryName = industryCode == QStringLiteral("TECH")
            ? QStringLiteral("科技") : QStringLiteral("金融");
        value.effectiveFrom = QDate(2026, 1, 1);
        return value;
    };
    snapshot.memberships = {
        membership(QStringLiteral("600001"), QStringLiteral("TECH")),
        membership(QStringLiteral("600002"), QStringLiteral("FIN"))};
    if (complete) {
        snapshot.memberships.append(
            membership(QStringLiteral("600003"), QStringLiteral("FIN")));
    }
    return snapshot;
}

void CrossSectionalBacktestTest::rankSortsDescendingWithStableTies()
{
    const QHash<QString, double> values {
        {QStringLiteral("600001"), 10.0},
        {QStringLiteral("600002"), 30.0},
        {QStringLiteral("600003"), 20.0},
        {QStringLiteral("600004"), 30.0}}; // 与 600002 同值，symbol 更大排后

    const auto ranks = CrossSectionalIndicatorRuntime::rank(values, false);
    QCOMPARE(ranks.value(QStringLiteral("600002")), 1.0);
    QCOMPARE(ranks.value(QStringLiteral("600004")), 2.0);
    QCOMPARE(ranks.value(QStringLiteral("600003")), 3.0);
    QCOMPARE(ranks.value(QStringLiteral("600001")), 4.0);

    const auto ascending = CrossSectionalIndicatorRuntime::rank(values, true);
    QCOMPARE(ascending.value(QStringLiteral("600001")), 1.0);
    QCOMPARE(ascending.value(QStringLiteral("600002")), 3.0);
    QCOMPARE(ascending.value(QStringLiteral("600004")), 4.0);
}

void CrossSectionalBacktestTest::zscoreUsesPopulationDeviation()
{
    const QHash<QString, double> values {
        {QStringLiteral("a"), 10.0},
        {QStringLiteral("b"), 30.0},
        {QStringLiteral("c"), 20.0}};
    const auto scores = CrossSectionalIndicatorRuntime::zscore(values);
    const double expected = 10.0 / std::sqrt(200.0 / 3.0);
    QVERIFY(qAbs(scores.value(QStringLiteral("a")) + expected) < 1e-9);
    QVERIFY(qAbs(scores.value(QStringLiteral("b")) - expected) < 1e-9);
    QVERIFY(qAbs(scores.value(QStringLiteral("c"))) < 1e-12);
}

void CrossSectionalBacktestTest::percentileMapsTopToLowest()
{
    const QHash<QString, double> values {
        {QStringLiteral("a"), 10.0},
        {QStringLiteral("b"), 30.0},
        {QStringLiteral("c"), 20.0}};
    const auto percentiles = CrossSectionalIndicatorRuntime::percentile(values);
    QCOMPARE(percentiles.value(QStringLiteral("b")), 1.0);
    QCOMPARE(percentiles.value(QStringLiteral("c")), 0.5);
    QCOMPARE(percentiles.value(QStringLiteral("a")), 0.0);
}

void CrossSectionalBacktestTest::meanAndMedianScalars()
{
    const QHash<QString, double> values {
        {QStringLiteral("a"), 10.0},
        {QStringLiteral("b"), 30.0},
        {QStringLiteral("c"), 20.0}};
    QCOMPARE(CrossSectionalIndicatorRuntime::mean(values), 20.0);
    QCOMPARE(CrossSectionalIndicatorRuntime::median(values), 20.0);

    const QHash<QString, double> even {
        {QStringLiteral("a"), 10.0},
        {QStringLiteral("b"), 20.0},
        {QStringLiteral("c"), 30.0},
        {QStringLiteral("d"), 40.0}};
    QCOMPARE(CrossSectionalIndicatorRuntime::median(even), 25.0);
}

void CrossSectionalBacktestTest::emptyInputReturnsEmpty()
{
    QVERIFY(CrossSectionalIndicatorRuntime::rank({}, false).isEmpty());
    QVERIFY(CrossSectionalIndicatorRuntime::zscore({}).isEmpty());
    QVERIFY(CrossSectionalIndicatorRuntime::percentile({}).isEmpty());
    QCOMPARE(CrossSectionalIndicatorRuntime::mean({}), 0.0);
    QCOMPARE(CrossSectionalIndicatorRuntime::median({}), 0.0);
}

void CrossSectionalBacktestTest::strategySelectsTopNByCrossRank()
{
    QJsonObject document;
    document.insert(QStringLiteral("schemaVersion"), 3);
    document.insert(QStringLiteral("strategyId"), QStringLiteral("custom_cross_test"));
    document.insert(QStringLiteral("version"), 1);
    document.insert(QStringLiteral("name"), QStringLiteral("横截面测试"));
    document.insert(QStringLiteral("author"), QStringLiteral("Qt Test"));
    document.insert(QStringLiteral("market"), QStringLiteral("A股"));
    document.insert(QStringLiteral("executionScope"), QStringLiteral("backtest_only"));
    document.insert(QStringLiteral("createdAt"), QStringLiteral("2026-08-15T00:00:00.000Z"));
    document.insert(QStringLiteral("parameters"), QJsonArray{});
    document.insert(QStringLiteral("universe"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("explicit")},
        {QStringLiteral("symbols"), QJsonArray{QStringLiteral("600001"),
                                               QStringLiteral("600002"),
                                               QStringLiteral("600003")}}});
    document.insert(QStringLiteral("crossIndicators"), QJsonArray{
        QJsonObject{{QStringLiteral("alias"), QStringLiteral("close_rank")},
                    {QStringLiteral("indicator"), QStringLiteral("RANK")},
                    {QStringLiteral("of"), QJsonObject{{QStringLiteral("field"), QStringLiteral("close")}}}}});
    // 选 rank 最小的 2 只（即 close 最大的 2 只）。
    document.insert(QStringLiteral("selection"), QJsonObject{
        {QStringLiteral("mode"), QStringLiteral("topN")},
        {QStringLiteral("rankBy"), QStringLiteral("close_rank")},
        {QStringLiteral("count"), 2},
        {QStringLiteral("ascending"), true}});
    document.insert(QStringLiteral("entry"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("gte")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("value"), 1}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 1}}}});
    document.insert(QStringLiteral("exit"), QJsonObject{
        {QStringLiteral("op"), QStringLiteral("lt")},
        {QStringLiteral("left"), QJsonObject{{QStringLiteral("value"), 1}}},
        {QStringLiteral("right"), QJsonObject{{QStringLiteral("value"), 0}}}});

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    CrossSectionalCustomStrategy strategy(*plan);
    AlignedBarFrame frame;
    frame.timestamp = QDateTime(QDate(2026, 8, 1), QTime(9, 30));
    frame.bars.insert(QStringLiteral("600001"), makeBar(QStringLiteral("600001"), 10.0));
    frame.bars.insert(QStringLiteral("600002"), makeBar(QStringLiteral("600002"), 30.0));
    frame.bars.insert(QStringLiteral("600003"), makeBar(QStringLiteral("600003"), 20.0));

    const CrossSectionalFrameResult result = strategy.evaluateFrame(frame);
    QCOMPARE(result.targetWeights.size(), 2);
    QVERIFY(result.targetWeights.contains(QStringLiteral("600002")));
    QVERIFY(result.targetWeights.contains(QStringLiteral("600003")));
    QVERIFY(!result.targetWeights.contains(QStringLiteral("600001")));
    QVERIFY(qAbs(result.targetWeights.value(QStringLiteral("600002")) - 0.5) < 1e-9);
}

void CrossSectionalBacktestTest::engineRebalancesToTopNSelection()
{
    const QJsonObject document = makeCrossV3Document();
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(document, &definition, &error), qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));

    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 1);

    PortfolioDefinition portfolioDefinition;
    portfolioDefinition.portfolioId = QStringLiteral("cross_test");
    portfolioDefinition.name = QStringLiteral("横截面回测");
    portfolioDefinition.initialCapital = 1000000.0;
    portfolioDefinition.commissionRate = 0.0003;
    portfolioDefinition.slippageRate = 0.0002;
    portfolioDefinition.lotSize = 100;
    portfolioDefinition.tPlusOne = true;
    for (const QString &symbol : {QStringLiteral("600001"),
                                  QStringLiteral("600002"),
                                  QStringLiteral("600003")}) {
        Instrument instrument;
        instrument.symbol = symbol;
        instrument.name = QStringLiteral("测试股票%1").arg(symbol);
        instrument.market = Market::AShare;
        portfolioDefinition.legs.append({instrument, 0.0});
    }

    const PortfolioResult result =
        CrossSectionalBacktestEngine::run(strategy, aligned, portfolioDefinition, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 1);
    const PortfolioBar &bar = result.equityCurve.first();
    QCOMPARE(bar.positions.size(), 3);
    double quantity600001 = 0.0;
    double quantity600002 = 0.0;
    double quantity600003 = 0.0;
    for (const PortfolioPosition &position : bar.positions) {
        if (position.instrument.symbol == QStringLiteral("600001")) quantity600001 = position.quantity;
        else if (position.instrument.symbol == QStringLiteral("600002")) quantity600002 = position.quantity;
        else if (position.instrument.symbol == QStringLiteral("600003")) quantity600003 = position.quantity;
    }
    QCOMPARE(quantity600001, 0.0);
    QVERIFY(quantity600002 > 0.0);
    QVERIFY(quantity600003 > 0.0);
    QVERIFY(!result.trades.isEmpty());
    for (const PortfolioTrade &trade : result.trades)
        QVERIFY2(!trade.instrument.name.isEmpty(), "成交明细必须保留标的名称以供归档");
}

void CrossSectionalBacktestTest::engineKeepsCashWhenMaximumLegWeightCapsSelection()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_risk_cap");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.maximumLegWeight = 0.30;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 1);
    const PortfolioBar &bar = result.equityCurve.first();
    for (const PortfolioPosition &position : bar.positions)
        QVERIFY(position.weight <= 0.30 + 1e-9);
    QVERIFY(bar.cash >= 399000.0);
    QCOMPARE(result.riskSummary.constrainedFrameCount, 1);
    QVERIFY(result.riskSummary.maximumObservedLegWeight <= 0.30 + 1e-9);
    QVERIFY(result.riskSummary.maximumObservedTop3Weight <= 0.60 + 1e-9);
}

void CrossSectionalBacktestTest::engineLimitsActiveLegsDeterministically()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_active_limit");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.maximumActiveLegs = 1;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    const PortfolioBar &bar = result.equityCurve.first();
    int activeCount = 0;
    QString activeSymbol;
    for (const PortfolioPosition &position : bar.positions) {
        if (position.quantity > 0.0) {
            ++activeCount;
            activeSymbol = position.instrument.symbol;
        }
    }
    QCOMPARE(activeCount, 1);
    QCOMPARE(activeSymbol, QStringLiteral("600002"));
    QCOMPARE(result.riskSummary.maximumObservedActiveLegs, 1);
}

void CrossSectionalBacktestTest::industryConstraintRequiresCompleteSnapshot()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_industry_incomplete");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.maximumIndustryWeight = 0.40;
    portfolio.industryDatasetId = QStringLiteral("industry-cross-v1");

    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error, {},
        makeIndustrySnapshot(false));
    QVERIFY(result.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("完整")));

    AShareIndustrySnapshot mismatched = makeIndustrySnapshot();
    mismatched.datasetId = QStringLiteral("other-dataset");
    error.clear();
    const PortfolioResult mismatchResult = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error, {},
        mismatched);
    QVERIFY(mismatchResult.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("身份匹配")));
}

void CrossSectionalBacktestTest::industryTargetWeightsAreCappedAndIdentified()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_industry_cap");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.maximumIndustryWeight = 0.40;
    portfolio.industryDatasetId = QStringLiteral("industry-cross-v1");

    const AShareIndustrySnapshot snapshot = makeIndustrySnapshot();
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error, {},
        snapshot);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 1);
    QVERIFY(result.equityCurve.first().cash >= 599000.0);
    QCOMPARE(result.riskSummary.constrainedFrameCount, 1);
    QCOMPARE(result.riskSummary.industryConstrainedGroupCount, 1);
    QCOMPARE(result.riskSummary.industryLimitBreachFrameCount, 0);
    QVERIFY(result.riskSummary.maximumObservedIndustryWeight <= 0.40 + 1e-6);
    QVERIFY(result.riskSummary.industryCoverageComplete);
    QCOMPARE(result.riskSummary.industrySnapshotVersion,
             snapshot.snapshotVersion);
    QCOMPARE(result.industrySnapshot.datasetId, snapshot.datasetId);
    QCOMPARE(result.industrySnapshot.asOfDate, snapshot.asOfDate);
}

void CrossSectionalBacktestTest::
    strategyFactorTargetExposureIsCappedByKeepingCash()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {makeBar(QStringLiteral("600001"), 10.0)}},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), {makeBar(QStringLiteral("600003"), 20.0)}}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_factor_exposure_cap");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.factorExposureAlias = QStringLiteral("close_rank");
    portfolio.maximumAbsoluteFactorExposure = 0.30;

    PortfolioDefinition invalidPortfolio = portfolio;
    invalidPortfolio.factorExposureAlias = QStringLiteral("missing_factor");
    const PortfolioResult invalidResult = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), invalidPortfolio, &error);
    QVERIFY(invalidResult.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("missing_factor")));
    error.clear();

    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 1);
    QVERIFY(result.equityCurve.first().cash >= 509000.0);
    QCOMPARE(result.riskSummary.factorExposureAlias,
             QStringLiteral("close_rank"));
    QVERIFY(result.riskSummary.factorExposureCoverageComplete);
    QCOMPARE(result.riskSummary.factorExposureConstrainedFrameCount, 1);
    QCOMPARE(result.riskSummary.factorExposureUnmeasuredFrameCount, 0);
    QCOMPARE(result.riskSummary.factorExposureLimitBreachFrameCount, 0);
    QVERIFY(qAbs(result.riskSummary.maximumRequestedAbsoluteFactorExposure
                 - 0.6123724357) < 1e-6);
    QVERIFY(result.riskSummary.maximumObservedAbsoluteFactorExposure
            <= 0.30 + 1e-6);
}

void CrossSectionalBacktestTest::
    heldSymbolWithoutCurrentFactorIsReportedUnmeasured()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto bars = [](const QString &symbol, double first, double second) {
        QVector<Candle> values{makeBar(symbol, first), makeBar(symbol, second)};
        values[1].startTime = QDateTime(QDate(2026, 8, 1), QTime(9, 31));
        return values;
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), bars(QStringLiteral("600001"), 10.0, 40.0)},
        {QStringLiteral("600002"), {makeBar(QStringLiteral("600002"), 30.0)}},
        {QStringLiteral("600003"), bars(QStringLiteral("600003"), 20.0, 20.0)}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_factor_missing");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.factorExposureAlias = QStringLiteral("close_rank");
    portfolio.maximumAbsoluteFactorExposure = 10.0;

    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 2);
    QVERIFY(!result.riskSummary.factorExposureCoverageComplete);
    QCOMPARE(result.riskSummary.factorExposureUnmeasuredFrameCount, 1);
    QCOMPARE(result.riskSummary.factorExposureLimitBreachFrameCount, 0);
}

void CrossSectionalBacktestTest::factorResearchUsesOnlyNextAlignedFrameReturns()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto bars = [](const QString &symbol, double first, double second) {
        QVector<Candle> values{makeBar(symbol, first), makeBar(symbol, second)};
        values[1].startTime = QDateTime(QDate(2026, 8, 1), QTime(9, 31));
        return values;
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), bars(QStringLiteral("600001"), 10.0, 10.3)},
        {QStringLiteral("600002"), bars(QStringLiteral("600002"), 20.0, 20.4)},
        {QStringLiteral("600003"), bars(QStringLiteral("600003"), 30.0, 30.3)}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_factor_research");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.factorResearchAlias = QStringLiteral("close_rank");
    portfolio.factorDefinitionId = QStringLiteral("custom_cross_test.close_rank");
    portfolio.factorDefinitionVersion = 1;
    portfolio.factorPreprocessing.minimumPairs = 3;

    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.factorResearchSummary.evaluatedFrameCount, 1);
    QCOMPARE(result.factorResearchSummary.validPairCount, 3);
    QVERIFY(result.factorResearchSummary.meanIc > 0.99);
    QCOMPARE(result.factorResearchSummary.meanRankIc, 1.0);
    QCOMPARE(result.factorResearchSummary.factorId,
             QStringLiteral("custom_cross_test.close_rank"));
    QCOMPARE(result.factorResearchSummary.factorVersion, 1);
}

void CrossSectionalBacktestTest::
    factorResearchProducesExpandingOutOfSampleWindows()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto bars = [](const QString &symbol, double base, double slope) {
        QVector<Candle> values;
        for (int index = 0; index < 41; ++index) {
            Candle candle = makeBar(symbol, base + slope * index);
            candle.startTime = QDateTime(
                QDate(2026, 8, 1), QTime(9, 30)).addSecs(index * 60);
            values.append(candle);
        }
        return values;
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), bars(QStringLiteral("600001"), 10.0, 0.01)},
        {QStringLiteral("600002"), bars(QStringLiteral("600002"), 20.0, 0.03)},
        {QStringLiteral("600003"), bars(QStringLiteral("600003"), 30.0, 0.08)}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_factor_oos");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.factorResearchAlias = QStringLiteral("close_rank");
    portfolio.factorDefinitionId = QStringLiteral("custom_cross_test.close_rank");
    portfolio.factorDefinitionVersion = 1;
    portfolio.factorPreprocessing.minimumPairs = 3;

    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.factorResearchSummary.evaluatedFrameCount, 40);
    QVERIFY(result.factorResearchSummary.outOfSample.available);
    QCOMPARE(result.factorResearchSummary.outOfSample.windows.size(), 2);
    QCOMPARE(result.factorResearchSummary.outOfSample.evaluationFrameCount, 20);
}

void CrossSectionalBacktestTest::
    factorResearchIndustryDemeaningRequiresAndUsesSnapshot()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto bars = [](const QString &symbol, double first, double second) {
        QVector<Candle> values{makeBar(symbol, first), makeBar(symbol, second)};
        values[1].startTime = QDateTime(QDate(2026, 8, 1), QTime(9, 31));
        return values;
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), bars(QStringLiteral("600001"), 10.0, 10.5)},
        {QStringLiteral("600002"), bars(QStringLiteral("600002"), 20.0, 22.0)},
        {QStringLiteral("600003"), bars(QStringLiteral("600003"), 30.0, 30.0)}};
    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_factor_industry_demean");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.factorResearchAlias = QStringLiteral("close_rank");
    portfolio.factorDefinitionId = QStringLiteral("custom_cross_test.close_rank");
    portfolio.factorDefinitionVersion = 1;
    portfolio.factorPreprocessing.minimumPairs = 2;
    portfolio.factorNeutralization.method =
        QStringLiteral("within_group_demean");
    portfolio.factorNeutralization.minimumGroupSize = 2;
    portfolio.industryDatasetId = QStringLiteral("industry-cross-v1");

    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    PortfolioResult rejected = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);
    QVERIFY(rejected.equityCurve.isEmpty());
    QVERIFY(error.contains(QStringLiteral("行业")));

    error.clear();
    const AShareIndustrySnapshot snapshot = makeIndustrySnapshot();
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error, {}, snapshot);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.factorResearchSummary.evaluatedFrameCount, 1);
    QCOMPARE(result.factorResearchSummary.neutralizationAppliedFrameCount, 1);
    QCOMPARE(result.factorResearchSummary.neutralizationSkippedFrameCount, 0);
    QCOMPARE(result.factorResearchSummary.neutralizedPairCount, 2);
    QCOMPARE(result.factorResearchSummary.smallGroupDroppedPairCount, 1);
    QCOMPARE(result.factorResearchSummary.validPairCount, 3);
    QCOMPARE(result.factorResearchSummary.timeSeries.first().validPairCount, 2);
    QVERIFY(result.factorResearchSummary.meanIc > 0.99);
    QCOMPARE(result.industrySnapshot.snapshotVersion,
             snapshot.snapshotVersion);
}

void CrossSectionalBacktestTest::circuitBreakerStopsStrategyReentry()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto twoBars = [](const QString &symbol, double close) {
        QVector<Candle> values{makeBar(symbol, close), makeBar(symbol, close)};
        values[1].startTime = QDateTime(QDate(2026, 8, 1), QTime(9, 31));
        return values;
    };
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), twoBars(QStringLiteral("600001"), 10.0)},
        {QStringLiteral("600002"), twoBars(QStringLiteral("600002"), 30.0)},
        {QStringLiteral("600003"), twoBars(QStringLiteral("600003"), 20.0)}};

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_circuit_breaker");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.commissionRate = 0.001;
    portfolio.slippageRate = 0.001;
    portfolio.tPlusOne = false;
    portfolio.maximumPortfolioLossPercent = 0.01;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(result.riskSummary.circuitBreakerTriggered);
    QCOMPARE(result.riskSummary.circuitBreakerReason, QStringLiteral("loss_limit"));
    QVERIFY(result.riskSummary.completedCircuitBreakerExitCount > 0);
    QCOMPARE(result.riskSummary.pendingCircuitBreakerExitCount, 0);
    const QDateTime secondFrame(QDate(2026, 8, 1), QTime(9, 31));
    for (const PortfolioTrade &trade : result.trades) {
        QVERIFY2(!(trade.side == QStringLiteral("buy")
                   && trade.timestamp == secondFrame),
                 "熔断锁存后策略不得在下一帧重新入场");
    }
    for (const PortfolioPosition &position : result.equityCurve.last().positions)
        QCOMPARE(position.quantity, 0.0);
}

void CrossSectionalBacktestTest::absentLegDoesNotTradeAtForwardFilledPrice()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    Candle first1 = makeBar(QStringLiteral("600001"), 10.0);
    Candle first2 = makeBar(QStringLiteral("600002"), 30.0);
    Candle first3 = makeBar(QStringLiteral("600003"), 20.0);
    Candle second1 = makeBar(QStringLiteral("600001"), 40.0);
    Candle second3 = makeBar(QStringLiteral("600003"), 20.0);
    second1.startTime = second3.startTime =
        QDateTime(QDate(2026, 8, 1), QTime(9, 31));
    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"), {first1, second1}},
        {QStringLiteral("600002"), {first2}},
        {QStringLiteral("600003"), {first3, second3}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 2);

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_absent_execution");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.equityCurve.size(), 2);
    for (const PortfolioTrade &trade : result.trades) {
        QVERIFY2(!(trade.instrument.symbol == QStringLiteral("600002")
                   && trade.timestamp == second1.startTime),
                 "缺报价腿不得按前向填充旧价成交");
    }
    const PortfolioBar &secondBar = result.equityCurve.last();
    const auto found = std::find_if(
        secondBar.positions.cbegin(), secondBar.positions.cend(),
        [](const PortfolioPosition &position) {
            return position.instrument.symbol == QStringLiteral("600002");
        });
    QVERIFY(found != secondBar.positions.cend());
    QVERIFY(found->quantity > 0.0);
    QVERIFY(found->priceStale);
    QCOMPARE(result.riskSummary.staleValuationPointCount, 1);
}

void CrossSectionalBacktestTest::suspensionFactIsNotConfusedWithUnknownMissingQuote()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto barAt = [](const QString &symbol, const QDate &date, double close) {
        Candle candle = makeBar(symbol, close);
        candle.startTime = QDateTime(date, QTime(9, 30));
        return candle;
    };
    const QDate firstDay(2026, 8, 3);
    const QDate secondDay(2026, 8, 4);
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {barAt(QStringLiteral("600001"), firstDay, 10.0),
          barAt(QStringLiteral("600001"), secondDay, 40.0)}},
        {QStringLiteral("600002"),
         {barAt(QStringLiteral("600002"), firstDay, 30.0)}},
        {QStringLiteral("600003"),
         {barAt(QStringLiteral("600003"), firstDay, 20.0),
          barAt(QStringLiteral("600003"), secondDay, 20.0)}}};
    AShareInstrumentDayRecord suspended;
    suspended.instrument.symbol = QStringLiteral("600002");
    suspended.instrument.market = Market::AShare;
    suspended.tradingDate = secondDay;
    suspended.state = AShareInstrumentDayState::Suspended;
    suspended.source = QStringLiteral("test-fixture");
    suspended.sourceVersion = QStringLiteral("fixture-v1");
    const AShareCalendarSnapshot calendar = AShareTradingCalendar::snapshot(
        firstDay, secondDay,
        {QStringLiteral("600001"), QStringLiteral("600002"),
         QStringLiteral("600003")}, {suspended});

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_suspension_fact");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, AlignedBarSeries::build(series), portfolio, &error,
        calendar);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(result.riskSummary.authoritativeSuspensionPointCount, 1);
    QCOMPARE(result.riskSummary.unknownMissingQuotePointCount, 0);
    const PortfolioBar &secondBar = result.equityCurve.last();
    const auto position = std::find_if(
        secondBar.positions.cbegin(), secondBar.positions.cend(),
        [](const PortfolioPosition &value) {
            return value.instrument.symbol == QStringLiteral("600002");
        });
    QVERIFY(position != secondBar.positions.cend());
    QCOMPARE(position->marketStatus, QStringLiteral("suspended"));
    QVERIFY(position->priceStale);
}

void CrossSectionalBacktestTest::missingLegExitsOnFirstFreshQuoteAfterThreshold()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto barAt = [](const QString &symbol, const QDate &date, double close) {
        Candle candle = makeBar(symbol, close);
        candle.startTime = QDateTime(date, QTime(9, 30));
        return candle;
    };
    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"),
         {barAt(QStringLiteral("600001"), QDate(2026, 8, 3), 10.0),
          barAt(QStringLiteral("600001"), QDate(2026, 8, 4), 40.0),
          barAt(QStringLiteral("600001"), QDate(2026, 8, 5), 40.0)}},
        {QStringLiteral("600002"),
         {barAt(QStringLiteral("600002"), QDate(2026, 8, 3), 30.0),
          barAt(QStringLiteral("600002"), QDate(2026, 8, 5), 50.0)}},
        {QStringLiteral("600003"),
         {barAt(QStringLiteral("600003"), QDate(2026, 8, 3), 20.0),
          barAt(QStringLiteral("600003"), QDate(2026, 8, 4), 20.0),
          barAt(QStringLiteral("600003"), QDate(2026, 8, 5), 20.0)}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 3);

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_missing_exit");
    portfolio.periodSeconds = 86400;
    portfolio.initialCapital = 1000000.0;
    portfolio.maximumConsecutiveMissingBars = 1;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto riskExit = std::find_if(
        result.trades.cbegin(), result.trades.cend(),
        [](const PortfolioTrade &trade) {
            return trade.instrument.symbol == QStringLiteral("600002")
                && trade.side == QStringLiteral("sell")
                && trade.reason.contains(QStringLiteral("连续缺失"));
        });
    QVERIFY(riskExit != result.trades.cend());
    QCOMPARE(riskExit->timestamp.date(), QDate(2026, 8, 5));
    const PortfolioBar &last = result.equityCurve.last();
    const auto position = std::find_if(
        last.positions.cbegin(), last.positions.cend(),
        [](const PortfolioPosition &value) {
            return value.instrument.symbol == QStringLiteral("600002");
        });
    QVERIFY(position != last.positions.cend());
    QCOMPARE(position->quantity, 0.0);
    QCOMPARE(result.riskSummary.maximumObservedConsecutiveMissingBars, 1);
    QCOMPARE(result.riskSummary.completedMissingRiskExitCount, 1);
    QCOMPARE(result.riskSummary.pendingMissingRiskExitCount, 0);
}

void CrossSectionalBacktestTest::tPlusOneDefersSameDayRotationSell()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    CrossSectionalCustomStrategy strategy(*plan);

    auto barAt = [](const QString &symbol, const QDate &date,
                    const QTime &time, double close) {
        Candle candle = makeBar(symbol, close);
        candle.startTime = QDateTime(date, time);
        return candle;
    };
    const QDate firstDay(2026, 8, 3);
    const QDate secondDay(2026, 8, 4);
    const QHash<QString, QVector<Candle>> series {
        {QStringLiteral("600001"),
         {barAt(QStringLiteral("600001"), firstDay, QTime(9, 30), 10.0),
          barAt(QStringLiteral("600001"), firstDay, QTime(9, 31), 40.0),
          barAt(QStringLiteral("600001"), secondDay, QTime(9, 30), 40.0)}},
        {QStringLiteral("600002"),
         {barAt(QStringLiteral("600002"), firstDay, QTime(9, 30), 30.0),
          barAt(QStringLiteral("600002"), firstDay, QTime(9, 31), 30.0),
          barAt(QStringLiteral("600002"), secondDay, QTime(9, 30), 30.0)}},
        {QStringLiteral("600003"),
         {barAt(QStringLiteral("600003"), firstDay, QTime(9, 30), 20.0),
          barAt(QStringLiteral("600003"), firstDay, QTime(9, 31), 20.0),
          barAt(QStringLiteral("600003"), secondDay, QTime(9, 30), 20.0)}}};
    const AlignedBarSeries aligned = AlignedBarSeries::build(series);

    PortfolioDefinition portfolio;
    portfolio.portfolioId = QStringLiteral("cross_t_plus_one");
    portfolio.periodSeconds = 60;
    portfolio.initialCapital = 1000000.0;
    portfolio.tPlusOne = true;
    const PortfolioResult result = CrossSectionalBacktestEngine::run(
        strategy, aligned, portfolio, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    const auto sameDaySell = std::find_if(
        result.trades.cbegin(), result.trades.cend(),
        [&firstDay](const PortfolioTrade &trade) {
            return trade.side == QStringLiteral("sell")
                && trade.timestamp.date() == firstDay;
        });
    QVERIFY(sameDaySell == result.trades.cend());
    const auto nextDaySell = std::find_if(
        result.trades.cbegin(), result.trades.cend(),
        [&secondDay](const PortfolioTrade &trade) {
            return trade.side == QStringLiteral("sell")
                && trade.timestamp.date() == secondDay;
        });
    QVERIFY(nextDaySell != result.trades.cend());
}

void CrossSectionalBacktestTest::serviceValidatesLifecycleAndRejectsMissingData()
{
    CrossSectionalBacktestService service;
    QVERIFY(!service.isReady());
    QVERIFY(!service.isRunning());
    QVERIFY(!service.start(QString()));
    QVERIFY(!service.isRunning());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(service.start(directory.filePath(QStringLiteral("cross.db"))));
    QVERIFY(service.isReady());
    QVERIFY(!service.isRunning());

    CustomStrategyDefinition definition;
    PortfolioDefinition portfolio;
    portfolio.periodSeconds = 60;
    QVERIFY(!service.request(0, definition, {}, portfolio, 10, {}));
    portfolio.maximumIndustryWeight = 0.40;
    QVERIFY(!service.request(1, definition, {}, portfolio, 10, {}));
    portfolio.maximumIndustryWeight = 0.0;

    const QJsonObject document = makeCrossV3Document();
    CustomStrategyValidator validator;
    QVERIFY2(validator.validate(document, &definition, nullptr), "v3 校验应通过");

    QSignalSpy failedSpy(&service, &CrossSectionalBacktestService::failed);
    QVERIFY(service.request(1, definition, {}, portfolio, 10, {}));
    QVERIFY(service.isRunning());
    QVERIFY(failedSpy.wait(3000));
    QVERIFY(!service.isRunning());
    QCOMPARE(failedSpy.size(), 1);
    const QString message = failedSpy.first().at(1).toString();
    QVERIFY(message.contains(QStringLiteral("数据集身份")));

    service.stop();
    QVERIFY(!service.isReady());
    QVERIFY(!service.isRunning());
}

void CrossSectionalBacktestTest::requestPolicyRequiresSamePeriodIdentityAndCapacity()
{
    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    QVERIFY2(validator.validate(makeCrossV3Document(), &definition, &error),
             qPrintable(error));

    QVector<LocalHistoryDataset> catalog;
    for (const QString &symbol : {QStringLiteral("600001"), QStringLiteral("600002"),
                                  QStringLiteral("600003")}) {
        LocalHistoryDataset dataset;
        dataset.instrument.symbol = symbol;
        dataset.instrument.market = Market::AShare;
        dataset.periodSeconds = 60;
        dataset.candleCount = 500;
        dataset.firstTime = QDateTime(QDate(2026, 1, 1), QTime(9, 30));
        dataset.lastTime = QDateTime(QDate(2026, 8, 19), QTime(15, 0));
        dataset.source = QStringLiteral("raw_tick_cache_v1");
        dataset.datasetVersion = QString(64, QLatin1Char('a'));
        catalog.append(dataset);
    }

    QVariantMap result = CrossSectionalBacktestService::validateLocalDatasets(
        definition, 60, 200, catalog);
    QVERIFY2(result.value(QStringLiteral("valid")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toString()));
    QCOMPARE(result.value(QStringLiteral("symbolCount")).toInt(), 3);
    QCOMPARE(result.value(QStringLiteral("minimumCandleCount")).toInt(), 500);

    result = CrossSectionalBacktestService::validateLocalDatasets(
        definition, 300, 200, catalog);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    QVERIFY(result.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("同周期")));

    catalog[1].candleCount = 100;
    result = CrossSectionalBacktestService::validateLocalDatasets(
        definition, 60, 200, catalog);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    QVERIFY(result.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("600002")));

    catalog[1].candleCount = 500;
    catalog[2].datasetVersion.clear();
    result = CrossSectionalBacktestService::validateLocalDatasets(
        definition, 60, 200, catalog);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    QVERIFY(result.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("身份")));
}

void CrossSectionalBacktestTest::riskConfigurationValidationHasStableBoundaries()
{
    QVariantMap result = CrossSectionalBacktestService::validateRiskConfiguration(
        0.30, 10, 5);
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("maximumLegWeight")).toDouble(), 0.30);
    QCOMPARE(result.value(QStringLiteral("maximumActiveLegs")).toInt(), 10);
    QCOMPARE(result.value(QStringLiteral("maximumConsecutiveMissingBars")).toInt(), 5);

    result = CrossSectionalBacktestService::validateRiskConfiguration(0.0, 10, 5);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
    result = CrossSectionalBacktestService::validateRiskConfiguration(0.30, -1, 5);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateRiskConfiguration(0.30, 10, -1);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());

    result = CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
        10.0, 5.0);
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("maximumPortfolioLossPercent")).toDouble(),
             10.0);
    QCOMPARE(result.value(QStringLiteral("maximumPortfolioDrawdownPercent")).toDouble(),
             5.0);
    result = CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
        0.0, 0.0);
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
        -0.1, 5.0);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
        10.0, 100.1);
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());

    result = CrossSectionalBacktestService::validateIndustryConfiguration(
        0.40, QStringLiteral("industry-cross-v1"));
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("maximumIndustryWeight")).toDouble(),
             0.40);
    QCOMPARE(result.value(QStringLiteral("industryDatasetId")).toString(),
             QStringLiteral("industry-cross-v1"));
    result = CrossSectionalBacktestService::validateIndustryConfiguration(
        0.0, QString());
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateIndustryConfiguration(
        0.40, QString());
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateIndustryConfiguration(
        1.01, QStringLiteral("industry-cross-v1"));
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());

    result = CrossSectionalBacktestService::validateFactorExposureConfiguration(
        0.30, QStringLiteral("close_rank"));
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("factorExposureAlias")).toString(),
             QStringLiteral("close_rank"));
    result = CrossSectionalBacktestService::validateFactorExposureConfiguration(
        0.0, QString());
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorExposureConfiguration(
        0.30, QString());
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorExposureConfiguration(
        10.01, QStringLiteral("close_rank"));
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());

    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.01, 0.99, 3, 10.0,
        false, 2, QString{});
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("factorResearchAlias")).toString(),
             QStringLiteral("close_rank"));
    QCOMPARE(result.value(QStringLiteral("costPolicy")).toMap()
                 .value(QStringLiteral("oneWayCostBps")).toDouble(), 10.0);
    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.99, 0.01, 3, 10.0,
        false, 2, QString{});
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.01, 0.99, 1, 10.0,
        false, 2, QString{});
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.01, 0.99, 3, 1000.01,
        false, 2, QString{});
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.01, 0.99, 3, 10.0,
        true, 2, QString{});
    QVERIFY(!result.value(QStringLiteral("valid")).toBool());
    result = CrossSectionalBacktestService::validateFactorResearchConfiguration(
        QStringLiteral("close_rank"), 0.01, 0.99, 3, 10.0,
        true, 2, QStringLiteral("industry-v1"));
    QVERIFY(result.value(QStringLiteral("valid")).toBool());
    QCOMPARE(result.value(QStringLiteral("neutralizationPolicy")).toMap()
                 .value(QStringLiteral("method")).toString(),
             QStringLiteral("within_group_demean"));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CrossSectionalBacktestTest)

#include "tst_cross_sectional_backtest.moc"
