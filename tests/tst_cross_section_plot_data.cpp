#include "visualization/CrossSectionPlotData.h"

#include <QTest>

namespace pp {

namespace {

CrossSectionPlotPoint point(int displayIndex, const QString &symbol,
                            const QString &board, double price, bool valid = true)
{
    CrossSectionPlotPoint result;
    result.displayIndex = displayIndex;
    result.symbol = symbol;
    result.name = symbol;
    result.board = board;
    result.boardBit = CrossSectionPlotData::boardBit(board);
    result.price = price;
    result.valid = valid;
    return result;
}

} // namespace

class CrossSectionPlotDataTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsBoardsAndPreservesDeferredBseSlot();
    void computesStableCoordinatesAndFilteredSummary();
    void findsNearestVisiblePoint();
    void handlesMoreThanFiveThousandPoints();
};

void CrossSectionPlotDataTest::mapsBoardsAndPreservesDeferredBseSlot()
{
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("主板")), 1);
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("深市主板")), 2);
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("创业板")), 4);
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("科创板")), 8);
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("北交所")), 16);
    QCOMPARE(CrossSectionPlotData::boardBit(QStringLiteral("未知")), 0);
}

void CrossSectionPlotDataTest::computesStableCoordinatesAndFilteredSummary()
{
    const QVector<CrossSectionPlotPoint> points {
        point(1, QStringLiteral("600001"), QStringLiteral("主板"), 10.0),
        point(2, QStringLiteral("000001"), QStringLiteral("深市主板"), 20.0),
        point(3, QStringLiteral("300001"), QStringLiteral("创业板"), 30.0),
        point(4, QStringLiteral("688001"), QStringLiteral("科创板"), 40.0, false),
        point(5, QStringLiteral("830001"), QStringLiteral("北交所"), 50.0)
    };

    const CrossSectionPlotSummary summary = CrossSectionPlotData::summarize(points, 15);
    QCOMPARE(summary.validCount, 3);
    QCOMPARE(summary.invalidCount, 1);
    QCOMPARE(summary.minimum, 10.0);
    QCOMPARE(summary.maximum, 30.0);
    QCOMPARE(summary.average, 20.0);
    QCOMPARE(summary.median, 20.0);
    QCOMPARE(summary.bseCount, 1);
    QCOMPARE(summary.topIndices, QVector<int>({2, 1, 0}));

    QCOMPARE(CrossSectionPlotData::xFor(points.at(0), points.size(), 400.0), 0.0);
    QCOMPARE(CrossSectionPlotData::xFor(points.at(4), points.size(), 400.0), 400.0);
    QCOMPARE(CrossSectionPlotData::xFor(points.at(2), points.size(), 400.0), 200.0);
    QCOMPARE(CrossSectionPlotData::yFor(30.0, false, 30.0, 300.0), 0.0);
}

void CrossSectionPlotDataTest::findsNearestVisiblePoint()
{
    const QVector<CrossSectionPlotPoint> points {
        point(1, QStringLiteral("600001"), QStringLiteral("主板"), 10.0),
        point(2, QStringLiteral("000001"), QStringLiteral("深市主板"), 20.0),
        point(3, QStringLiteral("300001"), QStringLiteral("创业板"), 30.0)
    };
    const CrossSectionPlotSummary summary = CrossSectionPlotData::summarize(points, 31);
    const QPointF target(
        CrossSectionPlotData::xFor(points.at(1), points.size(), 300.0),
        CrossSectionPlotData::yFor(points.at(1).price, false, summary.maximum, 300.0));
    QCOMPARE(CrossSectionPlotData::nearestPoint(
                 points, 31, false, summary.maximum, 300.0, 300.0, target, 10.0), 1);
    QCOMPARE(CrossSectionPlotData::nearestPoint(
                 points, 1, false, 10.0, 300.0, 300.0, target, 10.0), -1);
}

void CrossSectionPlotDataTest::handlesMoreThanFiveThousandPoints()
{
    QVector<CrossSectionPlotPoint> points;
    points.reserve(5207);
    for (int index = 0; index < 5207; ++index) {
        points.append(point(index + 1, QString::number(600000 + index),
                            QStringLiteral("主板"), 1.0 + index / 10.0));
    }
    const CrossSectionPlotSummary summary = CrossSectionPlotData::summarize(points, 31);
    QCOMPARE(summary.validCount, 5207);
    QCOMPARE(summary.mainBoardCount, 5207);
    QCOMPARE(summary.topIndices.size(), 3);
    QCOMPARE(points.at(summary.topIndices.constFirst()).displayIndex, 5207);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::CrossSectionPlotDataTest)

#include "tst_cross_section_plot_data.moc"
