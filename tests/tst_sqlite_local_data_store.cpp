#include "app/WorkspaceInstrumentSelectionState.h"
#include "models/BacktestArchiveListModel.h"
#include "services/SqliteLocalDataStore.h"
#include "services/BacktestDataVersion.h"
#include "services/StorageProfile.h"
#include "services/HistoricalDataService.h"
#include "services/AsyncTickWriter.h"

#include <QSignalSpy>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <algorithm>

namespace pp {

namespace {

BacktestArchiveRecord archiveFixture(const QString &resultId, const QString &taskId,
                                     const QString &strategyId, int version,
                                     const QDateTime &completedAt)
{
    BacktestArchiveRecord record;
    record.resultId = resultId;
    record.completedAt = completedAt;
    BacktestConfig &config = record.result.config;
    config.taskId = taskId;
    config.strategyId = strategyId;
    config.strategyVersion = version;
    config.strategyParameters = {{QStringLiteral("fastPeriod"), 5},
                                 {QStringLiteral("slowPeriod"), 20}};
    if (strategyId.startsWith(QStringLiteral("custom_"))) {
        config.ruleSnapshot = {{QStringLiteral("entry"), QVariantMap{{QStringLiteral("operator"),
                                                                       QStringLiteral("crosses_above")}}}};
    }
    config.dataVersion = QStringLiteral("sha256:")
        + QString(64, strategyId.startsWith(QStringLiteral("custom_")) ? 'b' : 'a');
    config.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                         Market::AShare, QStringLiteral("上交所")};
    config.frequency = QStringLiteral("day");
    config.periodSeconds = 86400;
    config.requestedBars = 500;
    config.actualBars = 2;
    config.from = QDateTime(QDate(2026, 1, 2), QTime(7, 0), Qt::UTC);
    config.to = config.from.addDays(1);
    config.initialCapital = 1000000.0;
    config.commissionRate = 0.0003;
    config.slippageRate = 0.0002;
    config.lotSize = 100;

    record.result.summary = {1000000.0, 1012500.0, 1.25, 0.45, 100.0,
                             1.1, 35.0, 1, 1, 0};
    record.result.equityCurve = {
        {config.from, 1000000.0, 500000.0, 500000.0, 0.0},
        {config.to, 1012500.0, 1012500.0, 0.0, 0.45}
    };
    BacktestTrade trade;
    trade.instrument = config.instrument;
    trade.entryTime = config.from;
    trade.exitTime = config.to;
    trade.entryPrice = 1500.0;
    trade.exitPrice = 1520.0;
    trade.quantity = 600.0;
    trade.grossPnl = 12000.0;
    trade.netPnl = 11450.0;
    trade.commission = 550.0;
    trade.returnPercent = 1.333333;
    trade.entryReason = QStringLiteral("金叉");
    trade.exitReason = QStringLiteral("回测结束平仓");
    record.result.trades = {trade};
    return record;
}

} // namespace

class FakeHistoricalDataProvider final : public HistoricalDataProvider {
public:
    void fetch(quint64 requestId, const Instrument &instrument,
               const QString &frequency, int count) override
    {
        requestIds.append(requestId);
        instruments.append(instrument);
        frequencies.append(frequency);
        counts.append(count);
        ++fetchCount;
    }

    void fetchAdjusted(quint64 requestId, const Instrument &instrument,
                       const QString &frequency, int count,
                       const QString &adjustment) override
    {
        adjustments.append(adjustment);
        fetch(requestId, instrument, frequency, count);
    }

    int fetchCount = 0;
    QVector<quint64> requestIds;
    QVector<Instrument> instruments;
    QVector<QString> frequencies;
    QVector<int> counts;
    QVector<QString> adjustments;
};

class SqliteLocalDataStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsRecordsAndRestoresCounts();
    void reportsActualLocalHistoryCatalog();
    void separatesCatalogAndReadsByDataSource();
    void servesCompleteCacheWithoutCallingProvider();
    void explicitProviderRequestPersistsValidCandlesAndRefreshesCatalog();
    void adjustmentMismatchDoesNotReuseCache();
    void providerFailurePreservesExistingCatalog();
    void ignoresSupersededHistoricalResponse();
    void persistsAndLoadsTradingState();
    void migratesLegacyAccountSnapshotSchema();
    void writesMarketSnapshotsAsynchronouslyAndIdempotently();
    void archivesBuiltInAndCustomBacktestsRoundTrip();
    void archivePreservesDatasetIdentity();
    void archiveTransactionRollsBackOnChildFailure();
    void archiveRestoreIsStableAndDoesNotRequestNetwork();
    void migratesLegacyDatabaseWithBackupAndStableIdentity();
    void migrationFailureRollsBackWithoutHalfSchema();
    void relocatesDatabaseByVerifiedCopyAndRejectsNetworkPath();
};

void SqliteLocalDataStoreTest::migratesLegacyDatabaseWithBackupAndStableIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("legacy-global.db"));
    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "create table legacy_marker(id integer primary key, value text not null)")));
        QVERIFY(query.exec(QStringLiteral(
            "insert into legacy_marker(id, value) values(1, 'preserved')")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(connection);

    QString uuid;
    QString backupPath;
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QCOMPARE(store.schemaVersion(), 6);
        QVERIFY(!store.databaseUuid().isEmpty());
        uuid = store.databaseUuid();
        backupPath = store.lastMigrationBackupPath();
        QVERIFY(QFileInfo::exists(backupPath));
        QString integrityError;
        QVERIFY2(store.verifyIntegrity(&integrityError), qPrintable(integrityError));

        const QString verifyConnection = QUuid::createUuid().toString();
        {
            QSqlDatabase verify = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), verifyConnection);
            verify.setDatabaseName(path);
            QVERIFY(verify.open());
            QSqlQuery query(verify);
            QVERIFY(query.exec(QStringLiteral("pragma user_version")) && query.next());
            QCOMPARE(query.value(0).toInt(), 6);
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 1"))
                && query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("global_schema_baseline"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 2"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("historical_dataset_identity"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 3"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("backtest_dataset_identity"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 4"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("batch_backtest_recovery"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 5"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("portfolio_backtest_archive"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 6"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("raw_tick_content_identity"));
            QVERIFY(query.exec(QStringLiteral("select value from legacy_marker where id = 1"))
                && query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("preserved"));
            verify.close();
        }
        QSqlDatabase::removeDatabase(verifyConnection);
    }

    const QString backupConnection = QUuid::createUuid().toString();
    {
        QSqlDatabase backup = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), backupConnection);
        backup.setDatabaseName(backupPath);
        QVERIFY(backup.open());
        QSqlQuery query(backup);
        QVERIFY(query.exec(QStringLiteral("pragma integrity_check")) && query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("ok"));
        QVERIFY(query.exec(QStringLiteral("pragma user_version")) && query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral("select value from legacy_marker where id = 1"))
            && query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("preserved"));
        backup.close();
    }
    QSqlDatabase::removeDatabase(backupConnection);

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.databaseUuid(), uuid);
    QCOMPARE(reopened.schemaVersion(), 6);
    QVERIFY(reopened.lastMigrationBackupPath().isEmpty());
}

void SqliteLocalDataStoreTest::migrationFailureRollsBackWithoutHalfSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken-legacy.db"));
    const QString connection = QUuid::createUuid().toString();
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "create table app_database_metadata(singleton_id integer primary key)")));
        QVERIFY(query.exec(QStringLiteral("create table legacy_marker(value text)")));
        QVERIFY(query.exec(QStringLiteral("insert into legacy_marker values('preserved')")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(connection);

    QString backupPath;
    {
        SqliteLocalDataStore store;
        QVERIFY(!store.initialize(path));
        backupPath = store.lastMigrationBackupPath();
        QVERIFY(QFileInfo::exists(backupPath));
    }

    const QString verifyConnection = QUuid::createUuid().toString();
    {
        QSqlDatabase verify = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), verifyConnection);
        verify.setDatabaseName(path);
        QVERIFY(verify.open());
        QSqlQuery query(verify);
        QVERIFY(query.exec(QStringLiteral("pragma user_version")) && query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral(
            "select count(*) from sqlite_master where type='table' "
            "and name='schema_migration_history'")) && query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        QVERIFY(query.exec(QStringLiteral("select value from legacy_marker")) && query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("preserved"));
        verify.close();
    }
    QSqlDatabase::removeDatabase(verifyConnection);
}

void SqliteLocalDataStoreTest::relocatesDatabaseByVerifiedCopyAndRejectsNetworkPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source/pptrader.db"));
    QString uuid;
    {
        SqliteLocalDataStore source;
        QVERIFY(source.initialize(sourcePath));
        Tick tick;
        tick.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                           Market::AShare, QStringLiteral("SSE")};
        tick.timestamp = QDateTime::currentDateTimeUtc();
        tick.price = 1400.0;
        tick.volume = 100.0;
        tick.bid = 1399.0;
        tick.ask = 1401.0;
        QVERIFY(source.appendTick(tick));
        uuid = source.databaseUuid();
        source.close();
    }

    const QString targetPath = directory.filePath(QStringLiteral("relocated/data.db"));
    const StorageRelocationResult result = DatabaseRelocationService::copyAndVerify(
        sourcePath, targetPath, uuid);
    QVERIFY2(result.succeeded, qPrintable(result.error));
    QCOMPARE(result.databaseUuid, uuid);
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(targetPath));

    SqliteLocalDataStore relocated;
    QVERIFY(relocated.initialize(targetPath));
    QCOMPARE(relocated.databaseUuid(), uuid);
    QCOMPARE(relocated.tickCount(), 1);
    QVERIFY(relocated.lastMigrationBackupPath().isEmpty());
    relocated.close();

    AsyncTickWriter restartableWriter;
    QVERIFY(restartableWriter.start(sourcePath));
    restartableWriter.stop();
    QVERIFY(!restartableWriter.isRunning());
    QVERIFY(restartableWriter.start(targetPath));
    restartableWriter.stop();

    const StorageRelocationResult overwrite = DatabaseRelocationService::copyAndVerify(
        sourcePath, targetPath, uuid);
    QVERIFY(!overwrite.succeeded);
    QVERIFY(overwrite.error.contains(QStringLiteral("已存在")));
    QString networkError;
    QVERIFY(!StorageProfile::validateLocalDatabasePath(
        QStringLiteral("//nas/market/pptrader.db"), &networkError));
    QVERIFY(networkError.contains(QStringLiteral("网络共享")));
}

void SqliteLocalDataStoreTest::archivesBuiltInAndCustomBacktestsRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-roundtrip.db"));
    const QDateTime older = QDateTime(QDate(2026, 7, 1), QTime(2, 0), Qt::UTC);
    const QDateTime newer = older.addSecs(30);
    const BacktestArchiveRecord builtIn = archiveFixture(
        QStringLiteral("result-built-in"), QStringLiteral("41"),
        QStringLiteral("sma_crossover"), 0, older);
    const BacktestArchiveRecord custom = archiveFixture(
        QStringLiteral("result-custom"), QStringLiteral("42"),
        QStringLiteral("custom_momentum"), 3, newer);
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(builtIn));
        QVERIFY(store.saveBacktestArchive(custom));
        QVERIFY(!store.saveBacktestArchive(custom));
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const QVector<BacktestArchiveSummary> summaries = reopened.loadBacktestArchiveSummaries();
    QCOMPARE(summaries.size(), 2);
    QCOMPARE(summaries.at(0).resultId, QStringLiteral("result-custom"));
    QCOMPARE(summaries.at(1).resultId, QStringLiteral("result-built-in"));
    QCOMPARE(summaries.at(0).actualBars, 2);

    const auto restoredBuiltIn = reopened.loadBacktestArchive(QStringLiteral("result-built-in"));
    QVERIFY(restoredBuiltIn.has_value());
    QCOMPARE(restoredBuiltIn->result.config.strategyParameters,
             builtIn.result.config.strategyParameters);
    QCOMPARE(restoredBuiltIn->result.config.ruleSnapshot,
             builtIn.result.config.ruleSnapshot);
    QCOMPARE(restoredBuiltIn->result.config.dataVersion,
             builtIn.result.config.dataVersion);
    QCOMPARE(restoredBuiltIn->result.equityCurve.size(), 2);
    QCOMPARE(restoredBuiltIn->result.trades.size(), 1);
    QCOMPARE(restoredBuiltIn->result.trades.first().entryReason, QStringLiteral("金叉"));
    QCOMPARE(restoredBuiltIn->result.summary.totalReturnPercent, 1.25);

    const auto restoredCustom = reopened.loadBacktestArchive(QStringLiteral("result-custom"));
    QVERIFY(restoredCustom.has_value());
    QCOMPARE(restoredCustom->result.config.strategyVersion, 3);
    QVERIFY(!restoredCustom->result.config.ruleSnapshot.isEmpty());

    BacktestArchiveListModel model;
    model.loadSummaries(summaries);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.indexOfResultId(QStringLiteral("result-built-in")), 1);
    QCOMPARE(model.get(0).value(QStringLiteral("customStrategy")).toBool(), true);
    QVERIFY(model.get(-1).isEmpty());
}

void SqliteLocalDataStoreTest::archivePreservesDatasetIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-dataset.db"));
    BacktestArchiveRecord record = archiveFixture(
        QStringLiteral("result-dataset"), QStringLiteral("43"),
        QStringLiteral("sma_crossover"), 0,
        QDateTime(QDate(2026, 7, 1), QTime(2, 0), Qt::UTC));

    BacktestDatasetIdentity identity;
    identity.instrument = record.result.config.instrument;
    identity.periodSeconds = 60;
    identity.aggregatorVersion = 1;
    RawTickDayFingerprint firstDay;
    firstDay.tradingDate = QDate(2026, 1, 5);
    firstDay.rawCount = 3;
    firstDay.rawMaximumId = 3;
    firstDay.barCount = 2;
    firstDay.cacheVersion = 1;
    RawTickDayFingerprint secondDay;
    secondDay.tradingDate = QDate(2026, 1, 6);
    secondDay.rawCount = 2;
    secondDay.rawMaximumId = 2;
    secondDay.barCount = 1;
    secondDay.cacheVersion = 1;
    identity.days = {firstDay, secondDay};
    identity.firstTradingDate = QDate(2026, 1, 5);
    identity.lastTradingDate = QDate(2026, 1, 6);
    identity.datasetVersion = computeBacktestDatasetVersion(identity);
    identity.expectedTradingDays = 2;
    identity.calendarCoverageKnown = true;
    identity.calendarSource = QStringLiteral("SSE-2025-45");
    record.result.config.datasetIdentity = identity;

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(record));
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const QVector<BacktestArchiveSummary> summaries = reopened.loadBacktestArchiveSummaries();
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(summaries.first().datasetVersion, identity.datasetVersion);
    QCOMPARE(summaries.first().datasetDayCount, 2);
    QCOMPARE(summaries.first().datasetRawTickCount, 5);
    QCOMPARE(summaries.first().datasetFirstDate, QDate(2026, 1, 5));
    QVERIFY(summaries.first().datasetCalendarCoverageKnown);

    BacktestArchiveListModel model;
    model.loadSummaries(summaries);
    const QVariantMap modelRow = model.get(0);
    QCOMPARE(modelRow.value(QStringLiteral("datasetVersion")).toString(),
             identity.datasetVersion);
    QCOMPARE(modelRow.value(QStringLiteral("datasetDayCount")).toInt(), 2);
    QCOMPARE(modelRow.value(QStringLiteral("datasetRawTickCount")).toInt(), 5);

    const auto restored = reopened.loadBacktestArchive(QStringLiteral("result-dataset"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.config.datasetIdentity.datasetVersion,
             identity.datasetVersion);
    QCOMPARE(restored->result.config.datasetIdentity.days.size(), 2);
    QCOMPARE(restored->result.config.datasetIdentity.days.first().rawCount, 3);
    QCOMPARE(restored->result.config.datasetIdentity.firstTradingDate,
             QDate(2026, 1, 5));
    QCOMPARE(restored->result.config.datasetIdentity.calendarSource,
             QStringLiteral("SSE-2025-45"));
}

void SqliteLocalDataStoreTest::archiveTransactionRollsBackOnChildFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-rollback.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));

    const QString connectionName = QStringLiteral("archive_sabotage_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "create trigger reject_archive_equity before insert on backtest_equity_points "
            "begin select raise(abort, 'forced child failure'); end")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);
    QVERIFY(!store.saveBacktestArchive(archiveFixture(
        QStringLiteral("rolled-back"), QStringLiteral("51"),
        QStringLiteral("sma_crossover"), 0, QDateTime::currentDateTimeUtc())));
    QVERIFY(!failureSpy.isEmpty());
    QVERIFY(store.loadBacktestArchiveSummaries().isEmpty());
    QVERIFY(!store.loadBacktestArchive(QStringLiteral("rolled-back")).has_value());
}

void SqliteLocalDataStoreTest::archiveRestoreIsStableAndDoesNotRequestNetwork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("archive-offline.db"))));
    BacktestArchiveRecord original = archiveFixture(
        QStringLiteral("immutable"), QStringLiteral("61"),
        QStringLiteral("custom_momentum"), 2, QDateTime::currentDateTimeUtc());
    const QVariantMap persistedParameters = original.result.config.strategyParameters;
    QVERIFY(store.saveBacktestArchive(original));

    FakeHistoricalDataProvider provider;
    HistoricalDataService history(&store, &provider);
    WorkspaceInstrumentSelectionState selection;
    selection.setLocalHistoryCatalog(store.localHistoryCatalog());
    QVERIFY(!selection.selectBacktestInstrument(QStringLiteral("600519"), 86400, 2));
    QCOMPARE(provider.fetchCount, 0);

    original.result.config.strategyParameters.insert(QStringLiteral("fastPeriod"), 99);
    original.result.config.dataVersion = QStringLiteral("sha256:") + QString(64, 'f');
    const auto restored = store.loadBacktestArchive(QStringLiteral("immutable"));
    QVERIFY(restored.has_value());
    QCOMPARE(provider.fetchCount, 0);
    QCOMPARE(restored->result.config.strategyParameters, persistedParameters);
    QCOMPARE(restored->result.config.dataVersion,
             QStringLiteral("sha256:") + QString(64, 'b'));
}

void SqliteLocalDataStoreTest::reportsActualLocalHistoryCatalog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("history-catalog.db"))));

    const Instrument moutai {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                             Market::AShare, QStringLiteral("上交所")};
    const Instrument pingan {QStringLiteral("000001"), QStringLiteral("平安银行"),
                             Market::AShare, QStringLiteral("深交所")};
    const Instrument excluded {QStringLiteral("300750"), QStringLiteral("宁德时代"),
                               Market::AShare, QStringLiteral("深交所")};
    QVector<Candle> rows;
    auto appendRows = [&](const Instrument &instrument, int periodSeconds, int count,
                          const QString &source) {
        for (int index = 0; index < count; ++index) {
            Candle candle;
            candle.instrument = instrument;
            candle.startTime = QDateTime(QDate(2026, 1, 2), QTime(7, 0), Qt::UTC)
                                   .addSecs(static_cast<qint64>(periodSeconds) * index);
            candle.periodSeconds = periodSeconds;
            candle.open = candle.high = candle.low = candle.close = 100.0 + index;
            candle.volume = 1000.0;
            candle.source = source;
            rows.append(candle);
        }
    };
    appendRows(pingan, 86400, 1, QStringLiteral("fixture"));
    appendRows(moutai, 300, 2, QStringLiteral("fixture"));
    appendRows(moutai, 86400, 3, QStringLiteral("fixture"));
    appendRows(excluded, 86400, 4, QStringLiteral("unknown"));
    QVERIFY(store.upsertCandles(rows));

    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 3);
    QCOMPARE(catalog.at(0).instrument.symbol, QStringLiteral("000001"));
    QCOMPARE(catalog.at(0).frequency(), QStringLiteral("day"));
    QCOMPARE(catalog.at(0).candleCount, 1);
    QCOMPARE(catalog.at(1).instrument.symbol, QStringLiteral("600519"));
    QCOMPARE(catalog.at(1).frequency(), QStringLiteral("5m"));
    QCOMPARE(catalog.at(1).candleCount, 2);
    QCOMPARE(catalog.at(2).instrument.symbol, QStringLiteral("600519"));
    QCOMPARE(catalog.at(2).periodSeconds, 86400);
    QCOMPARE(catalog.at(2).candleCount, 3);
    QCOMPARE(catalog.at(2).firstTime,
             QDateTime(QDate(2026, 1, 2), QTime(7, 0), Qt::UTC));
    QCOMPARE(catalog.at(2).lastTime,
             QDateTime(QDate(2026, 1, 4), QTime(7, 0), Qt::UTC));
}

void SqliteLocalDataStoreTest::separatesCatalogAndReadsByDataSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("source-separated.db"))));

    const Instrument instrument {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                                 Market::AShare, QStringLiteral("SSE")};
    QVector<Candle> candles;
    const QDateTime start(QDate(2026, 8, 6), QTime(1, 30), Qt::UTC);
    for (int index = 0; index < 4; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = start.addSecs(index * 60);
        candle.periodSeconds = 60;
        candle.open = candle.high = candle.low = candle.close = 100.0 + index;
        candle.volume = 1000.0;
        candle.source = index < 2
            ? QStringLiteral("mootdx_minute_fixture")
            : QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    QVERIFY(store.upsertCandles(candles));

    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 2);
    const auto rawDataset = std::find_if(
        catalog.cbegin(), catalog.cend(), [](const LocalHistoryDataset &item) {
            return item.source == QStringLiteral("raw_tick_cache_v1");
        });
    QVERIFY(rawDataset != catalog.cend());
    QCOMPARE(rawDataset->candleCount, 2);
    QVERIFY(rawDataset->isBacktestEligible());

    const QVector<Candle> rawCandles = store.loadRecentCandlesFromSource(
        instrument, 60, 10, QStringLiteral("raw_tick_cache_v1"));
    QCOMPARE(rawCandles.size(), 2);
    for (const Candle &candle : rawCandles) {
        QCOMPARE(candle.source, QStringLiteral("raw_tick_cache_v1"));
    }
}

void SqliteLocalDataStoreTest::persistsRecordsAndRestoresCounts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("pptrader-test.db"));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    const QDateTime timestamp(QDate(2026, 8, 3), QTime(1, 30), Qt::UTC);

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QCOMPARE(store.tickCount(), 0);
        QCOMPARE(store.signalCount(), 0);

        Tick tick;
        tick.instrument = instrument;
        tick.timestamp = timestamp;
        tick.price = 1500.5;
        tick.volume = 100.0;
        tick.bid = 1500.0;
        tick.ask = 1501.0;
        QVERIFY(store.appendTick(tick));

        Candle candle;
        candle.instrument = instrument;
        candle.startTime = timestamp;
        candle.periodSeconds = 60;
        candle.open = 1500.0;
        candle.high = 1502.0;
        candle.low = 1499.0;
        candle.close = 1501.0;
        candle.volume = 500.0;
        candle.source = QStringLiteral("mootdx");
        candle.adjustment = QStringLiteral("none");
        candle.downloadedAt = timestamp.addSecs(30);
        QVERIFY(store.appendCandle(candle));

        Candle correctedCandle = candle;
        correctedCandle.close = 1501.5;
        correctedCandle.volume = 550.0;
        correctedCandle.downloadedAt = timestamp.addSecs(60);
        QVERIFY(store.upsertCandles({correctedCandle}));

        TradeSignal signal;
        signal.instrument = instrument;
        signal.timestamp = timestamp;
        signal.side = SignalSide::Buy;
        signal.price = 1501.0;
        signal.volume = 100.0;
        signal.reason = QStringLiteral("test signal");
        QVERIFY(store.appendSignal(signal));

        QCOMPARE(store.tickCount(), 1);
        QCOMPARE(store.signalCount(), 1);

        const QVector<Candle> candles = store.loadCandles(
            instrument, 60, timestamp.addSecs(-1), timestamp.addSecs(1));
        QCOMPARE(candles.size(), 1);
        QCOMPARE(candles.first().close, correctedCandle.close);
        QCOMPARE(candles.first().volume, correctedCandle.volume);
        QCOMPARE(candles.first().source, QStringLiteral("mootdx"));
        QCOMPARE(candles.first().adjustment, QStringLiteral("none"));

        const QVector<Candle> recentCandles = store.loadRecentCandles(instrument, 60, 10);
        QCOMPARE(recentCandles.size(), 1);
        QCOMPARE(recentCandles.first().downloadedAt, correctedCandle.downloadedAt);
    }

    SqliteLocalDataStore reopenedStore;
    QVERIFY(reopenedStore.initialize(databasePath));
    QCOMPARE(reopenedStore.tickCount(), 1);
    QCOMPARE(reopenedStore.signalCount(), 1);
}

void SqliteLocalDataStoreTest::servesCompleteCacheWithoutCallingProvider()
{
    qRegisterMetaType<QVector<Candle>>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("cache-test.db"))));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    QVector<Candle> candles;
    const QDateTime start(QDate(2026, 7, 1), QTime(7, 0), Qt::UTC);
    for (int index = 0; index < 3; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = start.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = 100.0 + index;
        candle.high = 101.0 + index;
        candle.low = 99.0 + index;
        candle.close = 100.5 + index;
        candle.volume = 1000.0;
        candle.source = QStringLiteral("mootdx");
        candle.adjustment = QStringLiteral("none");
        candle.downloadedAt = start.addDays(index).addSecs(60);
        candles.append(candle);
    }
    QVERIFY(store.upsertCandles(candles));

    FakeHistoricalDataProvider provider;
    HistoricalDataService service(&store, &provider);
    QSignalSpy readySpy(&service, &HistoricalDataService::candlesReady);
    QSignalSpy identitySpy(&service, &HistoricalDataService::datasetIdentityReady);
    service.request(instrument, QStringLiteral("day"), 3);

    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(identitySpy.count(), 1);
    QCOMPARE(provider.fetchCount, 0);
    QCOMPARE(readySpy.first().at(2).toBool(), true);
    QCOMPARE(readySpy.first().at(1).value<QVector<Candle>>().size(), 3);
}

void SqliteLocalDataStoreTest::explicitProviderRequestPersistsValidCandlesAndRefreshesCatalog()
{
    qRegisterMetaType<QVector<Candle>>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("explicit-download.db"))));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");
    FakeHistoricalDataProvider provider;
    HistoricalDataService service(&store, &provider);
    QSignalSpy readySpy(&service, &HistoricalDataService::candlesReady);
    QSignalSpy errorSpy(&service, &HistoricalDataService::requestError);

    const quint64 requestId = service.request(instrument, QStringLiteral("day"), 5);
    QTRY_COMPARE(provider.fetchCount, 1);
    QCOMPARE(provider.requestIds.first(), requestId);
    QCOMPARE(provider.frequencies.first(), QStringLiteral("day"));
    QCOMPARE(provider.counts.first(), 5);

    QVector<Candle> response;
    const QDateTime start(QDate(2026, 7, 1), QTime(7, 0), Qt::UTC);
    for (int index = 0; index < 5; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = start.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = 100.0 + index;
        candle.high = 102.0 + index;
        candle.low = 99.0 + index;
        candle.close = 101.0 + index;
        candle.volume = 1000.0 + index;
        candle.source = QStringLiteral("fake_provider");
        response.append(candle);
    }
    Candle invalid = response.first();
    invalid.startTime = start.addDays(8);
    invalid.high = 90.0;
    response.append(invalid);
    emit provider.candlesReady(requestId, response);

    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(readySpy.first().at(2).toBool(), false);
    QCOMPARE(readySpy.first().at(1).value<QVector<Candle>>().size(), 5);
    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.first().candleCount, 5);
    QCOMPARE(catalog.first().frequency(), QStringLiteral("day"));
    QCOMPARE(catalog.first().source, QStringLiteral("fake_provider"));
    QCOMPARE(catalog.first().adjustment, QStringLiteral("none"));
    QVERIFY(catalog.first().dataVersion.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(catalog.first().dataVersion.size(), 71);

    WorkspaceInstrumentSelectionState selections;
    selections.setLocalHistoryCatalog(catalog);
    QString selectionError;
    QVERIFY(!selections.selectBacktestInstrument(
        instrument.symbol, 86400, 5, &selectionError));
    QCOMPARE(provider.fetchCount, 1);
    QVERIFY(selectionError.contains(QStringLiteral("历史 Tick")));
}

void SqliteLocalDataStoreTest::adjustmentMismatchDoesNotReuseCache()
{
    qRegisterMetaType<QVector<Candle>>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("adjustment-cache.db"))));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");

    QVector<Candle> cached;
    const QDateTime start(QDate(2026, 7, 1), QTime(7, 0), Qt::UTC);
    for (int index = 0; index < 3; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = start.addDays(index);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = 100.0 + index;
        candle.volume = 1000.0;
        candle.source = QStringLiteral("mootdx");
        candle.adjustment = QStringLiteral("none");
        cached.append(candle);
    }
    QVERIFY(store.upsertCandles(cached));

    FakeHistoricalDataProvider provider;
    HistoricalDataService service(&store, &provider);
    QSignalSpy readySpy(&service, &HistoricalDataService::candlesReady);
    QSignalSpy identitySpy(&service, &HistoricalDataService::datasetIdentityReady);
    const quint64 requestId = service.request(
        instrument, QStringLiteral("day"), 3, false, QStringLiteral("qfq"));
    QTRY_COMPARE(provider.fetchCount, 1);
    QCOMPARE(provider.adjustments.first(), QStringLiteral("qfq"));

    QVector<Candle> adjusted = cached;
    for (Candle &candle : adjusted) {
        candle.open *= 0.5;
        candle.high *= 0.5;
        candle.low *= 0.5;
        candle.close *= 0.5;
        candle.source = QStringLiteral("adjusted_provider");
        candle.adjustment = QStringLiteral("qfq");
    }
    emit provider.candlesReady(requestId, adjusted);
    QTRY_COMPARE(readySpy.count(), 1);
    QCOMPARE(identitySpy.count(), 1);
    QCOMPARE(identitySpy.first().at(2).toString(), QStringLiteral("qfq"));
    QVERIFY(identitySpy.first().at(3).toString().startsWith(QStringLiteral("sha256:")));

    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.first().source, QStringLiteral("adjusted_provider"));
    QCOMPARE(catalog.first().adjustment, QStringLiteral("qfq"));
    QVERIFY(catalog.first().dataVersion.startsWith(QStringLiteral("sha256:")));
}

void SqliteLocalDataStoreTest::providerFailurePreservesExistingCatalog()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("failure-protection.db"))));
    Instrument instrument;
    instrument.symbol = QStringLiteral("300750");
    instrument.name = QStringLiteral("宁德时代");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SZSE");
    QVector<Candle> cached;
    for (int index = 0; index < 2; ++index) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(QDate(2026, 7, 1).addDays(index),
                                     QTime(7, 0), Qt::UTC);
        candle.periodSeconds = 86400;
        candle.open = candle.high = candle.low = candle.close = 200.0 + index;
        candle.volume = 500.0;
        candle.source = QStringLiteral("existing_cache");
        cached.append(candle);
    }
    QVERIFY(store.upsertCandles(cached));
    const QVector<LocalHistoryDataset> before = store.localHistoryCatalog();

    FakeHistoricalDataProvider provider;
    HistoricalDataService service(&store, &provider);
    QSignalSpy readySpy(&service, &HistoricalDataService::candlesReady);
    QSignalSpy errorSpy(&service, &HistoricalDataService::requestError);
    const quint64 requestId = service.request(instrument, QStringLiteral("day"), 5);
    QTRY_COMPARE(provider.fetchCount, 1);
    emit provider.fetchError(requestId, QStringLiteral("受控 Provider 失败"));
    QTRY_COMPARE(errorSpy.count(), 1);
    QCOMPARE(readySpy.count(), 0);
    const QVector<LocalHistoryDataset> after = store.localHistoryCatalog();
    QCOMPARE(after.size(), before.size());
    QCOMPARE(after.first().candleCount, before.first().candleCount);
    QCOMPARE(after.first().firstTime, before.first().firstTime);
    QCOMPARE(after.first().lastTime, before.first().lastTime);
}

void SqliteLocalDataStoreTest::ignoresSupersededHistoricalResponse()
{
    qRegisterMetaType<QVector<Candle>>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("history-generation.db"))));

    Instrument first;
    first.symbol = QStringLiteral("600519");
    first.name = QStringLiteral("贵州茅台");
    first.market = Market::AShare;
    first.venue = QStringLiteral("SSE");
    Instrument second = first;
    second.symbol = QStringLiteral("300750");

    FakeHistoricalDataProvider provider;
    HistoricalDataService service(&store, &provider);
    QSignalSpy readySpy(&service, &HistoricalDataService::candlesReady);
    QSignalSpy errorSpy(&service, &HistoricalDataService::requestError);
    const quint64 firstId = service.request(first, QStringLiteral("day"), 3);
    QTRY_COMPARE(provider.fetchCount, 1);
    const quint64 secondId = service.request(second, QStringLiteral("day"), 3);
    QVERIFY(secondId > firstId);
    QTRY_COMPARE(provider.fetchCount, 2);
    QCOMPARE(provider.requestIds.first(), firstId);
    QCOMPARE(provider.requestIds.last(), secondId);

    Candle stale;
    stale.instrument = first;
    stale.startTime = QDateTime(QDate(2026, 1, 1), QTime(7, 0), Qt::UTC);
    stale.periodSeconds = 86400;
    stale.open = stale.high = stale.low = stale.close = 100.0;
    stale.source = QStringLiteral("test");
    stale.adjustment = QStringLiteral("none");
    stale.downloadedAt = stale.startTime.addSecs(1);
    emit provider.candlesReady(firstId, {stale});
    QTest::qWait(20);
    QCOMPARE(readySpy.count(), 0);

    QVector<Candle> current;
    for (int index = 0; index < 3; ++index) {
        Candle candle = stale;
        candle.instrument = second;
        candle.startTime = stale.startTime.addDays(index);
        current.append(candle);
    }
    emit provider.candlesReady(secondId, current);
    QTRY_VERIFY(readySpy.count() == 1 || errorSpy.count() == 1);
    if (!errorSpy.isEmpty()) {
        QFAIL(qPrintable(errorSpy.first().at(1).toString()));
    }
    QCOMPARE(readySpy.count(), 1);
    QCOMPARE(readySpy.first().at(0).toULongLong(), secondId);
    QCOMPARE(readySpy.first().at(1).value<QVector<Candle>>().first().instrument.symbol,
             second.symbol);
}

void SqliteLocalDataStoreTest::persistsAndLoadsTradingState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("trading-state.db"));

    Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("Kweichow Moutai");
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("SSE");
    const QDateTime timestamp(QDate(2026, 8, 3), QTime(2, 0), Qt::UTC);

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QSignalSpy writeFailedSpy(&store, &SqliteLocalDataStore::writeFailed);

        Order order;
        order.orderId = QStringLiteral("PAPER-1");
        order.request.clientOrderId = QStringLiteral("CLIENT-1");
        order.request.instrument = instrument;
        order.request.side = OrderSide::Buy;
        order.request.type = OrderType::Limit;
        order.request.timeInForce = TimeInForce::GoodTillCancelled;
        order.request.quantity = 100.0;
        order.request.limitPrice = 100.0;
        order.request.referencePrice = 99.5;
        order.request.createdAt = timestamp;
        order.request.expiresAt = timestamp.addSecs(3600);
        order.status = OrderStatus::Accepted;
        order.updatedAt = timestamp;
        QVERIFY2(store.upsertOrder(order),
                 writeFailedSpy.isEmpty() ? "unknown SQL error"
                                          : qPrintable(writeFailedSpy.last().first().toString()));
        order.status = OrderStatus::Filled;
        order.filledQuantity = 100.0;
        order.averageFillPrice = 100.0;
        order.message = QStringLiteral("全部成交");
        order.updatedAt = timestamp.addSecs(1);
        QVERIFY(store.upsertOrder(order));

        Execution execution;
        execution.executionId = QStringLiteral("EXEC-1");
        execution.orderId = order.orderId;
        execution.instrument = instrument;
        execution.side = OrderSide::Buy;
        execution.quantity = 100.0;
        execution.price = 100.0;
        execution.commission = 3.0;
        execution.timestamp = timestamp.addSecs(1);
        QVERIFY(store.appendExecution(execution));
        QVERIFY(store.appendExecution(execution));

        Position position;
        position.instrument = instrument;
        position.quantity = 100.0;
        position.availableQuantity = 100.0;
        position.averageCost = 100.03;
        position.marketPrice = 101.0;
        position.marketValue = 10100.0;
        position.unrealizedPnl = 97.0;

        AccountSnapshot account;
        account.tradingDay = QDate(2026, 8, 3);
        account.initialEquity = 100000.0;
        account.dayStartEquity = 100000.0;
        account.cash = 89997.0;
        account.marketValue = 10100.0;
        account.totalEquity = 100097.0;
        account.unrealizedPnl = 97.0;
        account.totalCommission = 3.0;
        account.dailyPnl = 97.0;
        QVERIFY(store.saveTradingState(account, {position}));

        StrategyExecutionConfig config;
        config.strategyId = QStringLiteral("sma-auto");
        config.enabled = true;
        config.mode = StrategyExecutionMode::ManualApproval;
        config.fixedOrderValue = 20000.0;
        config.approvalTimeoutSeconds = 180;
        config.timeInForce = TimeInForce::ImmediateOrCancel;
        config.orderTimeoutSeconds = 15;
        config.maxOrdersPerDay = 3;
        config.maxDailyOrderValue = 50000.0;
        QVERIFY(store.upsertStrategyConfiguration(config));

        StrategyOrderIntent intent;
        intent.intentId = QStringLiteral("INTENT-1");
        intent.strategyId = config.strategyId;
        intent.signal.instrument = instrument;
        intent.signal.timestamp = timestamp;
        intent.signal.side = SignalSide::Buy;
        intent.signal.price = 100.0;
        intent.signal.reason = QStringLiteral("test");
        intent.orderRequest.clientOrderId = intent.intentId;
        intent.orderRequest.instrument = instrument;
        intent.orderRequest.side = OrderSide::Buy;
        intent.orderRequest.type = OrderType::Market;
        intent.orderRequest.timeInForce = TimeInForce::ImmediateOrCancel;
        intent.orderRequest.quantity = 100.0;
        intent.orderRequest.referencePrice = 100.0;
        intent.orderRequest.expiresAt = timestamp.addSecs(15);
        intent.status = OrderIntentStatus::PendingApproval;
        intent.message = QStringLiteral("等待人工批准");
        intent.createdAt = timestamp;
        intent.updatedAt = timestamp;
        intent.expiresAt = timestamp.addSecs(180);
        QVERIFY(store.upsertStrategyIntent(intent));
    }

    SqliteLocalDataStore reopenedStore;
    QVERIFY(reopenedStore.initialize(databasePath));
    const TradingState state = reopenedStore.loadTradingState();
    QVERIFY(state.hasAccountSnapshot);
    QCOMPARE(state.orders.size(), 1);
    QVERIFY(state.orders.first().status == OrderStatus::Filled);
    QCOMPARE(state.orders.first().averageFillPrice, 100.0);
    QVERIFY(state.orders.first().request.timeInForce == TimeInForce::GoodTillCancelled);
    QCOMPARE(state.orders.first().request.expiresAt, timestamp.addSecs(3600));
    QCOMPARE(state.executions.size(), 1);
    QCOMPARE(state.positions.size(), 1);
    QCOMPARE(state.positions.first().instrument.symbol, instrument.symbol);
    QCOMPARE(state.positions.first().quantity, 100.0);
    QCOMPARE(state.account.totalEquity, 100097.0);
    QCOMPARE(state.account.totalCommission, 3.0);
    QCOMPARE(state.account.tradingDay, QDate(2026, 8, 3));
    QCOMPARE(state.account.dayStartEquity, 100000.0);
    QCOMPARE(state.account.dailyPnl, 97.0);

    const QVector<StrategyExecutionConfig> configurations = reopenedStore.loadStrategyConfigurations();
    QCOMPARE(configurations.size(), 1);
    QCOMPARE(configurations.first().strategyId, QStringLiteral("sma-auto"));
    QCOMPARE(configurations.first().approvalTimeoutSeconds, 180);
    QVERIFY(configurations.first().timeInForce == TimeInForce::ImmediateOrCancel);
    QCOMPARE(configurations.first().orderTimeoutSeconds, 15);
    QCOMPARE(configurations.first().maxOrdersPerDay, 3);
    QCOMPARE(configurations.first().maxDailyOrderValue, 50000.0);
    const QVector<StrategyOrderIntent> intents = reopenedStore.loadStrategyIntents();
    QCOMPARE(intents.size(), 1);
    QVERIFY(intents.first().status == OrderIntentStatus::PendingApproval);
    QCOMPARE(intents.first().orderRequest.quantity, 100.0);
    QVERIFY(intents.first().orderRequest.timeInForce == TimeInForce::ImmediateOrCancel);
    QCOMPARE(intents.first().orderRequest.expiresAt, timestamp.addSecs(15));
    QCOMPARE(intents.first().expiresAt, timestamp.addSecs(180));
}

void SqliteLocalDataStoreTest::migratesLegacyAccountSnapshotSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("legacy-account.db"));
    const QString connectionName = QStringLiteral("legacy_%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "create table account_snapshots("
            "id integer primary key autoincrement, timestamp_utc text not null, initial_equity real not null, "
            "cash real not null, frozen_cash real not null, market_value real not null, total_equity real not null, "
            "realized_pnl real not null, unrealized_pnl real not null, total_commission real not null)")));
        QVERIFY(query.exec(QStringLiteral(
            "insert into account_snapshots(timestamp_utc, initial_equity, cash, frozen_cash, market_value, "
            "total_equity, realized_pnl, unrealized_pnl, total_commission) "
            "values('2026-08-01T00:00:00.000Z', 100000, 90000, 0, 10000, 100000, 0, 0, 3)")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    const TradingState state = store.loadTradingState();
    QVERIFY(state.hasAccountSnapshot);
    QCOMPARE(state.account.initialEquity, 100000.0);
    QCOMPARE(state.account.totalEquity, 100000.0);
    QCOMPARE(state.account.dayStartEquity, 0.0);
    QCOMPARE(state.account.dailyPnl, 0.0);
}

void SqliteLocalDataStoreTest::writesMarketSnapshotsAsynchronouslyAndIdempotently()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("async-ticks.db"));

    MarketSnapshot snapshot;
    snapshot.batchId = QStringLiteral("BATCH-ASYNC-1");
    snapshot.market = Market::AShare;
    snapshot.timestamp = QDateTime::currentDateTimeUtc();
    constexpr int stockCount = 5000;
    snapshot.expectedCount = stockCount;
    for (int index = 0; index < stockCount; ++index) {
        CrossSectionalQuote quote;
        quote.instrument.symbol = QString::number(600000 + index);
        quote.instrument.market = Market::AShare;
        quote.instrument.venue = QStringLiteral("SSE");
        quote.timestamp = snapshot.timestamp;
        quote.price = 100.0 + index;
        quote.previousClose = 100.0;
        quote.volume = 1000.0 + index;
        quote.amount = quote.price * quote.volume;
        snapshot.quotes.append(quote);
    }

    AsyncTickWriter writer;
    QVERIFY(writer.start(databasePath));
    QSignalSpy writtenSpy(&writer, &AsyncTickWriter::batchWritten);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(writer.enqueue(snapshot));
    QTRY_COMPARE_WITH_TIMEOUT(writtenSpy.count(), 1, 10000);
    QCOMPARE(writtenSpy.at(0).at(0).toInt(), stockCount);
    qInfo("5000-stock asynchronous SQLite batch write: %lld ms",
          static_cast<long long>(timer.elapsed()));

    QVERIFY(writer.enqueue(snapshot));
    QTRY_COMPARE_WITH_TIMEOUT(writtenSpy.count(), 2, 10000);
    QCOMPARE(writtenSpy.at(1).at(0).toInt(), 0);
    writer.stop();

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    QCOMPARE(store.tickCount(), stockCount);
    const auto restoredSnapshot = store.loadMarketSnapshotAtOrBefore(
        snapshot.completedAt.isValid() ? snapshot.completedAt : snapshot.timestamp);
    QVERIFY(restoredSnapshot.has_value());
    QCOMPARE(restoredSnapshot->batchId, snapshot.batchId);
    QCOMPARE(restoredSnapshot->expectedCount, stockCount);
    QCOMPARE(restoredSnapshot->quotes.size(), stockCount);
    QVERIFY(!writer.enqueue(snapshot));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::SqliteLocalDataStoreTest)

#include "tst_sqlite_local_data_store.moc"
