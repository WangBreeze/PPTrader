#include "domain/BacktestDatasetTypes.h"
#include "services/AShareTradingCalendar.h"
#include "services/BacktestDataVersion.h"
#include "services/SqliteLocalDataStore.h"

#include <QSqlDatabase>
#include <QSqlQuery>
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

bool seedCache(SqliteLocalDataStore &store, const QDate &day, int periodSeconds,
               int barCount, qint64 rawCount, qint64 rawMaximumId)
{
    QVector<Candle> candles;
    candles.reserve(barCount);
    for (int index = 0; index < barCount; ++index) {
        Candle candle;
        candle.instrument = instrument();
        candle.startTime = QDateTime(day, QTime(9, 30), QTimeZone("Asia/Shanghai"))
                               .addSecs(qint64(index) * periodSeconds).toUTC();
        candle.periodSeconds = periodSeconds;
        candle.open = 10.0 + index;
        candle.high = candle.open + 1.0;
        candle.low = candle.open - 1.0;
        candle.close = candle.open + 0.5;
        candle.volume = 100.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    RawTradeFingerprint fingerprint;
    fingerprint.count = rawCount;
    fingerprint.maximumId = rawMaximumId;
    return store.replaceRawTickCandleCache(instrument(), day, periodSeconds,
                                           fingerprint, candles);
}

} // namespace

class BacktestDatasetIdentityTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyDatabaseHasNoIdentity();
    void assemblesFactsAndCacheState();
    void datasetVersionIsDeterministicAndChangesWithFacts();
    void staleCacheIsReported();
    void calendarCoverageComputesGaps();
    void v3MigrationAddsColumnsAndHistory();
};

void BacktestDatasetIdentityTest::emptyDatabaseHasNoIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("empty.db"))));
    QCOMPARE(store.schemaVersion(), 4);
    QVERIFY(store.loadBacktestDatasetIdentities().isEmpty());
    QVERIFY(!store.loadBacktestDatasetIdentity(instrument(), 60).has_value());
}

void BacktestDatasetIdentityTest::assemblesFactsAndCacheState()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("identity.db"))));

    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    QVERIFY(insertFacts(store, firstDay, 3));
    QVERIFY(insertFacts(store, secondDay, 2));
    QVERIFY(seedCache(store, firstDay, 60, 2, 3, 3));

    const auto identity = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(identity.has_value());
    QVERIFY(!identity->isEmpty());
    QCOMPARE(identity->periodSeconds, 60);
    QCOMPARE(identity->source, QStringLiteral("raw_tick_cache_v1"));
    QCOMPARE(identity->aggregatorVersion, 1);
    QCOMPARE(identity->days.size(), 2);
    QCOMPARE(identity->firstTradingDate, firstDay);
    QCOMPARE(identity->lastTradingDate, secondDay);
    QCOMPARE(identity->rawTickTotal(), 5);
    QCOMPARE(identity->derivedBarTotal(), 2);
    QCOMPARE(identity->staleDayCount, 1);
    QCOMPARE(identity->days.at(0).barCount, 2);
    QCOMPARE(identity->days.at(0).cacheVersion, 1);
    QCOMPARE(identity->days.at(1).barCount, 0);
    QVERIFY(identity->datasetVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(identity->toJson().contains(QStringLiteral("2026-01-05")));
    QCOMPARE(identity->toVariantMap().value(QStringLiteral("dayCount")).toInt(), 2);
    QCOMPARE(identity->toVariantMap().value(QStringLiteral("rawTickCount")).toInt(), 5);

    BacktestDatasetIdentity restored;
    QVERIFY(restored.fromJson(identity->toJson()));
    QCOMPARE(restored.datasetVersion, identity->datasetVersion);
    QCOMPARE(restored.days.size(), identity->days.size());
    QCOMPARE(restored.days.first().rawMaximumId, identity->days.first().rawMaximumId);
    QCOMPARE(restored.firstTradingDate, identity->firstTradingDate);
    QVERIFY(!restored.fromJson(QStringLiteral("{invalid")));

    const auto all = store.loadBacktestDatasetIdentities();
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first().datasetVersion, identity->datasetVersion);
}

void BacktestDatasetIdentityTest::datasetVersionIsDeterministicAndChangesWithFacts()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("version.db"))));

    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 3));
    QVERIFY(seedCache(store, day, 60, 2, 3, 3));

    const auto first = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(first.has_value());
    const auto second = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(second.has_value());
    QCOMPARE(second->datasetVersion, first->datasetVersion);
    QCOMPARE(computeBacktestDatasetVersion(*first), first->datasetVersion);

    // 追加事实（同日期替换为 4 条）后身份必须变化，旧身份字符串保持不变。
    QVERIFY(insertFacts(store, day, 4));
    const auto changed = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(changed.has_value());
    QVERIFY(changed->datasetVersion != first->datasetVersion);
    QCOMPARE(changed->rawTickTotal(), 4);
    QCOMPARE(changed->staleDayCount, 1);
    QCOMPARE(computeBacktestDatasetVersion(*first), first->datasetVersion);

    // 不同周期产生不同身份。
    QVERIFY(seedCache(store, day, 300, 1, 4, 4));
    const auto minute = store.loadBacktestDatasetIdentity(instrument(), 60);
    const auto fiveMinute = store.loadBacktestDatasetIdentity(instrument(), 300);
    QVERIFY(minute.has_value());
    QVERIFY(fiveMinute.has_value());
    QVERIFY(minute->datasetVersion != fiveMinute->datasetVersion);
}

void BacktestDatasetIdentityTest::staleCacheIsReported()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("stale.db"))));

    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 3));
    QVERIFY(seedCache(store, day, 60, 2, 3, 3));
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 60));

    auto identity = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(identity.has_value());
    QCOMPARE(identity->staleDayCount, 0);
    QVERIFY(!identity->hasStaleCache());

    QVERIFY(insertFacts(store, day, 5));
    QVERIFY(!store.rawTickCandleCacheFresh(instrument(), day, 60));
    identity = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(identity.has_value());
    QCOMPARE(identity->staleDayCount, 1);
    QVERIFY(identity->hasStaleCache());
    QCOMPARE(identity->days.first().barCount, 2);
    QCOMPARE(identity->days.first().cacheVersion, 1);
}

void BacktestDatasetIdentityTest::calendarCoverageComputesGaps()
{
    QTemporaryDir directory;
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("calendar.db"))));

    // 2026-10-08 与 10-09 是国庆假期后的两个连续交易日。
    const QDate first(2026, 10, 8);
    const QDate second(2026, 10, 9);
    QVERIFY(insertFacts(store, first, 2));
    QVERIFY(insertFacts(store, second, 2));
    QVERIFY(seedCache(store, first, 60, 1, 2, 2));
    QVERIFY(seedCache(store, second, 60, 1, 2, 2));

    auto identity = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(identity.has_value());
    AShareTradingCalendar::applyDatasetCoverage(*identity);
    QCOMPARE(identity->expectedTradingDays, 2);
    QCOMPARE(identity->gapDayCount, 0);
    QVERIFY(identity->calendarCoverageKnown);
    QCOMPARE(identity->calendarSource, QStringLiteral("SSE-2025-45"));
    QVERIFY(identity->calendarNote.isEmpty());

    // 中间缺 10-09：期望 3 个交易日（08、09、12），缺口 1。
    QTemporaryDir gapDirectory;
    SqliteLocalDataStore gapStore;
    QVERIFY(gapStore.initialize(gapDirectory.filePath(QStringLiteral("gap.db"))));
    QVERIFY(insertFacts(gapStore, QDate(2026, 10, 8), 2));
    QVERIFY(insertFacts(gapStore, QDate(2026, 10, 12), 2));
    QVERIFY(seedCache(gapStore, QDate(2026, 10, 8), 60, 1, 2, 2));
    QVERIFY(seedCache(gapStore, QDate(2026, 10, 12), 60, 1, 2, 2));
    auto gapIdentity = gapStore.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(gapIdentity.has_value());
    AShareTradingCalendar::applyDatasetCoverage(*gapIdentity);
    QCOMPARE(gapIdentity->expectedTradingDays, 3);
    QCOMPARE(gapIdentity->gapDayCount, 1);
    QVERIFY(gapIdentity->calendarCoverageKnown);

    // 2027 年无权威公告，按工作日推定并明确标注。
    QTemporaryDir fallbackDirectory;
    SqliteLocalDataStore fallbackStore;
    QVERIFY(fallbackStore.initialize(fallbackDirectory.filePath(QStringLiteral("fallback.db"))));
    QVERIFY(insertFacts(fallbackStore, QDate(2027, 1, 4), 2));
    QVERIFY(insertFacts(fallbackStore, QDate(2027, 1, 5), 2));
    QVERIFY(seedCache(fallbackStore, QDate(2027, 1, 4), 60, 1, 2, 2));
    QVERIFY(seedCache(fallbackStore, QDate(2027, 1, 5), 60, 1, 2, 2));
    auto fallbackIdentity = fallbackStore.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(fallbackIdentity.has_value());
    AShareTradingCalendar::applyDatasetCoverage(*fallbackIdentity);
    QCOMPARE(fallbackIdentity->expectedTradingDays, 2);
    QCOMPARE(fallbackIdentity->gapDayCount, 0);
    QVERIFY(!fallbackIdentity->calendarCoverageKnown);
    QCOMPARE(fallbackIdentity->calendarSource, QStringLiteral("weekday-fallback"));
    QVERIFY(!fallbackIdentity->calendarNote.isEmpty());
}

void BacktestDatasetIdentityTest::v3MigrationAddsColumnsAndHistory()
{
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("migration.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QCOMPARE(store.schemaVersion(), 4);
    }

    // 模拟旧 v2 库：降低 user_version 后重新打开，应自动备份并升级到当前版本。
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("identity-migration-probe"));
        probe.setDatabaseName(databasePath);
        QVERIFY(probe.open());
        QSqlQuery downgrade(probe);
        QVERIFY(downgrade.exec(QStringLiteral("pragma user_version=2")));
        probe.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("identity-migration-probe"));

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
        QCOMPARE(store.schemaVersion(), 4);
    QVERIFY(!store.lastMigrationBackupPath().isEmpty());

    {
        QSqlDatabase verify = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), QStringLiteral("identity-migration-verify"));
        verify.setDatabaseName(databasePath);
        QVERIFY(verify.open());
        {
            QSqlQuery history(verify);
            QVERIFY(history.exec(QStringLiteral(
                "select count(*) from schema_migration_history where version = 3 "
                "and migration_name = 'backtest_dataset_identity'")));
        QVERIFY(history.next());
        QCOMPARE(history.value(0).toInt(), 1);
    }
    {
        QSqlQuery history(verify);
        QVERIFY(history.exec(QStringLiteral(
            "select count(*) from schema_migration_history where version = 4 "
            "and migration_name = 'batch_backtest_recovery'")));
        QVERIFY(history.next());
        QCOMPARE(history.value(0).toInt(), 1);
    }
        {
            QSqlQuery columns(verify);
            QVERIFY(columns.exec(QStringLiteral("pragma table_info(backtest_results)")));
            QSet<QString> names;
            while (columns.next()) names.insert(columns.value(1).toString());
            QVERIFY(names.contains(QStringLiteral("dataset_version")));
            QVERIFY(names.contains(QStringLiteral("dataset_identity_json")));
            QVERIFY(names.contains(QStringLiteral("dataset_raw_tick_count")));
            QVERIFY(names.contains(QStringLiteral("dataset_derived_bar_count")));
            QVERIFY(names.contains(QStringLiteral("dataset_day_count")));
            QVERIFY(names.contains(QStringLiteral("dataset_first_date")));
            QVERIFY(names.contains(QStringLiteral("dataset_last_date")));
            QVERIFY(names.contains(QStringLiteral("dataset_gap_day_count")));
            QVERIFY(names.contains(QStringLiteral("dataset_stale_day_count")));
            QVERIFY(names.contains(QStringLiteral("dataset_calendar_source")));
            QVERIFY(names.contains(QStringLiteral("dataset_calendar_coverage_known")));
        }
        verify.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("identity-migration-verify"));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::BacktestDatasetIdentityTest)

#include "tst_backtest_dataset_identity.moc"
