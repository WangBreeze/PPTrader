#include "services/BacktestDatasetService.h"
#include "services/SqliteLocalDataStore.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace pp {

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

RawTradeTick rawTick(const QDate &day, int seconds, double price, qint64 line)
{
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
    tick.source = QStringLiteral("tickdata_csv_tick");
    return tick;
}

bool insertFacts(SqliteLocalDataStore &store, const QDate &day, int count, int startLine = 1)
{
    QVector<RawTradeTick> ticks;
    ticks.reserve(count);
    for (int index = 0; index < count; ++index) {
        ticks.append(rawTick(day, index * 40, 10.0 + index, startLine + index));
    }
    if (!store.beginRawTradeTickImport(instrument(), day, QStringLiteral("tickdata_csv_tick"))) {
        return false;
    }
    if (!store.appendRawTradeTicks(ticks)) return false;
    return store.commitRawTradeTickImport();
}

} // namespace

class BacktestDatasetServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsEmptyDatabase();
    void preparesDatasetAndRebuildsStaleCache();
    void rejectsInsufficientBars();
};

void BacktestDatasetServiceTest::rejectsEmptyDatabase()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("empty.db"))));

    BacktestDatasetService service;
    QVERIFY(service.start(store.databasePath()));
    QSignalSpy failedSpy(&service, &BacktestDatasetService::failed);
    QVERIFY(service.request(1, instrument().symbol, 60, 10, {instrument()}));
    QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 5000);
    QVERIFY(failedSpy.first().at(1).toString().contains(QStringLiteral("身份不可用")));
    service.stop();
}

void BacktestDatasetServiceTest::preparesDatasetAndRebuildsStaleCache()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("dataset.db"))));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 3));

    BacktestDatasetService service;
    QVERIFY(service.start(store.databasePath()));
    QSignalSpy preparedSpy(&service, &BacktestDatasetService::prepared);
    QVERIFY(service.request(1, instrument().symbol, 60, 2, {instrument()}));
    QTRY_VERIFY_WITH_TIMEOUT(preparedSpy.count() == 1, 5000);
    const BacktestDatasetIdentity identity =
        preparedSpy.first().at(1).value<BacktestDatasetIdentity>();
    QCOMPARE(identity.staleDayCount, 0);
    QCOMPARE(identity.days.size(), 1);
    QVERIFY(identity.datasetVersion.startsWith(QStringLiteral("sha256:")));
    const auto candles = preparedSpy.first().at(2).value<QVector<Candle>>();
    QVERIFY(candles.size() >= 2);
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 60));

    // 事实变更后再次准备：服务必须重建过期缓存并返回最新事实身份。
    QVERIFY(insertFacts(store, day, 5));
    QVERIFY(!store.rawTickCandleCacheFresh(instrument(), day, 60));
    preparedSpy.clear();
    QVERIFY(service.request(2, instrument().symbol, 60, 2, {instrument()}));
    QTRY_VERIFY_WITH_TIMEOUT(preparedSpy.count() == 1, 5000);
    const BacktestDatasetIdentity rebuilt =
        preparedSpy.first().at(1).value<BacktestDatasetIdentity>();
    QCOMPARE(rebuilt.staleDayCount, 0);
    QCOMPARE(rebuilt.rawTickTotal(), 5);
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 60));
    service.stop();
}

void BacktestDatasetServiceTest::rejectsInsufficientBars()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("short.db"))));
    QVERIFY(insertFacts(store, QDate(2026, 1, 5), 3));

    BacktestDatasetService service;
    QVERIFY(service.start(store.databasePath()));
    QSignalSpy failedSpy(&service, &BacktestDatasetService::failed);
    // 3 条事实在 60 秒周期下最多生成 2 根 K 线，请求 3 根必须失败。
    QVERIFY(service.request(1, instrument().symbol, 60, 3, {instrument()}));
    QTRY_VERIFY_WITH_TIMEOUT(failedSpy.count() == 1, 5000);
    QVERIFY(failedSpy.first().at(1).toString().contains(QStringLiteral("需要 3 根")));
    service.stop();
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::BacktestDatasetServiceTest)

#include "tst_backtest_dataset_service.moc"
