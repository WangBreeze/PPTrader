#include "services/ParameterGridScanner.h"
#include "strategy/CustomStrategyValidator.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

namespace pp {

class ParameterGridScannerTest : public QObject {
    Q_OBJECT

private slots:
    void expandGridComputesCartesianProduct();
    void scanRunsAllCombinationsAndSorts();
    void scanRejectsTooManyCombinations();
    void scanReportsEmptyGrid();
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

} // namespace pp

QTEST_GUILESS_MAIN(pp::ParameterGridScannerTest)

#include "tst_parameter_grid_scanner.moc"
