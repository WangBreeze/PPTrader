#include <QtTest>

#include "services/AShareIndustryCsvImporter.h"
#include "services/SqliteLocalDataStore.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace pp;

namespace {

QString writeCsv(QTemporaryDir &directory, const QString &name,
                 const QByteArray &content)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    if (file.write(content) != content.size()) return {};
    file.close();
    return path;
}

AShareIndustryImportRequest requestFor(const QString &path)
{
    AShareIndustryImportRequest request;
    request.filePath = path;
    request.classification = QStringLiteral("CSRC");
    request.source = QStringLiteral("fixture");
    request.sourceVersion = QStringLiteral("2026-08-22");
    return request;
}

QByteArray canonicalCsv(const QString &firstIndustry = QStringLiteral("白酒"))
{
    return QStringLiteral(
        "symbol,name,venue,industry_code,industry_name,effective_from,effective_to\n"
        "600519,贵州茅台,SSE,C15,%1,2020-01-01,\n"
        "000001,平安银行,SZSE,J66,银行,2020-01-01,2026-12-31\n")
        .arg(firstIndustry).toUtf8();
}

} // namespace

class AShareIndustryImportTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesCanonicalCsvAndRejectsInvalidInput();
    void storesImmutableDatasetsAndVersionedCorrections();
    void asyncPreviewDoesNotPersist();
    void commitRejectsFileChangedAfterPreview();
};

void AShareIndustryImportTest::parsesCanonicalCsvAndRejectsInvalidInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory, QStringLiteral("industry.csv"),
                                  canonicalCsv());
    const auto parsed = AShareIndustryCsvImporter::parse(requestFor(path));
    QVERIFY(parsed.succeeded);
    QCOMPARE(parsed.linesRead, 3);
    QCOMPARE(parsed.validRecords, 2);
    QCOMPARE(parsed.dataset.memberships.first().instrument.venue,
             QStringLiteral("上交所"));
    QCOMPARE(parsed.dataset.memberships.last().instrument.venue,
             QStringLiteral("深交所"));
    QVERIFY(!parsed.dataset.memberships.first().effectiveTo.isValid());
    QVERIFY(parsed.fileContentSha256.startsWith(QStringLiteral("sha256:")));

    const QString overlap = writeCsv(directory, QStringLiteral("overlap.csv"),
        QStringLiteral(
            "symbol,name,venue,industry_code,industry_name,effective_from,effective_to\n"
            "600519,贵州茅台,SSE,C15,白酒,2020-01-01,2025-12-31\n"
            "600519,贵州茅台,SSE,C15,白酒,2025-01-01,\n").toUtf8());
    auto invalid = AShareIndustryCsvImporter::parse(requestFor(overlap));
    QVERIFY(invalid.succeeded);
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("validation.db"))));
    const auto overlapAnalysis = store.analyzeAShareIndustryDataset(
        invalid.dataset);
    QVERIFY(!overlapAnalysis.valid);
    QVERIFY(overlapAnalysis.error.contains(QStringLiteral("重叠有效期")));

    const QString beijing = writeCsv(directory, QStringLiteral("beijing.csv"),
        QStringLiteral(
            "symbol,name,venue,industry_code,industry_name,effective_from,effective_to\n"
            "830799,艾融软件,BSE,I65,软件,2020-01-01,\n").toUtf8());
    invalid = AShareIndustryCsvImporter::parse(requestFor(beijing));
    QVERIFY(!invalid.succeeded);
    QVERIFY(invalid.dataset.memberships.isEmpty());

    const QString badHeader = writeCsv(directory, QStringLiteral("header.csv"),
        QByteArrayLiteral("symbol,name,venue,industry_code\n"));
    invalid = AShareIndustryCsvImporter::parse(requestFor(badHeader));
    QVERIFY(!invalid.succeeded);
    QCOMPARE(invalid.diagnostics.first().lineNumber, 1);
}

void AShareIndustryImportTest::storesImmutableDatasetsAndVersionedCorrections()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("industry.db"));
    const auto original = AShareIndustryCsvImporter::parse(requestFor(
        writeCsv(directory, QStringLiteral("original.csv"), canonicalCsv())));
    QVERIFY(original.succeeded);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    QCOMPARE(store.schemaVersion(), 25);

    AShareIndustryDataset first = original.dataset;
    bool reused = true;
    QString error;
    QVERIFY2(store.saveAShareIndustryDataset(first, &reused, &error),
             qPrintable(error));
    QVERIFY(!reused);
    QVERIFY(!first.datasetId.isEmpty());
    QCOMPARE(store.loadAShareIndustryDatasets().size(), 1);

    AShareIndustryDataset duplicate = original.dataset;
    QVERIFY(store.saveAShareIndustryDataset(duplicate, &reused, &error));
    QVERIFY(reused);
    QCOMPARE(duplicate.datasetId, first.datasetId);
    QCOMPARE(store.loadAShareIndustryDatasets().size(), 1);

    const auto corrected = AShareIndustryCsvImporter::parse(requestFor(
        writeCsv(directory, QStringLiteral("corrected.csv"),
                 canonicalCsv(QStringLiteral("酒、饮料制造")))));
    QVERIFY(corrected.succeeded);
    AShareIndustryDataset revision = corrected.dataset;
    const auto analysis = store.analyzeAShareIndustryDataset(revision);
    QVERIFY(analysis.valid);
    QVERIFY(analysis.requiresRevisionReason);
    QCOMPARE(analysis.previousDatasetId, first.datasetId);
    QVERIFY(!store.saveAShareIndustryDataset(revision, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("修订原因")));
    QCOMPARE(store.loadAShareIndustryDatasets().size(), 1);

    revision.revisionReason = QStringLiteral("来源文件更正，经人工复核");
    QVERIFY2(store.saveAShareIndustryDataset(revision, &reused, &error),
             qPrintable(error));
    QVERIFY(!reused);
    QCOMPARE(revision.previousDatasetId, first.datasetId);
    const auto history = store.loadAShareIndustryDatasets();
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.first().revisionReason, revision.revisionReason);
    QCOMPARE(history.first().previousDatasetId, first.datasetId);

    const auto snapshot = store.loadAShareIndustrySnapshot(
        revision.datasetId, QDate(2026, 8, 22),
        {QStringLiteral("000001"), QStringLiteral("600519")});
    QVERIFY(snapshot.has_value());
    QCOMPARE(snapshot->resolvedSymbols, 2);
    QVERIFY(snapshot->coverageComplete);
    QCOMPARE(snapshot->industryCodeFor(QStringLiteral("600519")),
             QStringLiteral("C15"));
}

void AShareIndustryImportTest::asyncPreviewDoesNotPersist()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("preview.db"));
    SqliteLocalDataStore bootstrap;
    QVERIFY(bootstrap.initialize(databasePath));
    bootstrap.close();

    const QString path = writeCsv(directory, QStringLiteral("preview.csv"),
                                  canonicalCsv());
    AShareIndustryImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &AShareIndustryImportService::finished);
    QVERIFY(service.preview(requestFor(path)) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
    const auto result = spy.takeFirst().at(1)
                            .value<AShareIndustryImportResult>();
    QVERIFY(result.succeeded);
    QVERIFY(result.previewOnly);
    QVERIFY(result.previewReady);
    QCOMPARE(result.validRecords, 2);
    QTRY_VERIFY_WITH_TIMEOUT(!service.running(), 5000);

    SqliteLocalDataStore verify;
    QVERIFY(verify.initialize(databasePath));
    QVERIFY(verify.loadAShareIndustryDatasets().isEmpty());
}

void AShareIndustryImportTest::commitRejectsFileChangedAfterPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("gate.db"));
    SqliteLocalDataStore bootstrap;
    QVERIFY(bootstrap.initialize(databasePath));
    bootstrap.close();

    const QString path = writeCsv(directory, QStringLiteral("gate.csv"),
                                  canonicalCsv());
    AShareIndustryImportRequest previewRequest = requestFor(path);
    const auto preview = AShareIndustryCsvImporter::parse(previewRequest);
    QVERIFY(preview.succeeded);
    QVERIFY(!writeCsv(directory, QStringLiteral("gate.csv"),
                      canonicalCsv(QStringLiteral("更正行业"))).isEmpty());

    AShareIndustryImportRequest commitRequest = requestFor(path);
    commitRequest.expectedFileContentSha256 = preview.fileContentSha256;
    AShareIndustryImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &AShareIndustryImportService::finished);
    QVERIFY(service.start(commitRequest) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
    const auto result = spy.takeFirst().at(1)
                            .value<AShareIndustryImportResult>();
    QVERIFY(!result.succeeded);
    QVERIFY(!result.diagnostics.isEmpty());
    QVERIFY(result.diagnostics.first().reason.contains(QStringLiteral("改变")));
    QTRY_VERIFY_WITH_TIMEOUT(!service.running(), 5000);

    SqliteLocalDataStore verify;
    QVERIFY(verify.initialize(databasePath));
    QVERIFY(verify.loadAShareIndustryDatasets().isEmpty());
}

QTEST_MAIN(AShareIndustryImportTest)
#include "tst_ashare_industry_import.moc"
