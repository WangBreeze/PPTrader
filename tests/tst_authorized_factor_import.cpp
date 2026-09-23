#include "services/AuthorizedFactorCsvImporter.h"
#include "services/SqliteLocalDataStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace pp;

class AuthorizedFactorImportTest final : public QObject {
    Q_OBJECT

private slots:
    void userCanPreviewAuthorizedFactorCsv();
    void identicalObservationIsDeduplicated();
    void previewAndCommitAreAsynchronousAndExplicit();
    void timezoneNaiveObservationReportsItsLine();
    void conflictingObservationRejectsWholePreview();
};

void AuthorizedFactorImportTest::userCanPreviewAuthorizedFactorCsv()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("factor.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(
                 "timestamp,symbol,value\n"
                 "2026-08-20T15:00:00+08:00,600519,1.25\n"
                 "2026-08-20T07:00:00Z,000001,-0.5\n"),
             qint64(94));
    file.close();

    AuthorizedFactorImportRequest request;
    request.filePath = path;
    request.factorKey = QStringLiteral("quality.score");
    request.factorDefinitionSha256 = QStringLiteral("sha256:definition");
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = QStringLiteral("vendor-2026-08-20");
    request.sourceReference = QStringLiteral("用户授权研究数据 v1");
    request.previewOnly = true;

    const AuthorizedFactorImportResult result =
        AuthorizedFactorCsvImporter::parse(request);

    QVERIFY2(result.succeeded, qPrintable(result.diagnostics.isEmpty()
                                              ? QString()
                                              : result.diagnostics.first().reason));
    QCOMPARE(result.linesRead, qint64(3));
    QCOMPARE(result.validRecords, qint64(2));
    QCOMPARE(result.observations.size(), 2);
    QCOMPARE(result.observations.at(0).timestamp,
             QDateTime::fromString(QStringLiteral("2026-08-20T07:00:00Z"),
                                   Qt::ISODate));
    QCOMPARE(result.observations.at(0).symbol, QStringLiteral("600519"));
    QCOMPARE(result.observations.at(0).value, 1.25);
    QCOMPARE(result.observations.at(1).symbol, QStringLiteral("000001"));
    QCOMPARE(result.observations.at(1).value, -0.5);
    QVERIFY(result.contentSha256.startsWith(QStringLiteral("sha256:")));
}

void AuthorizedFactorImportTest::timezoneNaiveObservationReportsItsLine()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("naive.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("timestamp,symbol,value\n"
               "2026-08-20T07:00:00,600519,1.25\n");
    file.close();
    AuthorizedFactorImportRequest request;
    request.filePath = path;
    request.factorKey = QStringLiteral("quality.score");
    request.factorDefinitionSha256 = QStringLiteral("sha256:definition");
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = QStringLiteral("vendor-1");
    request.sourceReference = QStringLiteral("用户授权数据");

    const auto result = AuthorizedFactorCsvImporter::parse(request);

    QVERIFY(!result.succeeded);
    QCOMPARE(result.diagnostics.size(), 1);
    QCOMPARE(result.diagnostics.first().lineNumber, qint64(2));
    QVERIFY(result.diagnostics.first().reason.contains(QStringLiteral("时区")));
    QVERIFY(result.observations.isEmpty());
}

void AuthorizedFactorImportTest::conflictingObservationRejectsWholePreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("conflict.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("timestamp,symbol,value\n"
               "2026-08-20T07:00:00Z,600519,1.25\n"
               "2026-08-20T07:00:00Z,600519,1.5\n");
    file.close();
    AuthorizedFactorImportRequest request;
    request.filePath = path;
    request.factorKey = QStringLiteral("quality.score");
    request.factorDefinitionSha256 = QStringLiteral("sha256:definition");
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = QStringLiteral("vendor-1");
    request.sourceReference = QStringLiteral("用户授权数据");

    const auto result = AuthorizedFactorCsvImporter::parse(request);

    QVERIFY(!result.succeeded);
    QCOMPARE(result.diagnostics.first().lineNumber, qint64(3));
    QVERIFY(result.diagnostics.first().reason.contains(QStringLiteral("冲突")));
    QVERIFY(result.observations.isEmpty());
}

void AuthorizedFactorImportTest::previewAndCommitAreAsynchronousAndExplicit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath = directory.filePath(QStringLiteral("factor.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
    }
    const QString path = directory.filePath(QStringLiteral("factor.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("timestamp,symbol,value\n"
               "2026-08-20T07:00:00Z,600519,1.25\n");
    file.close();

    AuthorizedFactorImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy finished(&service, &AuthorizedFactorImportService::finished);
    QVERIFY(finished.isValid());
    AuthorizedFactorImportRequest request;
    request.filePath = path;
    request.factorKey = QStringLiteral("quality.score");
    request.factorDefinitionSha256 =
        QStringLiteral("sha256:") + QString(64, 'd');
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = QStringLiteral("vendor-1");
    request.sourceReference = QStringLiteral("用户授权数据");

    const quint64 previewId = service.preview(request);
    QVERIFY(previewId > 0);
    QVERIFY(service.running());
    QCOMPARE(finished.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    const auto preview = qvariant_cast<AuthorizedFactorImportResult>(
        finished.takeFirst().at(1));
    QVERIFY(preview.succeeded);
    QVERIFY(preview.previewOnly);
    QVERIFY(preview.previewReady);
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QVERIFY(store.loadExternalFactorDatasets().isEmpty());
    }

    request.expectedContentSha256 = preview.contentSha256;
    const quint64 commitId = service.start(request);
    QVERIFY(commitId > previewId);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    const auto committed = qvariant_cast<AuthorizedFactorImportResult>(
        finished.takeFirst().at(1));
    QVERIFY2(committed.succeeded,
             qPrintable(committed.diagnostics.isEmpty()
                            ? QString() : committed.diagnostics.first().reason));
    QVERIFY(!committed.previewOnly);
    QCOMPARE(committed.writtenRecords, qint64(1));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(databasePath));
        QCOMPARE(store.loadExternalFactorDatasets().size(), 1);
    }
}

void AuthorizedFactorImportTest::identicalObservationIsDeduplicated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("duplicate.csv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("timestamp,symbol,value\n"
               "2026-08-20T07:00:00Z,600519,1.25\n"
               "2026-08-20T07:00:00Z,600519,1.25\n");
    file.close();

    AuthorizedFactorImportRequest request;
    request.filePath = path;
    request.factorKey = QStringLiteral("quality.score");
    request.factorDefinitionSha256 = QStringLiteral("sha256:definition");
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = QStringLiteral("vendor-2026-08-20");
    request.sourceReference = QStringLiteral("用户授权研究数据 v1");

    const auto result = AuthorizedFactorCsvImporter::parse(request);

    QVERIFY2(result.succeeded, qPrintable(result.diagnostics.isEmpty()
                                              ? QString()
                                              : result.diagnostics.first().reason));
    QCOMPARE(result.validRecords, qint64(1));
    QCOMPARE(result.duplicateRecords, qint64(1));
}

QTEST_MAIN(AuthorizedFactorImportTest)
#include "tst_authorized_factor_import.moc"
