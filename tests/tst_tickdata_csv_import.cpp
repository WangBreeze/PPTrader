#include "services/SqliteLocalDataStore.h"
#include "services/TickDataCsvImporter.h"
#include "services/TickDataBatchPlanner.h"
#include "services/AShareTradingCalendar.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimeZone>

namespace pp {

namespace {

const QDate TradingDate(2026, 8, 7);

Instrument instrument()
{
    return {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
            Market::AShare, QStringLiteral("上交所")};
}

QString writeCsv(const QString &root, const QString &folder,
                 const QString &contents)
{
    if (!QDir(root).mkpath(folder)) return {};
    const QString path = QDir(root).filePath(folder + QStringLiteral("/600519.csv"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    const QByteArray bytes = contents.toUtf8();
    if (file.write(bytes) != bytes.size()) return {};
    file.close();
    return path;
}

TickDataImportRequest tickRequest(const QString &root, int periodSeconds = 60)
{
    TickDataImportRequest request;
    request.rootDirectory = root;
    request.instrument = instrument();
    request.tradingDate = TradingDate;
    request.format = TickDataCsvFormat::Tick;
    request.periodSeconds = periodSeconds;
    return request;
}

TickDataImportRequest fiveMinuteRequest(const QString &root,
                                        FiveMinuteVolumeUnit unit)
{
    TickDataImportRequest request = tickRequest(root, 300);
    request.format = TickDataCsvFormat::FiveMinute;
    request.fiveMinuteVolumeUnit = unit;
    return request;
}

} // namespace

class TickDataCsvImportTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesTickRowsWithTimezoneLotsAndDirections();
    void parsesStandardEnglishHeaderWithoutCountingItAsBadData();
    void parsesChineseHeaderTickRows();
    void aggregatesSessionsWithoutCrossingLunch();
    void parsesFiveMinuteUnitsAndRejectsUnknown();
    void reportsStructuredLineDiagnostics();
    void upsertsIdempotentlyAndRefreshesCatalogData();
    void streamsLargeSyntheticFileWithBoundedLineBuffer();
    void reportsQualityForOutOfOrderAndMissingBars();
    void plansOnlyRequestedWeekdaysAndResetsRetryableFiles();
    void filtersOfficialCalendarAndReportsFallback();
    void preflightsCoverageAndRestoresCompletedDates();
    void persistsRunItemsAndFiltersHistory();
    void continuesIndependentFilesAndPersistsRunSummary();
    void cancelsStreamingParseBetweenLines();
    void servicePersistsRawTicksTransactionally();
};

void TickDataCsvImportTest::parsesTickRowsWithTimezoneLotsAndDirections()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:25:00,10,2,2000,B\n"
                       "09:30:00,10,1,1000,S\n"
                       "09:30:30,11,2,2200,M\n"
                       "09:31:00,9,1,900,B\n"
                       "15:00:00,12,3,3600,S\n"
                       "15:05:00,13,1,1300,B\n"
                       "10:00:00,10,1,1000,X\n"));
    QVERIFY(!path.isEmpty());

    const TickDataImportResult result = TickDataCsvImporter::parse(tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.filePath, path);
    QCOMPARE(result.linesRead, 7);
    QCOMPARE(result.validRecords, 6);
    QCOMPARE(result.skippedRecords, 1);
    QCOMPARE(result.candles.size(), 5);
    QCOMPARE(result.candles.at(0).startTime,
             QDateTime(TradingDate, QTime(9, 25), QTimeZone("Asia/Shanghai")).toUTC());
    QCOMPARE(result.candles.at(0).volume, 200.0);
    QCOMPARE(result.candles.at(0).buyVolume, 200.0);
    QVERIFY(result.candles.at(0).directionalVolumeKnown);

    const Candle &opening = result.candles.at(1);
    QCOMPARE(opening.open, 10.0);
    QCOMPARE(opening.high, 11.0);
    QCOMPARE(opening.low, 10.0);
    QCOMPARE(opening.close, 11.0);
    QCOMPARE(opening.volume, 300.0);
    QCOMPARE(opening.sellVolume, 100.0);
    QCOMPARE(opening.neutralVolume, 200.0);
    QCOMPARE(result.candles.at(3).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 0));
    QCOMPARE(result.candles.at(3).sellVolume, 300.0);
    QCOMPARE(result.candles.last().startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 5));
    QCOMPARE(result.candles.last().buyVolume, 100.0);
}

void TickDataCsvImportTest::parsesStandardEnglishHeaderWithoutCountingItAsBadData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("time,price,volume,amount,type\n"
                       "09:25:00,10.00,2,2000,B\n"
                       "09:30:00,10.10,3,3030,S\n"
                       "09:30:30,10.20,4,4080,M\n")).isEmpty());

    const TickDataImportResult result = TickDataCsvImporter::parse(
        tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.linesRead, 4);
    QCOMPARE(result.validRecords, 3);
    QCOMPARE(result.skippedRecords, 0);
    QVERIFY(result.diagnostics.isEmpty());
    QCOMPARE(result.candles.size(), 2);
}

void TickDataCsvImportTest::parsesChineseHeaderTickRows()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("成交时间,成交价格,价格变动,成交量,成交金额,性质\n"
                       "09:25:04,10.00,0.0,2,2000,卖盘\n"
                       "09:30:00,10.10,0.1,3,3030,买盘\n"
                       "09:30:30,10.20,0.1,4,4080,中性盘\n"
                       "15:05:00,10.30,0.1,1,1030,卖盘\n")).isEmpty());

    const TickDataImportResult result = TickDataCsvImporter::parse(
        tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.linesRead, 5);
    QCOMPARE(result.validRecords, 4);
    QCOMPARE(result.skippedRecords, 0);
    QCOMPARE(result.candles.size(), 3);
    QCOMPARE(result.candles.first().volume, 200.0);
    QCOMPARE(result.candles.first().sellVolume, 200.0);
    QCOMPARE(result.candles.at(1).open, 10.10);
    QCOMPARE(result.candles.at(1).close, 10.20);
    QCOMPARE(result.candles.at(1).buyVolume, 300.0);
    QCOMPARE(result.candles.at(1).neutralVolume, 400.0);
    QCOMPARE(result.candles.last().sellVolume, 100.0);
}

void TickDataCsvImportTest::aggregatesSessionsWithoutCrossingLunch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("11:29:59,10,1,1000,B\n"
                       "11:30:00,11,1,1100,S\n"
                       "12:59:59,99,1,9900,M\n"
                       "13:00:00,12,1,1200,M\n"
                       "15:00:00,13,1,1300,B\n"
                       "15:05:00,14,1,1400,S\n"
                       "15:30:00,15,1,1500,B\n"
                       "15:30:01,16,1,1600,S\n")).isEmpty());
    const TickDataImportResult result = TickDataCsvImporter::parse(
        tickRequest(directory.path(), 300));
    QVERIFY(result.succeeded);
    QCOMPARE(result.validRecords, 6);
    QCOMPARE(result.skippedRecords, 2);
    QCOMPARE(result.candles.size(), 5);
    QCOMPARE(result.candles.at(0).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(11, 25));
    QCOMPARE(result.candles.at(0).open, 10.0);
    QCOMPARE(result.candles.at(0).close, 11.0);
    QCOMPARE(result.candles.at(1).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(13, 0));
    QCOMPARE(result.candles.at(2).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 0));
    QCOMPARE(result.candles.at(3).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 5));
    QCOMPARE(result.candles.at(4).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 25));
}

void TickDataCsvImportTest::parsesFiveMinuteUnitsAndRejectsUnknown()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807+5min"),
        QStringLiteral("time,open,close,high,low,volume\n"
                       "09:25,10,10.2,10.3,9.9,2\n"
                       "09:30,10.2,10.4,10.5,10.1,3\n"
                       "11:30,10.4,10.1,10.6,10.0,4\n"
                       "12:00,10,10,10,10,5\n"
                       "13:00,10.1,10.5,10.6,10.0,6\n"
                       "15:00,10.5,10.8,10.9,10.4,7\n"
                       "15:05,10.8,10.9,11.0,10.7,8\n")).isEmpty());

    const TickDataImportResult unknown = TickDataCsvImporter::parse(
        fiveMinuteRequest(directory.path(), FiveMinuteVolumeUnit::Unknown));
    QVERIFY(!unknown.succeeded);
    QVERIFY(!unknown.diagnostics.isEmpty());
    QCOMPARE(unknown.diagnostics.first().lineNumber, 1);
    QVERIFY(unknown.diagnostics.first().reason.contains(QStringLiteral("单位未知")));

    const TickDataImportResult shares = TickDataCsvImporter::parse(
        fiveMinuteRequest(directory.path(), FiveMinuteVolumeUnit::Shares));
    QVERIFY(shares.succeeded);
    QCOMPARE(shares.validRecords, 6);
    QCOMPARE(shares.skippedRecords, 1);
    QCOMPARE(shares.candles.size(), 6);
    QCOMPARE(shares.candles.first().volume, 2.0);
    QVERIFY(!shares.candles.first().directionalVolumeKnown);
    QCOMPARE(shares.candles.at(2).startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(11, 25));
    QCOMPARE(shares.candles.last().startTime.toTimeZone(QTimeZone("Asia/Shanghai")).time(),
             QTime(15, 5));

    const TickDataImportResult lots = TickDataCsvImporter::parse(
        fiveMinuteRequest(directory.path(), FiveMinuteVolumeUnit::Lots));
    QVERIFY(lots.succeeded);
    QCOMPARE(lots.candles.first().volume, 200.0);
    QCOMPARE(lots.source, QStringLiteral("tickdata_csv_5min_lots_to_shares"));
}

void TickDataCsvImportTest::reportsStructuredLineDiagnostics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,10,1,1000,B\n"
                       "bad,row\n"
                       "09:31:00,-1,1,1000,S\n"));
    QVERIFY(!path.isEmpty());
    const TickDataImportResult result = TickDataCsvImporter::parse(tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.skippedRecords, 2);
    QCOMPARE(result.diagnostics.size(), 2);
    QCOMPARE(result.diagnostics.at(0).path, path);
    QCOMPARE(result.diagnostics.at(0).lineNumber, 2);
    QVERIFY(!result.diagnostics.at(0).reason.isEmpty());
    QCOMPARE(result.diagnostics.at(1).lineNumber, 3);
}

void TickDataCsvImportTest::upsertsIdempotentlyAndRefreshesCatalogData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,10,1,1000,B\n"
                       "09:30:20,11,2,2200,S\n"
                       "09:31:00,12,3,3600,M\n")).isEmpty());
    const TickDataImportResult result = TickDataCsvImporter::parse(tickRequest(directory.path()));
    QVERIFY(result.succeeded);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("tickdata.db"))));
    QVERIFY(store.upsertCandles(result.candles));
    QVERIFY(store.upsertCandles(result.candles));
    const QVector<LocalHistoryDataset> catalog = store.localHistoryCatalog();
    QCOMPARE(catalog.size(), 1);
    QCOMPARE(catalog.first().candleCount, 2);
    QCOMPARE(catalog.first().periodSeconds, 60);
    const QVector<Candle> restored = store.loadRecentCandles(instrument(), 60, 10);
    QCOMPARE(restored.size(), 2);
    QCOMPARE(restored.first().source, QStringLiteral("tickdata_csv_tick_1m"));
    QVERIFY(restored.first().directionalVolumeKnown);
    QCOMPARE(restored.first().buyVolume, 100.0);
    QCOMPARE(restored.first().sellVolume, 200.0);
}

void TickDataCsvImportTest::streamsLargeSyntheticFileWithBoundedLineBuffer()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("20260807")));
    QFile file(QDir(directory.path()).filePath(QStringLiteral("20260807/600519.csv")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    constexpr int RowCount = 100000;
    for (int index = 0; index < RowCount; ++index) {
        const QTime time = QTime(9, 30).addSecs(index % 7200);
        stream << time.toString(QStringLiteral("HH:mm:ss"))
               << ",10.5,1,1050," << (index % 3 == 0 ? "B" : index % 3 == 1 ? "S" : "M")
               << '\n';
    }
    stream.flush();
    file.close();

    const TickDataImportResult result = TickDataCsvImporter::parse(tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.linesRead, static_cast<qint64>(RowCount));
    QCOMPARE(result.validRecords, static_cast<qint64>(RowCount));
    QCOMPARE(result.skippedRecords, 0);
    QCOMPARE(result.candles.size(), 120);
    QVERIFY(result.maximumLineBytes < 128);
}

void TickDataCsvImportTest::reportsQualityForOutOfOrderAndMissingBars()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:31:00,11,1,1100,B\n"
                       "09:30:00,10,1,1000,S\n")).isEmpty());
    const TickDataImportResult result = TickDataCsvImporter::parse(
        tickRequest(directory.path()));
    QVERIFY(result.succeeded);
    QCOMPARE(result.outOfOrderRecords, 1);
    QCOMPARE(result.expectedContinuousBars, 270);
    QCOMPARE(result.missingContinuousBars, 268);
    QCOMPARE(result.qualityStatus, QStringLiteral("warning"));
    QVERIFY(result.qualityMessages.join(QLatin1Char(' ')).contains(QStringLiteral("时间倒序")));
    QVERIFY(result.qualityMessages.join(QLatin1Char(' ')).contains(QStringLiteral("少 268 根")));
}

void TickDataCsvImportTest::plansOnlyRequestedWeekdaysAndResetsRetryableFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
                      QStringLiteral("内容只用于证明计划阶段不解析\n")).isEmpty());
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("20260811")));
    QFile empty(QDir(directory.path()).filePath(QStringLiteral("20260811/600519.csv")));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();

    TickDataBatchPlanResult plan = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2026, 8, 7), QDate(2026, 8, 11),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown);
    QVERIFY(plan.succeeded);
    QCOMPARE(plan.excludedWeekendDays, 2);
    QCOMPARE(plan.items.size(), 3);
    QCOMPARE(plan.items.at(0).status, TickDataBatchItemStatus::Ready);
    QCOMPARE(plan.items.at(1).status, TickDataBatchItemStatus::Missing);
    QCOMPARE(plan.items.at(2).status, TickDataBatchItemStatus::Missing);
    QCOMPARE(plan.items.at(2).error, QStringLiteral("文件为空"));

    plan.items[0].status = TickDataBatchItemStatus::Succeeded;
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260810"),
                      QStringLiteral("09:30:00,10,1,1000,B\n")).isEmpty());
    QCOMPARE(TickDataBatchPlanner::resetRetryableFiles(&plan.items), 1);
    QCOMPARE(plan.items.at(0).status, TickDataBatchItemStatus::Succeeded);
    QCOMPARE(plan.items.at(1).status, TickDataBatchItemStatus::Ready);
    QCOMPARE(plan.items.at(2).status, TickDataBatchItemStatus::Missing);
}

void TickDataCsvImportTest::filtersOfficialCalendarAndReportsFallback()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260105"),
                      QStringLiteral("09:30:00,10,1,1000,B\n")).isEmpty());
    const TickDataBatchPlanResult covered = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2026, 1, 1), QDate(2026, 1, 5),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown);
    QVERIFY(covered.succeeded);
    QCOMPARE(covered.items.size(), 1);
    QCOMPARE(covered.items.first().request.tradingDate, QDate(2026, 1, 5));
    QCOMPARE(covered.excludedWeekendDays, 2);
    QCOMPARE(covered.excludedHolidayDays, 2);
    QCOMPARE(covered.calendarFallbackDays, 0);
    QCOMPARE(covered.calendarSource, QStringLiteral("SSE-2025-45"));
    QVERIFY(AShareTradingCalendar::status(QDate(2026, 2, 16)).holiday);
    QVERIFY(AShareTradingCalendar::status(QDate(2026, 2, 24)).tradingDay);

    const TickDataBatchPlanResult fallback = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2027, 1, 4), QDate(2027, 1, 4),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown);
    QVERIFY(fallback.succeeded);
    QCOMPARE(fallback.calendarFallbackDays, 1);
    QCOMPARE(fallback.calendarSource, QStringLiteral("weekday-fallback"));
    QVERIFY(!fallback.items.first().calendarCoverageKnown);
}

void TickDataCsvImportTest::preflightsCoverageAndRestoresCompletedDates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260806"),
                      QStringLiteral("09:30:00,10,1,1000,B\n")).isEmpty());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
                      QStringLiteral("09:30:00,11,1,1100,S\n")).isEmpty());
    QHash<QDate, int> coverage;
    coverage.insert(QDate(2026, 8, 6), 240);
    coverage.insert(QDate(2026, 8, 7), 12);
    TickDataBatchPlanResult plan = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2026, 8, 6), QDate(2026, 8, 7),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown,
        coverage, true);
    QVERIFY(plan.succeeded);
    QCOMPARE(plan.items.at(0).status, TickDataBatchItemStatus::SkippedExisting);
    QCOMPARE(plan.items.at(0).coverageStatus, QStringLiteral("complete"));
    QCOMPARE(plan.items.at(1).status, TickDataBatchItemStatus::Ready);
    QCOMPARE(plan.items.at(1).coverageStatus, QStringLiteral("partial"));
    QVERIFY(plan.items.at(1).willOverwrite);

    QVector<TickDataBatchPlanItem> stored = plan.items;
    stored[1].status = TickDataBatchItemStatus::Succeeded;
    stored[1].writtenBars = 1;
    TickDataBatchPlanResult fresh = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2026, 8, 6), QDate(2026, 8, 7),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown,
        coverage, false);
    TickDataBatchPlanner::restoreCompletedItems(&fresh.items, stored);
    QCOMPARE(fresh.items.at(0).status, TickDataBatchItemStatus::Ready);
    QCOMPARE(fresh.items.at(1).status, TickDataBatchItemStatus::Succeeded);
    QCOMPARE(fresh.items.at(1).writtenBars, 1);
    QVERIFY(!fresh.items.at(1).willOverwrite);
}

void TickDataCsvImportTest::persistsRunItemsAndFiltersHistory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("items.db"))));

    TickDataImportRunSummary summary;
    summary.runId = QStringLiteral("recoverable-run");
    summary.createdAt = QDateTime(QDate(2026, 8, 8), QTime(10, 0), QTimeZone("UTC"));
    summary.completedAt = summary.createdAt.addSecs(3);
    summary.instrument = instrument();
    summary.startDate = QDate(2026, 8, 6);
    summary.endDate = QDate(2026, 8, 7);
    summary.totalFiles = 2;
    summary.completedFiles = 2;
    summary.succeededFiles = 1;
    summary.failedFiles = 1;
    summary.skippedExistingFiles = 0;
    summary.overlapFiles = 1;
    summary.excludedHolidayDays = 2;
    summary.calendarSource = QStringLiteral("SSE-2025-45");
    summary.status = QStringLiteral("completed_with_errors");

    QVector<TickDataBatchPlanItem> items(2);
    items[0].request.tradingDate = QDate(2026, 8, 6);
    items[0].status = TickDataBatchItemStatus::Succeeded;
    items[0].writtenBars = 240;
    items[0].existingBars = 12;
    items[0].expectedBars = 240;
    items[0].coverageStatus = QStringLiteral("partial");
    items[1].request.tradingDate = QDate(2026, 8, 7);
    items[1].status = TickDataBatchItemStatus::Failed;
    items[1].error = QStringLiteral("测试错误");
    QVERIFY(store.upsertTickDataImportRun(summary, items));

    const auto loaded = store.loadTickDataImportRun(summary.runId);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->overlapFiles, 1);
    QCOMPARE(loaded->excludedHolidayDays, 2);
    QCOMPARE(loaded->calendarSource, QStringLiteral("SSE-2025-45"));
    const QVector<TickDataBatchPlanItem> loadedItems =
        store.loadTickDataImportRunItems(summary.runId);
    QCOMPARE(loadedItems.size(), 2);
    QCOMPARE(loadedItems.at(0).status, TickDataBatchItemStatus::Succeeded);
    QCOMPARE(loadedItems.at(1).status, TickDataBatchItemStatus::Failed);
    QCOMPARE(loadedItems.at(1).error, QStringLiteral("测试错误"));
    QCOMPARE(store.loadTickDataImportRuns(
                 20, QStringLiteral("600519"),
                 QStringLiteral("completed_with_errors")).size(), 1);
    QCOMPARE(store.loadTickDataImportRuns(
                 20, QStringLiteral("000001"), {}).size(), 0);
}

void TickDataCsvImportTest::continuesIndependentFilesAndPersistsRunSummary()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,10,1,1000,B\n09:31:00,11,1,1100,S\n")).isEmpty());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260810"),
                      QStringLiteral("broken,row\n")).isEmpty());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260811"),
        QStringLiteral("09:30:00,12,1,1200,M\n09:31:00,13,1,1300,B\n")).isEmpty());
    TickDataBatchPlanResult plan = TickDataBatchPlanner::create(
        directory.path(), instrument(), QDate(2026, 8, 7), QDate(2026, 8, 11),
        TickDataCsvFormat::Tick, 60, FiveMinuteVolumeUnit::Unknown);
    QVERIFY(plan.succeeded);

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(directory.filePath(QStringLiteral("batch.db"))));
    QString storeError;
    connect(&store, &SqliteLocalDataStore::writeFailed, this,
            [&storeError](const QString &message) { storeError = message; });
    int succeeded = 0;
    int failed = 0;
    int writtenBars = 0;
    for (TickDataBatchPlanItem &item : plan.items) {
        const TickDataImportResult result = TickDataCsvImporter::parse(item.request);
        if (!result.succeeded) {
            item.status = TickDataBatchItemStatus::Failed;
            ++failed;
            continue;
        }
        QVERIFY(store.upsertCandles(result.candles));
        item.status = TickDataBatchItemStatus::Succeeded;
        ++succeeded;
        writtenBars += result.candles.size();
    }
    QCOMPARE(succeeded, 2);
    QCOMPARE(failed, 1);
    QCOMPARE(store.localHistoryCatalog().first().candleCount, 4);

    TickDataImportRunSummary summary;
    summary.runId = QStringLiteral("fixture-run-2");
    summary.createdAt = QDateTime(QDate(2026, 8, 8), QTime(10, 0), QTimeZone("UTC"));
    summary.completedAt = summary.createdAt.addSecs(2);
    summary.instrument = instrument();
    summary.startDate = QDate(2026, 8, 7);
    summary.endDate = QDate(2026, 8, 11);
    summary.totalFiles = 3;
    summary.completedFiles = 3;
    summary.succeededFiles = succeeded;
    summary.failedFiles = failed;
    summary.writtenBars = writtenBars;
    summary.status = QStringLiteral("completed_with_errors");
    QVERIFY2(store.upsertTickDataImportRun(summary), qPrintable(storeError));
    summary.runId = QStringLiteral("fixture-run-1");
    summary.createdAt = summary.createdAt.addSecs(-10);
    QVERIFY2(store.upsertTickDataImportRun(summary), qPrintable(storeError));

    const QVector<TickDataImportRunSummary> restored = store.loadTickDataImportRuns();
    QCOMPARE(restored.size(), 2);
    QCOMPARE(restored.first().runId, QStringLiteral("fixture-run-2"));
    QCOMPARE(restored.first().succeededFiles, 2);
    QCOMPARE(restored.first().failedFiles, 1);
    QCOMPARE(restored.first().writtenBars, 4);
    QCOMPARE(restored.first().instrument.symbol, QStringLiteral("600519"));
    QCOMPARE(restored.first().status, QStringLiteral("completed_with_errors"));
}

void TickDataCsvImportTest::cancelsStreamingParseBetweenLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,10,1,1000,B\n"
                       "09:31:00,11,1,1100,S\n"
                       "09:32:00,12,1,1200,M\n")).isEmpty());
    int checks = 0;
    const TickDataImportResult result = TickDataCsvImporter::parse(
        tickRequest(directory.path()), [&checks]() { return ++checks > 1; });
    QVERIFY(result.cancelled);
    QVERIFY(!result.succeeded);
    QCOMPARE(result.linesRead, 1);
    QVERIFY(!result.diagnostics.isEmpty());
    QVERIFY(result.diagnostics.last().fatal);
}

void TickDataCsvImportTest::servicePersistsRawTicksTransactionally()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,10,1,1000,B\n"
                       "09:30:30,11,2,2200,S\n"
                       "13:00:00,12,1,1200,M\n")).isEmpty());
    const QString databasePath = directory.filePath(QStringLiteral("raw-import.db"));
    TickDataCsvImportService service;
    service.setDatabasePath(databasePath);
    QSignalSpy spy(&service, &TickDataCsvImportService::finished);
    QVERIFY(service.start(tickRequest(directory.path())) > 0);
    QVERIFY(spy.wait(5000));
    const TickDataImportResult result =
        qvariant_cast<TickDataImportResult>(spy.first().at(1));
    QVERIFY(result.succeeded);
    QCOMPARE(result.rawTicksWritten, 3);
    QTRY_VERIFY(!service.running());

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(databasePath));
    const RawTradeFingerprint firstFingerprint =
        store.rawTradeFingerprint(instrument(), TradingDate);
    QCOMPARE(firstFingerprint.count, 3);
    const QVector<RawTickImportDayAudit> audits =
        store.loadRawTickImportDayAudits(instrument(), TradingDate, TradingDate);
    QCOMPARE(audits.size(), 1);
    QCOMPARE(audits.first().rawContentSha256, firstFingerprint.contentSha256);
    QCOMPARE(audits.first().auditVersion, 2);
    QCOMPARE(audits.first().linesRead, 3);
    QCOMPARE(audits.first().validRecords, 3);
    QCOMPARE(audits.first().skippedRecords, 0);
    QCOMPARE(audits.first().outOfOrderRecords, 0);
    QCOMPARE(audits.first().qualityStatus, QStringLiteral("warning"));
    QCOMPARE(audits.first().source, QStringLiteral("tickdata_csv_tick"));

    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
        QStringLiteral("09:30:00,20,1,2000,B\n"
                       "13:00:00,21,1,2100,S\n")).isEmpty());
    spy.clear();
    QVERIFY(service.start(tickRequest(directory.path())) > 0);
    QVERIFY(spy.wait(5000));
    const TickDataImportResult replacement =
        qvariant_cast<TickDataImportResult>(spy.first().at(1));
    QVERIFY(replacement.succeeded);
    QTRY_VERIFY(!service.running());
    const RawTradeFingerprint replacementFingerprint =
        store.rawTradeFingerprint(instrument(), TradingDate);
    QCOMPARE(replacementFingerprint.count, 2);
    QVERIFY(replacementFingerprint.contentSha256 != firstFingerprint.contentSha256);
    const QVector<RawTickImportDayAudit> replacementAudits =
        store.loadRawTickImportDayAudits(instrument(), TradingDate, TradingDate);
    QCOMPARE(replacementAudits.size(), 1);
    QCOMPARE(replacementAudits.first().rawContentSha256,
             replacementFingerprint.contentSha256);
    QCOMPARE(replacementAudits.first().validRecords, 2);

    QVERIFY(!writeCsv(directory.path(), QStringLiteral("20260807"),
                      QStringLiteral("broken,row\n")).isEmpty());
    spy.clear();
    QVERIFY(service.start(tickRequest(directory.path())) > 0);
    QVERIFY(spy.wait(5000));
    const TickDataImportResult failed =
        qvariant_cast<TickDataImportResult>(spy.first().at(1));
    QVERIFY(!failed.succeeded);
    QTRY_VERIFY(!service.running());
    QCOMPARE(store.rawTradeFingerprint(instrument(), TradingDate).count, 2);
    QCOMPARE(store.loadRawTickImportDayAudits(
                 instrument(), TradingDate, TradingDate).size(), 1);

    RawTradeTick incremental;
    incremental.instrument = instrument();
    incremental.tradingDate = TradingDate;
    incremental.timestamp = QDateTime(
        TradingDate, QTime(14, 0), QTimeZone("Asia/Shanghai")).toUTC();
    incremental.sourceLine = 99;
    incremental.price = 22.0;
    incremental.volumeShares = 100.0;
    incremental.amount = 2200.0;
    incremental.direction = RawTradeDirection::Neutral;
    const RawTradeIncrementalAppendResult appended =
        store.appendRawTradeTicksIncremental({incremental});
    QVERIFY(appended.factCommitted);
    QCOMPARE(appended.insertedCount, 1);
    QVERIFY(store.loadRawTickImportDayAudits(
                instrument(), TradingDate, TradingDate).isEmpty());
    const RawTradeFingerprint appendedFingerprint =
        store.rawTradeFingerprint(instrument(), TradingDate);
    QCOMPARE(appendedFingerprint.count, 3);
    QVERIFY(appendedFingerprint.contentSha256
            != replacementFingerprint.contentSha256);
    QVERIFY(store.loadRawTickImportDayAudits(
                instrument(), TradingDate, TradingDate).isEmpty());
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::TickDataCsvImportTest)

#include "tst_tickdata_csv_import.moc"
