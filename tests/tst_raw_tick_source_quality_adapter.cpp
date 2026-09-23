#include "services/RawTickSourceQualityAdapter.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

namespace pp {

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString sha256(const QByteArray &bytes)
{
    return QStringLiteral("sha256:")
        + QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString writeFixture(const QTemporaryDir &directory, const QByteArray &contents)
{
    const QString path = directory.filePath(QStringLiteral("600519.csv"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        return {};
    file.close();
    return path;
}

RawTickSourceQualityRequest requestFor(const QString &path)
{
    RawTickSourceQualityRequest request;
    request.filePath = path;
    request.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                          Market::AShare, QStringLiteral("上交所")};
    request.tradingDate = QDate(2026, 8, 7);
    request.volumeUnit = RawTickSourceVolumeUnit::Lots;
    return request;
}

} // namespace

class RawTickSourceQualityAdapterTest final : public QObject {
    Q_OBJECT

private slots:
    void inspectsCanonicalFileWithoutChangingSource();
    void requiresExplicitUnitsAndVersionedNumericDirectionMapping();
    void aggregatesExplicitFilesIntoMachineReadableManifest();
    void boundsDiagnosticsWhileStreamingLargeInput();
    void acceptsProjectExtendedAfternoonSessionUntil1530();
};

void RawTickSourceQualityAdapterTest::inspectsCanonicalFileWithoutChangingSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original(
        "time,price,volume,amount,type\n"
        "09:30:00,10.00,2,2000,B\n"
        "09:30:01,10.01,3,3003,S\n"
        "09:30:02,10.02,4,4008,M\n");
    const QString path = writeFixture(directory, original);
    QVERIFY(!path.isEmpty());

    const RawTickSourceQualityReport report =
        RawTickSourceQualityAdapter::inspect(requestFor(path));

    QVERIFY(report.inspected);
    QVERIFY(report.normalizationReady);
    QCOMPARE(report.qualityStatus, QStringLiteral("pass"));
    QCOMPARE(report.detectedFormat, QStringLiteral("english_tick5"));
    QCOMPARE(report.contentSha256, sha256(original));
    QCOMPARE(report.linesRead, 4);
    QCOMPARE(report.metadataLines, 1);
    QCOMPARE(report.validRecords, 3);
    QCOMPARE(report.skippedRecords, 0);
    QCOMPARE(report.buyRecords, 1);
    QCOMPARE(report.sellRecords, 1);
    QCOMPARE(report.neutralRecords, 1);
    QCOMPARE(readAll(path), original);
    QCOMPARE(sha256(readAll(path)), report.contentSha256);
}

void RawTickSourceQualityAdapterTest::requiresExplicitUnitsAndVersionedNumericDirectionMapping()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original(
        "time,price,volume,amount,type\n"
        "09:30:00,10.00,2,2000,1\n"
        "09:30:01,10.01,3,3003,2\n"
        "09:30:02,10.02,4,4008,4\n");
    const QString path = writeFixture(directory, original);
    QVERIFY(!path.isEmpty());

    RawTickSourceQualityRequest unknownUnit = requestFor(path);
    unknownUnit.volumeUnit = RawTickSourceVolumeUnit::Unknown;
    unknownUnit.explicitDirectionMappings = {
        {QStringLiteral("1"), RawTradeDirection::Buy},
        {QStringLiteral("2"), RawTradeDirection::Sell},
        {QStringLiteral("4"), RawTradeDirection::Neutral}};
    unknownUnit.directionMappingVersion = QStringLiteral("vendor-contract-v1");
    const auto unknownReport = RawTickSourceQualityAdapter::inspect(unknownUnit);
    QVERIFY(unknownReport.inspected);
    QVERIFY(!unknownReport.normalizationReady);
    QCOMPARE(unknownReport.qualityStatus, QStringLiteral("reject"));

    RawTickSourceQualityRequest unversioned = unknownUnit;
    unversioned.volumeUnit = RawTickSourceVolumeUnit::Lots;
    unversioned.directionMappingVersion.clear();
    const auto unversionedReport = RawTickSourceQualityAdapter::inspect(unversioned);
    QVERIFY(!unversionedReport.normalizationReady);
    QCOMPARE(unversionedReport.mappedDirectionRecords, 3);
    QCOMPARE(unversionedReport.qualityStatus, QStringLiteral("reject"));

    RawTickSourceQualityRequest lots = unversioned;
    lots.directionMappingVersion = QStringLiteral("vendor-contract-v1");
    const auto lotsReport = RawTickSourceQualityAdapter::inspect(lots);
    QVERIFY(lotsReport.normalizationReady);
    QCOMPARE(lotsReport.buyRecords, 1);
    QCOMPARE(lotsReport.sellRecords, 1);
    QCOMPARE(lotsReport.neutralRecords, 1);
    QCOMPARE(lotsReport.totalSourceVolume, 9.0);
    QCOMPARE(lotsReport.normalizedVolumeShares, 900.0);

    RawTickSourceQualityRequest shares = lots;
    shares.volumeUnit = RawTickSourceVolumeUnit::Shares;
    const auto sharesReport = RawTickSourceQualityAdapter::inspect(shares);
    QVERIFY(sharesReport.normalizationReady);
    QCOMPARE(sharesReport.normalizedVolumeShares, 9.0);
    QCOMPARE(readAll(path), original);
}

void RawTickSourceQualityAdapterTest::aggregatesExplicitFilesIntoMachineReadableManifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString goodPath = writeFixture(
        directory,
        QByteArray("09:30:00,10.00,2,2000,B\n"
                   "09:30:01,10.01,3,3003,S\n"));
    QVERIFY(!goodPath.isEmpty());
    const QString dirtyPath = directory.filePath(QStringLiteral("000002.csv"));
    QFile dirtyFile(dirtyPath);
    QVERIFY(dirtyFile.open(QIODevice::WriteOnly));
    QVERIFY(dirtyFile.write("09:30:00,10.00,2,2000,B\n"
                            "broken,row\n") > 0);
    dirtyFile.close();

    RawTickSourceQualityRequest good = requestFor(goodPath);
    RawTickSourceQualityRequest dirty = requestFor(dirtyPath);
    dirty.instrument.symbol = QStringLiteral("000002");
    RawTickSourceQualityRequest missing = requestFor(
        directory.filePath(QStringLiteral("000003.csv")));
    missing.instrument.symbol = QStringLiteral("000003");

    const RawTickSourceQualityManifest manifest =
        RawTickSourceQualityAdapter::inspectBatch({good, dirty, missing});

    QCOMPARE(manifest.totalFiles, 3);
    QCOMPARE(manifest.readyFiles, 1);
    QCOMPARE(manifest.rejectedFiles, 2);
    QCOMPARE(manifest.totalValidRecords, 3);
    QCOMPARE(manifest.totalSkippedRecords, 1);
    QCOMPARE(manifest.reports.size(), 3);
    QCOMPARE(manifest.reports.at(0).symbol, QStringLiteral("600519"));
    QCOMPARE(manifest.reports.at(0).actionCode, QStringLiteral("ready"));
    QCOMPARE(manifest.reports.at(1).actionCode, QStringLiteral("clean_reexport"));
    QCOMPARE(manifest.reports.at(2).actionCode, QStringLiteral("missing_source_file"));

    const QByteArray json = manifest.toJson();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("totalFiles")).toInt(), 3);
    QCOMPARE(document.object().value(QStringLiteral("reports")).toArray().size(), 3);
    QCOMPARE(readAll(goodPath),
             QByteArray("09:30:00,10.00,2,2000,B\n"
                        "09:30:01,10.01,3,3003,S\n"));
}

void RawTickSourceQualityAdapterTest::boundsDiagnosticsWhileStreamingLargeInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray original;
    original.reserve(1'600'000);
    for (int index = 0; index < 50'000; ++index) {
        original.append("09:30:00,10.00,2,2000,B\n");
        if (index % 10'000 == 0) original.append("broken,row\n");
    }
    const QString path = writeFixture(directory, original);
    QVERIFY(!path.isEmpty());

    RawTickSourceQualityRequest request = requestFor(path);
    request.maximumDiagnostics = 2;
    const RawTickSourceQualityReport report =
        RawTickSourceQualityAdapter::inspect(request);

    QVERIFY(report.inspected);
    QVERIFY(report.normalizationReady);
    QCOMPARE(report.qualityStatus, QStringLiteral("warning"));
    QCOMPARE(report.validRecords, 50'000);
    QCOMPARE(report.skippedRecords, 5);
    QCOMPARE(report.diagnosticTotalCount, 5);
    QCOMPARE(report.diagnostics.size(), 2);
    QVERIFY(report.diagnosticsTruncated);
    QCOMPARE(readAll(path), original);
}

void RawTickSourceQualityAdapterTest::acceptsProjectExtendedAfternoonSessionUntil1530()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray original(
        "09:24:59,10.00,1,1000,B\n"
        "09:25:00,10.00,1,1000,B\n"
        "09:29:59,10.00,1,1000,B\n"
        "09:30:00,10.00,1,1000,B\n"
        "11:30:00,10.00,1,1000,B\n"
        "11:30:01,10.00,1,1000,B\n"
        "12:59:59,10.00,1,1000,B\n"
        "13:00:00,10.00,1,1000,B\n"
        "15:00:00,10.00,1,1000,B\n"
        "15:00:01,10.00,1,1000,B\n"
        "15:05:00,10.00,1,1000,B\n"
        "15:29:59,10.00,1,1000,B\n"
        "15:30:00,10.00,1,1000,B\n"
        "15:30:01,10.00,1,1000,B\n");
    const QString path = writeFixture(directory, original);
    QVERIFY(!path.isEmpty());

    const RawTickSourceQualityReport report =
        RawTickSourceQualityAdapter::inspect(requestFor(path));

    QVERIFY(report.inspected);
    QVERIFY(!report.normalizationReady);
    QCOMPARE(report.qualityStatus, QStringLiteral("reject"));
    QCOMPARE(report.validRecords, 10);
    QCOMPARE(report.skippedRecords, 4);
    QCOMPARE(report.outsideSessionRecords, 4);
    QCOMPARE(report.diagnosticTotalCount, 4);
    QCOMPARE(readAll(path), original);
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::RawTickSourceQualityAdapterTest)

#include "tst_raw_tick_source_quality_adapter.moc"
