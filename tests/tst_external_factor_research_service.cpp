#include "services/ExternalFactorResearchService.h"
#include "services/SqliteLocalDataStore.h"

#include <QSignalSpy>
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
    instrument.venue = symbol.startsWith(QLatin1Char('6'))
        ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
    return instrument;
}

bool seedLocalSeries(SqliteLocalDataStore &store, const QString &symbol,
                     double firstClose, double secondClose)
{
    const Instrument instrument = instrumentFor(symbol);
    const QDate day(2026, 8, 20);
    RawTradeTick tick;
    tick.instrument = instrument;
    tick.tradingDate = day;
    tick.timestamp = QDateTime(day, QTime(9, 59),
                               QTimeZone("Asia/Shanghai")).toUTC();
    tick.sourceLine = 1;
    tick.price = firstClose;
    tick.volumeShares = 100.0;
    tick.amount = firstClose * 100.0;
    tick.direction = RawTradeDirection::Buy;
    tick.source = QStringLiteral("tickdata_csv_tick");
    if (!store.beginRawTradeTickImport(
            instrument, day, QStringLiteral("tickdata_csv_tick"))
        || !store.appendRawTradeTicks({tick})
        || !store.commitRawTradeTickImport()) {
        return false;
    }
    const RawTradeFingerprint fingerprint = store.rawTradeFingerprint(
        instrument, day);
    QVector<Candle> candles;
    for (const auto &entry : QVector<QPair<QTime, double>>{
             {QTime(9, 59), firstClose}, {QTime(10, 4), secondClose}}) {
        Candle candle;
        candle.instrument = instrument;
        candle.startTime = QDateTime(day, entry.first,
                                     QTimeZone("Asia/Shanghai")).toUTC();
        candle.periodSeconds = 60;
        candle.open = candle.high = candle.low = candle.close = entry.second;
        candle.volume = 100.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    return store.replaceRawTickCandleCache(
        instrument, day, 60, fingerprint, candles);
}

ExternalFactorResearchRequest seedBoundResearch(SqliteLocalDataStore &store)
{
    FactorCandidateVersion candidate;
    candidate.candidateId = QStringLiteral("candidate.authorized.quality");
    candidate.name = QStringLiteral("授权质量因子");
    candidate.hypothesis = QStringLiteral("质量得分与下一观测收益负相关");
    candidate.sourceKind = QStringLiteral("authorized_import");
    candidate.sourceReference = QStringLiteral("测试授权数据");
    candidate.factorDefinition = {
        {QStringLiteral("factorKey"), QStringLiteral("quality.score")}};
    candidate.proposedAt = QDateTime(
        QDate(2026, 8, 20), QTime(1, 0), QTimeZone::utc());
    bool reused = false;
    QString error;
    if (!store.registerFactorCandidateVersion(candidate, &reused, &error)
        || !store.freezeFactorCandidateVersion(
            candidate.candidateId, candidate.version,
            candidate.proposedAt.addSecs(1), &error)) {
        return {};
    }

    const QDateTime first(QDate(2026, 8, 20), QTime(2, 0),
                          QTimeZone::utc());
    const QDateTime second = first.addSecs(300);
    ExternalFactorDataset dataset;
    dataset.factorKey = QStringLiteral("quality.score");
    dataset.factorDefinitionSha256 =
        candidate.computedFactorDefinitionSha256();
    dataset.sourceVersion = QStringLiteral("fixture-v1");
    dataset.sourceReference = QStringLiteral("测试授权数据");
    dataset.contentSha256 = QStringLiteral("sha256:") + QString(64, 'a');
    dataset.importedAt = first;
    QVector<ExternalFactorObservation> observations;
    const QStringList symbols{QStringLiteral("600001"),
                              QStringLiteral("600002"),
                              QStringLiteral("000001")};
    for (int index = 0; index < symbols.size(); ++index) {
        observations.append({first, symbols.at(index), double(index + 1)});
        observations.append({second, symbols.at(index), double(index + 2)});
    }
    if (!store.saveExternalFactorDataset(
            dataset, observations, &reused, &error)) {
        return {};
    }
    ExternalFactorDatasetBinding binding;
    if (!store.bindExternalFactorDataset(
            candidate.candidateId, candidate.version, dataset.datasetId,
            binding, &reused, &error)) {
        return {};
    }

    ExternalFactorResearchRequest request;
    request.candidateId = candidate.candidateId;
    request.candidateVersion = candidate.version;
    request.datasetId = dataset.datasetId;
    request.periodSeconds = 60;
    return request;
}

} // namespace

class ExternalFactorResearchServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void boundAuthorizedDatasetRunsAsynchronouslyAndArchives();
    void preflightReportsMissingLocalSymbolsWithoutArchiving();
    void requestGateRejectsOlderCompletion();
};

void ExternalFactorResearchServiceTest::
boundAuthorizedDatasetRunsAsynchronouslyAndArchives()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(
        QStringLiteral("external-factor-research.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    QVERIFY(seedLocalSeries(store, QStringLiteral("600001"), 10.0, 11.0));
    QVERIFY(seedLocalSeries(store, QStringLiteral("600002"), 10.0, 10.0));
    QVERIFY(seedLocalSeries(store, QStringLiteral("000001"), 10.0, 9.0));
    const ExternalFactorResearchRequest request = seedBoundResearch(store);
    QVERIFY(!request.datasetId.isEmpty());
    store.close();

    ExternalFactorResearchService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &ExternalFactorResearchService::finished);
    const quint64 requestId = service.start(request);
    QVERIFY(requestId > 0);
    QVERIFY(service.running());
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toULongLong(), requestId);
    const ExternalFactorResearchExecutionResult result =
        qvariant_cast<ExternalFactorResearchExecutionResult>(spy.at(0).at(1));
    QVERIFY2(result.succeeded, qPrintable(result.error));
    QCOMPARE(result.archive.summary.evaluatedFrameCount, 1);
    QCOMPARE(result.archive.summary.meanIc, -1.0);
    QCOMPARE(result.archive.returnDatasetIdentities.size(), 3);
    QVERIFY(result.archive.identitySha256.startsWith(QStringLiteral("sha256:")));

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(databasePath));
    const auto restored = reopened.loadExternalFactorResearchArchive(
        result.archive.resultId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->identitySha256, result.archive.identitySha256);
}

void ExternalFactorResearchServiceTest::requestGateRejectsOlderCompletion()
{
    ExternalFactorResearchRequestGate gate;
    gate.activate(41);
    gate.activate(42);
    QVERIFY(gate.loading());
    QVERIFY(!gate.finish(41));
    QVERIFY(gate.loading());
    QCOMPARE(gate.currentRequestId(), quint64(42));
    QVERIFY(gate.finish(42));
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), quint64(42));
}

void ExternalFactorResearchServiceTest::
preflightReportsMissingLocalSymbolsWithoutArchiving()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(
        QStringLiteral("external-factor-preflight.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    QVERIFY(seedLocalSeries(store, QStringLiteral("600001"), 10.0, 11.0));
    QVERIFY(seedLocalSeries(store, QStringLiteral("600002"), 10.0, 10.0));
    const ExternalFactorResearchRequest request = seedBoundResearch(store);
    QVERIFY(!request.datasetId.isEmpty());
    store.close();

    ExternalFactorResearchService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &ExternalFactorResearchService::preflightFinished);
    const quint64 requestId = service.preflight(request);
    QVERIFY(requestId > 0);
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toULongLong(), requestId);
    const ExternalFactorResearchPreflightResult missing =
        qvariant_cast<ExternalFactorResearchPreflightResult>(spy.at(0).at(1));
    QVERIFY(!missing.ready);
    QCOMPARE(missing.symbolCount, 3);
    QCOMPARE(missing.readySymbolCount, 2);
    QCOMPARE(missing.missingSymbols, QStringList{QStringLiteral("000001")});
    QTRY_VERIFY_WITH_TIMEOUT(!service.running(), 2000);

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(databasePath));
    QVERIFY(reopened.loadExternalFactorResearchArchiveSummaries().isEmpty());
    QVERIFY(seedLocalSeries(reopened, QStringLiteral("000001"), 10.0, 9.0));
    reopened.close();

    QSignalSpy readySpy(&service,
                        &ExternalFactorResearchService::preflightFinished);
    const quint64 readyRequestId = service.preflight(request);
    QVERIFY(readyRequestId > requestId);
    QVERIFY(readySpy.wait(5000));
    const ExternalFactorResearchPreflightResult ready =
        qvariant_cast<ExternalFactorResearchPreflightResult>(
            readySpy.at(0).at(1));
    QVERIFY2(ready.ready, qPrintable(ready.error));
    QCOMPARE(ready.readySymbolCount, 3);
    QCOMPARE(ready.evaluatedFrameCount, 1);
    QVERIFY(reopened.initialize(databasePath));
    QVERIFY(reopened.loadExternalFactorResearchArchiveSummaries().isEmpty());
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::ExternalFactorResearchServiceTest)

#include "tst_external_factor_research_service.moc"
