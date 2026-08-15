#include "strategy/CrossSectionalIndicatorRuntime.h"

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
};

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

} // namespace pp

QTEST_GUILESS_MAIN(pp::CrossSectionalBacktestTest)

#include "tst_cross_sectional_backtest.moc"
