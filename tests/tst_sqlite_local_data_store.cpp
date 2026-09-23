#include "app/WorkspaceInstrumentSelectionState.h"
#include "models/BacktestArchiveListModel.h"
#include "services/SqliteLocalDataStore.h"
#include "services/BacktestDataVersion.h"
#include "services/StorageProfile.h"
#include "services/HistoricalDataService.h"
#include "services/AsyncTickWriter.h"
#include "services/RawTickParameterSensitivityScanner.h"
#include "services/RawTickParameterSensitivityService.h"

#include <QSignalSpy>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <limits>

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
    Candle firstCandle;
    firstCandle.instrument = config.instrument;
    firstCandle.startTime = config.from;
    firstCandle.periodSeconds = config.periodSeconds;
    firstCandle.open = 1500.0;
    firstCandle.high = 1510.0;
    firstCandle.low = 1495.0;
    firstCandle.close = 1505.0;
    firstCandle.volume = 120000.0;
    firstCandle.buyVolume = 70000.0;
    firstCandle.sellVolume = 40000.0;
    firstCandle.neutralVolume = 10000.0;
    firstCandle.directionalVolumeKnown = true;
    firstCandle.source = QStringLiteral("raw_tick_cache_v1");
    firstCandle.adjustment = QStringLiteral("none");
    Candle secondCandle = firstCandle;
    secondCandle.startTime = config.to;
    secondCandle.open = 1505.0;
    secondCandle.high = 1525.0;
    secondCandle.low = 1500.0;
    secondCandle.close = 1520.0;
    secondCandle.volume = 180000.0;
    record.result.candles = {firstCandle, secondCandle};
    TradeSignal signal;
    signal.instrument = config.instrument;
    signal.timestamp = config.to;
    signal.side = SignalSide::Sell;
    signal.price = 1520.0;
    signal.volume = 600.0;
    signal.reason = QStringLiteral("均线死叉");
    record.result.chartSignals = {signal};
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

BacktestArchiveRecord rawArchiveFixture(const QString &resultId, int tickCount)
{
    BacktestArchiveRecord record = archiveFixture(
        resultId, QStringLiteral("raw-task"), QStringLiteral("tick_momentum"), 0,
        QDateTime(QDate(2026, 7, 1), QTime(2, 0), Qt::UTC));
    BacktestConfig &config = record.result.config;
    config.dataMode = BacktestDataMode::RawTicks;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.actualBars = tickCount;
    record.result.candles.clear();
    record.result.chartSignals.clear();
    record.result.rawTicks.clear();
    for (int index = 0; index < tickCount; ++index) {
        RawTradeTick tick;
        tick.instrument = config.instrument;
        tick.tradingDate = config.from.date();
        tick.timestamp = config.from.addSecs(index);
        tick.sourceLine = index + 1;
        tick.price = 1500.0 + index;
        tick.volumeShares = 100.0 + index;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = index % 2 == 0
            ? RawTradeDirection::Buy : RawTradeDirection::Sell;
        tick.source = QStringLiteral("test-fixture");
        record.result.rawTicks.append(tick);
    }
    config.to = record.result.rawTicks.last().timestamp;
    config.dataVersion = computeBacktestDataVersion(
        record.result.rawTicks, config.instrument.symbol);
    TradeSignal signal;
    signal.instrument = config.instrument;
    signal.timestamp = record.result.rawTicks.at(tickCount / 2).timestamp;
    signal.side = SignalSide::Sell;
    signal.price = record.result.rawTicks.at(tickCount / 2).price;
    signal.volume = 100.0;
    signal.reason = QStringLiteral("原始 Tick 测试信号");
    record.result.chartSignals = {signal};
    record.result.equityObservationCount = tickCount;
    record.result.equityCurveSampled = tickCount > record.result.equityCurve.size();
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
    void archiveSampleWindowsSurviveReopen_data();
    void archiveSampleWindowsSurviveReopen();
    void legacyArchiveDoesNotInventRequestedSampleWindow();
    void archiveRejectsCorruptRequestedSampleWindow_data();
    void archiveRejectsCorruptRequestedSampleWindow();
    void archiveFinalPositionIsModeIndependent_data();
    void archiveFinalPositionIsModeIndependent();
    void validationRunSampleWindowsRoundTrip_data();
    void validationRunSampleWindowsRoundTrip();
    void archiveDisplayRestoreDefersRawTickSnapshot();
    void archiveRawTickPagesPreserveStableOrder();
    void archiveRawTickPageRejectsUnboundedRequest();
    void archiveRestoresSampledRawTickEquityMetadata();
    void archivePreservesDatasetIdentity();
    void archiveTransactionRollsBackOnChildFailure();
    void archiveRestoreIsStableAndDoesNotRequestNetwork();
    void legacyV17ArchiveRestoresWithoutInventedCandles();
    void incompleteV18CandleSnapshotIsRejected();
    void migratesLegacyDatabaseWithBackupAndStableIdentity();
    void migrationFailureRollsBackWithoutHalfSchema();
    void relocatesDatabaseByVerifiedCopyAndRejectsNetworkPath();
    void storageProfileAcceptsExplicitEnvironmentOverride();
    void aShareInstrumentDayStatusRoundTripsTransactionally();
    void rawTickParameterSensitivityRoundTripsImmutableIdentity();
    void rawTickNestedWalkForwardArchiveRequiresAndRestoresAllFolds();
    void rawTickParameterSensitivityRunsAsynchronouslyAndArchives();
    void rawTickParameterSensitivityKeepsOnlyOneTradingDayResident();
};

void SqliteLocalDataStoreTest::storageProfileAcceptsExplicitEnvironmentOverride()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString expected = QDir::cleanPath(
        directory.filePath(QStringLiteral("explicit/pptrader.db")));
    const QByteArray previous = qgetenv("PPTRADER_DATABASE_PATH");
    qputenv("PPTRADER_DATABASE_PATH", expected.toLocal8Bit());
    const QString actual = StorageProfile::configuredDatabasePath();
    if (previous.isNull()) qunsetenv("PPTRADER_DATABASE_PATH");
    else qputenv("PPTRADER_DATABASE_PATH", previous);
    QCOMPARE(actual, expected);
}

void SqliteLocalDataStoreTest::aShareInstrumentDayStatusRoundTripsTransactionally()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("calendar-status.db"))));

    Instrument value;
    value.symbol = QStringLiteral("600519");
    value.name = QStringLiteral("贵州茅台");
    value.market = Market::AShare;
    value.venue = QStringLiteral("SSE");
    AShareInstrumentDayRecord record;
    record.instrument = value;
    record.tradingDate = QDate(2026, 1, 5);
    record.state = AShareInstrumentDayState::Suspended;
    record.reason = QStringLiteral("公告停牌");
    record.source = QStringLiteral("test-fixture");
    record.sourceVersion = QStringLiteral("fixture-v1");
    QVERIFY(record.isValid());
    QSignalSpy writeFailed(&store, &SqliteLocalDataStore::writeFailed);
    const bool inserted = store.upsertAShareInstrumentDayRecords({record});
    const QString insertError = writeFailed.isEmpty()
        ? QString{} : writeFailed.last().first().toString();
    QVERIFY2(inserted, qPrintable(insertError));

    QVector<AShareInstrumentDayRecord> loaded =
        store.loadAShareInstrumentDayRecords(
            {value.symbol}, QDate(2026, 1, 5), QDate(2026, 1, 5));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().state, AShareInstrumentDayState::Suspended);
    QCOMPARE(loaded.first().reason, QStringLiteral("公告停牌"));

    record.state = AShareInstrumentDayState::Tradable;
    record.reason.clear();
    record.sourceVersion = QStringLiteral("fixture-v2");
    writeFailed.clear();
    const bool updated = store.upsertAShareInstrumentDayRecords({record});
    const QString updateError = writeFailed.isEmpty()
        ? QString{} : writeFailed.last().first().toString();
    QVERIFY2(updated, qPrintable(updateError));
    loaded = store.loadAShareInstrumentDayRecords(
        {value.symbol}, QDate(2026, 1, 5), QDate(2026, 1, 5));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().state, AShareInstrumentDayState::Tradable);
    QCOMPARE(loaded.first().sourceVersion, QStringLiteral("fixture-v2"));

    AShareInstrumentDayRecord invalid = record;
    invalid.state = AShareInstrumentDayState::Unknown;
    QVERIFY(!store.upsertAShareInstrumentDayRecords({record, invalid}));
    loaded = store.loadAShareInstrumentDayRecords(
        {value.symbol}, QDate(2026, 1, 5), QDate(2026, 1, 5));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().sourceVersion, QStringLiteral("fixture-v2"));
}

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
        QCOMPARE(store.schemaVersion(), 25);
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
            QCOMPARE(query.value(0).toInt(), 25);
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
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 7"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("backtest_validation_runs"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 8"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("cross_sectional_backtest_archive"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 9"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("ashare_calendar_status"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 10"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("ashare_instrument_day_import_audit"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 11"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("ashare_instrument_day_revision_audit"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 12"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("ashare_industry_dataset"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 13"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("ashare_industry_import_audit"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 14"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("factor_definition_registry"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 15"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("factor_candidate_governance"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 16"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("authorized_factor_dataset"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 17"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("external_factor_research_archive"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 18"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("backtest_candle_snapshot"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 20"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("backtest_open_position"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 22"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("raw_tick_walk_forward"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 23"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("raw_tick_parameter_sensitivity"));
            QVERIFY(query.exec(QStringLiteral(
                "select migration_name from schema_migration_history where version = 24"))
                && query.next());
            QCOMPARE(query.value(0).toString(),
                     QStringLiteral("raw_tick_day_quality_audit"));
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
    QCOMPARE(reopened.schemaVersion(), 25);
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

void SqliteLocalDataStoreTest::archiveFinalPositionIsModeIndependent_data()
{
    QTest::addColumn<bool>("rawMode");
    QTest::newRow("candles") << false;
    QTest::newRow("raw-ticks") << true;
}

void SqliteLocalDataStoreTest::archiveSampleWindowsSurviveReopen_data()
{
    QTest::addColumn<bool>("rawMode");
    QTest::addColumn<bool>("hasFrom");
    QTest::addColumn<bool>("hasTo");
    QTest::newRow("candles-empty") << false << false << false;
    QTest::newRow("candles-from-only") << false << true << false;
    QTest::newRow("candles-to-only") << false << false << true;
    QTest::newRow("candles-both") << false << true << true;
    QTest::newRow("ticks-empty") << true << false << false;
    QTest::newRow("ticks-from-only") << true << true << false;
    QTest::newRow("ticks-to-only") << true << false << true;
    QTest::newRow("ticks-both") << true << true << true;
}

void SqliteLocalDataStoreTest::archiveSampleWindowsSurviveReopen()
{
    QFETCH(bool, rawMode);
    QFETCH(bool, hasFrom);
    QFETCH(bool, hasTo);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample-window.db"));
    BacktestArchiveRecord record = rawMode
        ? rawArchiveFixture(QStringLiteral("window"), 3)
        : archiveFixture(QStringLiteral("window"), QStringLiteral("window-task"),
                         QStringLiteral("sma_crossover"), 0,
                         QDateTime(QDate(2026, 1, 6), QTime(8, 0), Qt::UTC));
    // 请求边界故意不同于实际输入首末，禁止加载时从行情时间反推。
    const QDateTime requestedFrom = hasFrom
        ? QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00.123+08:00"),
                                Qt::ISODateWithMs) : QDateTime();
    const QDateTime requestedTo = hasTo
        ? QDateTime::fromString(QStringLiteral("2026-01-05T23:59:59.987+08:00"),
                                Qt::ISODateWithMs) : QDateTime();
    record.result.config.sampleFrom = requestedFrom;
    record.result.config.sampleTo = requestedTo;
    {
        SqliteLocalDataStore writer;
        QVERIFY(writer.initialize(path));
        QVERIFY(writer.saveBacktestArchive(record));
    }
    SqliteLocalDataStore reader;
    QVERIFY(reader.initialize(path));
    for (const auto mode : {BacktestArchiveLoadMode::Display,
                            BacktestArchiveLoadMode::CompleteInputSnapshot}) {
        const auto loaded = reader.loadBacktestArchive(record.resultId, mode);
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->result.config.sampleWindowKnown);
        QCOMPARE(loaded->result.config.sampleFrom, requestedFrom.toUTC());
        QCOMPARE(loaded->result.config.sampleTo, requestedTo.toUTC());
        QCOMPARE(loaded->result.config.from, record.result.config.from);
        QCOMPARE(loaded->result.config.to, record.result.config.to);
        QCOMPARE(loaded->result.config.dataVersion, record.result.config.dataVersion);
        QCOMPARE(loaded->result.config.ruleSnapshot, record.result.config.ruleSnapshot);
    }
}

void SqliteLocalDataStoreTest::legacyArchiveDoesNotInventRequestedSampleWindow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-v24.db"));
    const auto original = archiveFixture(QStringLiteral("legacy-window"), QStringLiteral("legacy-task"),
        QStringLiteral("custom_sample_window"), 1, QDateTime::currentDateTimeUtc());
    QVERIFY(!original.result.config.ruleSnapshot.isEmpty());
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(original));
    }
    // 仅临时库构造真实旧结构：两个新列均不存在，而非只调低版本号。
    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        auto legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral("alter table backtest_results drop column sample_from_utc")));
        QVERIFY(query.exec(QStringLiteral("alter table backtest_results drop column sample_to_utc")));
        QVERIFY(query.exec(QStringLiteral("delete from schema_migration_history where version=25")));
        QVERIFY(query.exec(QStringLiteral("update app_database_metadata set schema_version=24")));
        QVERIFY(query.exec(QStringLiteral("pragma user_version=24")));
    }
    QSqlDatabase::removeDatabase(connection);
    {
        SqliteLocalDataStore upgraded;
        QVERIFY(upgraded.initialize(path));
        QCOMPARE(upgraded.schemaVersion(), 25);
        QVERIFY(QFileInfo::exists(upgraded.lastMigrationBackupPath()));
        const auto loaded = upgraded.loadBacktestArchive(original.resultId,
            BacktestArchiveLoadMode::CompleteInputSnapshot);
        QVERIFY(loaded.has_value());
        QVERIFY(!loaded->result.config.sampleWindowKnown);
        QVERIFY(!loaded->result.config.sampleFrom.isValid());
        QVERIFY(!loaded->result.config.sampleTo.isValid());
        QCOMPARE(loaded->result.config.from, original.result.config.from);
        QCOMPARE(loaded->result.config.to, original.result.config.to);
        QCOMPARE(loaded->result.config.dataVersion, original.result.config.dataVersion);
        QCOMPARE(loaded->result.config.ruleSnapshot, original.result.config.ruleSnapshot);
        QCOMPARE(loaded->result.candles.size(), original.result.candles.size());
        QCOMPARE(loaded->result.equityCurve.size(), original.result.equityCurve.size());
        QCOMPARE(loaded->result.trades.size(), original.result.trades.size());
        auto copied = *loaded;
        copied.resultId = QStringLiteral("copied-legacy-window");
        QVERIFY(upgraded.saveBacktestArchive(copied));
    }
    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QVERIFY(reopened.lastMigrationBackupPath().isEmpty());
    const auto copied = reopened.loadBacktestArchive(QStringLiteral("copied-legacy-window"));
    QVERIFY(copied.has_value());
    QVERIFY(!copied->result.config.sampleWindowKnown);
}

void SqliteLocalDataStoreTest::archiveRejectsCorruptRequestedSampleWindow_data()
{
    QTest::addColumn<QString>("fromText");
    QTest::addColumn<QString>("toText");
    QTest::newRow("invalid-start") << QStringLiteral("not-a-date") << QStringLiteral("");
    QTest::newRow("invalid-end") << QStringLiteral("") << QStringLiteral("not-a-date");
    QTest::newRow("reversed") << QStringLiteral("2026-01-06T00:00:00.000Z")
                              << QStringLiteral("2026-01-01T00:00:00.000Z");
}

void SqliteLocalDataStoreTest::archiveRejectsCorruptRequestedSampleWindow()
{
    QFETCH(QString, fromText);
    QFETCH(QString, toText);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("corrupt-sample-window.db"));
    const auto original = archiveFixture(QStringLiteral("corrupt-window"), QStringLiteral("task"),
        QStringLiteral("sma_crossover"), 0, QDateTime::currentDateTimeUtc());
    {
        SqliteLocalDataStore writer;
        QVERIFY(writer.initialize(path));
        QVERIFY(writer.saveBacktestArchive(original));
    }
    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        auto fault = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        fault.setDatabaseName(path);
        QVERIFY(fault.open());
        QSqlQuery query(fault);
        query.prepare(QStringLiteral("update backtest_results set sample_from_utc=?, sample_to_utc=?"));
        query.addBindValue(fromText);
        query.addBindValue(toText);
        QVERIFY(query.exec());
    }
    QSqlDatabase::removeDatabase(connection);
    SqliteLocalDataStore reader;
    QVERIFY(reader.initialize(path));
    QSignalSpy errors(&reader, &SqliteLocalDataStore::writeFailed);
    QVERIFY(!reader.loadBacktestArchive(original.resultId).has_value());
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.last().first().toString().contains(QStringLiteral("请求样本窗")));
}

void SqliteLocalDataStoreTest::validationRunSampleWindowsRoundTrip_data()
{
    QTest::addColumn<bool>("hasFrom");
    QTest::addColumn<bool>("hasTo");
    QTest::newRow("empty") << false << false;
    QTest::newRow("from-only") << true << false;
    QTest::newRow("to-only") << false << true;
    QTest::newRow("both") << true << true;
}

void SqliteLocalDataStoreTest::validationRunSampleWindowsRoundTrip()
{
    QFETCH(bool, hasFrom);
    QFETCH(bool, hasTo);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("validation.db"));
    ValidationRun run;
    run.name = QStringLiteral("隔离验证");
    run.strategyId = QStringLiteral("custom_fixture");
    run.strategyVersion = 1;
    run.instrumentSymbol = QStringLiteral("600519");
    run.periodSeconds = 60;
    run.requestedBars = 500;
    run.completedAt = QDateTime(QDate(2026, 9, 7), QTime(1, 0), Qt::UTC);
    if (hasFrom) run.sampleFrom = run.completedAt.addDays(-2);
    if (hasTo) run.sampleTo = run.completedAt.addDays(-1);
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QSignalSpy failures(&store, &SqliteLocalDataStore::writeFailed);
        const bool saved = store.saveValidationRun(run);
        QVERIFY2(saved, failures.isEmpty() ? "" : qPrintable(failures.last().first().toString()));
    }
    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const auto runs = reopened.loadValidationRuns(10);
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.first().sampleFrom, run.sampleFrom);
    QCOMPARE(runs.first().sampleTo, run.sampleTo);
    QCOMPARE(runs.first().name, run.name);
    QCOMPARE(runs.first().strategyVersion, 1);
    QCOMPARE(runs.first().requestedBars, 500);
}

void SqliteLocalDataStoreTest::archiveFinalPositionIsModeIndependent()
{
    QFETCH(bool, rawMode);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("position.db"));
    BacktestArchiveRecord record = rawMode
        ? rawArchiveFixture(QStringLiteral("position"), 4)
        : archiveFixture(QStringLiteral("position"), QStringLiteral("task"),
                         QStringLiteral("sma_crossover"), 0, QDateTime::currentDateTimeUtc());
    auto &position = record.result.finalPosition;
    position.quantity = 100.0;
    position.averagePrice = 1500.0;
    position.entryCommission = 45.0;
    position.entryTime = record.result.config.to;
    position.entryReason = QStringLiteral("末日买入，保留未平仓持仓");
    const double lastPrice = rawMode ? record.result.rawTicks.last().price
                                    : record.result.candles.last().close;
    const double cash = record.result.config.initialCapital
        - position.quantity * position.averagePrice - position.entryCommission;
    const double positionValue = position.quantity * lastPrice;
    record.result.trades.clear();
    record.result.chartSignals.clear();
    record.result.equityCurve = {
        {record.result.config.from, record.result.config.initialCapital,
         record.result.config.initialCapital, 0.0, 0.0},
        {record.result.config.to, cash + positionValue, cash, positionValue, 0.0}
    };
    record.result.summary = {};
    record.result.summary.initialCapital = record.result.config.initialCapital;
    record.result.summary.finalEquity = cash + positionValue;
    record.result.summary.totalCommission = position.entryCommission;
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(record));
        const QVector<BacktestPosition> invalidPositions = [&]() {
            QVector<BacktestPosition> values;
            auto invalid = position;
            invalid.quantity = -1.0; values.append(invalid);
            invalid = position; invalid.quantity = std::numeric_limits<double>::quiet_NaN(); values.append(invalid);
            invalid = position; invalid.averagePrice = 0.0; values.append(invalid);
            invalid = position; invalid.averagePrice = std::numeric_limits<double>::infinity(); values.append(invalid);
            invalid = position; invalid.entryCommission = -1.0; values.append(invalid);
            invalid = position; invalid.entryCommission = std::numeric_limits<double>::infinity(); values.append(invalid);
            invalid = position; invalid.entryTime = {}; values.append(invalid);
            invalid = position; invalid.entryTime = record.result.config.to.addSecs(1); values.append(invalid);
            return values;
        }();
        for (int index = 0; index < invalidPositions.size(); ++index) {
            auto invalid = record;
            invalid.resultId = QStringLiteral("invalid-%1").arg(index);
            invalid.result.finalPosition = invalidPositions.at(index);
            QVERIFY(!store.saveBacktestArchive(invalid));
            QVERIFY(!store.loadBacktestArchive(invalid.resultId).has_value());
        }
        QCOMPARE(store.loadBacktestArchiveSummaries().size(), 1);
    }
    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    for (const auto mode : {BacktestArchiveLoadMode::CompleteInputSnapshot,
                            BacktestArchiveLoadMode::Display}) {
        const auto restored = reopened.loadBacktestArchive(record.resultId, mode);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->result.config.dataMode, record.result.config.dataMode);
        QCOMPARE(restored->result.config.dataVersion, record.result.config.dataVersion);
        QCOMPARE(restored->result.finalPosition.quantity, position.quantity);
        QCOMPARE(restored->result.finalPosition.averagePrice, position.averagePrice);
        QCOMPARE(restored->result.finalPosition.entryCommission, position.entryCommission);
        QCOMPARE(restored->result.finalPosition.entryTime, position.entryTime);
        QCOMPARE(restored->result.finalPosition.entryReason, position.entryReason);
        QCOMPARE(restored->result.equityCurve.last().cash, record.result.equityCurve.last().cash);
        QCOMPARE(restored->result.equityCurve.last().positionValue, record.result.equityCurve.last().positionValue);
    }
    // 临时库被破坏时恢复仍须失败，不能因放开 Candle 模式而放宽字段校验。
    const QString connectionName = QUuid::createUuid().toString();
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery mutation(database);
        QVERIFY(mutation.exec(QStringLiteral(
            "update backtest_results set final_position_average_price = 0 where result_id = 'position'")));
        QVERIFY(!reopened.loadBacktestArchive(record.resultId).has_value());
    }
    QSqlDatabase::removeDatabase(connectionName);
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
        QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);
        const bool builtInSaved = store.saveBacktestArchive(builtIn);
        const QString builtInError = failureSpy.isEmpty()
            ? QString{} : failureSpy.last().first().toString();
        QVERIFY2(builtInSaved, qPrintable(builtInError));
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
    QCOMPARE(restoredBuiltIn->result.candles.size(), 2);
    QCOMPARE(restoredBuiltIn->result.candles.first().source,
             QStringLiteral("raw_tick_cache_v1"));
    QCOMPARE(restoredBuiltIn->result.candles.first().buyVolume, 70000.0);
    QVERIFY(restoredBuiltIn->result.candles.first().directionalVolumeKnown);
    QCOMPARE(restoredBuiltIn->result.chartSignals.size(), 1);
    QCOMPARE(restoredBuiltIn->result.chartSignals.first().timestamp, builtIn.result.config.to);
    QCOMPARE(restoredBuiltIn->result.chartSignals.first().side, SignalSide::Sell);
    QCOMPARE(restoredBuiltIn->result.chartSignals.first().reason, QStringLiteral("均线死叉"));
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

void SqliteLocalDataStoreTest::archiveDisplayRestoreDefersRawTickSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(
        QStringLiteral("archive-display-restore.db"))));
    const BacktestArchiveRecord original = rawArchiveFixture(
        QStringLiteral("display-raw"), 12);
    QVERIFY(store.saveBacktestArchive(original));

    const auto restored = store.loadBacktestArchive(
        original.resultId, BacktestArchiveLoadMode::Display);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.config.actualBars, 12);
    QVERIFY(restored->result.rawTicks.isEmpty());
    QVERIFY(restored->result.rawTickSnapshotAvailable);
    QVERIFY(!restored->result.rawTickSnapshotLoaded);
    QCOMPARE(restored->result.chartSignals.size(), 1);
    QCOMPARE(restored->result.chartSignals.first().timestamp,
             original.result.chartSignals.first().timestamp);
    QCOMPARE(restored->result.equityCurve.size(), 2);
    QCOMPARE(restored->result.trades.size(), 1);
}

void SqliteLocalDataStoreTest::archiveRawTickPagesPreserveStableOrder()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(
        QStringLiteral("archive-raw-pages.db"))));
    const BacktestArchiveRecord original = rawArchiveFixture(
        QStringLiteral("paged-raw"), 12);
    QVERIFY(store.saveBacktestArchive(original));

    const auto first = store.loadBacktestRawTickPage(original.resultId, 0, 5);
    const auto second = store.loadBacktestRawTickPage(original.resultId, 5, 5);
    const auto last = store.loadBacktestRawTickPage(original.resultId, 10, 5);
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QVERIFY(last.has_value());
    QCOMPARE(first->offset, 0);
    QCOMPARE(first->totalCount, 12);
    QCOMPARE(first->ticks.size(), 5);
    QVERIFY(first->hasMore);
    QCOMPARE(second->offset, 5);
    QCOMPARE(second->ticks.size(), 5);
    QVERIFY(second->hasMore);
    QCOMPARE(last->offset, 10);
    QCOMPARE(last->ticks.size(), 2);
    QVERIFY(!last->hasMore);
    QCOMPARE(first->ticks.first().sourceLine, 1LL);
    QCOMPARE(first->ticks.last().sourceLine, 5LL);
    QCOMPARE(second->ticks.first().sourceLine, 6LL);
    QCOMPARE(second->ticks.last().sourceLine, 10LL);
    QCOMPARE(last->ticks.first().sourceLine, 11LL);
    QCOMPARE(last->ticks.last().sourceLine, 12LL);
}

void SqliteLocalDataStoreTest::archiveRawTickPageRejectsUnboundedRequest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(
        QStringLiteral("archive-raw-page-bound.db"))));
    const BacktestArchiveRecord original = rawArchiveFixture(
        QStringLiteral("bounded-page-raw"), 12);
    QVERIFY(store.saveBacktestArchive(original));
    QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);

    QVERIFY(!store.loadBacktestRawTickPage(
        original.resultId, 0, 4097).has_value());
    QVERIFY(!failureSpy.isEmpty());
    QVERIFY(failureSpy.last().first().toString().contains(
        QStringLiteral("最多 4096")));
}

void SqliteLocalDataStoreTest::archiveRestoresSampledRawTickEquityMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sampled-raw-equity.db"));
    BacktestArchiveRecord record = archiveFixture(
        QStringLiteral("result-sampled-raw"), QStringLiteral("sampled-raw-task"),
        QStringLiteral("tick_momentum"), 0,
        QDateTime(QDate(2026, 7, 1), QTime(2, 0), Qt::UTC));
    BacktestConfig &config = record.result.config;
    config.dataMode = BacktestDataMode::RawTicks;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.actualBars = 3;
    record.result.candles.clear();
    record.result.chartSignals.clear();
    record.result.rawTicks.clear();
    for (int index = 0; index < config.actualBars; ++index) {
        RawTradeTick tick;
        tick.instrument = config.instrument;
        tick.tradingDate = config.from.date();
        tick.timestamp = config.from.addSecs(index * 60);
        tick.sourceLine = index + 1;
        tick.price = 1500.0 + index;
        tick.volumeShares = 100.0;
        tick.amount = tick.price * tick.volumeShares;
        tick.direction = RawTradeDirection::Neutral;
        tick.source = QStringLiteral("test-fixture");
        record.result.rawTicks.append(tick);
    }
    config.to = record.result.rawTicks.last().timestamp;
    config.dataVersion = computeBacktestDataVersion(
        record.result.rawTicks, config.instrument.symbol);
    record.result.equityCurve = {
        {config.from, 1000000.0, 1000000.0, 0.0, 0.0},
        {config.to, 1000100.0, 1000100.0, 0.0, 0.25}
    };
    record.result.equityObservationCount = config.actualBars;
    record.result.equityCurveSampled = true;

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(record));
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const auto restored = reopened.loadBacktestArchive(
        record.resultId, BacktestArchiveLoadMode::CompleteInputSnapshot);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.equityObservationCount, qint64(config.actualBars));
    QVERIFY(restored->result.equityCurveSampled);
    QCOMPARE(restored->result.equityCurve.size(), 2);
    QCOMPARE(restored->result.rawTicks.size(), config.actualBars);
    QVERIFY(restored->result.rawTickSnapshotAvailable);
    QVERIFY(restored->result.rawTickSnapshotLoaded);
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
            "create trigger reject_archive_candles before insert on backtest_candles "
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

void SqliteLocalDataStoreTest::legacyV17ArchiveRestoresWithoutInventedCandles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-v17.db"));
    const BacktestArchiveRecord original = archiveFixture(
        QStringLiteral("legacy-v17"), QStringLiteral("71"),
        QStringLiteral("sma_crossover"), 0, QDateTime::currentDateTimeUtc());
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(original));
        store.close();
    }

    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "delete from backtest_candles where result_id = 'legacy-v17'")));
        QVERIFY(query.exec(QStringLiteral(
            "update backtest_results set candle_series_version = 0 "
            "where result_id = 'legacy-v17'")));
        QVERIFY(query.exec(QStringLiteral("pragma user_version=17")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.schemaVersion(), 25);
    const auto restored = reopened.loadBacktestArchive(QStringLiteral("legacy-v17"));
    QVERIFY(restored.has_value());
    QVERIFY(restored->result.candles.isEmpty());
    QVERIFY(restored->result.chartSignals.isEmpty());
    QCOMPARE(restored->result.equityCurve.size(), 2);
    QCOMPARE(restored->result.trades.size(), 1);
}

void SqliteLocalDataStoreTest::incompleteV18CandleSnapshotIsRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("archive-incomplete-v18.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QVERIFY(store.saveBacktestArchive(archiveFixture(
        QStringLiteral("incomplete-v18"), QStringLiteral("72"),
        QStringLiteral("sma_crossover"), 0, QDateTime::currentDateTimeUtc())));

    const QString connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "delete from backtest_candles where result_id = 'incomplete-v18' "
            "and sequence_no = 1")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);

    QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);
    QVERIFY(!store.loadBacktestArchive(QStringLiteral("incomplete-v18")).has_value());
    QVERIFY(!failureSpy.isEmpty());
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

void SqliteLocalDataStoreTest::rawTickParameterSensitivityRoundTripsImmutableIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("sensitivity.db"))));

    RawTickParameterSensitivityResult record;
    record.resultId = QStringLiteral("raw-sensitivity-1");
    record.completedAt = QDateTime(QDate(2026, 8, 29), QTime(1, 2), Qt::UTC);
    record.strategyId = QStringLiteral("tick_momentum");
    record.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                         Market::AShare, QStringLiteral("上交所")};
    record.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                             {QStringLiteral("thresholdPercent"), 0.05}};
    record.trainingStartDate = QDate(2026, 8, 17);
    record.trainingEndDate = QDate(2026, 8, 20);
    record.validationStartDate = QDate(2026, 8, 21);
    record.validationEndDate = QDate(2026, 8, 24);
    record.trainingActualTicks = 32;
    record.validationActualTicks = 16;
    record.loadMilliseconds = 17;
    record.scanMilliseconds = 43;
    record.trainingDataVersion = QStringLiteral("sha256:") + QString(64, 'a');
    record.validationDataVersion = QStringLiteral("sha256:") + QString(64, 'b');
    record.datasetVersion = QStringLiteral("sha256:") + QString(64, 'c');
    record.candidateSetSha256 = QStringLiteral("sha256:") + QString(64, 'd');
    record.identitySha256 = QStringLiteral("sha256:") + QString(64, 'e');
    record.selectionCriterion =
        QStringLiteral("training_return_desc_drawdown_asc_parameters_v1");
    record.initialCapital = 1000000.0;
    record.commissionRate = 0.0003;
    record.slippageRate = 0.0002;
    record.lotSize = 100;
    record.selectedParameters = {{QStringLiteral("lookbackTicks"), 2},
                                 {QStringLiteral("thresholdPercent"), 0.05}};
    record.validationSummary = {1000000.0, 1010000.0, 1.0, 0.5,
                                50.0, 0.8, 20.0, 2, 1, 1};
    RawTickParameterSensitivityCandidate candidate;
    candidate.rank = 1;
    candidate.parameters = record.selectedParameters;
    candidate.trainingSummary = {1000000.0, 1020000.0, 2.0, 0.4,
                                 60.0, 1.0, 15.0, 3, 2, 1};
    record.candidates = {candidate};

    QString error;
    QVERIFY2(store.saveRawTickParameterSensitivityResult(record, &error),
             qPrintable(error));
    const auto restored = store.loadRawTickParameterSensitivityResults();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().resultId, record.resultId);
    QCOMPARE(restored.first().identitySha256, record.identitySha256);
    QCOMPARE(restored.first().datasetVersion, record.datasetVersion);
    QCOMPARE(restored.first().selectedParameters, record.selectedParameters);
    QCOMPARE(restored.first().candidates.size(), 1);
    QCOMPARE(restored.first().loadMilliseconds, 17);
    QCOMPARE(restored.first().scanMilliseconds, 43);
    QCOMPARE(restored.first().validationSummary.totalReturnPercent, 1.0);
    QVERIFY(store.saveRawTickParameterSensitivityResult(record, &error));

    RawTickParameterSensitivityResult duplicateIdentity = record;
    duplicateIdentity.resultId = QStringLiteral("sensitivity-duplicate-identity");
    QVERIFY(!store.saveRawTickParameterSensitivityResult(duplicateIdentity, &error));
    QVERIFY(error.contains(QStringLiteral("已归档")));

    RawTickParameterSensitivityResult conflict = record;
    conflict.identitySha256 = QStringLiteral("sha256:") + QString(64, 'f');
    QVERIFY(!store.saveRawTickParameterSensitivityResult(conflict, &error));
    QVERIFY(error.contains(QStringLiteral("身份不同")));
}

void SqliteLocalDataStoreTest::rawTickNestedWalkForwardArchiveRequiresAndRestoresAllFolds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("nested-incomplete.db"))));

    RawTickParameterSensitivityResult record;
    record.resultId = QStringLiteral("raw-nested-incomplete");
    record.completedAt = QDateTime(QDate(2026, 8, 29), QTime(2, 3), Qt::UTC);
    record.analysisMode = QStringLiteral("nested_walk_forward");
    record.walkForwardFoldCount = 2;
    record.strategyId = QStringLiteral("tick_momentum");
    record.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                         Market::AShare, QStringLiteral("上交所")};
    record.baseParameters = {{QStringLiteral("lookbackTicks"), 2}};
    record.trainingStartDate = QDate(2026, 8, 17);
    record.trainingEndDate = QDate(2026, 8, 20);
    record.validationStartDate = QDate(2026, 8, 21);
    record.validationEndDate = QDate(2026, 8, 24);
    record.trainingActualTicks = 32;
    record.validationActualTicks = 16;
    record.trainingDataVersion = QStringLiteral("sha256:") + QString(64, 'a');
    record.validationDataVersion = QStringLiteral("sha256:") + QString(64, 'b');
    record.datasetVersion = QStringLiteral("sha256:") + QString(64, 'c');
    record.candidateSetSha256 = QStringLiteral("sha256:") + QString(64, 'd');
    record.identitySha256 = QStringLiteral("sha256:") + QString(64, 'e');
    record.selectionCriterion =
        QStringLiteral("training_return_desc_drawdown_asc_parameters_v1");
    record.initialCapital = 1000000.0;
    record.commissionRate = 0.0003;
    record.slippageRate = 0.0002;
    record.lotSize = 100;
    record.selectedParameters = {{QStringLiteral("lookbackTicks"), 2}};
    RawTickParameterSensitivityCandidate candidate;
    candidate.rank = 1;
    candidate.parameters = record.selectedParameters;
    candidate.trainingSummary = {1000000.0, 1010000.0, 1.0, 0.5,
                                 50.0, 0.8, 20.0, 2, 1, 1};
    record.candidates = {candidate};
    record.validationSummary = candidate.trainingSummary;

    QString error;
    QVERIFY(!store.saveRawTickParameterSensitivityResult(record, &error));
    QVERIFY(error.contains(QStringLiteral("外层折")));
    QVERIFY(store.loadRawTickParameterSensitivityResults().isEmpty());

    RawTickParameterSensitivityFold firstFold;
    firstFold.foldNumber = 1;
    firstFold.trainingStartDate = QDate(2026, 8, 17);
    firstFold.trainingEndDate = QDate(2026, 8, 18);
    firstFold.validationStartDate = QDate(2026, 8, 19);
    firstFold.validationEndDate = QDate(2026, 8, 20);
    firstFold.trainingActualTicks = 16;
    firstFold.validationActualTicks = 16;
    firstFold.trainingDataVersion = QStringLiteral("sha256:") + QString(64, '1');
    firstFold.validationDataVersion = QStringLiteral("sha256:") + QString(64, '2');
    firstFold.selectedParameters = record.selectedParameters;
    firstFold.validationSummary = record.validationSummary;
    firstFold.candidates = {candidate};

    RawTickParameterSensitivityFold secondFold = firstFold;
    secondFold.foldNumber = 2;
    secondFold.trainingEndDate = firstFold.validationEndDate;
    secondFold.validationStartDate = QDate(2026, 8, 21);
    secondFold.validationEndDate = QDate(2026, 8, 24);
    secondFold.trainingActualTicks = 32;
    secondFold.trainingDataVersion = record.trainingDataVersion;
    secondFold.validationDataVersion = record.validationDataVersion;
    record.folds = {firstFold, secondFold};
    record.meanValidationReturnPercent = 1.0;
    record.medianValidationReturnPercent = 1.0;
    record.minimumValidationReturnPercent = 1.0;
    record.maximumValidationReturnPercent = 1.0;
    record.positiveValidationFoldCount = 2;
    record.parameterSelectionVariantCount = 1;
    record.parameterSelectionDistribution = {QVariantMap {
        {QStringLiteral("parameters"), record.selectedParameters},
        {QStringLiteral("count"), 2},
        {QStringLiteral("foldNumbers"), QVariantList {1, 2}}}};

    QVERIFY2(store.saveRawTickParameterSensitivityResult(record, &error),
             qPrintable(error));
    const auto restored = store.loadRawTickParameterSensitivityResults();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().analysisMode,
             QStringLiteral("nested_walk_forward"));
    QCOMPARE(restored.first().folds.size(), 2);
    QCOMPARE(restored.first().folds.at(0).validationEndDate,
             firstFold.validationEndDate);
    QCOMPARE(restored.first().folds.at(1).trainingDataVersion,
             secondFold.trainingDataVersion);
    QCOMPARE(restored.first().positiveValidationFoldCount, 2);
    QCOMPARE(restored.first().parameterSelectionVariantCount, 1);
}

void SqliteLocalDataStoreTest::rawTickParameterSensitivityRunsAsynchronouslyAndArchives()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw-sensitivity-service.db"));
    const Instrument instrument {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                                 Market::AShare, QStringLiteral("上交所")};
    const QVector<QDate> dates {
        QDate(2026, 8, 17), QDate(2026, 8, 18), QDate(2026, 8, 19),
        QDate(2026, 8, 20), QDate(2026, 8, 21), QDate(2026, 8, 24)};
    const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        for (int dayIndex = 0; dayIndex < dates.size(); ++dayIndex) {
            QVector<RawTradeTick> ticks;
            for (int tickIndex = 0; tickIndex < 8; ++tickIndex) {
                RawTradeTick tick;
                tick.instrument = instrument;
                tick.tradingDate = dates.at(dayIndex);
                tick.timestamp = QDateTime(dates.at(dayIndex), QTime(9, 30), shanghai)
                                     .addSecs(tickIndex).toUTC();
                tick.sourceLine = tickIndex + 1;
                const double direction = dayIndex < 4 ? 1.0 : -1.0;
                tick.price = 100.0 + dayIndex * 2.0
                    + direction * tickIndex * 0.4;
                tick.volumeShares = 100.0;
                tick.amount = tick.price * tick.volumeShares;
                ticks.append(tick);
            }
            QVERIFY(store.beginRawTradeTickImport(
                instrument, dates.at(dayIndex), QStringLiteral("fixture")));
            QVERIFY(store.appendRawTradeTicks(ticks));
            QVERIFY(store.commitRawTradeTickImport());
        }
    }

    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05, 5.0}}};
    request.validationTradingDays = 2;
    request.maximumCombinations = 16;
    request.baseConfig.instrument = instrument;
    request.baseConfig.initialCapital = 1000000.0;
    request.baseConfig.commissionRate = 0.0;
    request.baseConfig.slippageRate = 0.0;
    request.baseConfig.lotSize = 100;

    RawTickParameterSensitivityService service;
    QVERIFY(service.start(path));
    QSignalSpy completedSpy(&service,
                            &RawTickParameterSensitivityService::completed);
    QSignalSpy failedSpy(&service,
                         &RawTickParameterSensitivityService::failed);
    QVERIFY(service.request(77, request, dates.first(), dates.last()));
    QVERIFY(service.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(completedSpy.size() + failedSpy.size() > 0, 10000);
    QVERIFY2(failedSpy.isEmpty(), failedSpy.isEmpty()
        ? "" : qPrintable(failedSpy.first().at(1).toString()));
    QCOMPARE(completedSpy.size(), 1);
    QCOMPARE(completedSpy.first().at(0).toULongLong(), quint64(77));
    const auto result = qvariant_cast<RawTickParameterSensitivityResult>(
        completedSpy.first().at(1));
    QVERIFY(!result.resultId.isEmpty());
    QCOMPARE(result.trainingStartDate, dates.at(0));
    QCOMPARE(result.trainingEndDate, dates.at(3));
    QCOMPARE(result.validationStartDate, dates.at(4));
    QCOMPARE(result.validationEndDate, dates.at(5));
    QVERIFY(result.datasetVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(!service.isBusy());

    completedSpy.clear();
    QVERIFY(service.request(78, request, dates.first(), dates.last()));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 10000);
    QCOMPARE(completedSpy.first().at(0).toULongLong(), quint64(78));
    const auto reused = qvariant_cast<RawTickParameterSensitivityResult>(
        completedSpy.first().at(1));
    QCOMPARE(reused.resultId, result.resultId);

    completedSpy.clear();
    request.walkForwardFoldCount = 2;
    QVERIFY(service.request(79, request, dates.first(), dates.last()));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 10000);
    QCOMPARE(failedSpy.size(), 0);
    const auto nested = qvariant_cast<RawTickParameterSensitivityResult>(
        completedSpy.first().at(1));
    QCOMPARE(nested.analysisMode, QStringLiteral("nested_walk_forward"));
    QCOMPARE(nested.walkForwardFoldCount, 2);
    QCOMPARE(nested.folds.size(), 2);
    QVERIFY(nested.folds.at(0).trainingEndDate
            < nested.folds.at(0).validationStartDate);
    QVERIFY(nested.folds.at(1).trainingEndDate
            < nested.folds.at(1).validationStartDate);
    QVERIFY(nested.resultId != result.resultId);
    service.stop();

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const auto restored = reopened.loadRawTickParameterSensitivityResults();
    QCOMPARE(restored.size(), 2);
    QVERIFY(std::any_of(restored.cbegin(), restored.cend(),
                        [&](const RawTickParameterSensitivityResult &record) {
        return record.resultId == result.resultId
            && record.identitySha256 == result.identitySha256;
    }));
    QVERIFY(std::any_of(restored.cbegin(), restored.cend(),
                        [&](const RawTickParameterSensitivityResult &record) {
        return record.resultId == nested.resultId
            && record.identitySha256 == nested.identitySha256
            && record.folds.size() == 2;
    }));
}

void SqliteLocalDataStoreTest::rawTickParameterSensitivityKeepsOnlyOneTradingDayResident()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("raw-sensitivity-streamed.db"));
    const Instrument instrument {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                                 Market::AShare, QStringLiteral("上交所")};
    const QVector<QDate> dates {
        QDate(2026, 8, 11), QDate(2026, 8, 12), QDate(2026, 8, 13),
        QDate(2026, 8, 14), QDate(2026, 8, 17), QDate(2026, 8, 18),
        QDate(2026, 8, 19), QDate(2026, 8, 20)};
    const QVector<int> dailyCounts {9, 12, 15, 18, 21, 24, 27, 30};
    const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        for (int dayIndex = 0; dayIndex < dates.size(); ++dayIndex) {
            QVector<RawTradeTick> ticks;
            for (int tickIndex = 0; tickIndex < dailyCounts.at(dayIndex);
                 ++tickIndex) {
                RawTradeTick tick;
                tick.instrument = instrument;
                tick.tradingDate = dates.at(dayIndex);
                tick.timestamp = QDateTime(dates.at(dayIndex), QTime(9, 30), shanghai)
                                     .addSecs(tickIndex).toUTC();
                tick.sourceLine = tickIndex + 1;
                tick.price = 100.0 + dayIndex * 0.5 + tickIndex * 0.02;
                tick.volumeShares = 100.0;
                tick.amount = tick.price * tick.volumeShares;
                ticks.append(tick);
            }
            QVERIFY(store.beginRawTradeTickImport(
                instrument, dates.at(dayIndex), QStringLiteral("fixture")));
            QVERIFY(store.appendRawTradeTicks(ticks));
            QVERIFY(store.commitRawTradeTickImport());
        }
    }

    RawTickParameterSensitivityRequest request;
    request.strategyId = QStringLiteral("tick_momentum");
    request.baseParameters = {{QStringLiteral("lookbackTicks"), 2},
                              {QStringLiteral("thresholdPercent"), 0.05},
                              {QStringLiteral("cooldownSeconds"), 0},
                              {QStringLiteral("minimumHoldSeconds"), 0}};
    request.axes = {{QStringLiteral("thresholdPercent"), {0.05, 5.0}}};
    request.validationTradingDays = 2;
    request.walkForwardFoldCount = 3;
    request.maximumCombinations = 16;
    request.baseConfig.instrument = instrument;
    request.baseConfig.initialCapital = 1000000.0;
    request.baseConfig.commissionRate = 0.0;
    request.baseConfig.slippageRate = 0.0;
    request.baseConfig.lotSize = 100;

    RawTickParameterSensitivityService service;
    QVERIFY(service.start(path));
    QSignalSpy completedSpy(&service,
                            &RawTickParameterSensitivityService::completed);
    QSignalSpy failedSpy(&service,
                         &RawTickParameterSensitivityService::failed);
    QSignalSpy residencySpy(
        &service, SIGNAL(rawTickResidencyChanged(quint64,int)));
    QVERIFY2(residencySpy.isValid(),
             "参数敏感度服务必须公开逐日 Tick 驻留量观测信号");

    QVERIFY(service.request(81, request, dates.first(), dates.last()));
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.size(), 1, 15000);
    QCOMPARE(failedSpy.size(), 0);
    const auto result = qvariant_cast<RawTickParameterSensitivityResult>(
        completedSpy.first().at(1));
    QCOMPARE(result.folds.size(), 3);
    QCOMPARE(result.folds.at(0).trainingActualTicks, 21);
    QCOMPARE(result.folds.at(0).validationActualTicks, 33);
    QCOMPARE(result.folds.at(1).trainingActualTicks, 54);
    QCOMPARE(result.folds.at(1).validationActualTicks, 45);
    QCOMPARE(result.folds.at(2).trainingActualTicks, 99);
    QCOMPARE(result.folds.at(2).validationActualTicks, 57);

    int peakResident = 0;
    int finalResident = -1;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(0).toULongLong(), quint64(81));
        finalResident = arguments.at(1).toInt();
        peakResident = std::max(peakResident, finalResident);
    }
    QCOMPARE(peakResident, dailyCounts.last());
    QCOMPARE(finalResident, 0);
    QVERIFY(peakResident < 156);

    RawTickParameterSensitivityRequest cancelledRequest = request;
    cancelledRequest.maximumCombinations = 64;
    QVector<double> thresholds;
    for (int index = 1; index <= 64; ++index)
        thresholds.append(index * 0.01);
    cancelledRequest.axes = {
        {QStringLiteral("thresholdPercent"), thresholds}};
    residencySpy.clear();
    completedSpy.clear();
    QVERIFY(service.request(82, cancelledRequest, dates.first(), dates.last()));
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(
        residencySpy.cbegin(), residencySpy.cend(),
        [](const QList<QVariant> &arguments) {
            return arguments.at(0).toULongLong() == quint64(82)
                && arguments.at(1).toInt() > 0;
        }), 5000);
    service.stop();
    QVERIFY(!service.isBusy());
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(
        residencySpy.cbegin(), residencySpy.cend(),
        [](const QList<QVariant> &arguments) {
            return arguments.at(0).toULongLong() == quint64(82)
                && arguments.at(1).toInt() == 0;
        }), 5000);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::SqliteLocalDataStoreTest)

#include "tst_sqlite_local_data_store.moc"
