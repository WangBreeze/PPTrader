#include "models/CandleListModel.h"
#include "models/LocalCandleSeriesListModel.h"
#include "visualization/LocalCandlePlotData.h"
#include "visualization/LocalCandleSeriesItem.h"

#include <QSGNode>
#include <QElapsedTimer>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace pp;

namespace {

class TestableCandleSeriesItem final : public LocalCandleSeriesItem {
public:
    using LocalCandleSeriesItem::LocalCandleSeriesItem;

    QSGNode *render(QSGNode *oldNode = nullptr)
    {
        return updatePaintNode(oldNode, nullptr);
    }
};

CandlePoint pointAt(int index, bool withSignal)
{
    CandlePoint point;
    point.candle.instrument.symbol = QStringLiteral("600519");
    point.candle.instrument.market = Market::AShare;
    point.candle.startTime = QDateTime(QDate(2026, 8, 24), QTime(1, 30), Qt::UTC)
                                 .addSecs(qint64(index) * 90);
    point.candle.periodSeconds = 90;
    point.candle.open = 100.0 + (index % 50) * 0.1;
    point.candle.high = point.candle.open + 0.8;
    point.candle.low = point.candle.open - 0.6;
    point.candle.close = point.candle.open + (index % 2 == 0 ? 0.3 : -0.2);
    point.candle.volume = 1000.0 + index;
    point.candle.source = QStringLiteral("backtest_fixture");
    point.hasSignal = withSignal;
    if (withSignal) {
        point.signal.instrument = point.candle.instrument;
        point.signal.timestamp = point.candle.startTime;
        point.signal.side = index % 2 == 0 ? SignalSide::Buy : SignalSide::Sell;
        point.signal.price = point.candle.close;
        point.signal.reason = QStringLiteral("测试信号 %1").arg(index);
    }
    return point;
}

} // namespace

class CandleSeriesItemTest final : public QObject {
    Q_OBJECT

private slots:
    void calculatesCommonPriceOverlaysWithoutInventingWarmupValues();
    void calculatesRsiAndMacdWithoutInventingWarmupValues();
    void createsReadablePriceAndTimeAxisTicks();
    void aggregatesDenseViewportWithoutLosingExtremes();
    void genericBacktestRolesAndSignals();
    void archivedResultRestoresCandleAndSignalRoles();
    void rendersVolumeIndicatorsAndAxesAsBoundedGeometry();
    void automaticRangeIncludesVisibleMovingAverageValues();
    void parameterizesIndicatorsAndRendersIndependentSubplots();
    void incrementalTailUpdatesMatchFullIndicatorRebuild();
    void liveSeriesGrowsViewportUntilUserAdjustsIt();
    void rendersFiveThousandBarsWithBoundedGeometry();
    void continuousTradingViewStyleInteractionStaysBounded();
    void fiftyThousandBarContinuousPerformanceStaysWithinBudget();
    void fiftyThousandBarFourHourRefreshStaysResponsive();
};

void CandleSeriesItemTest::createsReadablePriceAndTimeAxisTicks()
{
    const QVector<double> prices = LocalCandlePlotData::nicePriceTicks(
        {10.02, 10.98, true}, 6);
    QCOMPARE(prices.size(), 4);
    QCOMPARE(prices.at(0), 10.2);
    QCOMPARE(prices.at(1), 10.4);
    QCOMPARE(prices.at(2), 10.6);
    QCOMPARE(prices.at(3), 10.8);

    const QVector<int> indices = LocalCandlePlotData::timeTickIndices(
        {20, 60}, 6);
    QCOMPARE(indices, QVector<int>({20, 32, 44, 55, 67, 79}));
    QVERIFY(LocalCandlePlotData::nicePriceTicks({}, 6).isEmpty());
    QVERIFY(LocalCandlePlotData::timeTickIndices({}, 6).isEmpty());
}

void CandleSeriesItemTest::aggregatesDenseViewportWithoutLosingExtremes()
{
    QVector<LocalCandleSeriesPoint> points;
    for (int index = 0; index < 10; ++index) {
        LocalCandleSeriesPoint point;
        point.candle.open = 100.0 + index;
        point.candle.close = 100.5 + index;
        point.candle.high = 110.0 + index;
        point.candle.low = 90.0 - index;
        point.candle.volume = 1000.0 + index * 10.0;
        points.append(point);
    }

    QCOMPARE(LocalCandlePlotData::renderBucketCapacity(2.0), 4);
    const QVector<LocalCandleRenderBucket> buckets =
        LocalCandlePlotData::renderBuckets(points, {0, 10}, 2.0);
    QCOMPARE(buckets.size(), 4);
    QCOMPARE(buckets.at(1).firstIndex, 2);
    QCOMPARE(buckets.at(1).lastIndex, 4);
    QCOMPARE(buckets.at(1).sourceCount(), 3);
    QCOMPARE(buckets.at(1).open, 102.0);
    QCOMPARE(buckets.at(1).close, 104.5);
    QCOMPARE(buckets.at(1).high, 114.0);
    QCOMPARE(buckets.at(1).low, 86.0);
    QCOMPARE(buckets.at(1).peakVolume, 1040.0);

    const QVector<LocalCandleRenderBucket> exact =
        LocalCandlePlotData::renderBuckets(points, {0, 10}, 10.0);
    QCOMPARE(exact.size(), 10);
    for (const LocalCandleRenderBucket &bucket : exact)
        QCOMPARE(bucket.sourceCount(), 1);
    QVERIFY(LocalCandlePlotData::renderBuckets({}, {}, 10.0).isEmpty());
    QCOMPARE(LocalCandlePlotData::renderBucketCapacity(0.0), 0);
}

void CandleSeriesItemTest::rendersVolumeIndicatorsAndAxesAsBoundedGeometry()
{
    CandleListModel model;
    for (int index = 0; index < 120; ++index)
        model.addPoint(pointAt(index, false));

    TestableCandleSeriesItem item;
    item.setWidth(960);
    item.setHeight(540);
    item.setShowVolume(true);
    item.setShowMovingAverages(true);
    item.setShowBollingerBands(true);
    item.setModel(&model);

    QSGNode *node = item.render();
    QVERIFY(node != nullptr);
    QCOMPARE(item.volumeVertexCount(), 120 * 6);
    QCOMPARE(item.indicatorVertexCount(), 1030);
    QCOMPARE(item.geometryVertexCount(), 120 * 8 + 120 * 6 + 1030);
    QCOMPARE(item.maximumVolume(), 1119.0);
    QVERIFY(item.priceAxisTicks().size() >= 3);
    QVERIFY(item.priceAxisTicks().size() <= 10);
    QCOMPARE(item.timeAxisTicks().size(), 6);
    QCOMPARE(item.timeAxisTicks().first().toMap().value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(item.timeAxisTicks().last().toMap().value(QStringLiteral("index")).toInt(), 119);

    const QVariantMap last = item.findNearest(item.width() - 1.0);
    QVERIFY(std::isfinite(last.value(QStringLiteral("ma5")).toDouble()));
    QVERIFY(std::isfinite(last.value(QStringLiteral("ma20")).toDouble()));
    QVERIFY(std::isfinite(last.value(QStringLiteral("bollingerUpper")).toDouble()));
    delete node;
}

void CandleSeriesItemTest::calculatesCommonPriceOverlaysWithoutInventingWarmupValues()
{
    QVector<LocalCandleSeriesPoint> points;
    for (int index = 0; index < 5; ++index) {
        LocalCandleSeriesPoint point;
        point.candle.close = double(index + 1);
        points.append(point);
    }

    const QVector<double> sma = LocalCandlePlotData::simpleMovingAverage(points, 3);
    QCOMPARE(sma.size(), 5);
    QVERIFY(std::isnan(sma.at(0)));
    QVERIFY(std::isnan(sma.at(1)));
    QCOMPARE(sma.at(2), 2.0);
    QCOMPARE(sma.at(3), 3.0);
    QCOMPARE(sma.at(4), 4.0);

    const LocalCandleBollingerBands bands =
        LocalCandlePlotData::bollingerBands(points, 3, 2.0);
    QCOMPARE(bands.middle.size(), sma.size());
    QVERIFY(std::isnan(bands.middle.at(0)));
    QCOMPARE(bands.middle.at(2), 2.0);
    QVERIFY(std::isnan(bands.upper.at(1)));
    QVERIFY(qAbs(bands.upper.at(2) - 3.632993161855452) < 1e-12);
    QVERIFY(qAbs(bands.lower.at(2) - 0.367006838144548) < 1e-12);
}

void CandleSeriesItemTest::calculatesRsiAndMacdWithoutInventingWarmupValues()
{
    QVector<LocalCandleSeriesPoint> flat;
    QVector<LocalCandleSeriesPoint> rising;
    QVector<LocalCandleSeriesPoint> falling;
    for (int index = 0; index < 40; ++index) {
        LocalCandleSeriesPoint flatPoint;
        flatPoint.candle.close = 100.0;
        flat.append(flatPoint);
        LocalCandleSeriesPoint risingPoint;
        risingPoint.candle.close = 100.0 + index;
        rising.append(risingPoint);
        LocalCandleSeriesPoint fallingPoint;
        fallingPoint.candle.close = 100.0 - index;
        falling.append(fallingPoint);
    }

    const QVector<double> flatRsi =
        LocalCandlePlotData::relativeStrengthIndex(flat, 14);
    const QVector<double> risingRsi =
        LocalCandlePlotData::relativeStrengthIndex(rising, 14);
    const QVector<double> fallingRsi =
        LocalCandlePlotData::relativeStrengthIndex(falling, 14);
    QVERIFY(std::isnan(flatRsi.at(13)));
    QCOMPARE(flatRsi.at(14), 50.0);
    QCOMPARE(risingRsi.at(14), 100.0);
    QCOMPARE(fallingRsi.at(14), 0.0);

    const LocalCandleMacdValues macd = LocalCandlePlotData::macd(flat, 12, 26, 9);
    QVERIFY(std::isnan(macd.line.at(24)));
    QCOMPARE(macd.line.at(25), 0.0);
    QVERIFY(std::isnan(macd.signal.at(32)));
    QCOMPARE(macd.signal.at(33), 0.0);
    QCOMPARE(macd.histogram.at(33), 0.0);
}

void CandleSeriesItemTest::parameterizesIndicatorsAndRendersIndependentSubplots()
{
    CandleListModel model;
    for (int index = 0; index < 120; ++index)
        model.addPoint(pointAt(index, false));

    TestableCandleSeriesItem item;
    item.setWidth(960);
    item.setHeight(640);
    item.setMovingAverageSlowPeriod(8);
    item.setMovingAverageFastPeriod(3);
    item.setBollingerPeriod(10);
    item.setBollingerStandardDeviations(1.5);
    item.setRsiPeriod(5);
    item.setMacdFastPeriod(3);
    item.setMacdSlowPeriod(6);
    item.setMacdSignalPeriod(2);
    item.setShowVolume(true);
    item.setShowMovingAverages(true);
    item.setShowBollingerBands(true);
    item.setShowRsi(true);
    item.setShowMacd(true);
    item.setModel(&model);

    QCOMPARE(item.movingAverageFastPeriod(), 3);
    QCOMPARE(item.movingAverageSlowPeriod(), 8);
    QCOMPARE(item.bollingerPeriod(), 10);
    QCOMPARE(item.bollingerStandardDeviations(), qreal(1.5));
    QCOMPARE(item.rsiPeriod(), 5);
    QCOMPARE(item.macdFastPeriod(), 3);
    QCOMPARE(item.macdSlowPeriod(), 6);
    QCOMPARE(item.macdSignalPeriod(), 2);
    QCOMPARE(item.pricePlotRatio(), qreal(0.42));
    QCOMPARE(item.volumePlotTop(), qreal(0.46));
    QCOMPARE(item.rsiPlotTop(), qreal(0.62));
    QCOMPARE(item.macdPlotTop(), qreal(0.82));

    const QVariantMap beforeWarmup = item.findNearest(
        LocalCandlePlotData::xFor(4, item.visibleCount(), item.width()));
    QVERIFY(std::isnan(beforeWarmup.value(QStringLiteral("rsi")).toDouble()));
    const QVariantMap afterWarmup = item.findNearest(
        LocalCandlePlotData::xFor(6, item.visibleCount(), item.width()));
    QVERIFY(std::isfinite(afterWarmup.value(QStringLiteral("rsi")).toDouble()));
    QVERIFY(std::isfinite(afterWarmup.value(QStringLiteral("macd")).toDouble()));
    QVERIFY(std::isfinite(afterWarmup.value(QStringLiteral("macdSignal")).toDouble()));
    QCOMPARE(afterWarmup.value(QStringLiteral("movingAverageFastPeriod")).toInt(), 3);
    QCOMPARE(afterWarmup.value(QStringLiteral("movingAverageSlowPeriod")).toInt(), 8);

    QSGNode *node = item.render();
    QVERIFY(node != nullptr);
    QVERIFY(item.oscillatorVertexCount() > 0);
    QVERIFY(item.indicatorVertexCount() > item.oscillatorVertexCount());
    QVERIFY(!item.macdAxisTicks().isEmpty());
    QVERIFY(item.geometryVertexCount() <= item.visibleCount() * 40);
    delete node;
}

void CandleSeriesItemTest::incrementalTailUpdatesMatchFullIndicatorRebuild()
{
    QVector<LocalCandleSeriesPoint> points;
    points.reserve(201);
    for (int index = 0; index < 200; ++index) {
        const CandlePoint source = pointAt(index, false);
        LocalCandleSeriesPoint point;
        point.candle = source.candle;
        point.endTime = source.candle.startTime.addSecs(source.candle.periodSeconds);
        point.sourceCount = 1;
        points.append(point);
    }
    LocalCandleSeriesListModel model;
    model.setPoints(points);
    TestableCandleSeriesItem incremental;
    incremental.setWidth(1000);
    incremental.setHeight(600);
    incremental.setModel(&model);
    const qulonglong initialFullRebuilds = incremental.fullIndicatorRebuildCount();

    points.last().candle.close += 1.25;
    points.last().candle.high = points.last().candle.close + 0.2;
    model.setPoints(points);
    LocalCandleSeriesPoint appended = points.last();
    appended.candle.startTime = appended.endTime;
    appended.endTime = appended.candle.startTime.addSecs(
        appended.candle.periodSeconds);
    appended.candle.open = appended.candle.close;
    appended.candle.high = appended.candle.open + 0.5;
    appended.candle.low = appended.candle.open - 0.4;
    appended.candle.close = appended.candle.open - 0.15;
    points.append(appended);
    model.setPoints(points);

    LocalCandleSeriesListModel freshModel;
    freshModel.setPoints(points);
    TestableCandleSeriesItem fresh;
    fresh.setWidth(1000);
    fresh.setHeight(600);
    fresh.setModel(&freshModel);
    QCOMPARE(incremental.fullIndicatorRebuildCount(), initialFullRebuilds);
    QCOMPARE(incremental.incrementalIndicatorUpdateCount(), qulonglong(2));

    const QStringList keys {
        QStringLiteral("movingAverageFast"),
        QStringLiteral("movingAverageSlow"),
        QStringLiteral("bollingerUpper"),
        QStringLiteral("bollingerMiddle"),
        QStringLiteral("bollingerLower"),
        QStringLiteral("rsi"),
        QStringLiteral("macd"),
        QStringLiteral("macdSignal"),
        QStringLiteral("macdHistogram")
    };
    for (int index : {198, 199, 200}) {
        const qreal x = LocalCandlePlotData::xFor(
            index, points.size(), incremental.width());
        const QVariantMap actual = incremental.findNearest(x);
        const QVariantMap expected = fresh.findNearest(x);
        for (const QString &key : keys) {
            const double actualValue = actual.value(key).toDouble();
            const double expectedValue = expected.value(key).toDouble();
            if (std::isnan(expectedValue)) QVERIFY(std::isnan(actualValue));
            else QVERIFY2(qAbs(actualValue - expectedValue) < 1e-10,
                          qPrintable(QStringLiteral("%1[%2] incremental=%3 full=%4")
                              .arg(key).arg(index).arg(actualValue, 0, 'g', 16)
                              .arg(expectedValue, 0, 'g', 16)));
        }
    }
}

void CandleSeriesItemTest::liveSeriesGrowsViewportUntilUserAdjustsIt()
{
    const auto makePoint = [](int index) {
        const CandlePoint source = pointAt(index, false);
        LocalCandleSeriesPoint point;
        point.candle = source.candle;
        point.candle.periodSeconds = 60;
        point.candle.startTime = QDateTime(
            QDate(2026, 9, 1), QTime(1, 30), Qt::UTC).addSecs(index * 60);
        point.endTime = point.candle.startTime.addSecs(60);
        point.sourceCount = 1;
        return point;
    };

    QVector<LocalCandleSeriesPoint> points {makePoint(0)};
    LocalCandleSeriesListModel model;
    model.setPoints(points);
    TestableCandleSeriesItem item;
    item.setWidth(1000);
    item.setHeight(600);
    item.setModel(&model);
    QCOMPARE(item.pointCount(), 1);
    QCOMPARE(item.visibleCount(), 1);

    for (int index = 1; index < 131; ++index) {
        points.append(makePoint(index));
        model.setPoints(points);
        QCOMPARE(item.pointCount(), index + 1);
        QCOMPARE(item.visibleCount(), index + 1);
        QVERIFY(item.viewportAtRightEdge());
    }

    item.zoomAt(item.width(), 2.0);
    const int userVisibleCount = item.visibleCount();
    QVERIFY(userVisibleCount < item.pointCount());
    points.append(makePoint(131));
    model.setPoints(points);
    QCOMPARE(item.pointCount(), 132);
    QCOMPARE(item.visibleCount(), userVisibleCount);
    QVERIFY(item.viewportAtRightEdge());

    item.panByPixels(item.width() / 2.0);
    QVERIFY(!item.viewportAtRightEdge());
    const int historyStart = item.visibleStart();
    points.append(makePoint(132));
    model.setPoints(points);
    QCOMPARE(item.visibleStart(), historyStart);
    QCOMPARE(item.visibleCount(), userVisibleCount);
    QVERIFY(!item.viewportAtRightEdge());
}

void CandleSeriesItemTest::automaticRangeIncludesVisibleMovingAverageValues()
{
    CandleListModel model;
    for (int index = 0; index < 30; ++index) {
        CandlePoint point = pointAt(index, false);
        const double price = index < 20 ? 200.0 : 100.0;
        point.candle.open = price;
        point.candle.high = price + 0.5;
        point.candle.low = price - 0.5;
        point.candle.close = price;
        model.addPoint(point);
    }

    TestableCandleSeriesItem item;
    item.setWidth(900);
    item.setHeight(480);
    item.setShowMovingAverages(true);
    item.setModel(&model);
    item.zoomAt(item.width(), 3.0);

    QCOMPARE(item.visibleStart(), 20);
    QCOMPARE(item.visibleCount(), 10);
    const QVariantMap firstVisible = item.findNearest(0.0);
    QCOMPARE(firstVisible.value(QStringLiteral("ma20")).toDouble(), 195.0);
    QVERIFY(item.maximumPrice() >= 195.0);
}

void CandleSeriesItemTest::archivedResultRestoresCandleAndSignalRoles()
{
    BacktestResult result;
    const CandlePoint first = pointAt(0, false);
    const CandlePoint second = pointAt(1, true);
    result.candles = {first.candle, second.candle};
    result.chartSignals = {second.signal};

    CandleListModel model;
    model.loadBacktestResult(result);
    QCOMPARE(model.count(), 2);
    QVERIFY(!model.get(0).value(QStringLiteral("hasSignal")).toBool());
    QVERIFY(model.get(1).value(QStringLiteral("hasSignal")).toBool());
    QCOMPARE(model.get(1).value(QStringLiteral("signalSide")).toString(),
             QStringLiteral("卖出"));
    QCOMPARE(model.get(1).value(QStringLiteral("signalReason")).toString(),
             QStringLiteral("测试信号 1"));
}

void CandleSeriesItemTest::genericBacktestRolesAndSignals()
{
    CandleListModel model;
    model.addPoint(pointAt(0, true));
    model.addPoint(pointAt(1, false));

    QCOMPARE(model.roleNames().value(CandleListModel::TimestampRole),
             QByteArray("timestamp"));
    QCOMPARE(model.roleNames().value(CandleListModel::PeriodSecondsRole),
             QByteArray("periodSeconds"));
    QCOMPARE(model.roleNames().value(CandleListModel::SourceRole),
             QByteArray("source"));

    TestableCandleSeriesItem item;
    item.setWidth(800);
    item.setHeight(400);
    item.setModel(&model);
    QCOMPARE(item.pointCount(), 2);
    QCOMPARE(item.signalCount(), 1);

    const QVariantMap first = item.findNearest(1.0);
    QCOMPARE(first.value(QStringLiteral("periodSeconds")).toInt(), 90);
    QCOMPARE(first.value(QStringLiteral("source")).toString(),
             QStringLiteral("backtest_fixture"));
    QVERIFY(first.value(QStringLiteral("hasSignal")).toBool());
    QCOMPARE(first.value(QStringLiteral("signalSide")).toString(),
             QStringLiteral("买入"));
    QCOMPARE(first.value(QStringLiteral("signalReason")).toString(),
             QStringLiteral("测试信号 0"));
}

void CandleSeriesItemTest::rendersFiveThousandBarsWithBoundedGeometry()
{
    CandleListModel model;
    int expectedSignals = 0;
    for (int index = 0; index < 5000; ++index) {
        const bool signal = index % 250 == 0;
        expectedSignals += signal ? 1 : 0;
        model.addPoint(pointAt(index, signal));
    }
    QCOMPARE(model.count(), 5000);

    TestableCandleSeriesItem item;
    item.setWidth(1200);
    item.setHeight(600);
    item.setModel(&model);
    QCOMPARE(item.pointCount(), 5000);
    QCOMPARE(item.signalCount(), expectedSignals);

    QSGNode *node = item.render();
    QVERIFY(node != nullptr);
    QCOMPARE(item.renderBucketCount(), 2400);
    QVERIFY(item.levelOfDetailActive());
    QVERIFY(item.averageBarsPerRenderBucket() > 2.0);
    QCOMPARE(item.geometryVertexCount(),
             LocalCandlePlotData::wickVertexCount(item.renderBucketCount())
                 + LocalCandlePlotData::bodyVertexCount(item.renderBucketCount())
                 + LocalCandlePlotData::signalMarkerVertexCount(expectedSignals));

    item.setShowSignals(false);
    node = item.render(node);
    QCOMPARE(item.geometryVertexCount(),
             LocalCandlePlotData::wickVertexCount(item.renderBucketCount())
                 + LocalCandlePlotData::bodyVertexCount(item.renderBucketCount()));
    delete node;
}

void CandleSeriesItemTest::continuousTradingViewStyleInteractionStaysBounded()
{
    CandleListModel model;
    for (int index = 0; index < 5000; ++index)
        model.addPoint(pointAt(index, index % 400 == 0));

    TestableCandleSeriesItem item;
    item.setWidth(1280);
    item.setHeight(640);
    item.setShowVolume(true);
    item.setShowMovingAverages(true);
    item.setShowBollingerBands(true);
    item.setModel(&model);
    for (int iteration = 0; iteration < 200; ++iteration) {
        item.zoomAt((iteration % 10) / 10.0 * item.width(),
                    iteration % 2 == 0 ? 1.08 : 1.0 / 1.04);
        item.panByPixels(iteration % 3 == 0 ? 24.0 : -13.0);
        item.scalePriceRange(iteration % 2 == 0 ? 1.01 : 1.0 / 1.01);
        QVERIFY(item.visibleCount() >= 10);
        QVERIFY(item.visibleCount() <= item.pointCount());
        QVERIFY(item.visibleStart() >= 0);
        QVERIFY(item.visibleStart() + item.visibleCount() <= item.pointCount());
        QVERIFY(item.maximumPrice() > item.minimumPrice());
    }
    item.resetViewport();
    item.resetPriceRange();
    QCOMPARE(item.visibleStart(), 0);
    QCOMPARE(item.visibleCount(), 5000);
    QVERIFY(item.automaticPriceRange());
    QVERIFY(!item.findNearest(item.width() / 2.0).isEmpty());
    QSGNode *node = item.render();
    QVERIFY(node != nullptr);
    QVERIFY(item.volumeVertexCount() <= item.visibleCount() * 6);
    QVERIFY(item.indicatorVertexCount() <= item.visibleCount() * 10);
    QVERIFY(item.geometryVertexCount() <= item.visibleCount() * 26);
    QVERIFY(item.priceAxisTicks().size() >= 3);
    QVERIFY(item.priceAxisTicks().size() <= 10);
    QCOMPARE(item.timeAxisTicks().size(), 6);
    for (const QVariant &tick : item.priceAxisTicks()) {
        const double position = tick.toMap().value(QStringLiteral("position")).toDouble();
        QVERIFY(position >= 0.0);
        QVERIFY(position <= 1.0);
    }
    for (const QVariant &tick : item.timeAxisTicks()) {
        const QVariantMap map = tick.toMap();
        const int index = map.value(QStringLiteral("index")).toInt();
        const double position = map.value(QStringLiteral("position")).toDouble();
        QVERIFY(index >= item.visibleStart());
        QVERIFY(index < item.visibleStart() + item.visibleCount());
        QVERIFY(position >= 0.0);
        QVERIFY(position <= 1.0);
    }
    delete node;
}

void CandleSeriesItemTest::fiftyThousandBarContinuousPerformanceStaysWithinBudget()
{
    QVector<LocalCandleSeriesPoint> points;
    points.reserve(50000);
    for (int index = 0; index < 50000; ++index) {
        const CandlePoint source = pointAt(index, false);
        LocalCandleSeriesPoint point;
        point.candle = source.candle;
        point.endTime = source.candle.startTime.addSecs(source.candle.periodSeconds);
        point.sourceCount = 1;
        points.append(point);
    }
    LocalCandleSeriesListModel model;
    model.setPoints(points);

    TestableCandleSeriesItem item;
    item.setWidth(1440);
    item.setHeight(720);
    item.setShowVolume(true);
    item.setShowMovingAverages(true);
    item.setShowBollingerBands(true);
    item.setShowRsi(true);
    item.setShowMacd(true);
    QElapsedTimer timer;
    timer.start();
    item.setModel(&model);
    const qint64 preparationMs = timer.elapsed();
    QCOMPARE(item.pointCount(), 50000);
    QVERIFY2(preparationMs < 5000,
             qPrintable(QStringLiteral("50000 根指标准备耗时 %1 ms").arg(preparationMs)));

    item.zoomAt(item.width() * 0.75, 5.0);
    QVector<qint64> interactionMicros;
    QVector<qint64> renderMillis;
    interactionMicros.reserve(300);
    QSGNode *node = nullptr;
    for (int iteration = 0; iteration < 300; ++iteration) {
        timer.restart();
        item.zoomAt((iteration % 20) / 20.0 * item.width(),
                    iteration % 2 == 0 ? 1.015 : 1.0 / 1.01);
        item.panByPixels(iteration % 3 == 0 ? 21.0 : -13.0);
        item.scalePriceRange(iteration % 2 == 0 ? 1.002 : 1.0 / 1.002);
        interactionMicros.append(timer.nsecsElapsed() / 1000);
        if (iteration % 30 == 0) {
            timer.restart();
            node = item.render(node);
            renderMillis.append(timer.elapsed());
        }
    }
    std::sort(interactionMicros.begin(), interactionMicros.end());
    std::sort(renderMillis.begin(), renderMillis.end());
    const auto percentile = [](const QVector<qint64> &values, double ratio) {
        const int index = std::clamp(
            int(std::ceil(double(values.size()) * ratio)) - 1,
            0, int(values.size()) - 1);
        return values.at(index);
    };
    const qint64 interactionP50 = percentile(interactionMicros, 0.50);
    const qint64 interactionP95 = percentile(interactionMicros, 0.95);
    const qint64 renderP50 = percentile(renderMillis, 0.50);
    const qint64 renderP95 = percentile(renderMillis, 0.95);
    qInfo().noquote() << QStringLiteral(
        "50k 图表基线：准备 %1 ms，交互 p50/p95 %2/%3 us，几何重建 p50/p95 %4/%5 ms")
        .arg(preparationMs).arg(interactionP50).arg(interactionP95)
        .arg(renderP50).arg(renderP95);
    QVERIFY(interactionP95 < 100000);
    QVERIFY(renderP95 < 1000);
    QVERIFY(item.visibleCount() >= 10);
    QVERIFY(item.visibleStart() >= 0);
    QVERIFY(item.visibleStart() + item.visibleCount() <= item.pointCount());
    QVERIFY(item.geometryVertexCount() <= item.visibleCount() * 40);
    delete node;
}

void CandleSeriesItemTest::fiftyThousandBarFourHourRefreshStaysResponsive()
{
    QVector<LocalCandleSeriesPoint> points;
    points.reserve(50241);
    for (int index = 0; index < 50000; ++index) {
        const CandlePoint source = pointAt(index, false);
        LocalCandleSeriesPoint point;
        point.candle = source.candle;
        point.endTime = source.candle.startTime.addSecs(source.candle.periodSeconds);
        point.sourceCount = 1;
        points.append(point);
    }

    LocalCandleSeriesListModel model;
    model.setPoints(points);
    TestableCandleSeriesItem item;
    item.setWidth(1440);
    item.setHeight(720);
    item.setShowVolume(true);
    item.setShowMovingAverages(true);
    item.setShowBollingerBands(true);
    item.setShowRsi(true);
    item.setShowMacd(true);
    item.setModel(&model);
    const qulonglong initialFullRebuilds = item.fullIndicatorRebuildCount();
    item.zoomAt(item.width(), 10.0);
    QVERIFY(item.viewportAtRightEdge());

    QVector<qint64> refreshMicros;
    QVector<qint64> renderMillis;
    refreshMicros.reserve(480);
    renderMillis.reserve(8);
    QElapsedTimer timer;
    QSGNode *node = nullptr;
    for (int minute = 0; minute < 240; ++minute) {
        LocalCandleSeriesPoint &last = points.last();
        last.candle.close += minute % 2 == 0 ? 0.01 : -0.005;
        last.candle.high = std::max(last.candle.high, last.candle.close);
        last.candle.low = std::min(last.candle.low, last.candle.close);
        last.candle.volume += 100.0 + minute;
        timer.restart();
        model.setPoints(points);
        refreshMicros.append(timer.nsecsElapsed() / 1000);
        QVERIFY(item.viewportAtRightEdge());

        LocalCandleSeriesPoint next = last;
        next.candle.startTime = last.endTime;
        next.endTime = next.candle.startTime.addSecs(next.candle.periodSeconds);
        next.candle.open = last.candle.close;
        next.candle.high = next.candle.open + 0.3;
        next.candle.low = next.candle.open - 0.2;
        next.candle.close = next.candle.open + (minute % 2 == 0 ? 0.08 : -0.04);
        next.candle.volume = 1000.0 + minute;
        points.append(next);
        timer.restart();
        model.setPoints(points);
        refreshMicros.append(timer.nsecsElapsed() / 1000);
        QVERIFY(item.viewportAtRightEdge());
        QCOMPARE(item.pointCount(), points.size());

        if (minute % 30 == 0) {
            timer.restart();
            node = item.render(node);
            renderMillis.append(timer.elapsed());
            QVERIFY(item.geometryVertexCount() <= item.visibleCount() * 40);
        }
    }

    item.panByPixels(item.width() * 0.4);
    QVERIFY(!item.viewportAtRightEdge());
    const int historicalStart = item.visibleStart();
    LocalCandleSeriesPoint next = points.last();
    next.candle.startTime = next.endTime;
    next.endTime = next.candle.startTime.addSecs(next.candle.periodSeconds);
    points.append(next);
    model.setPoints(points);
    QCOMPARE(item.visibleStart(), historicalStart);
    QCOMPARE(item.fullIndicatorRebuildCount(), initialFullRebuilds);
    QCOMPARE(item.incrementalIndicatorUpdateCount(), qulonglong(481));

    std::sort(refreshMicros.begin(), refreshMicros.end());
    std::sort(renderMillis.begin(), renderMillis.end());
    const auto percentile = [](const QVector<qint64> &values, double ratio) {
        const int index = std::clamp(
            int(std::ceil(double(values.size()) * ratio)) - 1,
            0, int(values.size()) - 1);
        return values.at(index);
    };
    const qint64 refreshP50 = percentile(refreshMicros, 0.50);
    const qint64 refreshP95 = percentile(refreshMicros, 0.95);
    const qint64 renderP50 = percentile(renderMillis, 0.50);
    const qint64 renderP95 = percentile(renderMillis, 0.95);
    qInfo().noquote() << QStringLiteral(
        "50k 四小时刷新基线：480 次更新/追加 p50/p95 %1/%2 us，"
        "8 次几何重建 p50/p95 %3/%4 ms")
        .arg(refreshP50).arg(refreshP95).arg(renderP50).arg(renderP95);
    QVERIFY2(refreshP95 < 250000,
             qPrintable(QStringLiteral("连续刷新 p95 %1 us").arg(refreshP95)));
    QVERIFY(renderP95 < 1000);
    delete node;
}

QTEST_MAIN(CandleSeriesItemTest)
#include "tst_candle_series_item.moc"
