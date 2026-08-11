#include "models/MarketSnapshotListModel.h"
#include "services/AShareUniverseLoader.h"
#include "services/AsyncMarketSnapshotQueryService.h"
#include "services/AsyncTickWriter.h"
#include "services/MarketDataProvider.h"
#include "services/MarketSnapshotAssembler.h"
#include "services/MarketSnapshotQueryService.h"
#include "services/SqliteLocalDataStore.h"

#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>

namespace pp {

namespace {

CrossSectionalQuote quote(const QString &symbol, const QString &name,
                          const QString &venue, const QDateTime &timestamp,
                          double price = 100.0, double previousClose = 95.0)
{
    CrossSectionalQuote value;
    value.instrument = {symbol, name, Market::AShare, venue};
    value.timestamp = timestamp;
    value.price = price;
    value.previousClose = previousClose;
    value.volume = 1000.0;
    value.amount = price * value.volume;
    return value;
}

MarketSnapshot snapshot(const QString &batchId, const QDateTime &timestamp,
                        const QDateTime &completedAt, int expectedCount,
                        const QVector<CrossSectionalQuote> &quotes)
{
    MarketSnapshot value;
    value.batchId = batchId;
    value.market = Market::AShare;
    value.timestamp = timestamp;
    value.completedAt = completedAt;
    value.expectedCount = expectedCount;
    value.quotes = quotes;
    return value;
}

} // namespace

class MarketSnapshotContractTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesShanghaiMarketTimes();
    void resolvesMostRecentCompletedSnapshotAndTenSecondBoundary();
    void exposesNumericRolesBoardFallbackAndFiveThousandRows();
    void reportsEmptyAndIncompleteSnapshots();
    void loadsAndValidatesFullAShareUniverse();
    void rejectsStaleSnapshotResponsesByGeneration();
    void reportsStructuredProviderHealth();
    void queriesSnapshotOnIndependentDatabaseThread();
    void sustainsWritesAndQueriesWithoutBlockingOrGenerationRegression();
};

void MarketSnapshotContractTest::parsesShanghaiMarketTimes()
{
    QString error;
    const auto local = MarketSnapshotQueryService::parseMarketTime(
        QStringLiteral("2026-08-03 10:30:05"), &error);
    QVERIFY(local.has_value());
    QVERIFY(error.isEmpty());
    QCOMPARE(local->timeSpec(), Qt::UTC);
    QCOMPARE(local->date(), QDate(2026, 8, 3));
    QCOMPARE(local->time(), QTime(2, 30, 5));

    const auto zoned = MarketSnapshotQueryService::parseMarketTime(
        QStringLiteral("2026-08-03T02:30:05Z"), &error);
    QVERIFY(zoned.has_value());
    QCOMPARE(*zoned, *local);

    QVERIFY(!MarketSnapshotQueryService::parseMarketTime(
        QStringLiteral("2026/08/03 10:30"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void MarketSnapshotContractTest::resolvesMostRecentCompletedSnapshotAndTenSecondBoundary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("snapshots.db"))));

    const QDateTime base(QDate(2026, 8, 3), QTime(2, 30), Qt::UTC);
    const MarketSnapshot first = snapshot(
        QStringLiteral("BATCH-1"), base, base.addSecs(2), 2,
        {quote(QStringLiteral("600519"), QStringLiteral("贵州茅台"),
               QStringLiteral("SSE"), base),
         quote(QStringLiteral("000001"), QStringLiteral("平安银行"),
               QStringLiteral("SZSE"), base)});
    const MarketSnapshot second = snapshot(
        QStringLiteral("BATCH-2"), base.addSecs(8), base.addSecs(12), 1,
        {quote(QStringLiteral("300750"), QStringLiteral("宁德时代"),
               QStringLiteral("SZSE"), base.addSecs(8))});
    QVERIFY(store.appendMarketSnapshot(first));
    QVERIFY(store.appendMarketSnapshot(second));

    MarketSnapshotQueryService service(&store);
    const auto boundary = service.query(base.addSecs(10));
    QVERIFY(boundary.found);
    QCOMPARE(boundary.snapshot.batchId, QStringLiteral("BATCH-1"));
    QCOMPARE(boundary.stalenessMilliseconds, 10000);
    QVERIFY(boundary.health == MarketSnapshotHealth::Healthy);

    const auto stale = service.query(base.addMSecs(10001));
    QVERIFY(stale.found);
    QCOMPARE(stale.snapshot.batchId, QStringLiteral("BATCH-1"));
    QVERIFY(stale.health == MarketSnapshotHealth::Stale);

    const auto resolvedSecond = service.query(base.addSecs(12));
    QVERIFY(resolvedSecond.found);
    QCOMPARE(resolvedSecond.snapshot.batchId, QStringLiteral("BATCH-2"));
    QCOMPARE(resolvedSecond.resolvedTime, base.addSecs(8));
    QCOMPARE(resolvedSecond.snapshot.quotes.size(), 1);
}

void MarketSnapshotContractTest::exposesNumericRolesBoardFallbackAndFiveThousandRows()
{
    const QDateTime timestamp = QDateTime::currentDateTimeUtc();
    MarketSnapshot value;
    value.batchId = QStringLiteral("MODEL-5001");
    value.timestamp = timestamp;
    value.completedAt = timestamp.addMSecs(500);
    value.expectedCount = 5001;
    value.quotes.reserve(5001);
    for (int index = 0; index < 5001; ++index) {
        value.quotes.append(quote(QString::number(600000 + index),
                                  QStringLiteral("测试股票"), QStringLiteral("SSE"),
                                  timestamp, 100.0 + index, 100.0));
    }

    MarketSnapshotListModel model;
    model.loadSnapshot(value, false);
    QCOMPARE(model.count(), 5001);
    QCOMPARE(model.data(model.index(0), MarketSnapshotListModel::DisplayIndexRole).toInt(), 1);
    QCOMPARE(model.data(model.index(0), MarketSnapshotListModel::PriceRole).toDouble(), 100.0);
    QVERIFY(model.data(model.index(0), MarketSnapshotListModel::ChangePercentRole)
                .metaType().id() == QMetaType::Double);
    QCOMPARE(model.data(model.index(0), MarketSnapshotListModel::BoardRole).toString(),
             QStringLiteral("主板"));
    QCOMPARE(model.indexOfSymbol(QStringLiteral("600010")), 10);
    QVERIFY(model.get(-1).isEmpty());

    QCOMPARE(MarketSnapshotListModel::boardForInstrument(
                 {QStringLiteral("000001"), {}, Market::AShare, {}}),
             QStringLiteral("深市主板"));
    QCOMPARE(MarketSnapshotListModel::boardForInstrument(
                 {QStringLiteral("300750"), {}, Market::AShare, {}}),
             QStringLiteral("创业板"));
    QCOMPARE(MarketSnapshotListModel::boardForInstrument(
                 {QStringLiteral("688001"), {}, Market::AShare, {}}),
             QStringLiteral("科创板"));
    QCOMPARE(MarketSnapshotListModel::boardForInstrument(
                 {QStringLiteral("830001"), {}, Market::AShare, {}}),
             QStringLiteral("北交所"));
    QCOMPARE(MarketSnapshotListModel::boardForInstrument(
                 {QStringLiteral("UNKNOWN"), {}, Market::AShare, {}}),
             QStringLiteral("未知"));
}

void MarketSnapshotContractTest::reportsEmptyAndIncompleteSnapshots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("empty.db"))));
    MarketSnapshotQueryService service(&store);
    const QDateTime requested = QDateTime::currentDateTimeUtc();

    const auto missing = service.query(requested);
    QVERIFY(!missing.found);
    QVERIFY(missing.health == MarketSnapshotHealth::Empty);

    const MarketSnapshot partial = snapshot(
        QStringLiteral("PARTIAL"), requested.addSecs(-5), requested.addSecs(-4), 4,
        {quote(QStringLiteral("600001"), QStringLiteral("股票一"), QStringLiteral("SSE"),
               requested.addSecs(-5)),
         quote(QStringLiteral("600002"), QStringLiteral("股票二"), QStringLiteral("SSE"),
               requested.addSecs(-5))});
    QVERIFY(store.appendMarketSnapshot(partial));
    const auto incomplete = service.query(requested);
    QVERIFY(incomplete.found);
    QCOMPARE(incomplete.snapshot.completenessRatio(), 0.5);
    QVERIFY(incomplete.health == MarketSnapshotHealth::Incomplete);

    const MarketSnapshot empty = snapshot(
        QStringLiteral("EMPTY"), requested.addSecs(1), requested.addSecs(2), 4, {});
    QVERIFY(store.appendMarketSnapshot(empty));
    const auto emptyResult = service.query(requested.addSecs(2));
    QVERIFY(emptyResult.found);
    QVERIFY(emptyResult.health == MarketSnapshotHealth::Empty);
    QVERIFY(!emptyResult.error.isEmpty());
}

void MarketSnapshotContractTest::loadsAndValidatesFullAShareUniverse()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString fixturePath = directory.filePath(QStringLiteral("universe.json"));
    QFile fixture(fixturePath);
    QVERIFY(fixture.open(QIODevice::WriteOnly));
    const QJsonArray rows {
        QJsonObject{{QStringLiteral("code"), QStringLiteral("600519")},
                    {QStringLiteral("name"), QStringLiteral("贵州茅台")},
                    {QStringLiteral("market"), QStringLiteral("上海")}},
        QJsonObject{{QStringLiteral("code"), QStringLiteral("000001")},
                    {QStringLiteral("name"), QStringLiteral("平安银行")},
                    {QStringLiteral("market"), QStringLiteral("深圳")}},
        QJsonObject{{QStringLiteral("code"), QStringLiteral("430001")},
                    {QStringLiteral("name"), QStringLiteral("北交样本")},
                    {QStringLiteral("market"), QStringLiteral("北京")}},
        QJsonObject{{QStringLiteral("code"), QStringLiteral("000003")},
                    {QStringLiteral("name"), QStringLiteral("上证指数候选")},
                    {QStringLiteral("market"), QStringLiteral("上海")}},
        QJsonObject{{QStringLiteral("code"), QStringLiteral("600519")},
                    {QStringLiteral("name"), QStringLiteral("重复")},
                    {QStringLiteral("market"), QStringLiteral("上海")}},
        QJsonObject{{QStringLiteral("code"), QStringLiteral("ABC")},
                    {QStringLiteral("name"), QStringLiteral("无效")},
                    {QStringLiteral("market"), QStringLiteral("深圳")}}
    };
    fixture.write(QJsonDocument(rows).toJson(QJsonDocument::Compact));
    fixture.close();

    const AShareUniverseLoadResult fixtureResult = AShareUniverseLoader::load(fixturePath);
    QVERIFY(fixtureResult.error.isEmpty());
    QCOMPARE(fixtureResult.sourceCount, 6);
    QCOMPARE(fixtureResult.instruments.size(), 3);
    QCOMPARE(fixtureResult.rejectedCount, 3);
    QCOMPARE(fixtureResult.instruments.at(0).venue, QStringLiteral("深交所"));
    QCOMPARE(fixtureResult.instruments.at(1).venue, QStringLiteral("北交所"));
    QCOMPARE(fixtureResult.instruments.at(2).venue, QStringLiteral("上交所"));

    const QString repositoryCache = QStringLiteral(PPTRADER_SOURCE_DIR)
        + QStringLiteral("/data/astock_cache.json");
    const AShareUniverseLoadResult repositoryResult =
        AShareUniverseLoader::load(repositoryCache);
    QVERIFY2(repositoryResult.error.isEmpty(), qPrintable(repositoryResult.error));
    QVERIFY(repositoryResult.sourceCount >= 5000);
    QVERIFY(repositoryResult.instruments.size() >= 5000);
    QVERIFY(repositoryResult.rejectedCount > 0);
    QSet<QString> symbols;
    for (const Instrument &instrument : repositoryResult.instruments) {
        QVERIFY(instrument.market == Market::AShare);
        QVERIFY(instrument.venue == QStringLiteral("上交所")
                || instrument.venue == QStringLiteral("深交所")
                || instrument.venue == QStringLiteral("北交所"));
        symbols.insert(instrument.symbol);
    }
    QCOMPARE(symbols.size(), repositoryResult.instruments.size());
}

void MarketSnapshotContractTest::rejectsStaleSnapshotResponsesByGeneration()
{
    SnapshotRequestGate gate;
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();
    QVERIFY(second > first);
    QVERIFY(gate.loading());
    QVERIFY(!gate.finish(first));
    QVERIFY(gate.loading());
    QCOMPARE(gate.resolvedRequestId(), quint64(0));
    QVERIFY(gate.finish(second));
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), second);
    QVERIFY(!gate.finish(first));
    QCOMPARE(gate.resolvedRequestId(), second);
}

void MarketSnapshotContractTest::reportsStructuredProviderHealth()
{
    QVERIFY(providerHealthForBatch(5000, 5000, false) == ProviderHealthState::Healthy);
    QVERIFY(providerHealthForBatch(5000, 4772, false) == ProviderHealthState::Degraded);
    QVERIFY(providerHealthForBatch(5000, 4999, true) == ProviderHealthState::Degraded);
    QVERIFY(providerHealthForBatch(5000, 0, true) == ProviderHealthState::Error);
    QVERIFY(providerHealthForBatch(0, 0, false) == ProviderHealthState::Error);
}

void MarketSnapshotContractTest::queriesSnapshotOnIndependentDatabaseThread()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("async.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    const QDateTime timestamp = QDateTime::currentDateTimeUtc().addSecs(-1);
    QVERIFY(store.appendMarketSnapshot(snapshot(
        QStringLiteral("ASYNC-BATCH"), timestamp, timestamp.addMSecs(250), 2,
        {quote(QStringLiteral("600519"), QStringLiteral("贵州茅台"),
               QStringLiteral("上交所"), timestamp),
         quote(QStringLiteral("000001"), QStringLiteral("平安银行"),
               QStringLiteral("深交所"), timestamp)})));

    AsyncMarketSnapshotQueryService service;
    QVERIFY(service.start(databasePath));
    QSignalSpy resultSpy(&service, &AsyncMarketSnapshotQueryService::resultReady);
    bool eventLoopResponsive = false;
    QTimer::singleShot(0, this, [&eventLoopResponsive]() { eventLoopResponsive = true; });
    QVERIFY(service.request(42, QDateTime::currentDateTimeUtc()));
    QTRY_VERIFY_WITH_TIMEOUT(eventLoopResponsive, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 5000);
    const QList<QVariant> arguments = resultSpy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), quint64(42));
    const MarketSnapshotQueryResult result =
        arguments.at(1).value<MarketSnapshotQueryResult>();
    QVERIFY(result.found);
    QCOMPARE(result.snapshot.batchId, QStringLiteral("ASYNC-BATCH"));
    QCOMPARE(result.snapshot.quotes.size(), 2);
}

void MarketSnapshotContractTest::sustainsWritesAndQueriesWithoutBlockingOrGenerationRegression()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("refresh-stress.db"));
    const QDateTime baseTime = QDateTime::currentDateTimeUtc().addSecs(-10);

    QVector<CrossSectionalQuote> quotes;
    quotes.reserve(5207);
    for (int index = 0; index < 5207; ++index) {
        const QString symbol = index % 2 == 0
            ? QStringLiteral("60%1").arg(index, 4, 10, QLatin1Char('0'))
            : QStringLiteral("00%1").arg(index, 4, 10, QLatin1Char('0'));
        quotes.append(quote(symbol, symbol,
                            index % 2 == 0 ? QStringLiteral("上交所")
                                           : QStringLiteral("深交所"),
                            baseTime, 10.0 + index / 100.0, 10.0));
    }

    SqliteLocalDataStore seedStore;
    QVERIFY(seedStore.initialize(databasePath));
    QVERIFY(seedStore.appendMarketSnapshot(snapshot(
        QStringLiteral("REFRESH-0"), baseTime, baseTime.addMSecs(500),
        quotes.size(), quotes)));

    AsyncTickWriter writer;
    AsyncMarketSnapshotQueryService queryService;
    QVERIFY(writer.start(databasePath));
    QVERIFY(queryService.start(databasePath));
    QSignalSpy writtenSpy(&writer, &AsyncTickWriter::snapshotWritten);
    SnapshotRequestGate gate;
    quint64 acceptedRequestId = 0;
    QString acceptedBatchId;
    int rejectedGenerationCount = 0;
    connect(&queryService, &AsyncMarketSnapshotQueryService::resultReady,
            this, [&](quint64 requestId, const MarketSnapshotQueryResult &result) {
        if (!gate.finish(requestId)) {
            ++rejectedGenerationCount;
            return;
        }
        acceptedRequestId = requestId;
        acceptedBatchId = result.snapshot.batchId;
    });

    int eventLoopHeartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(0);
    connect(&heartbeat, &QTimer::timeout, this,
            [&eventLoopHeartbeats]() { ++eventLoopHeartbeats; });
    heartbeat.start();

    qint64 maximumDispatchMilliseconds = 0;
    for (int batch = 1; batch <= 3; ++batch) {
        MarketSnapshot next = snapshot(
            QStringLiteral("REFRESH-%1").arg(batch), baseTime.addSecs(batch),
            baseTime.addSecs(batch).addMSecs(500), quotes.size(), quotes);
        QElapsedTimer dispatchTimer;
        dispatchTimer.start();
        QVERIFY(writer.enqueue(next));
        const quint64 requestId = gate.begin();
        QVERIFY(queryService.request(requestId, baseTime.addSecs(batch + 1)));
        maximumDispatchMilliseconds = std::max(
            maximumDispatchMilliseconds, dispatchTimer.elapsed());
    }

    QVERIFY2(maximumDispatchMilliseconds < 100,
             "enqueue/request 在 GUI 线程的调用超过 100 ms");
    QTRY_COMPARE_WITH_TIMEOUT(writtenSpy.size(), 3, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(rejectedGenerationCount >= 2, 10000);

    const quint64 finalRequestId = gate.begin();
    QVERIFY(queryService.request(finalRequestId, baseTime.addSecs(20)));
    QTRY_COMPARE_WITH_TIMEOUT(acceptedRequestId, finalRequestId, 10000);
    QCOMPARE(acceptedBatchId, QStringLiteral("REFRESH-3"));
    QVERIFY(eventLoopHeartbeats > 0);
    qInfo("refresh stress: 3 x 5207 writes, max GUI dispatch %lld ms, heartbeats %d",
          static_cast<long long>(maximumDispatchMilliseconds), eventLoopHeartbeats);

    heartbeat.stop();
    queryService.stop();
    writer.stop();
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::MarketSnapshotContractTest)

#include "tst_market_snapshot_contract.moc"
