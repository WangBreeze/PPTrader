#include <QtTest>

#include "services/AShareInstrumentDayCsvImporter.h"
#include "services/SqliteLocalDataStore.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>

using namespace pp;

namespace {

QString writeCsv(QTemporaryDir &directory, const QString &name,
                 const QByteArray &content)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    if (file.write(content) != content.size()) return {};
    file.close();
    return path;
}

AShareInstrumentDayImportRun makeRun(
    const AShareInstrumentDayImportResult &parsed)
{
    AShareInstrumentDayImportRun run;
    run.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    run.importedAt = QDateTime::currentDateTimeUtc();
    run.fileName = QFileInfo(parsed.filePath).fileName();
    run.source = parsed.source;
    run.sourceVersion = parsed.sourceVersion;
    run.contentSha256 = parsed.contentSha256;
    run.linesRead = parsed.linesRead;
    run.recordCount = parsed.validRecords;
    run.firstDate = parsed.firstDate;
    run.lastDate = parsed.lastDate;
    return run;
}

} // namespace

class AShareInstrumentDayImportTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesCanonicalCsvAndQuotedReason();
    void rejectsInvalidFactsWithoutPartialRecords();
    void cancellationStopsBeforeReadingRows();
    void transactionRoundTripAndIdempotency();
    void revisionRequiresReasonAndPreservesImmutableRuns();
    void asyncPreviewDoesNotPersist();
    void asyncServicePersistsAndReportsAuditIdentity();
};

void AShareInstrumentDayImportTest::parsesCanonicalCsvAndQuotedReason()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("sse.csv"),
        QByteArray("\xEF\xBB\xBF")
            + QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                             "600519,贵州茅台,SSE,2026-08-20,suspended,\"公告,临时停牌\"\n"
                             "601318,中国平安,上交所,2026-08-21,tradable,恢复交易\n")
                  .toUtf8());
    QVERIFY(!path.isEmpty());

    const auto result = AShareInstrumentDayCsvImporter::parse(
        {path, QStringLiteral("SSE"), QStringLiteral("SSE-export-20260821")});
    QVERIFY(result.succeeded);
    QCOMPARE(result.linesRead, 3);
    QCOMPARE(result.validRecords, 2);
    QVERIFY(result.contentSha256.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(result.contentSha256.size(), 71);
    QCOMPARE(result.firstDate, QDate(2026, 8, 20));
    QCOMPARE(result.lastDate, QDate(2026, 8, 21));
    QCOMPARE(result.records.first().instrument.venue, QStringLiteral("上交所"));
    QCOMPARE(result.records.first().reason, QStringLiteral("公告,临时停牌"));
    QCOMPARE(result.records.first().state, AShareInstrumentDayState::Suspended);
    QCOMPARE(result.records.last().state, AShareInstrumentDayState::Tradable);
}

void AShareInstrumentDayImportTest::rejectsInvalidFactsWithoutPartialRecords()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray header = QByteArrayLiteral(
        "symbol,name,venue,trading_date,state,reason\n");

    const QString mismatch = writeCsv(directory, QStringLiteral("mismatch.csv"),
        header + QStringLiteral(
            "600519,贵州茅台,SZSE,2026-08-20,suspended,错误市场\n").toUtf8());
    auto result = AShareInstrumentDayCsvImporter::parse(
        {mismatch, QStringLiteral("SSE"), QStringLiteral("v1")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());
    QCOMPARE(result.diagnostics.first().lineNumber, 2);

    const QString weekend = writeCsv(directory, QStringLiteral("weekend.csv"),
        header + QStringLiteral(
            "600519,贵州茅台,SSE,2026-08-22,suspended,周末\n").toUtf8());
    result = AShareInstrumentDayCsvImporter::parse(
        {weekend, QStringLiteral("SSE"), QStringLiteral("v1")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());

    const QString duplicate = writeCsv(directory, QStringLiteral("duplicate.csv"),
        header + QStringLiteral(
            "000001,平安银行,SZSE,2026-08-20,suspended,停牌\n"
            "000001,平安银行,SZSE,2026-08-20,tradable,复牌\n").toUtf8());
    result = AShareInstrumentDayCsvImporter::parse(
        {duplicate, QStringLiteral("SZSE"), QStringLiteral("v2")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());

    const QString state = writeCsv(directory, QStringLiteral("state.csv"),
        header + QStringLiteral(
            "000001,平安银行,SZSE,2026-08-20,maybe,未知\n").toUtf8());
    result = AShareInstrumentDayCsvImporter::parse(
        {state, QStringLiteral("SZSE"), QStringLiteral("v2")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());

    const QString invalidUtf8 = writeCsv(directory, QStringLiteral("encoding.csv"),
        header + QByteArray("600519,") + QByteArray(1, char(0xff))
        + QByteArray(",SSE,2026-08-20,suspended,reason\n"));
    result = AShareInstrumentDayCsvImporter::parse(
        {invalidUtf8, QStringLiteral("SSE"), QStringLiteral("v3")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());

    const QString trailingQuote = writeCsv(directory, QStringLiteral("quote.csv"),
        header + QByteArrayLiteral(
            "600519,name,SSE,2026-08-20,suspended,\"reason\"junk\n"));
    result = AShareInstrumentDayCsvImporter::parse(
        {trailingQuote, QStringLiteral("SSE"), QStringLiteral("v4")});
    QVERIFY(!result.succeeded);
    QVERIFY(result.records.isEmpty());
}

void AShareInstrumentDayImportTest::cancellationStopsBeforeReadingRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("cancel.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "600519,贵州茅台,SSE,2026-08-20,suspended,停牌\n").toUtf8());
    QVERIFY(!path.isEmpty());
    const auto result = AShareInstrumentDayCsvImporter::parse(
        {path, QStringLiteral("SSE"), QStringLiteral("v1")}, []() {
            return true;
        });
    QVERIFY(result.cancelled);
    QVERIFY(!result.succeeded);
    QCOMPARE(result.linesRead, 0);
    QVERIFY(result.records.isEmpty());
}

void AShareInstrumentDayImportTest::transactionRoundTripAndIdempotency()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("facts.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "600519,贵州茅台,SSE,2026-08-20,suspended,临时停牌\n"
                       "601318,中国平安,SSE,2026-08-20,tradable,正常交易\n").toUtf8());
    const auto parsed = AShareInstrumentDayCsvImporter::parse(
        {path, QStringLiteral("SSE"), QStringLiteral("SSE-20260821")});
    QVERIFY(parsed.succeeded);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("test.db"))));
    QCOMPARE(store.schemaVersion(), 25);
    AShareInstrumentDayImportRun first = makeRun(parsed);
    bool reused = true;
    QString importError;
    QVERIFY2(store.importAShareInstrumentDayRecords(
                 first, parsed.records, &reused, &importError),
             qPrintable(importError));
    QVERIFY(!reused);
    const QString persistedRunId = first.runId;

    const auto facts = store.loadAShareInstrumentDayRecords(
        {QStringLiteral("600519"), QStringLiteral("601318")},
        QDate(2026, 8, 20), QDate(2026, 8, 20));
    QCOMPARE(facts.size(), 2);
    const auto history = store.loadAShareInstrumentDayImportRuns();
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().runId, persistedRunId);
    QCOMPARE(history.first().contentSha256, parsed.contentSha256);

    AShareInstrumentDayImportRun second = makeRun(parsed);
    QVERIFY(store.importAShareInstrumentDayRecords(second, parsed.records, &reused));
    QVERIFY(reused);
    QCOMPARE(second.runId, persistedRunId);
    QCOMPARE(store.loadAShareInstrumentDayImportRuns().size(), 1);

    AShareInstrumentDayImportRun invalid = makeRun(parsed);
    invalid.recordCount = parsed.records.size() + 1;
    QVERIFY(!store.importAShareInstrumentDayRecords(invalid, parsed.records));
    QCOMPARE(store.loadAShareInstrumentDayImportRuns().size(), 1);
    QCOMPARE(store.loadAShareInstrumentDayRecords(
        {QStringLiteral("600519"), QStringLiteral("601318")},
        QDate(2026, 8, 20), QDate(2026, 8, 20)).size(), 2);
}

void AShareInstrumentDayImportTest::revisionRequiresReasonAndPreservesImmutableRuns()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString originalPath = writeCsv(directory, QStringLiteral("original.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "600519,贵州茅台,SSE,2026-08-20,suspended,临时停牌\n").toUtf8());
    const QString correctedPath = writeCsv(directory, QStringLiteral("corrected.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "600519,贵州茅台,SSE,2026-08-20,tradable,公告更正为正常交易\n").toUtf8());
    const auto original = AShareInstrumentDayCsvImporter::parse(
        {originalPath, QStringLiteral("SSE"), QStringLiteral("SSE-v1")});
    const auto corrected = AShareInstrumentDayCsvImporter::parse(
        {correctedPath, QStringLiteral("SSE"), QStringLiteral("SSE-v2")});
    QVERIFY(original.succeeded);
    QVERIFY(corrected.succeeded);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("revision.db"))));
    AShareInstrumentDayImportRun first = makeRun(original);
    QVERIFY(store.importAShareInstrumentDayRecords(first, original.records));
    QCOMPARE(store.loadAShareInstrumentDayImportRunRecords(first.runId).size(), 1);

    const auto preview = store.analyzeAShareInstrumentDayImport(corrected.records);
    QVERIFY(preview.valid);
    QCOMPARE(preview.newRecords, 0);
    QCOMPARE(preview.unchangedRecords, 0);
    QCOMPARE(preview.correctedRecords, 1);

    AShareInstrumentDayImportRun rejected = makeRun(corrected);
    QString error;
    QVERIFY(!store.importAShareInstrumentDayRecords(
        rejected, corrected.records, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("修订原因")));
    QCOMPARE(store.loadAShareInstrumentDayImportRuns().size(), 1);
    QCOMPARE(store.loadAShareInstrumentDayRecords(
        {QStringLiteral("600519")}, QDate(2026, 8, 20), QDate(2026, 8, 20))
                 .first().state,
             AShareInstrumentDayState::Suspended);

    AShareInstrumentDayImportRun revision = makeRun(corrected);
    revision.revisionReason = QStringLiteral("交易所公告更正，人工复核");
    QVERIFY(store.importAShareInstrumentDayRecords(
        revision, corrected.records, nullptr, &error));
    QCOMPARE(revision.correctedRecords, 1);
    QCOMPARE(store.loadAShareInstrumentDayImportRuns().size(), 2);
    const auto originalItems = store.loadAShareInstrumentDayImportRunRecords(first.runId);
    const auto revisionItems = store.loadAShareInstrumentDayImportRunRecords(revision.runId);
    QCOMPARE(originalItems.size(), 1);
    QCOMPARE(revisionItems.size(), 1);
    QCOMPARE(originalItems.first().state, AShareInstrumentDayState::Suspended);
    QCOMPARE(revisionItems.first().state, AShareInstrumentDayState::Tradable);
    QCOMPARE(store.loadAShareInstrumentDayRecords(
        {QStringLiteral("600519")}, QDate(2026, 8, 20), QDate(2026, 8, 20))
                 .first().state,
             AShareInstrumentDayState::Tradable);
}

void AShareInstrumentDayImportTest::asyncPreviewDoesNotPersist()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("preview.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "000001,平安银行,SZSE,2026-08-20,suspended,停牌\n").toUtf8());
    const QString databasePath = directory.filePath(QStringLiteral("preview.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
    }

    AShareInstrumentDayImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &AShareInstrumentDayImportService::finished);
    const quint64 requestId = service.preview(
        {path, QStringLiteral("SZSE"), QStringLiteral("SZSE-preview")});
    QVERIFY(requestId > 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto result = qvariant_cast<AShareInstrumentDayImportResult>(
        spy.takeFirst().at(1));
    QVERIFY(result.succeeded);
    QVERIFY(result.previewOnly);
    QVERIFY(result.previewReady);
    QCOMPARE(result.newRecords, 1);
    QCOMPARE(result.writtenRecords, 0);

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(databasePath));
    QVERIFY(reopened.loadAShareInstrumentDayImportRuns().isEmpty());
    QVERIFY(reopened.loadAShareInstrumentDayRecords(
        {QStringLiteral("000001")}, QDate(2026, 8, 20), QDate(2026, 8, 20))
                .isEmpty());
}

void AShareInstrumentDayImportTest::asyncServicePersistsAndReportsAuditIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("szse.csv"),
        QStringLiteral("symbol,name,venue,trading_date,state,reason\n"
                       "000001,平安银行,SZSE,2026-08-20,suspended,临时停牌\n").toUtf8());
    const QString databasePath = directory.filePath(QStringLiteral("async.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
    }

    AShareInstrumentDayImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &AShareInstrumentDayImportService::finished);
    const quint64 requestId = service.start(
        {path, QStringLiteral("SZSE"), QStringLiteral("SZSE-20260821")});
    QVERIFY(requestId > 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const auto arguments = spy.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), requestId);
    const auto result = qvariant_cast<AShareInstrumentDayImportResult>(arguments.at(1));
    QVERIFY(result.succeeded);
    QVERIFY(!result.reused);
    QCOMPARE(result.writtenRecords, 1);
    QVERIFY(!result.runId.isEmpty());

    SqliteLocalDataStore reopened;
    QVERIFY(reopened.initialize(databasePath));
    QCOMPARE(reopened.loadAShareInstrumentDayImportRuns().size(), 1);
    QCOMPARE(reopened.loadAShareInstrumentDayImportRuns().first().runId,
             result.runId);
}

QTEST_MAIN(AShareInstrumentDayImportTest)
#include "tst_ashare_instrument_day_import.moc"
