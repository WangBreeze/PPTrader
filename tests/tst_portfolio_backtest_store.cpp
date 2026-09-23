#include "services/PortfolioBacktestEngine.h"
#include "services/PortfolioBacktestService.h"
#include "services/SqliteLocalDataStore.h"
#include "models/FactorResearchComparisonListModel.h"
#include "domain/FactorCandidateTypes.h"

#include <QDate>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace pp {

namespace {

Instrument instrumentFor(const QString &symbol)
{
    Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = Market::AShare;
    instrument.venue = QStringLiteral("XSHG");
    return instrument;
}

QVector<Candle> seriesFor(const QString &symbol,
                          const QVector<QDate> &days,
                          const QVector<double> &closes)
{
    QVector<Candle> candles;
    static const QTimeZone shanghai("Asia/Shanghai");
    for (int index = 0; index < days.size(); ++index) {
        Candle candle;
        candle.instrument = instrumentFor(symbol);
        candle.startTime = QDateTime(days.at(index), QTime(15, 0), shanghai)
                               .toUTC();
        candle.periodSeconds = 60;
        candle.open = candle.high = candle.low = candle.close = closes.at(index);
        candle.volume = 100000.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    return candles;
}

PortfolioDefinition definition()
{
    PortfolioDefinition value;
    value.portfolioId = QStringLiteral("portfolio_test");
    value.name = QStringLiteral("测试组合");
    value.periodSeconds = 60;
    value.initialCapital = 1000000.0;
    value.legs = {
        {instrumentFor(QStringLiteral("600001")), 0.0},
        {instrumentFor(QStringLiteral("600002")), 0.0}
    };
    return value;
}

PortfolioResult makeResult()
{
    const QVector<QDate> days{QDate(2026, 1, 5), QDate(2026, 1, 6)};
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"), seriesFor(QStringLiteral("600001"),
                                             days, {100.0, 110.0})},
        {QStringLiteral("600002"), seriesFor(QStringLiteral("600002"),
                                             {days.first()}, {100.0})}};
    PortfolioResult result = PortfolioBacktestEngine::run(definition(), series);
    result.datasetVersions = {
        QStringLiteral("sha256:") + QString(64, 'a'),
        QStringLiteral("sha256:") + QString(64, 'b')};
    return result;
}

BacktestDatasetIdentity datasetIdentityFor(const QString &symbol,
                                           const QChar fingerprintFill)
{
    BacktestDatasetIdentity identity;
    identity.instrument = instrumentFor(symbol);
    identity.periodSeconds = 60;
    identity.source = QStringLiteral("raw_tick_cache_v1");
    identity.aggregatorVersion = 1;
    identity.fingerprintVersion = 2;
    identity.firstTradingDate = QDate(2026, 1, 5);
    identity.lastTradingDate = QDate(2026, 1, 6);
    identity.expectedTradingDays = 2;
    identity.calendarCoverageKnown = true;
    identity.calendarSource = QStringLiteral("test_fixture");
    identity.days = {
        {QDate(2026, 1, 5), 120, 120,
         QStringLiteral("sha256:") + QString(64, fingerprintFill), 48, 1},
        {QDate(2026, 1, 6), 130, 250,
         QStringLiteral("sha256:") + QString(64, fingerprintFill), 49, 1}};
    identity.datasetVersion =
        QStringLiteral("sha256:") + QString(64, fingerprintFill);
    return identity;
}

AShareIndustryMembership industryMembership(
    const QString &symbol, const QString &industryCode,
    const QString &industryName, const QDate &effectiveFrom,
    const QDate &effectiveTo = {})
{
    AShareIndustryMembership membership;
    membership.instrument = instrumentFor(symbol);
    membership.industryCode = industryCode;
    membership.industryName = industryName;
    membership.effectiveFrom = effectiveFrom;
    membership.effectiveTo = effectiveTo;
    return membership;
}

} // namespace

class PortfolioBacktestStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void initializedServiceIsReadyButNotRunning();
    void archiveRoundTripAndMigrationVersion();
    void crossSectionalArchiveRoundTripsImmutableIdentityAndLegDatasets();
    void crossSectionalArchiveRejectsIncompleteIdentity();
    void crossSectionalArchiveRejectsCorruptRestoredIdentity();
    void archiveRejectsInvalidResult();
    void industryDatasetRoundTripAndDateResolution();
    void industryDatasetRejectsOverlappingPeriodsAtomically();
    void industryConstrainedArchiveRequiresAndRestoresIdentity();
    void factorConstrainedArchiveRequiresAndRestoresIdentity();
    void factorDefinitionRegistryVersionsByImmutableContent();
    void factorCandidateRegistryRoundTripAndEvaluationAudit();
    void authorizedFactorDatasetRoundTripsAcrossRestart();
    void authorizedFactorDatasetBindingRequiresFrozenMatchingCandidate();
    void invalidAuthorizedFactorObservationLeavesCatalogUnchanged();
    void externalFactorResearchArchiveRoundTripsImmutableIdentity();
};

void PortfolioBacktestStoreTest::initializedServiceIsReadyButNotRunning()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PortfolioBacktestService service;
    QVERIFY(service.start(directory.filePath(QStringLiteral("service.db"))));
    QVERIFY(service.isReady());
    QVERIFY(!service.isRunning());
}

void PortfolioBacktestStoreTest::archiveRoundTripAndMigrationVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("portfolio.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 25);

    const PortfolioResult result = makeResult();
    PortfolioArchiveRecord record;
    record.resultId = QStringLiteral("portfolio-archive-1");
    record.completedAt = QDateTime::currentDateTimeUtc();
    record.result = result;
    QVERIFY(store.savePortfolioBacktestArchive(record));

    const QVector<PortfolioArchiveSummary> summaries =
        store.loadPortfolioBacktestArchiveSummaries();
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(summaries.first().resultId, QStringLiteral("portfolio-archive-1"));
    QCOMPARE(summaries.first().legCount, 2);
    QCOMPARE(summaries.first().periodSeconds, 60);
    QCOMPARE(summaries.first().barCount, result.equityCurve.size());
    QCOMPARE(summaries.first().datasetVersions, result.datasetVersions);
    QCOMPARE(summaries.first().summary.totalTrades, result.summary.totalTrades);
    QVERIFY(!summaries.first().toVariantMap()
                .value(QStringLiteral("resultId")).toString().isEmpty());

    const auto restored = store.loadPortfolioBacktestArchive(
        QStringLiteral("portfolio-archive-1"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.definition.portfolioId,
             QStringLiteral("portfolio_test"));
    QCOMPARE(restored->result.definition.legs.size(), 2);
    QCOMPARE(restored->result.definition.legs.first().instrument.symbol,
             QStringLiteral("600001"));
    QCOMPARE(restored->result.periodSeconds, 60);
    QCOMPARE(restored->result.equityCurve.size(), result.equityCurve.size());
    QCOMPARE(restored->result.equityCurve.first().equity,
             result.equityCurve.first().equity);
    QCOMPARE(restored->result.equityCurve.first().positions.size(), 2);
    QVERIFY(result.equityCurve.last().positions.at(1).priceStale);
    QVERIFY(restored->result.equityCurve.last().positions.at(1).priceStale);
    QCOMPARE(result.equityCurve.last().positions.at(1).marketStatus,
             QStringLiteral("missing_unknown"));
    QCOMPARE(restored->result.equityCurve.last().positions.at(1).marketStatus,
             QStringLiteral("missing_unknown"));
    QCOMPARE(restored->result.trades.size(), result.trades.size());
    QCOMPARE(restored->result.trades.first().side,
             result.trades.first().side);
    QCOMPARE(restored->result.datasetVersions, result.datasetVersions);
}

void PortfolioBacktestStoreTest::crossSectionalArchiveRejectsIncompleteIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(
        directory.filePath(QStringLiteral("incomplete-cross.db"))));
    QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);

    PortfolioResult result = makeResult();
    result.resultKind = QStringLiteral("cross_sectional");
    result.immutableIdentity = {
        {QStringLiteral("strategyId"), QStringLiteral("missing_legs")},
        {QStringLiteral("strategyVersion"), 1},
        {QStringLiteral("definitionJson"), QStringLiteral("{}")}};
    PortfolioArchiveRecord record;
    record.resultId = QStringLiteral("incomplete-cross");
    record.completedAt = QDateTime::currentDateTimeUtc();
    record.result = result;

    QString archiveError;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &archiveError));
    QVERIFY(archiveError.contains(QStringLiteral("身份")));
    QVERIFY(!failureSpy.isEmpty());
    QVERIFY(store.loadPortfolioBacktestArchiveSummaries().isEmpty());
}

void PortfolioBacktestStoreTest::
    crossSectionalArchiveRejectsCorruptRestoredIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("corrupt-cross.db"));
    PortfolioResult result = makeResult();
    result.resultKind = QStringLiteral("cross_sectional");
    result.datasetIdentities = {
        datasetIdentityFor(QStringLiteral("600001"), 'a'),
        datasetIdentityFor(QStringLiteral("600002"), 'b')};
    result.immutableIdentity = {
        {QStringLiteral("strategyId"), QStringLiteral("cross_corrupt")},
        {QStringLiteral("strategyVersion"), 1},
        {QStringLiteral("definitionJson"), QStringLiteral("{}")},
        {QStringLiteral("requestedBars"), 2},
        {QStringLiteral("actualBars"), 2}};
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.savePortfolioBacktestArchive(
            {QStringLiteral("corrupt-cross"),
             QDateTime::currentDateTimeUtc(), result}));
    }

    const QString connectionName = QStringLiteral("corrupt_cross_fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
            "update portfolio_backtest_results set dataset_identities_json='[]' "
            "where result_id='corrupt-cross'")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QSignalSpy failureSpy(&reopened, &SqliteLocalDataStore::writeFailed);
    QVERIFY(!reopened.loadPortfolioBacktestArchive(
        QStringLiteral("corrupt-cross")).has_value());
    QVERIFY(!failureSpy.isEmpty());
}

void PortfolioBacktestStoreTest::
    crossSectionalArchiveRoundTripsImmutableIdentityAndLegDatasets()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cross-sectional.db"));

    PortfolioResult result = makeResult();
    result.resultKind = QStringLiteral("cross_sectional");
    result.datasetIdentities = {
        datasetIdentityFor(QStringLiteral("600001"), 'a'),
        datasetIdentityFor(QStringLiteral("600002"), 'b')};
    result.riskSummary.constrainedFrameCount = 2;
    result.riskSummary.maximumObservedLegWeight = 0.30;
    result.riskSummary.completedMissingRiskExitCount = 1;
    result.definition.maximumPortfolioLossPercent = 12.5;
    result.definition.maximumPortfolioDrawdownPercent = 8.0;
    result.riskSummary.maximumObservedLossPercent = 13.0;
    result.riskSummary.maximumObservedDrawdownPercent = 9.0;
    result.riskSummary.circuitBreakerTriggered = true;
    result.riskSummary.circuitBreakerReason = QStringLiteral("loss_limit");
    result.riskSummary.circuitBreakerTriggeredAt =
        QDateTime(QDate(2026, 8, 20), QTime(10, 15), Qt::UTC);
    result.riskSummary.circuitBreakerEquity = 870000.0;
    result.riskSummary.circuitBreakerLossPercent = 13.0;
    result.riskSummary.circuitBreakerDrawdownPercent = 9.0;
    result.riskSummary.completedCircuitBreakerExitCount = 2;
    result.riskSummary.authoritativeSuspensionPointCount = 3;
    result.riskSummary.unknownMissingQuotePointCount = 4;
    result.riskSummary.marketCalendarCoverageComplete = true;
    result.riskSummary.instrumentStatusCoverageComplete = false;
    result.riskSummary.calendarSnapshotVersion =
        QStringLiteral("sha256:") + QString(64, 'c');
    result.definition.maximumIndustryWeight = 0.50;
    result.definition.industryDatasetId =
        QStringLiteral("industry-cross-archive-v1");
    result.riskSummary.industryCoverageComplete = true;
    result.riskSummary.industryConstrainedGroupCount = 1;
    result.riskSummary.maximumObservedIndustryWeight = 0.49;
    result.riskSummary.industrySnapshotVersion =
        QStringLiteral("sha256:") + QString(64, 'd');
    result.immutableIdentity = {
        {QStringLiteral("strategyId"), QStringLiteral("cross_momentum")},
        {QStringLiteral("strategyVersion"), 3},
        {QStringLiteral("normalizedParameters"),
         QVariantMap{{QStringLiteral("lookback"), 20}}},
        {QStringLiteral("ruleSnapshot"),
         QVariantList{QVariantMap{{QStringLiteral("type"),
                                  QStringLiteral("rank")}}}},
        {QStringLiteral("definitionJson"),
         QStringLiteral("{\"schema_version\":3,\"strategy_id\":\"cross_momentum\"}")},
        {QStringLiteral("requestedBars"), 500},
        {QStringLiteral("actualBars"), result.equityCurve.size()},
        {QStringLiteral("industrySnapshot"),
         QVariantMap{
             {QStringLiteral("datasetId"),
              result.definition.industryDatasetId},
             {QStringLiteral("snapshotVersion"),
              result.riskSummary.industrySnapshotVersion},
             {QStringLiteral("asOfDate"), QDate(2026, 8, 20)},
             {QStringLiteral("coverageComplete"), true}}},
        {QStringLiteral("riskSummary"), result.riskSummary.toVariantMap()}};

    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        PortfolioArchiveRecord record;
        record.resultId = QStringLiteral("cross-sectional-archive-1");
        record.completedAt = QDateTime::currentDateTimeUtc();
        record.result = result;
        QVERIFY(store.savePortfolioBacktestArchive(record));
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const QVector<PortfolioArchiveSummary> summaries =
        reopened.loadPortfolioBacktestArchiveSummaries();
    QCOMPARE(summaries.size(), 1);
    QCOMPARE(summaries.first().resultKind, QStringLiteral("cross_sectional"));
    QCOMPARE(summaries.first().strategyId, QStringLiteral("cross_momentum"));
    QCOMPARE(summaries.first().strategyVersion, 3);

    const auto restored = reopened.loadPortfolioBacktestArchive(
        QStringLiteral("cross-sectional-archive-1"));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.resultKind, QStringLiteral("cross_sectional"));
    QCOMPARE(restored->result.immutableIdentity
                 .value(QStringLiteral("strategyId")).toString(),
             QStringLiteral("cross_momentum"));
    QCOMPARE(restored->result.immutableIdentity
                 .value(QStringLiteral("strategyVersion")).toInt(), 3);
    const QVariantMap restoredIdentityRisk = restored->result.immutableIdentity
        .value(QStringLiteral("riskSummary")).toMap();
    QVERIFY(restoredIdentityRisk
                .value(QStringLiteral("circuitBreakerTriggered")).toBool());
    QCOMPARE(restoredIdentityRisk
                 .value(QStringLiteral("circuitBreakerReason")).toString(),
             QStringLiteral("loss_limit"));
    QVERIFY(restoredIdentityRisk
                .value(QStringLiteral("circuitBreakerTriggeredAt"))
                .toDateTime().isValid());
    QCOMPARE(restored->result.datasetIdentities.size(), 2);
    QCOMPARE(restored->result.datasetIdentities.first().instrument.symbol,
             QStringLiteral("600001"));
    QCOMPARE(restored->result.datasetIdentities.first().days.size(), 2);
    QCOMPARE(restored->result.datasetIdentities.first().datasetVersion,
             result.datasetIdentities.first().datasetVersion);
    QCOMPARE(restored->result.datasetIdentities.last().instrument.symbol,
             QStringLiteral("600002"));
    QCOMPARE(restored->result.definition.maximumIndustryWeight, 0.50);
    QCOMPARE(restored->result.definition.industryDatasetId,
             QStringLiteral("industry-cross-archive-v1"));
    QVERIFY(restored->result.riskSummary.industryCoverageComplete);
    QCOMPARE(restored->result.riskSummary.industrySnapshotVersion,
             result.riskSummary.industrySnapshotVersion);
    QCOMPARE(restored->result.immutableIdentity
                 .value(QStringLiteral("industrySnapshot")).toMap()
                 .value(QStringLiteral("asOfDate")).toDate(),
             QDate(2026, 8, 20));
    QCOMPARE(restored->result.riskSummary.constrainedFrameCount, 2);
    QCOMPARE(restored->result.riskSummary.maximumObservedLegWeight, 0.30);
    QCOMPARE(restored->result.riskSummary.completedMissingRiskExitCount, 1);
    QCOMPARE(restored->result.definition.maximumPortfolioLossPercent, 12.5);
    QCOMPARE(restored->result.definition.maximumPortfolioDrawdownPercent, 8.0);
    QVERIFY(restored->result.riskSummary.circuitBreakerTriggered);
    QCOMPARE(restored->result.riskSummary.circuitBreakerReason,
             QStringLiteral("loss_limit"));
    QCOMPARE(restored->result.riskSummary.circuitBreakerEquity, 870000.0);
    QCOMPARE(restored->result.riskSummary.completedCircuitBreakerExitCount, 2);
    QCOMPARE(restored->result.riskSummary.authoritativeSuspensionPointCount, 3);
    QCOMPARE(restored->result.riskSummary.unknownMissingQuotePointCount, 4);
    QVERIFY(restored->result.riskSummary.marketCalendarCoverageComplete);
    QVERIFY(!restored->result.riskSummary.instrumentStatusCoverageComplete);
    QCOMPARE(restored->result.riskSummary.calendarSnapshotVersion,
             QStringLiteral("sha256:") + QString(64, 'c'));
    QVERIFY(restored->result.riskSummary.circuitBreakerTriggeredAt.isValid());
}

void PortfolioBacktestStoreTest::industryDatasetRoundTripAndDateResolution()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("industry.db"));
    QString datasetId;
    QString contentSha256;
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QCOMPARE(store.schemaVersion(), 25);
        AShareIndustryDataset dataset;
        dataset.classification = QStringLiteral("test-classification");
        dataset.source = QStringLiteral("test-fixture");
        dataset.sourceVersion = QStringLiteral("2026-v1");
        dataset.memberships = {
            industryMembership(QStringLiteral("600001"),
                               QStringLiteral("TECH"), QStringLiteral("科技"),
                               QDate(2025, 1, 1), QDate(2025, 12, 31)),
            industryMembership(QStringLiteral("600001"),
                               QStringLiteral("FIN"), QStringLiteral("金融"),
                               QDate(2026, 1, 1)),
            industryMembership(QStringLiteral("600002"),
                               QStringLiteral("BANK"), QStringLiteral("银行"),
                               QDate(2025, 1, 1))};
        bool reused = true;
        QString error;
        QVERIFY2(store.saveAShareIndustryDataset(dataset, &reused, &error),
                 qPrintable(error));
        QVERIFY(!reused);
        QVERIFY(!dataset.datasetId.isEmpty());
        QVERIFY(dataset.contentSha256.startsWith(QStringLiteral("sha256:")));
        datasetId = dataset.datasetId;
        contentSha256 = dataset.contentSha256;

        AShareIndustryDataset duplicate = dataset;
        duplicate.datasetId.clear();
        duplicate.importedAt = {};
        QVERIFY(store.saveAShareIndustryDataset(duplicate, &reused, &error));
        QVERIFY(reused);
        QCOMPARE(duplicate.datasetId, datasetId);

        const auto oldSnapshot = store.loadAShareIndustrySnapshot(
            datasetId, QDate(2025, 6, 1),
            {QStringLiteral("600001"), QStringLiteral("600002")});
        QVERIFY(oldSnapshot.has_value());
        QVERIFY(oldSnapshot->coverageComplete);
        QCOMPARE(oldSnapshot->industryCodeFor(QStringLiteral("600001")),
                 QStringLiteral("TECH"));
        QCOMPARE(oldSnapshot->datasetContentSha256, contentSha256);
        QVERIFY(oldSnapshot->snapshotVersion.startsWith(
            QStringLiteral("sha256:")));

        const auto newSnapshot = store.loadAShareIndustrySnapshot(
            datasetId, QDate(2026, 6, 1),
            {QStringLiteral("600001"), QStringLiteral("600002")});
        QVERIFY(newSnapshot.has_value());
        QCOMPARE(newSnapshot->industryCodeFor(QStringLiteral("600001")),
                 QStringLiteral("FIN"));
        QVERIFY(newSnapshot->snapshotVersion != oldSnapshot->snapshotVersion);

        const auto incomplete = store.loadAShareIndustrySnapshot(
            datasetId, QDate(2026, 6, 1),
            {QStringLiteral("600001"), QStringLiteral("600003")});
        QVERIFY(incomplete.has_value());
        QVERIFY(!incomplete->coverageComplete);
        QCOMPARE(incomplete->resolvedSymbols, 1);
        QCOMPARE(incomplete->expectedSymbols, 2);
    }

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    const auto restored = reopened.loadAShareIndustrySnapshot(
        datasetId, QDate(2026, 6, 1),
        {QStringLiteral("600001"), QStringLiteral("600002")});
    QVERIFY(restored.has_value());
    QVERIFY(restored->coverageComplete);
    QCOMPARE(restored->datasetContentSha256, contentSha256);
}

void PortfolioBacktestStoreTest::
    industryDatasetRejectsOverlappingPeriodsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("overlap.db"))));
    AShareIndustryDataset dataset;
    dataset.datasetId = QStringLiteral("overlap-fixture");
    dataset.classification = QStringLiteral("test-classification");
    dataset.source = QStringLiteral("test-fixture");
    dataset.sourceVersion = QStringLiteral("overlap-v1");
    dataset.memberships = {
        industryMembership(QStringLiteral("600001"),
                           QStringLiteral("TECH"), QStringLiteral("科技"),
                           QDate(2025, 1, 1), QDate(2025, 12, 31)),
        industryMembership(QStringLiteral("600001"),
                           QStringLiteral("FIN"), QStringLiteral("金融"),
                           QDate(2025, 12, 31))};
    QString error;
    QVERIFY(!store.saveAShareIndustryDataset(dataset, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("重叠")));
    QVERIFY(!store.loadAShareIndustrySnapshot(
        QStringLiteral("overlap-fixture"), QDate(2025, 6, 1),
        {QStringLiteral("600001")}).has_value());
}

void PortfolioBacktestStoreTest::
    industryConstrainedArchiveRequiresAndRestoresIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(
        directory.filePath(QStringLiteral("industry-archive.db"))));

    PortfolioResult result = makeResult();
    result.definition.maximumIndustryWeight = 0.5;
    result.definition.industryDatasetId = QStringLiteral("industry-fixture-v1");
    PortfolioArchiveRecord record{
        QStringLiteral("industry-archive"),
        QDateTime::currentDateTimeUtc(), result};
    QString error;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &error));
    QVERIFY(error.contains(QStringLiteral("行业约束身份")));

    const QString snapshotVersion =
        QStringLiteral("sha256:") + QString(64, 'b');
    result.riskSummary.industryCoverageComplete = true;
    result.riskSummary.industrySnapshotVersion = snapshotVersion;
    result.immutableIdentity.insert(
        QStringLiteral("industrySnapshot"),
        QVariantMap{{QStringLiteral("datasetId"),
                     result.definition.industryDatasetId},
                    {QStringLiteral("snapshotVersion"), snapshotVersion},
                    {QStringLiteral("coverageComplete"), true}});
    result.immutableIdentity.insert(QStringLiteral("riskSummary"),
                                    result.riskSummary.toVariantMap());
    record.result = result;
    QVERIFY2(store.savePortfolioBacktestArchive(record, &error),
             qPrintable(error));
    const auto restored = store.loadPortfolioBacktestArchive(record.resultId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.definition.maximumIndustryWeight, 0.5);
    QCOMPARE(restored->result.definition.industryDatasetId,
             QStringLiteral("industry-fixture-v1"));
    QCOMPARE(restored->result.riskSummary.industrySnapshotVersion,
             snapshotVersion);
    QCOMPARE(restored->result.immutableIdentity
                 .value(QStringLiteral("industrySnapshot")).toMap()
                 .value(QStringLiteral("datasetId")).toString(),
             QStringLiteral("industry-fixture-v1"));
}

void PortfolioBacktestStoreTest::
    factorConstrainedArchiveRequiresAndRestoresIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(
        directory.filePath(QStringLiteral("factor-archive.db"))));

    PortfolioResult result = makeResult();
    result.resultKind = QStringLiteral("cross_sectional");
    result.datasetIdentities = {
        datasetIdentityFor(QStringLiteral("600001"), 'a'),
        datasetIdentityFor(QStringLiteral("600002"), 'b')};
    result.definition.factorExposureAlias = QStringLiteral("close_rank");
    result.definition.maximumAbsoluteFactorExposure = 0.30;
    result.riskSummary.factorExposureAlias = QStringLiteral("close_rank");
    result.riskSummary.factorExposureCoverageComplete = true;
    result.riskSummary.factorExposureConstrainedFrameCount = 2;
    result.riskSummary.maximumRequestedAbsoluteFactorExposure = 0.62;
    result.riskSummary.maximumObservedAbsoluteFactorExposure = 0.29;
    result.immutableIdentity = {
        {QStringLiteral("strategyId"), QStringLiteral("factor_cross")},
        {QStringLiteral("strategyVersion"), 1},
        {QStringLiteral("definitionJson"), QStringLiteral("{}")},
        {QStringLiteral("requestedBars"), 2},
        {QStringLiteral("actualBars"), 2}};
    PortfolioArchiveRecord record{
        QStringLiteral("factor-archive"),
        QDateTime::currentDateTimeUtc(), result};
    QString error;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &error));
    QVERIFY(error.contains(QStringLiteral("因子暴露约束身份")));

    const QVariantMap factorDefinition{
        {QStringLiteral("alias"), QStringLiteral("close_rank")},
        {QStringLiteral("indicator"), QStringLiteral("RANK")},
        {QStringLiteral("of"),
         QVariantMap{{QStringLiteral("field"), QStringLiteral("close")}}}};
    const QByteArray definitionJson = QJsonDocument::fromVariant(factorDefinition)
        .toJson(QJsonDocument::Compact);
    const QString definitionSha256 = QStringLiteral("sha256:%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            definitionJson, QCryptographicHash::Sha256).toHex()));
    result.immutableIdentity.insert(
        QStringLiteral("factorExposure"),
        QVariantMap{
            {QStringLiteral("alias"), QStringLiteral("close_rank")},
            {QStringLiteral("definition"), factorDefinition},
            {QStringLiteral("definitionSha256"), definitionSha256},
            {QStringLiteral("standardization"),
             QStringLiteral("population_zscore")},
            {QStringLiteral("weighting"),
             QStringLiteral("portfolio_equity_including_cash")},
            {QStringLiteral("limitPolicy"),
             QStringLiteral("proportional_de_risk_to_cash")}});
    FactorDefinitionVersion researchDefinition;
    researchDefinition.factorId = QStringLiteral("factor_cross.close_rank");
    researchDefinition.name = QStringLiteral("测试因子");
    researchDefinition.alias = QStringLiteral("close_rank");
    researchDefinition.sourceStrategyId = QStringLiteral("factor_cross");
    researchDefinition.sourceStrategyVersion = 1;
    researchDefinition.definition = factorDefinition;
    result.definition.factorResearchAlias = QStringLiteral("close_rank");
    result.definition.factorDefinitionId = researchDefinition.factorId;
    result.definition.factorDefinitionVersion = 1;
    result.definition.factorDefinitionSha256 =
        researchDefinition.computedContentSha256();
    result.definition.factorResearchCost.oneWayCostBps = 10.0;
    result.factorResearchSummary.factorId = researchDefinition.factorId;
    result.factorResearchSummary.factorVersion = 1;
    result.factorResearchSummary.factorAlias = QStringLiteral("close_rank");
    result.factorResearchSummary.evaluatedFrameCount = 1;
    result.factorResearchSummary.validPairCount = 3;
    result.factorResearchSummary.meanIc = 0.42;
    result.factorResearchSummary.icStandardDeviation = 0.21;
    result.factorResearchSummary.icInformationRatio = 2.0;
    result.factorResearchSummary.icirAvailable = true;
    result.factorResearchSummary.costPolicy = result.definition.factorResearchCost;
    result.factorResearchSummary.timeSeries = {
        {QDateTime::fromString(QStringLiteral("2026-08-20T01:35:00.000Z"),
                               Qt::ISODateWithMs),
         QDateTime::fromString(QStringLiteral("2026-08-20T01:40:00.000Z"),
                               Qt::ISODateWithMs),
         3, 3, 0.42, 0.50}};
    result.factorResearchSummary.quantileReturns = {
        {1, 1, -0.01}, {2, 1, 0.00}, {3, 1, 0.01},
        {4, 1, 0.02}, {5, 1, 0.03}};
    result.factorResearchSummary.topMinusBottomMeanReturn = 0.04;
    result.factorResearchSummary.topMinusBottomMeanNetReturn = 0.038;
    result.factorResearchSummary.topMinusBottomMeanTransactionCost = 0.002;
    result.factorResearchSummary.topMinusBottomMeanOneWayTurnover = 1.0;
    result.factorResearchSummary.topMinusBottomFrameCount = 2;
    result.factorResearchSummary.turnoverObservationCount = 1;
    result.factorResearchSummary.quantileReturns[0].meanNetForwardReturn = -0.011;
    result.factorResearchSummary.quantileReturns[0].meanOneWayTurnover = 0.5;
    result.factorResearchSummary.quantileReturns[0].meanTransactionCost = 0.001;
    result.factorResearchSummary.quantileReturns[0].turnoverObservationCount = 1;
    result.factorResearchSummary.quantileReturns[4].meanNetForwardReturn = 0.029;
    result.factorResearchSummary.quantileReturns[4].meanOneWayTurnover = 0.5;
    result.factorResearchSummary.quantileReturns[4].meanTransactionCost = 0.001;
    result.factorResearchSummary.quantileReturns[4].turnoverObservationCount = 1;
    result.immutableIdentity.insert(QStringLiteral("riskSummary"),
                                    result.riskSummary.toVariantMap());
    record.result = result;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &error));
    QVERIFY(error.contains(QStringLiteral("因子研究身份")));
    result.immutableIdentity.insert(
        QStringLiteral("factorResearch"),
        QVariantMap{
            {QStringLiteral("factorId"), researchDefinition.factorId},
            {QStringLiteral("factorVersion"), 1},
            {QStringLiteral("alias"), QStringLiteral("close_rank")},
            {QStringLiteral("definitionSha256"),
             result.definition.factorDefinitionSha256},
            {QStringLiteral("preprocessing"),
             result.definition.factorPreprocessing.toVariantMap()},
            {QStringLiteral("returnHorizon"),
             QStringLiteral("next_aligned_frame")},
            {QStringLiteral("returnCalculation"),
             QStringLiteral("close_to_close")},
            {QStringLiteral("frameAggregation"),
             QStringLiteral("equal_weighted_mean")},
            {QStringLiteral("summary"),
             result.factorResearchSummary.toVariantMap()}});
    record.result = result;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &error));
    QVERIFY(error.contains(QStringLiteral("因子研究方法身份")));
    QVariantMap researchIdentity = result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    researchIdentity.insert(QStringLiteral("researchMethodVersion"),
                            QStringLiteral("factor_research_v2"));
    researchIdentity.insert(QStringLiteral("icirConvention"),
                            QStringLiteral("mean_over_sample_sd_unannualized"));
    researchIdentity.insert(QStringLiteral("quantileCount"), 5);
    researchIdentity.insert(QStringLiteral("quantileMethod"),
                            QStringLiteral("stable_factor_then_symbol_equal_count"));
    researchIdentity.insert(
        QStringLiteral("quantileAggregation"),
        QStringLiteral("equal_weighted_within_frame_then_across_frames"));
    researchIdentity.insert(QStringLiteral("spreadDefinition"),
                            QStringLiteral("q5_minus_q1"));
    result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                    researchIdentity);
    record.result = result;
    QVERIFY(!store.savePortfolioBacktestArchive(record, &error));
    QVERIFY(error.contains(QStringLiteral("换手成本身份")));
    researchIdentity.insert(QStringLiteral("researchMethodVersion"),
                            QStringLiteral("factor_research_v3"));
    researchIdentity.insert(QStringLiteral("turnoverConvention"),
                            QStringLiteral("half_sum_absolute_weight_change"));
    researchIdentity.insert(QStringLiteral("initialFormation"),
                            QStringLiteral("excluded"));
    researchIdentity.insert(QStringLiteral("costFormula"),
                            QStringLiteral("absolute_weight_change_times_one_way_cost_rate"));
    researchIdentity.insert(QStringLiteral("costPolicy"),
                            result.definition.factorResearchCost.toVariantMap());
    result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                    researchIdentity);
    record.result = result;
    QVERIFY2(store.savePortfolioBacktestArchive(record, &error),
             qPrintable(error));
    const auto restored = store.loadPortfolioBacktestArchive(record.resultId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.definition.factorExposureAlias,
             QStringLiteral("close_rank"));
    QCOMPARE(restored->result.definition.maximumAbsoluteFactorExposure, 0.30);
    QCOMPARE(restored->result.riskSummary.factorExposureConstrainedFrameCount, 2);
    QCOMPARE(restored->result.riskSummary.maximumObservedAbsoluteFactorExposure,
             0.29);
    QCOMPARE(restored->result.factorResearchSummary.meanIc, 0.42);
    QCOMPARE(restored->result.factorResearchSummary.timeSeries.size(), 1);
    QCOMPARE(restored->result.factorResearchSummary.quantileReturns.size(), 5);
    QCOMPARE(restored->result.factorResearchSummary.icInformationRatio, 2.0);
    QCOMPARE(restored->result.factorResearchSummary.topMinusBottomMeanReturn,
             0.04);
    QCOMPARE(restored->result.factorResearchSummary.topMinusBottomMeanNetReturn,
             0.038);
    QCOMPARE(restored->result.factorResearchSummary.turnoverObservationCount, 1);
    QCOMPARE(restored->result.definition.factorResearchCost.oneWayCostBps, 10.0);
    QCOMPARE(restored->result.definition.factorDefinitionVersion, 1);
    QCOMPARE(restored->result.immutableIdentity
                 .value(QStringLiteral("factorExposure")).toMap()
                 .value(QStringLiteral("definitionSha256")).toString(),
             definitionSha256);
    FactorResearchComparisonListModel comparisonModel;
    QVERIFY2(comparisonModel.addArchive(*restored, &error), qPrintable(error));
    QCOMPARE(comparisonModel.count(), 1);
    QCOMPARE(comparisonModel.get(0).value(QStringLiteral("factorId")).toString(),
             researchDefinition.factorId);
    QCOMPARE(comparisonModel.get(0)
                 .value(QStringLiteral("comparabilityLabel")).toString(),
             QStringLiteral("比较基准"));

    PortfolioArchiveRecord neutralizedRecord = *restored;
    neutralizedRecord.resultId = QStringLiteral("factor-neutralized-v4");
    neutralizedRecord.completedAt = QDateTime::currentDateTimeUtc();
    neutralizedRecord.result.definition.factorNeutralization.method =
        QStringLiteral("within_group_demean");
    neutralizedRecord.result.definition.factorNeutralization.minimumGroupSize = 2;
    neutralizedRecord.result.definition.industryDatasetId =
        QStringLiteral("industry-neutralization-v1");
    neutralizedRecord.result.factorResearchSummary.neutralizationPolicy =
        neutralizedRecord.result.definition.factorNeutralization;
    neutralizedRecord.result.factorResearchSummary
        .neutralizationAppliedFrameCount = 1;
    neutralizedRecord.result.factorResearchSummary.neutralizedPairCount = 3;
    const QString neutralizationSnapshotVersion =
        QStringLiteral("sha256:") + QString(64, QLatin1Char('c'));
    neutralizedRecord.result.riskSummary.industryCoverageComplete = true;
    neutralizedRecord.result.riskSummary.industrySnapshotVersion =
        neutralizationSnapshotVersion;
    neutralizedRecord.result.immutableIdentity.insert(
        QStringLiteral("riskSummary"),
        neutralizedRecord.result.riskSummary.toVariantMap());
    QVariantMap neutralResearchIdentity = neutralizedRecord.result
        .immutableIdentity.value(QStringLiteral("factorResearch")).toMap();
    neutralResearchIdentity.insert(QStringLiteral("researchMethodVersion"),
                                   QStringLiteral("factor_research_v4"));
    neutralResearchIdentity.insert(
        QStringLiteral("neutralizationPolicy"),
        neutralizedRecord.result.definition.factorNeutralization.toVariantMap());
    neutralResearchIdentity.insert(
        QStringLiteral("preprocessingOrder"),
        QStringLiteral(
            "pairwise_drop_then_global_winsorize_then_group_demean"));
    neutralResearchIdentity.insert(
        QStringLiteral("neutralizationDatasetId"),
        neutralizedRecord.result.definition.industryDatasetId);
    neutralResearchIdentity.insert(
        QStringLiteral("neutralizationSnapshotVersion"),
        neutralizationSnapshotVersion);
    neutralResearchIdentity.insert(QStringLiteral("neutralizationAsOfDate"),
                                   QDate(2026, 1, 5));
    neutralResearchIdentity.insert(
        QStringLiteral("summary"),
        neutralizedRecord.result.factorResearchSummary.toVariantMap());
    neutralizedRecord.result.immutableIdentity.insert(
        QStringLiteral("factorResearch"), neutralResearchIdentity);
    QVERIFY(!store.savePortfolioBacktestArchive(neutralizedRecord, &error));
    QVERIFY(error.contains(QStringLiteral("行业快照身份")));

    neutralizedRecord.result.immutableIdentity.insert(
        QStringLiteral("industrySnapshot"),
        QVariantMap{
            {QStringLiteral("datasetId"),
             neutralizedRecord.result.definition.industryDatasetId},
            {QStringLiteral("snapshotVersion"),
             neutralizationSnapshotVersion},
            {QStringLiteral("asOfDate"), QDate(2026, 1, 5)},
            {QStringLiteral("coverageComplete"), true}});
    QVERIFY2(store.savePortfolioBacktestArchive(neutralizedRecord, &error),
             qPrintable(error));
    const auto restoredNeutralized = store.loadPortfolioBacktestArchive(
        neutralizedRecord.resultId);
    QVERIFY(restoredNeutralized.has_value());
    QVERIFY(restoredNeutralized->result.definition.factorNeutralization.enabled());
    QCOMPARE(restoredNeutralized->result.definition.factorNeutralization
                 .minimumGroupSize,
             2);
    QCOMPARE(restoredNeutralized->result.factorResearchSummary
                 .neutralizationAppliedFrameCount,
             1);
    QCOMPARE(restoredNeutralized->result.factorResearchSummary
                 .neutralizedPairCount,
             3);

    PortfolioArchiveRecord inferenceRecord = *restored;
    inferenceRecord.resultId = QStringLiteral("factor-inference-v5");
    inferenceRecord.completedAt = QDateTime::currentDateTimeUtc();
    FactorResearchSummary &inferenceSummary =
        inferenceRecord.result.factorResearchSummary;
    inferenceSummary.timeSeries.clear();
    for (int index = 0; index < 20; ++index) {
        const QDateTime factorTime = QDateTime(
            QDate(2026, 1, 5), QTime(9, 35), QTimeZone::utc())
                                         .addSecs(index * 300);
        inferenceSummary.timeSeries.append(
            {factorTime, factorTime.addSecs(300), 3, 3,
             index < 15 ? 1.0 : -1.0,
             index < 15 ? 1.0 : -1.0});
    }
    inferenceSummary.evaluatedFrameCount = 20;
    inferenceSummary.inferenceFrameCount = 20;
    inferenceSummary.hacLag = 2;
    inferenceSummary.icInferenceAvailable = true;
    inferenceSummary.rankIcInferenceAvailable = true;
    inferenceSummary.icMeanStandardError = 0.301385688667085;
    inferenceSummary.rankIcMeanStandardError = 0.301385688667085;
    inferenceSummary.icConfidenceLower = -0.090705095243289;
    inferenceSummary.icConfidenceUpper = 1.09070509524329;
    inferenceSummary.rankIcConfidenceLower = -0.090705095243289;
    inferenceSummary.rankIcConfidenceUpper = 1.09070509524329;
    inferenceSummary.icTwoSidedPValue = 0.097;
    inferenceSummary.rankIcTwoSidedPValue = 0.097;
    inferenceSummary.temporalStabilityAvailable = true;
    inferenceSummary.temporalSegments = {
        {1, inferenceSummary.timeSeries.at(0).factorTimestamp,
         inferenceSummary.timeSeries.at(5).factorTimestamp, 6, 1.0, 1.0},
        {2, inferenceSummary.timeSeries.at(6).factorTimestamp,
         inferenceSummary.timeSeries.at(12).factorTimestamp, 7, 1.0, 1.0},
        {3, inferenceSummary.timeSeries.at(13).factorTimestamp,
         inferenceSummary.timeSeries.at(19).factorTimestamp, 7,
         -3.0 / 7.0, -3.0 / 7.0}};
    inferenceSummary.icSameSignAcrossSegments = false;
    inferenceSummary.rankIcSameSignAcrossSegments = false;
    QVariantMap inferenceIdentity = inferenceRecord.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    inferenceIdentity.insert(QStringLiteral("researchMethodVersion"),
                             QStringLiteral("factor_research_v5"));
    inferenceIdentity.insert(QStringLiteral("summary"),
                             inferenceSummary.toVariantMap());
    inferenceRecord.result.immutableIdentity.insert(
        QStringLiteral("factorResearch"), inferenceIdentity);
    QVERIFY(!store.savePortfolioBacktestArchive(inferenceRecord, &error));
    QVERIFY(error.contains(QStringLiteral("统计推断身份")));

    inferenceIdentity.insert(QStringLiteral("inferenceMethodVersion"),
                             QStringLiteral("newey_west_bartlett_v1"));
    inferenceIdentity.insert(QStringLiteral("inferenceMinimumFrames"), 20);
    inferenceIdentity.insert(QStringLiteral("confidenceLevel"), 0.95);
    inferenceIdentity.insert(
        QStringLiteral("hacLagRule"),
        QStringLiteral("floor_4_times_n_over_100_power_2_over_9"));
    inferenceIdentity.insert(QStringLiteral("temporalSegmentCount"), 3);
    inferenceIdentity.insert(QStringLiteral("temporalSegmentMethod"),
                             QStringLiteral("contiguous_equal_count"));
    inferenceIdentity.insert(
        QStringLiteral("neutralizationPolicy"),
        inferenceRecord.result.definition.factorNeutralization.toVariantMap());
    inferenceIdentity.insert(
        QStringLiteral("preprocessingOrder"),
        QStringLiteral(
            "pairwise_drop_then_global_winsorize_then_group_demean"));
    inferenceRecord.result.immutableIdentity.insert(
        QStringLiteral("factorResearch"), inferenceIdentity);
    QVERIFY2(store.savePortfolioBacktestArchive(inferenceRecord, &error),
             qPrintable(error));
    const auto restoredInference = store.loadPortfolioBacktestArchive(
        inferenceRecord.resultId);
    QVERIFY(restoredInference.has_value());
    QVERIFY(restoredInference->result.factorResearchSummary
                .icInferenceAvailable);
    QCOMPARE(restoredInference->result.factorResearchSummary.hacLag, 2);
    QCOMPARE(restoredInference->result.factorResearchSummary
                 .temporalSegments.size(),
             3);

    PortfolioArchiveRecord outOfSampleRecord = *restoredInference;
    outOfSampleRecord.resultId = QStringLiteral("factor-oos-v6");
    outOfSampleRecord.completedAt = QDateTime::currentDateTimeUtc();
    FactorResearchSummary &outOfSampleSummary =
        outOfSampleRecord.result.factorResearchSummary;
    outOfSampleSummary.timeSeries.clear();
    const QDateTime firstFactorTime(
        QDate(2026, 1, 5), QTime(9, 35), QTimeZone::utc());
    for (int index = 0; index < 40; ++index) {
        const QDateTime factorTime = firstFactorTime.addSecs(index * 300);
        outOfSampleSummary.timeSeries.append(
            {factorTime, factorTime.addSecs(300), 3, 3,
             index < 35 ? 1.0 : -1.0,
             index < 35 ? 1.0 : -1.0});
    }
    outOfSampleSummary.evaluatedFrameCount = 40;
    outOfSampleSummary.inferenceFrameCount = 40;
    outOfSampleSummary.hacLag = 3;
    outOfSampleSummary.temporalSegments = {
        {1, outOfSampleSummary.timeSeries.at(0).factorTimestamp,
         outOfSampleSummary.timeSeries.at(12).factorTimestamp,
         13, 1.0, 1.0},
        {2, outOfSampleSummary.timeSeries.at(13).factorTimestamp,
         outOfSampleSummary.timeSeries.at(25).factorTimestamp,
         13, 1.0, 1.0},
        {3, outOfSampleSummary.timeSeries.at(26).factorTimestamp,
         outOfSampleSummary.timeSeries.at(39).factorTimestamp,
         14, 2.0 / 7.0, 2.0 / 7.0}};
    FactorOutOfSampleSummary &oos = outOfSampleSummary.outOfSample;
    oos.available = true;
    oos.totalFrameCount = 40;
    oos.evaluationFrameCount = 20;
    oos.evaluationFromFactorTimestamp =
        outOfSampleSummary.timeSeries.at(20).factorTimestamp;
    oos.evaluationToFactorTimestamp =
        outOfSampleSummary.timeSeries.at(39).factorTimestamp;
    oos.meanIc = 0.5;
    oos.meanRankIc = 0.5;
    oos.icInferenceAvailable = true;
    oos.rankIcInferenceAvailable = true;
    oos.inferenceFrameCount = 20;
    oos.hacLag = 2;
    oos.icMeanStandardError = 0.301385688667085;
    oos.rankIcMeanStandardError = 0.301385688667085;
    oos.icConfidenceLower = -0.090705095243289;
    oos.icConfidenceUpper = 1.09070509524329;
    oos.rankIcConfidenceLower = -0.090705095243289;
    oos.rankIcConfidenceUpper = 1.09070509524329;
    oos.icTwoSidedPValue = 0.097;
    oos.rankIcTwoSidedPValue = 0.097;
    oos.windows = {
        {1, 20, 10,
         outOfSampleSummary.timeSeries.at(0).factorTimestamp,
         outOfSampleSummary.timeSeries.at(19).factorTimestamp,
         outOfSampleSummary.timeSeries.at(20).factorTimestamp,
         outOfSampleSummary.timeSeries.at(29).factorTimestamp,
         1.0, 1.0},
        {2, 30, 10,
         outOfSampleSummary.timeSeries.at(0).factorTimestamp,
         outOfSampleSummary.timeSeries.at(29).factorTimestamp,
         outOfSampleSummary.timeSeries.at(30).factorTimestamp,
         outOfSampleSummary.timeSeries.at(39).factorTimestamp,
         0.0, 0.0}};
    QVariantMap outOfSampleIdentity = outOfSampleRecord.result
        .immutableIdentity.value(QStringLiteral("factorResearch")).toMap();
    outOfSampleIdentity.insert(QStringLiteral("researchMethodVersion"),
                               QStringLiteral("factor_research_v6"));
    outOfSampleIdentity.insert(QStringLiteral("summary"),
                               outOfSampleSummary.toVariantMap());
    outOfSampleRecord.result.immutableIdentity.insert(
        QStringLiteral("factorResearch"), outOfSampleIdentity);
    QVERIFY(!store.savePortfolioBacktestArchive(outOfSampleRecord, &error));
    QVERIFY(error.contains(QStringLiteral("样本外")));

    outOfSampleIdentity.insert(QStringLiteral("outOfSampleMethodVersion"),
                               QStringLiteral("expanding_oos_v1"));
    outOfSampleIdentity.insert(QStringLiteral("outOfSamplePolicy"),
                               oos.policy.toVariantMap());
    outOfSampleIdentity.insert(
        QStringLiteral("outOfSampleInferenceMethodVersion"),
        QStringLiteral("newey_west_bartlett_v1"));
    outOfSampleRecord.result.immutableIdentity.insert(
        QStringLiteral("factorResearch"), outOfSampleIdentity);
    QVERIFY2(store.savePortfolioBacktestArchive(outOfSampleRecord, &error),
             qPrintable(error));
    const auto restoredOutOfSample = store.loadPortfolioBacktestArchive(
        outOfSampleRecord.resultId);
    QVERIFY(restoredOutOfSample.has_value());
    const FactorOutOfSampleSummary &restoredOos =
        restoredOutOfSample->result.factorResearchSummary.outOfSample;
    QVERIFY(restoredOos.available);
    QCOMPARE(restoredOos.windows.size(), 2);
    QCOMPARE(restoredOos.evaluationFrameCount, 20);
    QCOMPARE(restoredOos.policy.toVariantMap(), oos.policy.toVariantMap());
}

void PortfolioBacktestStoreTest::archiveRejectsInvalidResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(
        directory.filePath(QStringLiteral("invalid.db"))));
    QSignalSpy failureSpy(&store, &SqliteLocalDataStore::writeFailed);
    PortfolioArchiveRecord record;
    record.resultId = QStringLiteral("bad");
    record.completedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(!store.savePortfolioBacktestArchive(record));
    QVERIFY(!failureSpy.isEmpty());
    QVERIFY(store.loadPortfolioBacktestArchiveSummaries().isEmpty());
    QVERIFY(!store.loadPortfolioBacktestArchive(QStringLiteral("bad")).has_value());
}

void PortfolioBacktestStoreTest::factorDefinitionRegistryVersionsByImmutableContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("factors.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 25);

    FactorDefinitionVersion first;
    first.factorId = QStringLiteral("strategy.factor.momentum");
    first.name = QStringLiteral("动量因子");
    first.alias = QStringLiteral("mom_rank");
    first.sourceStrategyId = QStringLiteral("cross_strategy");
    first.sourceStrategyVersion = 1;
    first.definition = {{QStringLiteral("name"), QStringLiteral("RANK")},
                        {QStringLiteral("of"), QVariantMap{
                             {QStringLiteral("field"), QStringLiteral("close")}}}};
    bool reused = true;
    QString error;
    QVERIFY(store.registerFactorDefinitionVersion(first, &reused, &error));
    QVERIFY(!reused);
    QCOMPARE(first.version, 1);
    QCOMPARE(first.contentSha256, first.computedContentSha256());
    QVERIFY(first.createdAt.isValid());

    FactorDefinitionVersion identical = first;
    identical.version = 0;
    identical.contentSha256.clear();
    identical.createdAt = {};
    QVERIFY(store.registerFactorDefinitionVersion(identical, &reused, &error));
    QVERIFY(reused);
    QCOMPARE(identical.version, 1);

    FactorDefinitionVersion changed = identical;
    changed.version = 0;
    changed.contentSha256.clear();
    changed.preprocessing.winsorizationPolicy = QStringLiteral("percentile_clip");
    QVERIFY(store.registerFactorDefinitionVersion(changed, &reused, &error));
    QVERIFY(!reused);
    QCOMPARE(changed.version, 2);
    QVERIFY(changed.contentSha256 != first.contentSha256);

    const QVector<FactorDefinitionVersion> definitions =
        store.loadFactorDefinitionVersions();
    QCOMPARE(definitions.size(), 2);
    QCOMPARE(definitions.at(0).version, 2);
    QCOMPARE(definitions.at(1).version, 1);
    store.close();

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.loadFactorDefinitionVersions().size(), 2);
}

void PortfolioBacktestStoreTest::
factorCandidateRegistryRoundTripAndEvaluationAudit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("candidates.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 25);

    FactorCandidateVersion first;
    first.candidateId = QStringLiteral("candidate.momentum");
    first.name = QStringLiteral("动量候选");
    first.hypothesis = QStringLiteral("过去收益较高的股票后续收益更高");
    first.sourceKind = QStringLiteral("manual");
    first.factorDefinition = {
        {QStringLiteral("indicator"), QStringLiteral("ROC")},
        {QStringLiteral("period"), 20}};
    first.proposedAt = QDateTime(QDate(2026, 8, 23), QTime(9, 0),
                                 QTimeZone::utc());
    bool reused = true;
    QString error;
    QVERIFY2(store.registerFactorCandidateVersion(first, &reused, &error),
             qPrintable(error));
    QVERIFY(!reused);
    QCOMPARE(first.version, 1);
    QCOMPARE(first.contentSha256, first.computedContentSha256());

    FactorCandidateVersion identical = first;
    identical.version = 0;
    identical.contentSha256.clear();
    identical.frozenAt = {};
    QVERIFY2(store.registerFactorCandidateVersion(identical, &reused, &error),
             qPrintable(error));
    QVERIFY(reused);
    QCOMPARE(identical.version, 1);

    FactorCandidateVersion changed = first;
    changed.version = 0;
    changed.contentSha256.clear();
    changed.hypothesis += QStringLiteral("，并在十个评价窗口保持稳定");
    QVERIFY2(store.registerFactorCandidateVersion(changed, &reused, &error),
             qPrintable(error));
    QVERIFY(!reused);
    QCOMPARE(changed.version, 2);

    FactorCandidateVersion reverted = first;
    reverted.version = 0;
    reverted.contentSha256.clear();
    reverted.frozenAt = {};
    QVERIFY2(store.registerFactorCandidateVersion(reverted, &reused, &error),
             qPrintable(error));
    QVERIFY(reused);
    QCOMPARE(reverted.version, 1);

    const QDateTime frozenAt = first.proposedAt.addSecs(60);
    QVERIFY2(store.freezeFactorCandidateVersion(first.candidateId, 1,
                                                frozenAt, &error),
             qPrintable(error));

    PortfolioArchiveRecord archive;
    archive.resultId = QStringLiteral("future-v6");
    archive.completedAt = frozenAt.addSecs(1);
    archive.result.resultKind = QStringLiteral("cross_sectional");
    archive.result.factorResearchSummary.factorId = QStringLiteral("momentum");
    archive.result.factorResearchSummary.factorVersion = 1;
    archive.result.factorResearchSummary.factorAlias = QStringLiteral("momentum");
    archive.result.factorResearchSummary.outOfSample.available = true;
    archive.result.immutableIdentity = {
        {QStringLiteral("factorResearch"),
         QVariantMap{{QStringLiteral("factorId"), QStringLiteral("momentum")},
                     {QStringLiteral("factorVersion"), 1},
                     {QStringLiteral("researchMethodVersion"),
                      QStringLiteral("factor_research_v6")},
                     {QStringLiteral("outOfSampleMethodVersion"),
                      QStringLiteral("expanding_oos_v1")}}}};
    FactorCandidateEvaluation evaluation;
    QVERIFY2(store.linkFactorCandidateEvaluation(
                 first.candidateId, 1, archive,
                 FactorCandidateEvaluationRole::ProspectiveHoldout,
                 evaluation, &reused, &error),
             qPrintable(error));
    QVERIFY(!reused);
    QVERIFY(evaluation.preregistered);
    QVERIFY(!evaluation.archiveIdentitySha256.isEmpty());

    FactorCandidateEvaluation duplicate;
    QVERIFY2(store.linkFactorCandidateEvaluation(
                 first.candidateId, 1, archive,
                 FactorCandidateEvaluationRole::ProspectiveHoldout,
                 duplicate, &reused, &error),
             qPrintable(error));
    QVERIFY(reused);
    QCOMPARE(duplicate.evaluationId, evaluation.evaluationId);

    const QVector<FactorCandidateVersion> candidates =
        store.loadFactorCandidateVersions();
    QCOMPARE(candidates.size(), 2);
    QVERIFY(candidates.at(1).frozenAt.isValid());
    const QVector<FactorCandidateEvaluation> evaluations =
        store.loadFactorCandidateEvaluations(first.candidateId, 1);
    QCOMPARE(evaluations.size(), 1);
    QCOMPARE(evaluations.first().archiveResultId, archive.resultId);
    QVERIFY(evaluations.first().preregistered);
    store.close();

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.schemaVersion(), 25);
    QCOMPARE(reopened.loadFactorCandidateVersions().size(), 2);
    QCOMPARE(reopened.loadFactorCandidateEvaluations(first.candidateId, 1).size(),
             1);

    PortfolioArchiveRecord oldArchive = archive;
    oldArchive.resultId = QStringLiteral("old-v6");
    oldArchive.completedAt = frozenAt.addSecs(-1);
    FactorCandidateEvaluation rejected;
    QVERIFY(!reopened.linkFactorCandidateEvaluation(
        first.candidateId, 1, oldArchive,
        FactorCandidateEvaluationRole::ProspectiveHoldout,
        rejected, &reused, &error));
    QVERIFY(error.contains(QStringLiteral("晚于")));
    QVERIFY2(reopened.linkFactorCandidateEvaluation(
                 first.candidateId, 1, oldArchive,
                 FactorCandidateEvaluationRole::RetrospectiveReference,
                 rejected, &reused, &error),
             qPrintable(error));
    QVERIFY(!rejected.preregistered);
}

void PortfolioBacktestStoreTest::authorizedFactorDatasetRoundTripsAcrossRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("external-factor.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));

    ExternalFactorDataset dataset;
    dataset.factorKey = QStringLiteral("quality.score");
    dataset.factorDefinitionSha256 = QStringLiteral("sha256:") + QString(64, 'd');
    dataset.source = QStringLiteral("authorized_csv");
    dataset.sourceVersion = QStringLiteral("vendor-2026-08-20");
    dataset.sourceReference = QStringLiteral("用户授权研究数据 v1");
    dataset.contentSha256 = QStringLiteral("sha256:") + QString(64, 'a');
    dataset.importedAt = QDateTime(QDate(2026, 8, 23), QTime(10, 0),
                                   QTimeZone::utc());
    QVector<ExternalFactorObservation> observations{
        {QDateTime(QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("600519"), 1.25},
        {QDateTime(QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("000001"), -0.5}};

    bool reused = true;
    QString error;
    QVERIFY2(store.saveExternalFactorDataset(dataset, observations, &reused,
                                             &error), qPrintable(error));
    QVERIFY(!reused);
    QVERIFY(!dataset.datasetId.isEmpty());
    QCOMPARE(dataset.recordCount, qint64(2));
    QCOMPARE(dataset.symbolCount, 2);
    QCOMPARE(dataset.timestampCount, 1);
    ExternalFactorDataset repeated = dataset;
    repeated.datasetId.clear();
    repeated.importedAt = dataset.importedAt.addSecs(30);
    QVERIFY2(store.saveExternalFactorDataset(repeated, observations, &reused,
                                             &error), qPrintable(error));
    QVERIFY(reused);
    QCOMPARE(repeated.datasetId, dataset.datasetId);
    QCOMPARE(store.loadExternalFactorDatasets().size(), 1);
    store.close();

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.schemaVersion(), 25);
    const auto catalog = reopened.loadExternalFactorDatasets();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.first().datasetId, dataset.datasetId);
    QCOMPARE(catalog.first().factorDefinitionSha256,
             dataset.factorDefinitionSha256);
    const auto restored = reopened.loadExternalFactorObservations(
        dataset.datasetId);
    QCOMPARE(restored.size(), 2);
    QCOMPARE(restored.at(0).symbol, QStringLiteral("000001"));
    QCOMPARE(restored.at(0).value, -0.5);
    QCOMPARE(restored.at(1).symbol, QStringLiteral("600519"));
    QCOMPARE(restored.at(1).value, 1.25);
}

void PortfolioBacktestStoreTest::
authorizedFactorDatasetBindingRequiresFrozenMatchingCandidate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("binding.db"))));

    FactorCandidateVersion candidate;
    candidate.candidateId = QStringLiteral("candidate.quality");
    candidate.name = QStringLiteral("质量候选");
    candidate.hypothesis = QStringLiteral("质量得分较高的股票后续收益更高");
    candidate.factorDefinition = {
        {QStringLiteral("indicator"), QStringLiteral("EXTERNAL_QUALITY")},
        {QStringLiteral("frequency"), QStringLiteral("day")}};
    candidate.proposedAt = QDateTime(QDate(2026, 8, 23), QTime(9, 0),
                                     QTimeZone::utc());
    bool reused = false;
    QString error;
    QVERIFY2(store.registerFactorCandidateVersion(candidate, &reused, &error),
             qPrintable(error));

    ExternalFactorDataset dataset;
    dataset.factorKey = QStringLiteral("quality.score");
    dataset.factorDefinitionSha256 =
        candidate.computedFactorDefinitionSha256();
    dataset.source = QStringLiteral("authorized_csv");
    dataset.sourceVersion = QStringLiteral("vendor-1");
    dataset.sourceReference = QStringLiteral("用户授权数据");
    dataset.contentSha256 = QStringLiteral("sha256:") + QString(64, 'a');
    const QVector<ExternalFactorObservation> observations{
        {QDateTime(QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("600519"), 1.25}};
    QVERIFY2(store.saveExternalFactorDataset(dataset, observations, &reused,
                                             &error), qPrintable(error));

    ExternalFactorDatasetBinding binding;
    QVERIFY(!store.bindExternalFactorDataset(
        candidate.candidateId, candidate.version, dataset.datasetId,
        binding, &reused, &error));
    QVERIFY(error.contains(QStringLiteral("冻结")));

    const QDateTime frozenAt = candidate.proposedAt.addSecs(60);
    QVERIFY2(store.freezeFactorCandidateVersion(candidate.candidateId,
                                                candidate.version, frozenAt,
                                                &error), qPrintable(error));

    ExternalFactorDataset mismatched = dataset;
    mismatched.datasetId.clear();
    mismatched.sourceVersion = QStringLiteral("vendor-mismatch");
    mismatched.contentSha256 = QStringLiteral("sha256:") + QString(64, 'b');
    mismatched.factorDefinitionSha256 =
        QStringLiteral("sha256:") + QString(64, 'c');
    QVERIFY2(store.saveExternalFactorDataset(mismatched, observations, &reused,
                                             &error), qPrintable(error));
    QVERIFY(!store.bindExternalFactorDataset(
        candidate.candidateId, candidate.version, mismatched.datasetId,
        binding, &reused, &error));
    QVERIFY(error.contains(QStringLiteral("定义")));

    QVERIFY2(store.bindExternalFactorDataset(
                 candidate.candidateId, candidate.version, dataset.datasetId,
                 binding, &reused, &error), qPrintable(error));
    QVERIFY(!reused);
    QVERIFY(!binding.bindingId.isEmpty());
    QCOMPARE(binding.candidateContentSha256, candidate.contentSha256);
    const auto bindings = store.loadExternalFactorDatasetBindings(
        candidate.candidateId, candidate.version);
    QCOMPARE(bindings.size(), 1);
    QCOMPARE(bindings.first().datasetId, dataset.datasetId);
}

void PortfolioBacktestStoreTest::
invalidAuthorizedFactorObservationLeavesCatalogUnchanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("invalid.db"))));
    ExternalFactorDataset dataset;
    dataset.factorKey = QStringLiteral("quality.score");
    dataset.factorDefinitionSha256 = QStringLiteral("sha256:") + QString(64, 'd');
    dataset.source = QStringLiteral("authorized_csv");
    dataset.sourceVersion = QStringLiteral("vendor-1");
    dataset.sourceReference = QStringLiteral("用户授权数据");
    dataset.contentSha256 = QStringLiteral("sha256:") + QString(64, 'a');
    const QVector<ExternalFactorObservation> observations{
        {QDateTime(QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("ABCDEF"), 1.25}};
    bool reused = false;
    QString error;

    QVERIFY(!store.saveExternalFactorDataset(dataset, observations, &reused,
                                             &error));
    QVERIFY(error.contains(QStringLiteral("沪深")));
    QVERIFY(store.loadExternalFactorDatasets().isEmpty());
}

void PortfolioBacktestStoreTest::
externalFactorResearchArchiveRoundTripsImmutableIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("external-research.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));

    FactorCandidateVersion candidate;
    candidate.candidateId = QStringLiteral("candidate.quality");
    candidate.name = QStringLiteral("授权质量因子");
    candidate.hypothesis = QStringLiteral("质量得分与下一时刻收益负相关");
    candidate.sourceKind = QStringLiteral("authorized_import");
    candidate.sourceReference = QStringLiteral("用户授权研究数据");
    candidate.factorDefinition = {
        {QStringLiteral("factorKey"), QStringLiteral("quality.score")}};
    candidate.proposedAt = QDateTime(
        QDate(2026, 8, 23), QTime(9, 0), QTimeZone::utc());
    bool reused = false;
    QString error;
    QVERIFY2(store.registerFactorCandidateVersion(candidate, &reused, &error),
             qPrintable(error));
    QVERIFY2(store.freezeFactorCandidateVersion(
                 candidate.candidateId, candidate.version,
                 candidate.proposedAt.addSecs(1), &error), qPrintable(error));

    ExternalFactorDataset dataset;
    dataset.factorKey = QStringLiteral("quality.score");
    dataset.factorDefinitionSha256 =
        candidate.computedFactorDefinitionSha256();
    dataset.source = QStringLiteral("authorized_csv");
    dataset.sourceVersion = QStringLiteral("vendor-1");
    dataset.sourceReference = QStringLiteral("用户授权研究数据");
    dataset.contentSha256 = QStringLiteral("sha256:") + QString(64, 'b');
    dataset.importedAt = candidate.proposedAt.addSecs(2);
    const QVector<ExternalFactorObservation> observations{
        {QDateTime(QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("600001"), 1.0},
        {QDateTime(QDate(2026, 8, 21), QTime(7, 0), QTimeZone::utc()),
         QStringLiteral("600001"), 2.0}};
    QVERIFY2(store.saveExternalFactorDataset(dataset, observations, &reused,
                                             &error), qPrintable(error));
    ExternalFactorDatasetBinding binding;
    QVERIFY2(store.bindExternalFactorDataset(
                 candidate.candidateId, candidate.version, dataset.datasetId,
                 binding, &reused, &error), qPrintable(error));

    ExternalFactorResearchArchiveRecord record;
    record.resultId = QStringLiteral("external-factor-research-1");
    record.completedAt = QDateTime(
        QDate(2026, 8, 23), QTime(16, 0), QTimeZone::utc());
    record.candidateId = candidate.candidateId;
    record.candidateVersion = candidate.version;
    record.candidateContentSha256 = candidate.contentSha256;
    record.datasetId = dataset.datasetId;
    record.datasetContentSha256 = dataset.contentSha256;
    record.factorDefinitionSha256 = dataset.factorDefinitionSha256;
    record.factorKey = dataset.factorKey;
    record.periodSeconds = 60;
    record.returnDatasetIdentities = {
        datasetIdentityFor(QStringLiteral("600001"), 'd'),
        datasetIdentityFor(QStringLiteral("600002"), 'e'),
        datasetIdentityFor(QStringLiteral("600003"), 'f')};
    record.summary.factorId = record.factorKey;
    record.summary.factorVersion = 1;
    record.summary.evaluatedFrameCount = 1;
    record.summary.meanIc = -1.0;
    record.summary.meanRankIc = -1.0;
    FactorResearchPoint point;
    point.factorTimestamp = QDateTime(
        QDate(2026, 8, 20), QTime(7, 0), QTimeZone::utc());
    point.returnTimestamp = point.factorTimestamp.addSecs(300);
    point.candidatePairCount = 3;
    point.validPairCount = 3;
    point.ic = -1.0;
    point.rankIc = -1.0;
    record.summary.timeSeries = {point};
    record.identitySha256 = record.computedIdentitySha256();
    QVERIFY2(store.saveExternalFactorResearchArchive(record, &error),
             qPrintable(error));
    store.close();

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(path));
    QCOMPARE(reopened.schemaVersion(), 25);
    const auto restored = reopened.loadExternalFactorResearchArchive(
        record.resultId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->candidateId, record.candidateId);
    QCOMPARE(restored->datasetId, record.datasetId);
    QCOMPARE(restored->returnDatasetIdentities.size(), 3);
    QCOMPARE(restored->summary.meanIc, -1.0);
    QCOMPARE(restored->identitySha256, record.identitySha256);
    QCOMPARE(reopened.loadExternalFactorResearchArchiveSummaries().size(), 1);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::PortfolioBacktestStoreTest)

#include "tst_portfolio_backtest_store.moc"
