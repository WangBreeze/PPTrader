#include "strategy/CrossSectionalIndicatorRuntime.h"
#include "strategy/CrossSectionalCustomStrategy.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomStrategyValidator.h"
#include "domain/CustomStrategyTypes.h"
#include "services/CrossSectionalBacktestEngine.h"

#include <QJsonArray>
#include <QJsonObject>
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
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CrossSectionalBacktestTest)

#include "tst_cross_sectional_backtest.moc"
