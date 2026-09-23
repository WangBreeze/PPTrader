#include "models/LocalCandleSeriesListModel.h"
#include "models/LocalTickSeriesListModel.h"
#include "domain/LocalHistoryTypes.h"
#include "services/AsyncLocalCandleSeriesQueryService.h"
#include "services/AsyncLocalTickSeriesQueryService.h"
#include "services/LocalCandleSeriesQueryService.h"
#include "services/LocalTickSeriesQueryService.h"
#include "services/SqliteLocalDataStore.h"
#include "services/RawTickCandleAggregator.h"
#include "services/RawTradeIncrementalIngestService.h"
#include "visualization/LocalCandlePlotData.h"
#include "visualization/LocalTickPlotData.h"

#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

using namespace pp;

namespace {

Instrument instrument()
{
    Instrument value;
    value.symbol = QStringLiteral("600519");
    value.name = QStringLiteral("贵州茅台");
    value.market = Market::AShare;
    value.venue = QStringLiteral("SSE");
    return value;
}

Candle candleAt(int index, int periodSeconds = 60)
{
    Candle candle;
    candle.instrument = instrument();
    candle.startTime = QDateTime(QDate(2026, 1, 5), QTime(1, 30), Qt::UTC)
                           .addSecs(qint64(index) * periodSeconds);
    candle.periodSeconds = periodSeconds;
    candle.open = 100.0 + index;
    candle.high = candle.open + 2.0;
    candle.low = candle.open - 1.0;
    candle.close = candle.open + 1.0;
    candle.volume = 100.0 + index;
    candle.buyVolume = 40.0 + index;
    candle.sellVolume = 30.0;
    candle.neutralVolume = 30.0;
    candle.directionalVolumeKnown = true;
    candle.source = QStringLiteral("tickdata_csv_tick");
    return candle;
}

} // namespace

class LocalCandleSeriesTest final : public QObject {
    Q_OBJECT

private slots:
    void latestFirstPagination();
    void aggregatePreservesOhlcv();
    void aggregateDoesNotCrossSessions();
    void rawTickAggregationUsesExtendedAfternoonSession();
    void emptyAndInvalidQueries();
    void requestGateRejectsStaleResponse();
    void modelRolesAndPlotGeometry();
    void materializesArbitraryPeriodAndInvalidatesCache();
    void incrementalAppendIsIdempotentAndRefreshesLocalCache();
    void incrementalAppendRejectsMixedBatchAtomically();
    void tradingViewStyleViewportMath();
    void rawTickSequenceUsesLatestDayAndStableEventOrder();
    void rawTickSequenceSamplingPreservesBoundaries();
    void rawTickModelGateAndPlotContract();
    void dynamicCandleUpdateAvoidsModelReset();
    void dynamicViewportFollowsOnlyAtRightEdge();
    void olderPagePrependsWithoutMovingViewportAnchor();
    void priceAxisScaleAndArbitraryFrequencyContract();
    void stopDiscardsQueuedQueries();
};

void LocalCandleSeriesTest::stopDiscardsQueuedQueries()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("shutdown.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVector<RawTradeTick> ticks;
    for (int i = 0; i < 20000; ++i) {
        RawTradeTick tick;
        tick.instrument = instrument();
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, QTime(1, 30), Qt::UTC).addMSecs(i * 100);
        tick.sourceLine = i + 1;
        tick.price = 100.0;
        tick.volumeShares = 100.0;
        tick.amount = 10000.0;
        ticks.append(tick);
    }
    QVERIFY(store.beginRawTradeTickImport(instrument(), day, QStringLiteral("fixture")));
    QVERIFY(store.appendRawTradeTicks(ticks));
    QVERIFY(store.commitRawTradeTickImport());
    AsyncLocalTickSeriesQueryService tickService;
    AsyncLocalCandleSeriesQueryService candleService;
    QVERIFY(tickService.start(path));
    QVERIFY(candleService.start(path));
    QSignalSpy tickReady(&tickService, &AsyncLocalTickSeriesQueryService::resultReady);
    QSignalSpy candleReady(&candleService, &AsyncLocalCandleSeriesQueryService::resultReady);
    LocalTickSeriesQuery tickQuery;
    tickQuery.instrument = instrument();
    LocalCandleSeriesQuery candleQuery;
    candleQuery.instrument = instrument();
    for (quint64 id = 1; id <= 64; ++id) {
        QVERIFY(tickService.request(id, tickQuery));
        candleQuery.periodSeconds = int(id + 60);
        QVERIFY(candleService.request(id, candleQuery));
    }
    QElapsedTimer timer;
    timer.start();
    tickService.stop();
    candleService.stop();
    const qint64 elapsed = timer.elapsed();
    qInfo() << "查询积压关闭耗时(ms)" << elapsed;
    // Qt 6.11 Debug 在主机负载较高时，正在执行的 2 万 Tick SQLite
    // 查询收到取消信号后偶尔需要约 1.6 秒释放连接。仍要求 2.5 秒内
    // 完成，既覆盖及时取消，又避免把调试运行库抖动误判为功能失败。
    QVERIFY2(elapsed < 2500, qPrintable(QString::number(elapsed)));
    QCoreApplication::processEvents();
    QCOMPARE(tickReady.count(), 0);
    QCOMPARE(candleReady.count(), 0);
    QVERIFY(!tickService.request(65, tickQuery));
    QVERIFY(!candleService.request(65, candleQuery));
    QVERIFY(tickService.start(path));
    QVERIFY(candleService.start(path));
    QVERIFY(tickService.request(66, tickQuery));
    QVERIFY(candleService.request(66, candleQuery));
    QTRY_COMPARE(tickReady.count(), 1);
    QTRY_COMPARE(candleReady.count(), 1);
    QCOMPARE(tickReady.first().first().toULongLong(), quint64(66));
    QCOMPARE(candleReady.first().first().toULongLong(), quint64(66));
}

void LocalCandleSeriesTest::rawTickSequenceUsesLatestDayAndStableEventOrder()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("tick-series.db"))));
    const QDate oldDay(2026, 1, 5);
    const QDate latestDay(2026, 1, 6);
    const auto tickAt = [](const QDate &day, int seconds, qint64 line, double price) {
        RawTradeTick tick;
        tick.instrument = instrument();
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, QTime(9, 30), QTimeZone("Asia/Shanghai"))
                             .addSecs(seconds).toUTC();
        tick.sourceLine = line;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * tick.volumeShares;
        tick.direction = line % 2 == 0 ? RawTradeDirection::Buy : RawTradeDirection::Sell;
        return tick;
    };
    QVERIFY(store.beginRawTradeTickImport(instrument(), oldDay, QStringLiteral("fixture")));
    QVERIFY(store.appendRawTradeTicks({tickAt(oldDay, 0, 1, 99.0)}));
    QVERIFY(store.commitRawTradeTickImport());
    QVERIFY(store.beginRawTradeTickImport(instrument(), latestDay, QStringLiteral("fixture")));
    QVERIFY(store.appendRawTradeTicks({tickAt(latestDay, 1, 3, 103.0),
                                       tickAt(latestDay, 0, 2, 102.0),
                                       tickAt(latestDay, 0, 1, 101.0)}));
    QVERIFY(store.commitRawTradeTickImport());

    LocalTickSeriesQuery query;
    query.instrument = instrument();
    query.maximumPoints = 100;
    const LocalTickSeriesResult result = LocalTickSeriesQueryService(&store).query(query);
    QVERIFY(result.succeeded);
    QCOMPARE(result.resolvedTradingDate, latestDay);
    QCOMPARE(result.rawTickCount, 3);
    QCOMPARE(result.points.size(), 3);
    QCOMPARE(result.points.at(0).tick.sourceLine, 1);
    QCOMPARE(result.points.at(1).tick.sourceLine, 2);
    QCOMPARE(result.points.at(2).tick.sourceLine, 3);
    QCOMPARE(result.points.at(2).eventIndex, qint64(2));
}

void LocalCandleSeriesTest::rawTickSequenceSamplingPreservesBoundaries()
{
    QVector<RawTradeTick> ticks;
    for (int i = 0; i < 11; ++i) {
        RawTradeTick tick;
        tick.price = 100.0 + i;
        tick.sourceLine = i + 1;
        ticks.append(tick);
    }
    const QVector<LocalTickSeriesPoint> sampled =
        LocalTickSeriesQueryService::sampleByEventIndex(ticks, 4);
    QCOMPARE(sampled.size(), 4);
    QCOMPARE(sampled.first().eventIndex, qint64(0));
    QCOMPARE(sampled.last().eventIndex, qint64(10));
    QCOMPARE(sampled.first().tick.price, 100.0);
    QCOMPARE(sampled.last().tick.price, 110.0);
    qint64 represented = 0;
    for (const LocalTickSeriesPoint &point : sampled) represented += point.representedCount;
    QCOMPARE(represented, qint64(11));
}

void LocalCandleSeriesTest::rawTickModelGateAndPlotContract()
{
    LocalTickSeriesRequestGate gate;
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();
    QVERIFY(!gate.finish(first));
    QVERIFY(gate.finish(second));

    LocalTickSeriesPoint firstPoint;
    firstPoint.tick.timestamp = QDateTime(QDate(2026, 1, 6), QTime(1, 30), Qt::UTC);
    firstPoint.tick.price = 100.0;
    firstPoint.tick.volumeShares = 200.0;
    firstPoint.tick.direction = RawTradeDirection::Buy;
    firstPoint.eventIndex = 0;
    LocalTickSeriesPoint secondPoint = firstPoint;
    secondPoint.tick.price = 102.0;
    secondPoint.tick.direction = RawTradeDirection::Sell;
    secondPoint.eventIndex = 1;
    LocalTickSeriesListModel model;
    model.setPoints({firstPoint, secondPoint});
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.roleNames().value(LocalTickSeriesListModel::DirectionTextRole),
             QByteArray("directionText"));
    QCOMPARE(model.get(0).value(QStringLiteral("directionText")).toString(),
             QStringLiteral("买盘"));
    const LocalTickPlotRange range = LocalTickPlotData::priceRange(model.points());
    QVERIFY(range.valid);
    QCOMPARE(range.minimum, 100.0);
    QCOMPARE(range.maximum, 102.0);
    QCOMPARE(LocalTickPlotData::nearestIndex(2, 200.0, 160.0), 1);
}

void LocalCandleSeriesTest::dynamicCandleUpdateAvoidsModelReset()
{
    LocalCandleSeriesListModel model;
    QVector<LocalCandleSeriesPoint> initial =
        LocalCandleSeriesQueryService::aggregate({candleAt(0), candleAt(1)}, 10);
    model.setPoints(initial);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);

    QVector<LocalCandleSeriesPoint> refreshed = initial;
    refreshed.last().candle.close += 0.5;
    refreshed.append(LocalCandleSeriesQueryService::aggregate({candleAt(2)}, 10).first());
    model.setPoints(refreshed);

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.get(1).value(QStringLiteral("close")).toDouble(),
             initial.last().candle.close + 0.5);

    QVector<LocalCandleSeriesPoint> otherInstrument = refreshed;
    for (LocalCandleSeriesPoint &point : otherInstrument)
        point.candle.instrument.symbol = QStringLiteral("000001");
    model.setPoints(otherInstrument);
    QCOMPARE(resetSpy.count(), 1);
}

void LocalCandleSeriesTest::dynamicViewportFollowsOnlyAtRightEdge()
{
    QCOMPARE(LocalCandlePlotData::viewportAfterDataChange(
                 2, 1, {0, 1}, true), LocalCandleViewport({0, 2}));
    QCOMPARE(LocalCandlePlotData::viewportAfterDataChange(
                 131, 130, {0, 130}, true), LocalCandleViewport({0, 131}));
    QCOMPARE(LocalCandlePlotData::viewportAfterDataChange(
                 4, 3, {1, 2}, true), LocalCandleViewport({2, 2}));
    QCOMPARE(LocalCandlePlotData::viewportAfterDataChange(
                 4, 3, {0, 2}, false), LocalCandleViewport({0, 2}));
}

void LocalCandleSeriesTest::olderPagePrependsWithoutMovingViewportAnchor()
{
    LocalCandleSeriesListModel model;
    const QVector<LocalCandleSeriesPoint> current =
        LocalCandleSeriesQueryService::aggregate({candleAt(0), candleAt(1)}, 10);
    const QVector<LocalCandleSeriesPoint> older =
        LocalCandleSeriesQueryService::aggregate({candleAt(-2), candleAt(-1)}, 10);
    model.setPoints(current);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    QVERIFY(model.prependPoints(older));
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.points().first().candle.startTime, candleAt(-2).startTime);
    QCOMPARE(model.points().last().candle.startTime, candleAt(1).startTime);

    QCOMPARE(LocalCandlePlotData::viewportAfterPrepend(
                 102, {0, 40}, 2), LocalCandleViewport({2, 40}));
    QCOMPARE(LocalCandlePlotData::viewportAfterPrepend(
                 102, {60, 40}, 2), LocalCandleViewport({62, 40}));
}

void LocalCandleSeriesTest::priceAxisScaleAndArbitraryFrequencyContract()
{
    const LocalCandlePlotRange base {90.0, 110.0, true};
    const LocalCandlePlotRange zoomedOut =
        LocalCandlePlotData::scaledPriceRange(base, 2.0);
    QVERIFY(zoomedOut.valid);
    QCOMPARE(zoomedOut.minimum, 80.0);
    QCOMPARE(zoomedOut.maximum, 120.0);
    const LocalCandlePlotRange zoomedIn =
        LocalCandlePlotData::scaledPriceRange(base, 0.5);
    QCOMPARE(zoomedIn.minimum, 95.0);
    QCOMPARE(zoomedIn.maximum, 105.0);

    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("90s")), 90);
    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("2m")), 120);
    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("1h")), 3600);
    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("day")), 86400);
    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("86401s")), 0);
    QCOMPARE(localHistoryPeriodSeconds(QStringLiteral("free")), 0);
}

void LocalCandleSeriesTest::latestFirstPagination()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("series.db"))));
    QVector<Candle> candles;
    candles.reserve(1250);
    for (int index = 0; index < 1250; ++index) candles.append(candleAt(index));
    QVERIFY(store.upsertCandles(candles));
    QCOMPARE(store.candleCount(instrument(), 60), 1250);

    LocalCandleSeriesQuery request;
    request.instrument = instrument();
    request.periodSeconds = 60;
    request.pageSize = 1000;
    request.maximumPoints = 1000;
    LocalCandleSeriesQueryService service(&store);
    LocalCandleSeriesResult latest = service.query(request);
    QVERIFY(latest.succeeded);
    QCOMPARE(latest.pageCount, 2);
    QCOMPARE(latest.rawBarCount, 1000);
    QCOMPARE(latest.points.first().candle.startTime, candleAt(250).startTime);
    QCOMPARE(latest.points.last().candle.startTime, candleAt(1249).startTime);

    request.page = 1;
    LocalCandleSeriesResult older = service.query(request);
    QVERIFY(older.succeeded);
    QCOMPARE(older.rawBarCount, 250);
    QCOMPARE(older.points.first().candle.startTime, candleAt(0).startTime);
    QCOMPARE(older.points.last().candle.startTime, candleAt(249).startTime);
}

void LocalCandleSeriesTest::aggregatePreservesOhlcv()
{
    QVector<Candle> candles;
    for (int index = 0; index < 10; ++index) candles.append(candleAt(index));
    int bucketSize = 0;
    const QVector<LocalCandleSeriesPoint> points =
        LocalCandleSeriesQueryService::aggregate(candles, 4, &bucketSize);
    QCOMPARE(bucketSize, 3);
    QCOMPARE(points.size(), 4);
    QCOMPARE(points.first().sourceCount, 3);
    QCOMPARE(points.first().candle.open, candles.at(0).open);
    QCOMPARE(points.first().candle.close, candles.at(2).close);
    QCOMPARE(points.first().candle.high, candles.at(2).high);
    QCOMPARE(points.first().candle.low, candles.at(0).low);
    QCOMPARE(points.first().candle.volume,
             candles.at(0).volume + candles.at(1).volume + candles.at(2).volume);
    QCOMPARE(points.first().endTime, candles.at(2).startTime);
    QVERIFY(points.first().candle.directionalVolumeKnown);
}

void LocalCandleSeriesTest::aggregateDoesNotCrossSessions()
{
    Candle morning = candleAt(0);
    morning.startTime = QDateTime(QDate(2026, 1, 5), QTime(3, 29), Qt::UTC);
    Candle afternoon = candleAt(1);
    afternoon.startTime = QDateTime(QDate(2026, 1, 5), QTime(5, 0), Qt::UTC);
    Candle nextDay = candleAt(2);
    nextDay.startTime = QDateTime(QDate(2026, 1, 6), QTime(1, 30), Qt::UTC);
    int bucketSize = 0;
    const QVector<LocalCandleSeriesPoint> points =
        LocalCandleSeriesQueryService::aggregate(
            {morning, afternoon, nextDay}, 1, &bucketSize);
    QCOMPARE(bucketSize, 3);
    QCOMPARE(points.size(), 3);
    QCOMPARE(points.at(0).sourceCount, 1);
    QCOMPARE(points.at(1).sourceCount, 1);
    QCOMPARE(points.at(2).sourceCount, 1);
}

void LocalCandleSeriesTest::rawTickAggregationUsesExtendedAfternoonSession()
{
    const QDate date(2026, 8, 7);
    const QTimeZone shanghai("Asia/Shanghai");
    const auto at = [&](const QTime &time) {
        return QDateTime(date, time, shanghai).toUTC();
    };

    const auto at1505 = RawTickCandleAggregator::bucketStart(at(QTime(15, 5)), 300);
    QVERIFY(at1505.has_value());
    QCOMPARE(at1505->toTimeZone(shanghai).time(), QTime(15, 5));

    const auto at1530 = RawTickCandleAggregator::bucketStart(at(QTime(15, 30)), 300);
    QVERIFY(at1530.has_value());
    QCOMPARE(at1530->toTimeZone(shanghai).time(), QTime(15, 25));

    QVERIFY(!RawTickCandleAggregator::bucketStart(at(QTime(15, 30, 1)), 300)
                 .has_value());
}

void LocalCandleSeriesTest::emptyAndInvalidQueries()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("empty.db"))));
    LocalCandleSeriesQuery request;
    request.instrument = instrument();
    request.periodSeconds = 60;
    LocalCandleSeriesResult empty = LocalCandleSeriesQueryService(&store).query(request);
    QVERIFY(empty.succeeded);
    QCOMPARE(empty.totalBars, 0);
    QVERIFY(empty.points.isEmpty());

    request.periodSeconds = 86401;
    LocalCandleSeriesResult invalid = LocalCandleSeriesQueryService(&store).query(request);
    QVERIFY(!invalid.succeeded);
    QVERIFY(invalid.error.contains(QStringLiteral("1–86400")));
}

void LocalCandleSeriesTest::requestGateRejectsStaleResponse()
{
    LocalCandleSeriesRequestGate gate;
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();
    QVERIFY(second > first);
    QVERIFY(gate.loading());
    QVERIFY(!gate.finish(first));
    QVERIFY(gate.loading());
    QVERIFY(gate.finish(second));
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), second);
}

void LocalCandleSeriesTest::modelRolesAndPlotGeometry()
{
    QVector<Candle> candles;
    candles << candleAt(0) << candleAt(1);
    const QVector<LocalCandleSeriesPoint> points =
        LocalCandleSeriesQueryService::aggregate(candles, 10);
    LocalCandleSeriesListModel model;
    model.setPoints(points);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.roleNames().value(LocalCandleSeriesListModel::TimestampRole),
             QByteArray("timestamp"));
    QCOMPARE(model.data(model.index(0), LocalCandleSeriesListModel::OpenRole).toDouble(),
             candles.first().open);
    QCOMPARE(model.get(1).value(QStringLiteral("sourceCount")).toInt(), 1);

    const LocalCandlePlotRange range = LocalCandlePlotData::priceRange(points);
    QVERIFY(range.valid);
    QCOMPARE(range.minimum, candles.first().low);
    QCOMPARE(range.maximum, candles.last().high);
    QCOMPARE(LocalCandlePlotData::nearestIndex(2, 200.0, 160.0), 1);
    QCOMPARE(LocalCandlePlotData::wickVertexCount(800), 1600);
    QCOMPARE(LocalCandlePlotData::bodyVertexCount(800), 4800);
    QCOMPARE(LocalCandlePlotData::signalMarkerVertexCount(20), 60);
    QCOMPARE(LocalCandlePlotData::bodyWidth(800, 800.0), 1.0);
}

void LocalCandleSeriesTest::materializesArbitraryPeriodAndInvalidatesCache()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("raw.db"))));
    const QDate day(2026, 1, 5);
    const auto rawTick = [day](int seconds, double price, qint64 line) {
        RawTradeTick tick;
        tick.instrument = instrument();
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, QTime(9, 30), QTimeZone("Asia/Shanghai"))
                             .addSecs(seconds).toUTC();
        tick.sourceLine = line;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * 100.0;
        tick.direction = line % 2 ? RawTradeDirection::Buy : RawTradeDirection::Sell;
        return tick;
    };
    QVERIFY(store.beginRawTradeTickImport(
        instrument(), day, QStringLiteral("tickdata_csv_tick")));
    QVERIFY(store.appendRawTradeTicks(
        {rawTick(0, 10.0, 1), rawTick(40, 11.0, 2), rawTick(100, 9.0, 3)}));
    QVERIFY(store.commitRawTradeTickImport());

    LocalCandleSeriesQuery request;
    request.instrument = instrument();
    request.periodSeconds = 90;
    request.pageSize = 100;
    request.maximumPoints = 100;
    request.latestTradingDayOnly = true;
    LocalCandleSeriesQueryService service(&store);
    LocalCandleSeriesResult first = service.query(request);
    QVERIFY(first.succeeded);
    QVERIFY(first.rawTickBacked);
    QCOMPARE(first.regeneratedCacheDays, 1);
    QCOMPARE(first.points.size(), 2);
    QCOMPARE(first.points.first().candle.open, 10.0);
    QCOMPARE(first.points.first().candle.close, 11.0);
    QCOMPARE(first.points.first().candle.volume, 200.0);

    LocalCandleSeriesResult cached = service.query(request);
    QVERIFY(cached.succeeded);
    QCOMPARE(cached.regeneratedCacheDays, 0);
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 90));

    QVERIFY(store.beginRawTradeTickImport(
        instrument(), day, QStringLiteral("tickdata_csv_tick")));
    QVERIFY(store.appendRawTradeTicks(
        {rawTick(0, 20.0, 1), rawTick(100, 21.0, 2)}));
    QVERIFY(store.commitRawTradeTickImport());
    QVERIFY(!store.rawTickCandleCacheFresh(instrument(), day, 90));
    LocalCandleSeriesResult rebuilt = service.query(request);
    QVERIFY(rebuilt.succeeded);
    QCOMPARE(rebuilt.regeneratedCacheDays, 1);
    QCOMPARE(rebuilt.points.first().candle.open, 20.0);
}

void LocalCandleSeriesTest::incrementalAppendIsIdempotentAndRefreshesLocalCache()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("incremental.db"))));
    const QDate day(2026, 8, 7);
    const auto tickAt = [day](int seconds, double price, qint64 sequence) {
        RawTradeTick tick;
        tick.instrument = instrument();
        tick.tradingDate = day;
        tick.timestamp = QDateTime(day, QTime(9, 30), QTimeZone("Asia/Shanghai"))
                             .addSecs(seconds).toUTC();
        tick.sourceLine = sequence;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * 100.0;
        tick.direction = RawTradeDirection::Buy;
        tick.source = QStringLiteral("fake_realtime_trade");
        return tick;
    };
    QVERIFY(store.beginRawTradeTickImport(
        instrument(), day, QStringLiteral("fake_realtime_trade")));
    QVERIFY(store.appendRawTradeTicks({tickAt(0, 10.0, 1), tickAt(40, 11.0, 2)}));
    QVERIFY(store.commitRawTradeTickImport());

    LocalCandleSeriesQuery query;
    query.instrument = instrument();
    query.periodSeconds = 60;
    query.pageSize = 100;
    query.maximumPoints = 100;
    query.latestTradingDayOnly = true;
    LocalCandleSeriesQueryService queryService(&store);
    QVERIFY(queryService.query(query).succeeded);
    query.periodSeconds = 300;
    QVERIFY(queryService.query(query).succeeded);

    RawTradeIncrementalIngestService ingestService(&store);
    const RawTradeIncrementalAppendResult result = ingestService.ingest(
        {tickAt(40, 11.0, 2), tickAt(70, 12.0, 3)});
    QVERIFY(result.factCommitted);
    QCOMPARE(result.receivedCount, 2);
    QCOMPARE(result.insertedCount, 1);
    QCOMPARE(result.duplicateCount, 1);
    QCOMPARE(result.invalidatedPeriods, QVector<int>({60, 300}));
    QCOMPARE(result.refreshedPeriods, QVector<int>({60, 300}));
    QVERIFY(result.cacheRefreshError.isEmpty());
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 60));
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 300));

    query.periodSeconds = 60;
    const LocalCandleSeriesResult refreshed = queryService.query(query);
    QVERIFY(refreshed.succeeded);
    QCOMPARE(refreshed.regeneratedCacheDays, 0);
    QCOMPARE(refreshed.points.size(), 2);
    QCOMPARE(refreshed.points.last().candle.close, 12.0);

    const RawTradeIncrementalAppendResult replay = ingestService.ingest(
        {tickAt(70, 12.0, 3)});
    QVERIFY(replay.factCommitted);
    QCOMPARE(replay.insertedCount, 0);
    QCOMPARE(replay.duplicateCount, 1);
    QVERIFY(replay.refreshedPeriods.isEmpty());
}

void LocalCandleSeriesTest::incrementalAppendRejectsMixedBatchAtomically()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("reject.db"))));
    RawTradeTick first;
    first.instrument = instrument();
    first.tradingDate = QDate(2026, 8, 7);
    first.timestamp = QDateTime(first.tradingDate, QTime(9, 30),
                                QTimeZone("Asia/Shanghai")).toUTC();
    first.sourceLine = 1;
    first.price = 10.0;
    first.volumeShares = 100.0;
    first.amount = 1000.0;
    first.source = QStringLiteral("fake_realtime_trade");
    RawTradeTick mixed = first;
    mixed.instrument.symbol = QStringLiteral("000001");
    mixed.sourceLine = 2;

    RawTradeIncrementalIngestService service(&store);
    const RawTradeIncrementalAppendResult result = service.ingest({first, mixed});
    QVERIFY(!result.factCommitted);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(store.rawTradeFingerprint(instrument(), first.tradingDate).count, 0);
}

void LocalCandleSeriesTest::tradingViewStyleViewportMath()
{
    LocalCandleViewport viewport = LocalCandlePlotData::normalizeViewport(5000, 0, 5000);
    QCOMPARE(viewport.start, 0);
    QCOMPARE(viewport.count, 5000);

    viewport = LocalCandlePlotData::zoomViewport(5000, viewport, 0.75, 2.0);
    QCOMPARE(viewport.count, 2500);
    QVERIFY(viewport.start > 0);
    const qreal anchorBefore = 0.75 * 4999.0;
    const qreal anchorAfter = viewport.start + 0.75 * (viewport.count - 1);
    QVERIFY(std::abs(anchorAfter - anchorBefore) <= 1.0);

    const LocalCandleViewport panned =
        LocalCandlePlotData::panViewport(5000, viewport, -100000);
    QCOMPARE(panned.start, 0);
    QCOMPARE(panned.count, viewport.count);
    const LocalCandleViewport rightEdge =
        LocalCandlePlotData::panViewport(5000, viewport, 100000);
    QCOMPARE(rightEdge.start, 5000 - viewport.count);

    const LocalCandleViewport minimum =
        LocalCandlePlotData::zoomViewport(5000, rightEdge, 0.5, 100000.0);
    QCOMPARE(minimum.count, 10);
    const LocalCandleViewport reset =
        LocalCandlePlotData::normalizeViewport(5000, 0, 5000);
    QCOMPARE(reset.count, 5000);
}

QTEST_GUILESS_MAIN(LocalCandleSeriesTest)
#include "tst_local_candle_series.moc"
