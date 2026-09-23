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
    const RawTradeFingerprint current = store.rawTradeFingerprint(instrument(), day);
    if (current.count == rawCount && current.maximumId == rawMaximumId) {
        fingerprint.contentSha256 = current.contentSha256;
    }
    return store.replaceRawTickCandleCache(instrument(), day, periodSeconds,
                                           fingerprint, candles);
}

} // namespace

class BacktestDatasetIdentityTest final : public QObject {
    Q_OBJECT

private slots:
    void emptyDatabaseHasNoIdentity();
    void rawImportPrewarmsContentIdentity();
    void rawContentHashQueryUsesOrderedIndex();
    void catalogUsesCleanFingerprintMetadata();
    void catalogTreatsDirtyFingerprintAsStale();
    void assemblesFactsAndCacheState();
    void datasetVersionIsDeterministicAndChangesWithFacts();
    void datasetVersionChangesWhenTickContentChangesInPlace();
    void legacyIdentityJsonRemainsReadable();
    void staleCacheIsReported();
    void calendarCoverageComputesGaps();
    void authoritativeCalendarSnapshotKeepsUnknownExplicit();
    void v3MigrationAddsColumnsAndHistory();
};

void BacktestDatasetIdentityTest::emptyDatabaseHasNoIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("empty.db"))));
    QCOMPARE(store.schemaVersion(), 25);
    QVERIFY(store.loadBacktestDatasetIdentities().isEmpty());
    QVERIFY(!store.loadBacktestDatasetIdentity(instrument(), 60).has_value());
}

void BacktestDatasetIdentityTest::rawImportPrewarmsContentIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("prewarmed-identity.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 128));

    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery query(probe);
        query.prepare(QStringLiteral(
            "select raw_count, raw_max_id, content_sha256, dirty "
            "from raw_tick_content_fingerprints where symbol = ? and trading_date = ?"));
        query.addBindValue(instrument().symbol);
        query.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(query.exec());
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toLongLong(), 128);
        QVERIFY(query.value(1).toLongLong() > 0);
        QVERIFY(query.value(2).toString().startsWith(QStringLiteral("sha256:")));
        QCOMPARE(query.value(3).toInt(), 0);
        probe.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void BacktestDatasetIdentityTest::rawContentHashQueryUsesOrderedIndex()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("content-order-index.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 4));

    const QString connection = QUuid::createUuid().toString();
    bool usesTemporarySort = false;
    bool usesOrderedIndex = false;
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery query(probe);
        query.prepare(QStringLiteral(
            "explain query plan select timestamp_utc, source_line, price, volume_shares, "
            "amount, direction, source from raw_trade_ticks "
            "where symbol = ? and trading_date = ? "
            "order by timestamp_utc asc, source asc, source_line asc, id asc"));
        query.addBindValue(instrument().symbol);
        query.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(query.exec());
        while (query.next()) {
            const QString detail = query.value(3).toString();
            usesTemporarySort = usesTemporarySort
                || detail.contains(QStringLiteral("TEMP B-TREE"), Qt::CaseInsensitive);
            usesOrderedIndex = usesOrderedIndex
                || detail.contains(QStringLiteral("idx_raw_trade_content_order"));
        }
        probe.close();
    }
    QSqlDatabase::removeDatabase(connection);
    QVERIFY(usesOrderedIndex);
    QVERIFY(!usesTemporarySort);
}

void BacktestDatasetIdentityTest::catalogUsesCleanFingerprintMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("catalog-clean-metadata.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 4));
    const RawTradeFingerprint fingerprint = store.rawTradeFingerprint(instrument(), day);
    QVERIFY(seedCache(store, day, 60, 2, fingerprint.count, fingerprint.maximumId));

    // dirty=0 是应用自有数据库中的强身份承诺；目录刷新必须直接复用，
    // 而不是重新扫描 raw_trade_ticks 并用物理行数覆盖它。
    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery query(probe);
        QVERIFY(query.exec(QStringLiteral(
            "update raw_tick_content_fingerprints set raw_count=400, raw_max_id=400 "
            "where symbol='600519' and trading_date='2026-01-05' and dirty=0")));
        QVERIFY(query.exec(QStringLiteral(
            "update raw_tick_candle_cache_state set raw_count=400, raw_max_id=400 "
            "where symbol='600519' and trading_date='2026-01-05' and period_seconds=60")));
        probe.close();
    }
    QSqlDatabase::removeDatabase(connection);

    const QVector<BacktestDatasetIdentity> identities =
        store.loadBacktestDatasetIdentities();
    QCOMPARE(identities.size(), 1);
    QCOMPARE(identities.first().rawTickTotal(), 400);
    QCOMPARE(identities.first().staleDayCount, 0);
    QVERIFY(identities.first().days.first().cacheFresh);
}

void BacktestDatasetIdentityTest::catalogTreatsDirtyFingerprintAsStale()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("catalog-dirty-stale.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 4));
    const RawTradeFingerprint fingerprint = store.rawTradeFingerprint(instrument(), day);
    QVERIFY(seedCache(store, day, 60, 2, fingerprint.count, fingerprint.maximumId));

    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery query(probe);
        QVERIFY(query.exec(QStringLiteral(
            "update raw_trade_ticks set price=price+1 where symbol='600519' "
            "and trading_date='2026-01-05' and source_line=1")));
        probe.close();
    }
    QSqlDatabase::removeDatabase(connection);

    const QVector<BacktestDatasetIdentity> identities =
        store.loadBacktestDatasetIdentities();
    QCOMPARE(identities.size(), 1);
    QCOMPARE(identities.first().rawTickTotal(), 4);
    QCOMPARE(identities.first().staleDayCount, 1);
    QVERIFY(!identities.first().days.first().cacheFresh);
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
    QCOMPARE(identity->fingerprintVersion, 2);
    QCOMPARE(identity->days.size(), 2);
    QCOMPARE(identity->firstTradingDate, firstDay);
    QCOMPARE(identity->lastTradingDate, secondDay);
    QCOMPARE(identity->rawTickTotal(), 5);
    QCOMPARE(identity->derivedBarTotal(), 2);
    QCOMPARE(identity->staleDayCount, 1);
    QCOMPARE(identity->days.at(0).barCount, 2);
    QCOMPARE(identity->days.at(0).cacheVersion, 1);
    QVERIFY(identity->days.at(0).cacheFresh);
    QVERIFY(identity->days.at(0).rawContentSha256.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(identity->days.at(1).barCount, 0);
    QVERIFY(!identity->days.at(1).cacheFresh);
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

    // 相同事实重导只改变 SQLite 物理行 ID，不应改变 v2 内容身份或使缓存过期。
    QVERIFY(insertFacts(store, day, 3));
    const auto reimported = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(reimported.has_value());
    QVERIFY(reimported->days.first().rawMaximumId
            != first->days.first().rawMaximumId);
    QCOMPARE(reimported->days.first().rawContentSha256,
             first->days.first().rawContentSha256);
    QCOMPARE(reimported->datasetVersion, first->datasetVersion);
    QCOMPARE(reimported->staleDayCount, 0);
    QVERIFY(store.rawTickCandleCacheFresh(instrument(), day, 60));

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

void BacktestDatasetIdentityTest::datasetVersionChangesWhenTickContentChangesInPlace()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("content-change.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const QDate day(2026, 1, 5);
    QVERIFY(insertFacts(store, day, 3));
    QVERIFY(seedCache(store, day, 60, 2, 3, 3));
    const auto before = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(before.has_value());
    QCOMPARE(before->staleDayCount, 0);

    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        probe.setDatabaseName(path);
        QVERIFY(probe.open());
        QSqlQuery update(probe);
        update.prepare(QStringLiteral(
            "update raw_trade_ticks set price = price + 0.25, amount = amount + 25 "
            "where symbol = ? and trading_date = ? and source_line = 2"));
        update.addBindValue(instrument().symbol);
        update.addBindValue(day.toString(Qt::ISODate));
        QVERIFY(update.exec());
        QCOMPARE(update.numRowsAffected(), 1);
        probe.close();
    }
    QSqlDatabase::removeDatabase(connection);

    const auto after = store.loadBacktestDatasetIdentity(instrument(), 60);
    QVERIFY(after.has_value());
    QCOMPARE(after->rawTickTotal(), before->rawTickTotal());
    QCOMPARE(after->days.first().rawMaximumId, before->days.first().rawMaximumId);
    QVERIFY(after->days.first().rawContentSha256
            != before->days.first().rawContentSha256);
    QVERIFY(after->datasetVersion != before->datasetVersion);
    QCOMPARE(after->staleDayCount, 1);
    QVERIFY(!store.rawTickCandleCacheFresh(instrument(), day, 60));
}

void BacktestDatasetIdentityTest::legacyIdentityJsonRemainsReadable()
{
    BacktestDatasetIdentity legacy;
    legacy.instrument = instrument();
    legacy.periodSeconds = 60;
    legacy.fingerprintVersion = 1;
    RawTickDayFingerprint day;
    day.tradingDate = QDate(2026, 1, 5);
    day.rawCount = 3;
    day.rawMaximumId = 7;
    legacy.days.append(day);
    legacy.firstTradingDate = day.tradingDate;
    legacy.lastTradingDate = day.tradingDate;
    legacy.datasetVersion = computeBacktestDatasetVersion(legacy);
    QString json = legacy.toJson();
    json.replace(QStringLiteral(",\"fingerprintVersion\":1"), QString());
    json.replace(QStringLiteral(",\"rawContentSha256\":\"\""), QString());

    BacktestDatasetIdentity restored;
    QVERIFY(restored.fromJson(json));
    QCOMPARE(restored.fingerprintVersion, 1);
    QVERIFY(restored.days.first().rawContentSha256.isEmpty());
    QCOMPARE(computeBacktestDatasetVersion(restored), legacy.datasetVersion);
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
    QVERIFY(!identity->days.first().cacheFresh);
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

void BacktestDatasetIdentityTest::authoritativeCalendarSnapshotKeepsUnknownExplicit()
{
    const AShareTradingDayStatus holiday2024 =
        AShareTradingCalendar::status(QDate(2024, 2, 12));
    QVERIFY(holiday2024.holiday);
    QVERIFY(holiday2024.coverageKnown);
    QCOMPARE(holiday2024.source, QStringLiteral("SSE-2023-47"));

    const AShareTradingDayStatus trading2025 =
        AShareTradingCalendar::status(QDate(2025, 1, 2));
    QVERIFY(trading2025.tradingDay);
    QVERIFY(trading2025.coverageKnown);
    QCOMPARE(trading2025.source, QStringLiteral("SSE-2024-38"));

    Instrument suspendedInstrument = instrument();
    suspendedInstrument.symbol = QStringLiteral("600001");
    AShareInstrumentDayRecord suspended;
    suspended.instrument = suspendedInstrument;
    suspended.tradingDate = QDate(2026, 1, 5);
    suspended.state = AShareInstrumentDayState::Suspended;
    suspended.reason = QStringLiteral("公告停牌");
    suspended.source = QStringLiteral("test-fixture");
    suspended.sourceVersion = QStringLiteral("fixture-v1");

    const AShareCalendarSnapshot snapshot = AShareTradingCalendar::snapshot(
        QDate(2026, 1, 5), QDate(2026, 1, 5),
        {QStringLiteral("600001"), QStringLiteral("600002")}, {suspended});
    QVERIFY(snapshot.marketCoverageComplete);
    QVERIFY(!snapshot.instrumentCoverageComplete);
    QCOMPARE(snapshot.tradingDays.size(), 1);
    QCOMPARE(snapshot.suspendedInstrumentDays, 1);
    QCOMPARE(snapshot.unknownInstrumentDays, 1);
    QCOMPARE(snapshot.instrumentState(QStringLiteral("600001"), QDate(2026, 1, 5)),
             AShareInstrumentDayState::Suspended);
    QCOMPARE(snapshot.instrumentState(QStringLiteral("600002"), QDate(2026, 1, 5)),
             AShareInstrumentDayState::Unknown);
    QVERIFY(snapshot.snapshotVersion.startsWith(QStringLiteral("sha256:")));

    const AShareCalendarSnapshot fallback = AShareTradingCalendar::snapshot(
        QDate(2027, 1, 4), QDate(2027, 1, 5),
        {QStringLiteral("600001")});
    QVERIFY(!fallback.marketCoverageComplete);
    QVERIFY(!fallback.instrumentCoverageComplete);
    QVERIFY(fallback.marketSource.contains(QStringLiteral("weekday-fallback")));
}

void BacktestDatasetIdentityTest::v3MigrationAddsColumnsAndHistory()
{
    QTemporaryDir directory;
    const QString databasePath = directory.filePath(QStringLiteral("migration.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QCOMPARE(store.schemaVersion(), 25);
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
    QCOMPARE(store.schemaVersion(), 25);
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
        QSqlQuery history(verify);
        QVERIFY(history.exec(QStringLiteral(
            "select count(*) from schema_migration_history where version = 6 "
            "and migration_name = 'raw_tick_content_identity'")));
        QVERIFY(history.next());
        QCOMPARE(history.value(0).toInt(), 1);
    }
    {
        QSqlQuery history(verify);
        QVERIFY(history.exec(QStringLiteral(
            "select count(*) from schema_migration_history where version = 5 "
            "and migration_name = 'portfolio_backtest_archive'")));
        QVERIFY(history.next());
        QCOMPARE(history.value(0).toInt(), 1);
    }
    {
        QSqlQuery history(verify);
        QVERIFY(history.exec(QStringLiteral(
            "select count(*) from schema_migration_history where version = 18 "
            "and migration_name = 'backtest_candle_snapshot'")));
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
            QVERIFY(names.contains(QStringLiteral("candle_series_version")));
        }
        {
            QSqlQuery table(verify);
            QVERIFY(table.exec(QStringLiteral(
                "select count(*) from sqlite_master where type = 'table' "
                "and name = 'backtest_candles'")));
            QVERIFY(table.next());
            QCOMPARE(table.value(0).toInt(), 1);
        }
        verify.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("identity-migration-verify"));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::BacktestDatasetIdentityTest)

#include "tst_backtest_dataset_identity.moc"
