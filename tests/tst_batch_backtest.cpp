#include "domain/BatchBacktestTypes.h"
#include "domain/RawTradeTypes.h"
#include "models/BatchBacktestResultListModel.h"
#include "services/BatchBacktestResultRetention.h"
#include "services/BatchBacktestService.h"
#include "services/AsyncBacktestArchiveService.h"
#include "services/AsyncRawTickAvailabilityService.h"
#include "services/BacktestDataVersion.h"
#include "services/SqliteLocalDataStore.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>
#include <QtTest>

#include <cmath>

using namespace pp;

namespace {

QString createDatabase(const QString &directory, int symbolCount, int barsPerSymbol)
{
    const QString path = directory + QStringLiteral("/batch.db");
    SqliteLocalDataStore store;
    if (!store.initialize(path)) return {};
    const QDate day(2026, 1, 5);
    const QTimeZone shanghai("Asia/Shanghai");
    for (int symbolIndex = 0; symbolIndex < symbolCount; ++symbolIndex) {
        const QString symbol = QStringLiteral("%1")
            .arg(600000 + symbolIndex, 6, 10, QLatin1Char('0'));
        const Instrument instrument{symbol,
            QStringLiteral("测试%1").arg(symbolIndex), Market::AShare,
            QStringLiteral("上交所")};

        QVector<RawTradeTick> ticks;
        ticks.reserve(barsPerSymbol);
        QVector<Candle> candles;
        candles.reserve(barsPerSymbol);
        for (int bar = 0; bar < barsPerSymbol; ++bar) {
            const double close = 10.0 + symbolIndex * 0.01 + bar * 0.02
                                 + (bar % 7 == 0 ? -0.03 : 0.0);
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = day;
            tick.timestamp = QDateTime(day, QTime(9, 30), shanghai)
                                 .addSecs(bar * 60).toUTC();
            tick.sourceLine = bar + 1;
            tick.price = close;
            tick.volumeShares = 100.0;
            tick.amount = close * 100.0;
            tick.direction = bar % 2 ? RawTradeDirection::Buy
                                     : RawTradeDirection::Sell;
            tick.source = QStringLiteral("tickdata_csv_tick");
            ticks.append(tick);

            Candle candle;
            candle.instrument = instrument;
            candle.startTime = QDateTime(day, QTime(9, 30), shanghai)
                                   .addSecs(bar * 60).toUTC();
            candle.periodSeconds = 60;
            candle.open = close - 0.01;
            candle.high = close + 0.03;
            candle.low = close - 0.04;
            candle.close = close;
            candle.volume = 10000.0 + bar * 100.0;
            candle.source = QStringLiteral("raw_tick_cache_v1");
            candle.adjustment = QStringLiteral("none");
            candles.append(candle);
        }
        if (!store.beginRawTradeTickImport(
                instrument, day, QStringLiteral("tickdata_csv_tick"))) {
            return {};
        }
        if (!store.appendRawTradeTicks(ticks)) return {};
        TickDataImportResult audit;
        audit.succeeded = true;
        audit.linesRead = ticks.size();
        audit.validRecords = ticks.size();
        audit.qualityStatus = QStringLiteral("healthy");
        if (!store.commitRawTradeTickImport(audit)) return {};

        const RawTradeFingerprint fingerprint =
            store.rawTradeFingerprint(instrument, day);
        if (fingerprint.count != barsPerSymbol) return {};
        if (!store.replaceRawTickCandleCache(
                instrument, day, 60, fingerprint, candles)) {
            return {};
        }
    }
    return path;
}

LocalHistoryDataset datasetFor(int symbolIndex, int candleCount)
{
    LocalHistoryDataset dataset;
    dataset.instrument = {
        QStringLiteral("%1").arg(600000 + symbolIndex, 6, 10, QLatin1Char('0')),
        QStringLiteral("测试%1").arg(symbolIndex), Market::AShare, QStringLiteral("上交所")};
    dataset.periodSeconds = 60;
    dataset.candleCount = candleCount;
    dataset.source = QStringLiteral("raw_tick_cache_v1");
    dataset.adjustment = QStringLiteral("none");
    return dataset;
}

BatchBacktestRequest requestFor(int requestId, int symbolCount, int bars)
{
    BatchBacktestRequest request;
    request.requestId = requestId;
    for (int index = 0; index < symbolCount; ++index)
        request.datasets.append(datasetFor(index, bars));
    request.strategyId = QStringLiteral("sma_crossover");
    request.strategyParameters = {{QStringLiteral("fastPeriod"), 3},
                                  {QStringLiteral("slowPeriod"), 8}};
    request.dataMode = BacktestDataMode::Candles;
    request.startDate = QDate(2026, 1, 5);
    request.endDate = QDate(2026, 1, 5);
    request.periodSeconds = 60;
    request.barCount = bars;
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    request.maximumActiveEngines = 4;
    return request;
}

BatchBacktestRequest rawRequestFor(int requestId, const QDate &startDate,
                                   const QDate &endDate)
{
    BatchBacktestRequest request;
    request.requestId = requestId;
    request.datasets = {datasetFor(0, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001},
                                  {QStringLiteral("cooldownSeconds"), 0},
                                  {QStringLiteral("minimumHoldSeconds"), 0}};
    request.dataMode = BacktestDataMode::RawTicks;
    request.startDate = startDate;
    request.endDate = endDate;
    request.periodSeconds = 0;
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    return request;
}

bool seedDatasetDay(SqliteLocalDataStore *store, const Instrument &instrument,
                    const QDate &day, int observationCount,
                    qint64 skippedRecords = 0)
{
    if (!store || observationCount <= 0) return false;
    const QTimeZone shanghai("Asia/Shanghai");
    QVector<RawTradeTick> ticks;
    QVector<Candle> candles;
    ticks.reserve(observationCount);
    candles.reserve(observationCount);
    for (int index = 0; index < observationCount; ++index) {
        const double price = 10.0 + index * 0.02
            + (day.dayOfYear() % 5) * 0.01;
        const QDateTime timestamp = QDateTime(day, QTime(9, 30), shanghai)
                                        .addSecs(index * 60).toUTC();
        RawTradeTick tick;
        tick.instrument = instrument;
        tick.tradingDate = day;
        tick.timestamp = timestamp;
        tick.sourceLine = index + 1;
        tick.price = price;
        tick.volumeShares = 100.0;
        tick.amount = price * 100.0;
        tick.direction = index % 2 ? RawTradeDirection::Buy
                                   : RawTradeDirection::Sell;
        ticks.append(tick);

        Candle candle;
        candle.instrument = instrument;
        candle.startTime = timestamp;
        candle.periodSeconds = 60;
        candle.open = price;
        candle.high = price + 0.01;
        candle.low = price - 0.01;
        candle.close = price;
        candle.volume = 100.0;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candles.append(candle);
    }
    if (!store->beginRawTradeTickImport(
            instrument, day, QStringLiteral("tickdata_csv_tick"))
        || !store->appendRawTradeTicks(ticks)) {
        return false;
    }
    TickDataImportResult audit;
    audit.succeeded = true;
    audit.linesRead = observationCount + skippedRecords;
    audit.validRecords = observationCount;
    audit.skippedRecords = skippedRecords;
    audit.qualityStatus = skippedRecords > 0
        ? QStringLiteral("warning") : QStringLiteral("healthy");
    if (!store->commitRawTradeTickImport(audit)) return false;
    return store->replaceRawTickCandleCache(
        instrument, day, 60, store->rawTradeFingerprint(instrument, day), candles);
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return hash.result();
}

} // namespace

class BatchBacktestTest final : public QObject {
    Q_OBJECT

private slots:
    void completesMultipleSymbolsAndReportsInsufficientData();
    void loadsCompletePerSymbolDateRangeWithoutUniformBarCount();
    void defaultsToCompleteRawTicksForDateRange();
    void deferredRawTickArchiveRejectsChangedDatasetIdentity();
    void asyncArchiveMaterializesAndPersistsDeferredRawTicks();
    void asyncArchiveStreamsDeferredRawTicksWithBoundedResidency();
    void asyncArchiveLoadsLightRecordAndRawTickPage();
    void asyncArchiveRejectsChangedIdentityWithoutPartialRecord();
    void runsEveryRegisteredRawTickStrategyForDateRange();
    void rawTickHoldoutUsesLastObservedTradingDaysWithoutLeakage();
    void rawTickHoldoutKeepsOnlyOneTradingDayResident();
    void rawTickHoldoutRejectsInsufficientTrainingDays();
    void rawTickWalkForwardUsesExpandingTrainingAndNonOverlappingFolds();
    void rawTickWalkForwardKeepsOnlyOneTradingDayResident();
    void rawTickWalkForwardRejectsInsufficientObservedDays();
    void identityAndArchiveRoundTrip();
    void staleDatasetFailsBatchItem();
    void rawTickMissingTradingDayFailsQualityGate();
    void segmentedRawTickResearchRunsIndependentCleanSegments();
    void segmentedResearchEvidenceTracksProvenanceNotPresentationText();
    void availabilityAnalysisIsReadOnly();
    void availabilityCancellationTracksPendingWork();
    void rawTickQualityRejectsBeforeFactMaterialization();
    void explicitSuspensionAllowsMissingRawTickDay();
    void excessiveImportedBadRowsFailQualityGate();
    void runPersistenceRoundTrip();
    void rawTickBatchKeepsOneResidentDataset();
    void retainedResultsEvictOldestWhenByteBudgetIsExceeded();
    void cancellationAndBusyGate();
    void resultModelRolesAndOrdering();
};

void BatchBacktestTest::runsEveryRegisteredRawTickStrategyForDateRange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());

    const QVector<QPair<QString, QVariantMap>> strategies {
        {QStringLiteral("tick_momentum"),
         {{QStringLiteral("lookbackTicks"), 3},
          {QStringLiteral("thresholdPercent"), 0.01},
          {QStringLiteral("cooldownSeconds"), 0},
          {QStringLiteral("minimumHoldSeconds"), 0}}},
        {QStringLiteral("tick_mean_reversion"),
         {{QStringLiteral("lookbackTicks"), 3},
          {QStringLiteral("entryDeviationPercent"), 0.01},
          {QStringLiteral("exitDeviationPercent"), 0.0},
          {QStringLiteral("cooldownSeconds"), 0},
          {QStringLiteral("minimumHoldSeconds"), 0}}},
        {QStringLiteral("tick_volume_breakout"),
         {{QStringLiteral("lookbackTicks"), 3},
          {QStringLiteral("breakoutPercent"), 0.01},
          {QStringLiteral("breakdownPercent"), 0.01},
          {QStringLiteral("volumeMultiplier"), 1.0},
          {QStringLiteral("cooldownSeconds"), 0},
          {QStringLiteral("minimumHoldSeconds"), 0}}},
        {QStringLiteral("tick_directional_imbalance"),
         {{QStringLiteral("lookbackTicks"), 3},
          {QStringLiteral("entryImbalancePercent"), 20.0},
          {QStringLiteral("exitImbalancePercent"), -20.0},
          {QStringLiteral("minimumDirectionalCoveragePercent"), 50.0},
          {QStringLiteral("cooldownSeconds"), 0},
          {QStringLiteral("minimumHoldSeconds"), 0}}}
    };

    quint64 requestId = 100;
    for (const auto &entry : strategies) {
        BatchBacktestRequest request;
        request.requestId = requestId++;
        request.datasets = {datasetFor(0, 0)};
        request.strategyId = entry.first;
        request.strategyParameters = entry.second;
        request.dataMode = BacktestDataMode::RawTicks;
        request.startDate = QDate(2026, 1, 5);
        request.endDate = QDate(2026, 1, 5);
        request.initialCapital = 1000000.0;

        BatchBacktestService service;
        service.setDatabasePath(path);
        QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
        QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
        QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
        QVERIFY2(service.start(request), qPrintable(entry.first));
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
        QCOMPARE(itemSpy.count(), 1);
        QCOMPARE(completedSpy.count(), 1);
        const auto item = qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
        QVERIFY2(item.succeeded, qPrintable(entry.first + QStringLiteral(": ") + item.error));
        QCOMPARE(item.dataMode, BacktestDataMode::RawTicks);
        QCOMPARE(item.actualBars, 60);
        const auto result = qvariant_cast<BacktestResult>(completedSpy.first().at(2));
        QCOMPARE(result.config.strategyId, entry.first);
        QCOMPARE(result.config.dataMode, BacktestDataMode::RawTicks);
        QVERIFY(result.rawTickSnapshotDeferred);
        QVERIFY(result.rawTicks.isEmpty());
        QVERIFY(result.config.dataVersion.startsWith(QStringLiteral("sha256:")));
    }
}

void BatchBacktestTest::rawTickHoldoutUsesLastObservedTradingDaysWithoutLeakage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw-holdout.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    for (const QDate day : {QDate(2026, 1, 5), QDate(2026, 1, 6),
                            QDate(2026, 1, 7), QDate(2026, 1, 8),
                            QDate(2026, 1, 9)}) {
        QVERIFY(seedDatasetDay(&store, instrument, day, 12));
    }

    BatchBacktestRequest request;
    request.requestId = 44;
    request.datasets = {datasetFor(0, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001},
                                  {QStringLiteral("cooldownSeconds"), 0},
                                  {QStringLiteral("minimumHoldSeconds"), 0}};
    request.startDate = QDate(2026, 1, 5);
    request.endDate = QDate(2026, 1, 9);
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    request.holdoutTradingDays = 2;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 1);

    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QVERIFY(item.holdoutEnabled);
    QCOMPARE(item.holdoutTradingDays, 2);
    QCOMPARE(item.trainingStartDate, QDate(2026, 1, 5));
    QCOMPARE(item.trainingEndDate, QDate(2026, 1, 7));
    QCOMPARE(item.holdoutStartDate, QDate(2026, 1, 8));
    QCOMPARE(item.holdoutEndDate, QDate(2026, 1, 9));
    QCOMPARE(item.trainingActualTicks, 36);
    QCOMPARE(item.holdoutActualTicks, 24);
    QVERIFY(item.trainingDataVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(item.holdoutDataVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(item.trainingDataVersion != item.holdoutDataVersion);
    QVERIFY(item.trainingDatasetVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(item.holdoutDatasetVersion.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(item.summary.totalReturnPercent,
             item.holdoutSummary.totalReturnPercent);

    const BacktestResult holdout =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    const QTimeZone shanghai("Asia/Shanghai");
    QVERIFY(holdout.rawTickSnapshotDeferred);
    QVERIFY(holdout.rawTicks.isEmpty());
    QCOMPARE(holdout.equityObservationCount, 24LL);
    QCOMPARE(holdout.config.sampleFrom.toTimeZone(shanghai).date(),
             QDate(2026, 1, 8));
    QCOMPARE(holdout.config.ruleSnapshot.value(
                 QStringLiteral("evaluationMethod")).toString(),
             QStringLiteral("time_ordered_holdout_v1"));
    QCOMPARE(holdout.config.ruleSnapshot.value(
                 QStringLiteral("holdoutTradingDays")).toInt(), 2);
    QCOMPARE(holdout.config.ruleSnapshot.value(
                 QStringLiteral("trainingDataVersion")).toString(),
             item.trainingDataVersion);
    for (const BacktestTrade &trade : holdout.trades) {
        QVERIFY(trade.exitTime.toTimeZone(shanghai).date()
                > trade.entryTime.toTimeZone(shanghai).date());
    }
}

void BatchBacktestTest::rawTickHoldoutKeepsOnlyOneTradingDayResident()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw-holdout-residency.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    const QVector<QPair<QDate, int>> dayCounts {
        {QDate(2026, 1, 5), 9},
        {QDate(2026, 1, 6), 12},
        {QDate(2026, 1, 7), 15},
        {QDate(2026, 1, 8), 18},
        {QDate(2026, 1, 9), 21},
    };
    for (const auto &[day, count] : dayCounts) {
        QVERIFY(seedDatasetDay(&store, instrument, day, count));
    }

    BatchBacktestRequest request = rawRequestFor(
        45, QDate(2026, 1, 5), QDate(2026, 1, 9));
    request.holdoutTradingDays = 2;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy residencySpy(&service, &BatchBacktestService::rawTickResidencyChanged);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 1);

    qint64 peakObservations = 0;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(0).toULongLong(), 45ULL);
        peakObservations = std::max(
            peakObservations, arguments.at(2).toLongLong());
    }
    QCOMPARE(peakObservations, 21LL);

    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QCOMPARE(item.trainingActualTicks, 36);
    QCOMPARE(item.holdoutActualTicks, 39);
    const BacktestResult holdout =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    QCOMPARE(holdout.config.actualBars, 39);
    QCOMPARE(holdout.equityObservationCount, 39LL);
    QVERIFY(holdout.rawTickSnapshotDeferred);
    QVERIFY(holdout.rawTicks.isEmpty());
    QVERIFY(!residencySpy.isEmpty());
    QCOMPARE(residencySpy.last().at(1).toInt(), 0);
    QCOMPARE(residencySpy.last().at(2).toLongLong(), 0LL);
}

void BatchBacktestTest::rawTickHoldoutRejectsInsufficientTrainingDays()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("short-holdout.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    for (const QDate day : {QDate(2026, 1, 5), QDate(2026, 1, 6),
                            QDate(2026, 1, 7)}) {
        QVERIFY(seedDatasetDay(&store, instrument, day, 12));
    }

    BatchBacktestRequest request;
    request.requestId = 45;
    request.datasets = {datasetFor(0, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001}};
    request.startDate = QDate(2026, 1, 5);
    request.endDate = QDate(2026, 1, 7);
    request.initialCapital = 1000000.0;
    request.holdoutTradingDays = 2;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 0);
    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY(!item.succeeded);
    QVERIFY(item.holdoutEnabled);
    QCOMPARE(item.actualBars, 36);
    QVERIFY(item.error.contains(QStringLiteral("至少需要 2 个训练交易日")));
}

void BatchBacktestTest::rawTickWalkForwardUsesExpandingTrainingAndNonOverlappingFolds()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw-walk-forward.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    const QVector<QDate> observedDates {
        QDate(2026, 1, 5), QDate(2026, 1, 6), QDate(2026, 1, 7),
        QDate(2026, 1, 8), QDate(2026, 1, 9), QDate(2026, 1, 12),
        QDate(2026, 1, 13), QDate(2026, 1, 14)};
    for (const QDate &day : observedDates) {
        QVERIFY(seedDatasetDay(&store, instrument, day, 12));
    }
    store.close();

    BatchBacktestRequest request;
    request.requestId = 46;
    request.datasets = {datasetFor(0, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001},
                                  {QStringLiteral("cooldownSeconds"), 0},
                                  {QStringLiteral("minimumHoldSeconds"), 0}};
    request.dataMode = BacktestDataMode::RawTicks;
    request.startDate = observedDates.first();
    request.endDate = observedDates.last();
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    request.holdoutTradingDays = 2;
    request.walkForwardFoldCount = 3;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 15000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 0);

    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QVERIFY(item.walkForwardEnabled);
    QCOMPARE(item.walkForwardFoldCount, 3);
    QVERIFY(item.researchOnly);
    QVERIFY(!item.archivable);
    QCOMPARE(item.walkForwardFolds.size(), 3);

    const QVector<QDate> expectedTrainingEnds {
        QDate(2026, 1, 6), QDate(2026, 1, 8), QDate(2026, 1, 12)};
    const QVector<QDate> expectedValidationStarts {
        QDate(2026, 1, 7), QDate(2026, 1, 9), QDate(2026, 1, 13)};
    const QVector<QDate> expectedValidationEnds {
        QDate(2026, 1, 8), QDate(2026, 1, 12), QDate(2026, 1, 14)};
    for (int index = 0; index < item.walkForwardFolds.size(); ++index) {
        const RawTickWalkForwardFoldResult &fold = item.walkForwardFolds.at(index);
        QCOMPARE(fold.foldNumber, index + 1);
        QCOMPARE(fold.trainingStartDate, observedDates.first());
        QCOMPARE(fold.trainingEndDate, expectedTrainingEnds.at(index));
        QCOMPARE(fold.validationStartDate, expectedValidationStarts.at(index));
        QCOMPARE(fold.validationEndDate, expectedValidationEnds.at(index));
        QCOMPARE(fold.trainingActualTicks, (index + 1) * 24);
        QCOMPARE(fold.validationActualTicks, 24);
        QVERIFY(fold.trainingDataVersion.startsWith(QStringLiteral("sha256:")));
        QVERIFY(fold.validationDataVersion.startsWith(QStringLiteral("sha256:")));
        QVERIFY(fold.trainingDataVersion != fold.validationDataVersion);
    }
    QVERIFY(std::isfinite(item.walkForwardMeanReturnPercent));
    QVERIFY(std::isfinite(item.walkForwardMedianReturnPercent));
    QVERIFY(std::isfinite(item.walkForwardWorstReturnPercent));
    QVERIFY(std::isfinite(item.walkForwardBestReturnPercent));
}

void BatchBacktestTest::rawTickWalkForwardKeepsOnlyOneTradingDayResident()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("raw-walk-forward-residency.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    const QVector<QPair<QDate, int>> dayCounts {
        {QDate(2026, 1, 5), 9},
        {QDate(2026, 1, 6), 12},
        {QDate(2026, 1, 7), 15},
        {QDate(2026, 1, 8), 18},
        {QDate(2026, 1, 9), 21},
        {QDate(2026, 1, 12), 24},
        {QDate(2026, 1, 13), 27},
        {QDate(2026, 1, 14), 30},
    };
    for (const auto &[day, count] : dayCounts) {
        QVERIFY(seedDatasetDay(&store, instrument, day, count));
    }

    BatchBacktestRequest request = rawRequestFor(
        47, QDate(2026, 1, 5), QDate(2026, 1, 14));
    request.holdoutTradingDays = 2;
    request.walkForwardFoldCount = 3;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy residencySpy(&service, &BatchBacktestService::rawTickResidencyChanged);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 0);

    qint64 peakObservations = 0;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(0).toULongLong(), 47ULL);
        peakObservations = std::max(
            peakObservations, arguments.at(2).toLongLong());
    }
    QCOMPARE(peakObservations, 30LL);

    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QVERIFY(item.walkForwardEnabled);
    QCOMPARE(item.walkForwardFolds.size(), 3);
    QCOMPARE(item.actualBars, 135);
    QCOMPARE(item.walkForwardFolds.at(0).trainingActualTicks, 21);
    QCOMPARE(item.walkForwardFolds.at(0).validationActualTicks, 33);
    QCOMPARE(item.walkForwardFolds.at(1).trainingActualTicks, 54);
    QCOMPARE(item.walkForwardFolds.at(1).validationActualTicks, 45);
    QCOMPARE(item.walkForwardFolds.at(2).trainingActualTicks, 99);
    QCOMPARE(item.walkForwardFolds.at(2).validationActualTicks, 57);
    QVERIFY(!residencySpy.isEmpty());
    QCOMPARE(residencySpy.last().at(1).toInt(), 0);
    QCOMPARE(residencySpy.last().at(2).toLongLong(), 0LL);
}

void BatchBacktestTest::rawTickWalkForwardRejectsInsufficientObservedDays()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("short-walk-forward.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    const QVector<QDate> observedDates {
        QDate(2026, 1, 5), QDate(2026, 1, 6), QDate(2026, 1, 7),
        QDate(2026, 1, 8), QDate(2026, 1, 9), QDate(2026, 1, 12),
        QDate(2026, 1, 13)};
    for (const QDate &day : observedDates) {
        QVERIFY(seedDatasetDay(&store, instrument, day, 12));
    }
    store.close();

    BatchBacktestRequest request;
    request.requestId = 47;
    request.datasets = {datasetFor(0, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001}};
    request.dataMode = BacktestDataMode::RawTicks;
    request.startDate = observedDates.first();
    request.endDate = observedDates.last();
    request.initialCapital = 1000000.0;
    request.holdoutTradingDays = 2;
    request.walkForwardFoldCount = 3;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 0);

    const BatchBacktestItemResult item =
        qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    QVERIFY(!item.succeeded);
    QVERIFY(item.error.contains(QStringLiteral("3×2 个验证交易日")));
    QVERIFY(item.error.contains(QStringLiteral("当前只有 7 日")));
}

void BatchBacktestTest::loadsCompletePerSymbolDateRangeWithoutUniformBarCount()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("range.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument first{QStringLiteral("600000"), QStringLiteral("测试0"),
                           Market::AShare, QStringLiteral("上交所")};
    const Instrument second{QStringLiteral("600001"), QStringLiteral("测试1"),
                            Market::AShare, QStringLiteral("上交所")};
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 5), 10));
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 6), 15));
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 7), 9));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 5), 8));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 6), 12));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 7), 11));
    const QVector<Instrument> rawInstruments = store.rawBacktestInstruments();
    QCOMPARE(rawInstruments.size(), 2);
    QCOMPARE(rawInstruments.at(0).symbol, QStringLiteral("600000"));
    QCOMPARE(rawInstruments.at(1).symbol, QStringLiteral("600001"));

    BatchBacktestRequest request = requestFor(42, 2, 999);
    request.startDate = QDate(2026, 1, 5);
    request.endDate = QDate(2026, 1, 6);
    request.barCount = 0;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 2);

    QHash<QString, BatchBacktestItemResult> results;
    for (const QList<QVariant> &arguments : itemSpy) {
        const auto item = qvariant_cast<BatchBacktestItemResult>(arguments.at(1));
        results.insert(item.instrument.symbol, item);
    }
    QVERIFY(results.value(QStringLiteral("600000")).succeeded);
    QVERIFY(results.value(QStringLiteral("600001")).succeeded);
    QCOMPARE(results.value(QStringLiteral("600000")).actualBars, 25);
    QCOMPARE(results.value(QStringLiteral("600001")).actualBars, 20);
    QCOMPARE(results.value(QStringLiteral("600000")).datasetDayCount, 2);
    QCOMPARE(results.value(QStringLiteral("600001")).datasetDayCount, 2);
}

void BatchBacktestTest::defaultsToCompleteRawTicksForDateRange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw-range.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument first{QStringLiteral("600000"), QStringLiteral("测试0"),
                           Market::AShare, QStringLiteral("上交所")};
    const Instrument second{QStringLiteral("600001"), QStringLiteral("测试1"),
                            Market::AShare, QStringLiteral("上交所")};
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 5), 10));
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 6), 15));
    QVERIFY(seedDatasetDay(&store, first, QDate(2026, 1, 7), 9));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 5), 8));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 6), 12));
    QVERIFY(seedDatasetDay(&store, second, QDate(2026, 1, 7), 11));

    BatchBacktestRequest request;
    request.requestId = 43;
    request.datasets = {datasetFor(0, 0), datasetFor(1, 0)};
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters = {{QStringLiteral("lookbackTicks"), 3},
                                  {QStringLiteral("thresholdPercent"), 0.001},
                                  {QStringLiteral("cooldownSeconds"), 0}};
    request.startDate = QDate(2026, 1, 5);
    request.endDate = QDate(2026, 1, 6);
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    request.maximumActiveEngines = 2;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy residencySpy(&service,
                            &BatchBacktestService::rawTickResidencyChanged);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 2);

    QHash<QString, BatchBacktestItemResult> results;
    for (const QList<QVariant> &arguments : itemSpy) {
        const auto item = qvariant_cast<BatchBacktestItemResult>(arguments.at(1));
        results.insert(item.instrument.symbol, item);
    }
    QVERIFY2(results.value(QStringLiteral("600000")).succeeded,
             qPrintable(results.value(QStringLiteral("600000")).error));
    QVERIFY2(results.value(QStringLiteral("600001")).succeeded,
             qPrintable(results.value(QStringLiteral("600001")).error));
    QCOMPARE(completedSpy.count(), 2);
    QCOMPARE(results.value(QStringLiteral("600000")).frequency, QStringLiteral("tick"));
    QCOMPARE(results.value(QStringLiteral("600000")).actualBars, 25);
    QCOMPARE(results.value(QStringLiteral("600001")).actualBars, 20);
    QCOMPARE(results.value(QStringLiteral("600000")).datasetRawTickCount, 25);
    QCOMPARE(results.value(QStringLiteral("600001")).datasetRawTickCount, 20);
    qint64 peakResidentObservations = 0;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(1).toInt(),
                 arguments.at(2).toLongLong() > 0 ? 1 : 0);
        peakResidentObservations = std::max(
            peakResidentObservations, arguments.at(2).toLongLong());
    }
    QCOMPARE(peakResidentObservations, 15LL);
    const QVariantMap firstItemMap =
        results.value(QStringLiteral("600000")).toVariantMap();
    QVERIFY(firstItemMap.value(QStringLiteral("hasOpenPosition")).toBool());
    QVERIFY(firstItemMap.value(QStringLiteral("openPositionQuantity")).toDouble() > 0.0);
    QVERIFY(firstItemMap.value(QStringLiteral("openPositionEntryTime")).toDateTime().isValid());
    BacktestResult rawArchiveResult;
    for (const QList<QVariant> &arguments : completedSpy) {
        const BacktestResult result = qvariant_cast<BacktestResult>(arguments.at(2));
        QCOMPARE(result.config.dataMode, BacktestDataMode::RawTicks);
        QVERIFY(result.candles.isEmpty());
        QVERIFY(result.rawTicks.isEmpty());
        QVERIFY(result.rawTickSnapshotDeferred);
        if (arguments.at(1).toString() == QStringLiteral("600000")) {
            rawArchiveResult = result;
        }
    }
    BacktestArchiveRecord archive;
    archive.resultId = QStringLiteral("raw-batch-archive");
    archive.completedAt = QDateTime::currentDateTimeUtc();
    QString materializeError;
    QVERIFY2(store.materializeRawTickSnapshot(
                 rawArchiveResult, &archive.result, &materializeError),
             qPrintable(materializeError));
    QVERIFY(!archive.result.rawTickSnapshotDeferred);
    QCOMPARE(archive.result.rawTicks.size(), archive.result.config.actualBars);
    QCOMPARE(archive.result.config.dataVersion,
             computeBacktestDataVersion(archive.result.rawTicks,
                                        archive.result.config.instrument.symbol));
    QVERIFY(rawArchiveResult.finalPosition.quantity > 0.0);
    QVERIFY(store.saveBacktestArchive(archive));
    const auto restored = store.loadBacktestArchive(
        archive.resultId, BacktestArchiveLoadMode::CompleteInputSnapshot);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.config.dataMode, BacktestDataMode::RawTicks);
    QCOMPARE(restored->result.rawTicks.size(), archive.result.rawTicks.size());
    QCOMPARE(restored->result.rawTicks.first().timestamp,
             archive.result.rawTicks.first().timestamp);
    QCOMPARE(restored->result.config.dataVersion,
             rawArchiveResult.config.dataVersion);
    QCOMPARE(restored->result.finalPosition.quantity,
             rawArchiveResult.finalPosition.quantity);
    QCOMPARE(restored->result.finalPosition.averagePrice,
             rawArchiveResult.finalPosition.averagePrice);
    QCOMPARE(restored->result.finalPosition.entryTime,
             rawArchiveResult.finalPosition.entryTime);
    QCOMPARE(restored->result.finalPosition.entryReason,
             rawArchiveResult.finalPosition.entryReason);
}

void BatchBacktestTest::deferredRawTickArchiveRejectsChangedDatasetIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    const BatchBacktestRequest request = rawRequestFor(
        143, QDate(2026, 1, 5), QDate(2026, 1, 5));
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 1);
    const BacktestResult deferred =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    QVERIFY(deferred.rawTickSnapshotDeferred);
    QVERIFY(deferred.rawTicks.isEmpty());

    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    RawTradeTick appended;
    appended.instrument = datasetFor(0, 0).instrument;
    appended.tradingDate = QDate(2026, 1, 5);
    appended.timestamp = QDateTime(appended.tradingDate, QTime(14, 0),
                                   QTimeZone("Asia/Shanghai")).toUTC();
    appended.sourceLine = 1000;
    appended.price = 12.34;
    appended.volumeShares = 100.0;
    appended.amount = 1234.0;
    appended.direction = RawTradeDirection::Buy;
    appended.source = QStringLiteral("tickdata_csv_tick");
    const RawTradeIncrementalAppendResult appendResult =
        store.appendRawTradeTicksIncremental({appended});
    QVERIFY2(appendResult.factCommitted, qPrintable(appendResult.error));
    QCOMPARE(appendResult.insertedCount, 1);

    BacktestResult materialized;
    QString error;
    QVERIFY(!store.materializeRawTickSnapshot(deferred, &materialized, &error));
    QVERIFY(error.contains(QStringLiteral("身份已变化")));
    QVERIFY(materialized.rawTicks.isEmpty());
}

void BatchBacktestTest::asyncArchiveMaterializesAndPersistsDeferredRawTicks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService batchService;
    batchService.setDatabasePath(path);
    QSignalSpy completedSpy(&batchService, &BatchBacktestService::itemCompleted);
    QSignalSpy batchFinishedSpy(&batchService, &BatchBacktestService::finished);
    const BatchBacktestRequest request = rawRequestFor(
        144, QDate(2026, 1, 5), QDate(2026, 1, 5));
    QVERIFY(batchService.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(batchFinishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 1);
    const BacktestResult deferred =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    QVERIFY(deferred.rawTickSnapshotDeferred);
    QVERIFY(deferred.rawTicks.isEmpty());

    AsyncBacktestArchiveService archiveService;
    QVERIFY(archiveService.start(path));
    QSignalSpy archiveFinishedSpy(
        &archiveService, &AsyncBacktestArchiveService::finished);
    const QString resultId = QStringLiteral("async-raw-archive");

    QVERIFY(archiveService.request(
        701, resultId, QDateTime::currentDateTimeUtc(),
        std::make_shared<const BacktestResult>(deferred)));
    QCOMPARE(archiveFinishedSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(archiveFinishedSpy.count(), 1, 10000);
    QCOMPARE(archiveFinishedSpy.first().at(0).toULongLong(), quint64(701));
    QCOMPARE(archiveFinishedSpy.first().at(1).toString(), QStringLiteral("600000"));
    QCOMPARE(archiveFinishedSpy.first().at(2).toString(), resultId);
    QVERIFY2(archiveFinishedSpy.first().at(3).toString().isEmpty(),
             qPrintable(archiveFinishedSpy.first().at(3).toString()));

    SqliteLocalDataStore verificationStore;
    QVERIFY(verificationStore.initialize(path));
    const auto restored = verificationStore.loadBacktestArchive(
        resultId, BacktestArchiveLoadMode::CompleteInputSnapshot);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.rawTicks.size(), 60);
    QVERIFY(!restored->result.rawTickSnapshotDeferred);
    QCOMPARE(restored->result.config.dataVersion, deferred.config.dataVersion);
}

void BatchBacktestTest::asyncArchiveStreamsDeferredRawTicksWithBoundedResidency()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("streamed-archive.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    const QVector<QDate> days {
        QDate(2026, 1, 5), QDate(2026, 1, 6), QDate(2026, 1, 7),
        QDate(2026, 1, 8), QDate(2026, 1, 9), QDate(2026, 1, 12),
        QDate(2026, 1, 13), QDate(2026, 1, 14)
    };
    int expectedTicks = 0;
    for (int index = 0; index < days.size(); ++index) {
        const int count = 9 + index * 3;
        QVERIFY(seedDatasetDay(&store, instrument, days.at(index), count));
        expectedTicks += count;
    }
    QCOMPARE(expectedTicks, 156);

    BatchBacktestService batchService;
    batchService.setDatabasePath(path);
    QSignalSpy completedSpy(&batchService, &BatchBacktestService::itemCompleted);
    QSignalSpy batchFinishedSpy(&batchService, &BatchBacktestService::finished);
    QVERIFY(batchService.start(rawRequestFor(146, days.first(), days.last())));
    QTRY_COMPARE_WITH_TIMEOUT(batchFinishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 1);
    const BacktestResult deferred =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    QVERIFY(deferred.rawTickSnapshotDeferred);
    QVERIFY(deferred.rawTicks.isEmpty());
    QCOMPARE(deferred.config.actualBars, expectedTicks);

    AsyncBacktestArchiveService archiveService;
    QVERIFY(archiveService.start(path));
    QSignalSpy residencySpy(
        &archiveService,
        SIGNAL(rawTickResidencyChanged(quint64,qint64)));
    QVERIFY(residencySpy.isValid());
    QSignalSpy archiveFinishedSpy(
        &archiveService, &AsyncBacktestArchiveService::finished);
    const QString resultId = QStringLiteral("streamed-raw-archive");
    QVERIFY(archiveService.request(
        703, resultId, QDateTime::currentDateTimeUtc(),
        std::make_shared<const BacktestResult>(deferred)));
    QTRY_COMPARE_WITH_TIMEOUT(archiveFinishedSpy.count(), 1, 10000);
    QVERIFY2(archiveFinishedSpy.first().at(3).toString().isEmpty(),
             qPrintable(archiveFinishedSpy.first().at(3).toString()));
    QVERIFY(!residencySpy.isEmpty());
    qint64 peakResidentTicks = 0;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(0).toULongLong(), quint64(703));
        peakResidentTicks = std::max(
            peakResidentTicks, arguments.at(1).toLongLong());
    }
    QCOMPARE(peakResidentTicks, 1LL);
    QCOMPARE(residencySpy.last().at(1).toLongLong(), 0LL);

    SqliteLocalDataStore verificationStore;
    QVERIFY(verificationStore.initialize(path));
    const auto restored = verificationStore.loadBacktestArchive(
        resultId, BacktestArchiveLoadMode::CompleteInputSnapshot);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->result.rawTicks.size(), expectedTicks);
    QVERIFY(!restored->result.rawTickSnapshotDeferred);
    QCOMPARE(restored->result.rawTicks.first().timestamp, deferred.config.from);
    QCOMPARE(restored->result.rawTicks.last().timestamp, deferred.config.to);
    QCOMPARE(restored->result.config.dataVersion, deferred.config.dataVersion);
    QCOMPARE(restored->result.chartSignals.size(), deferred.chartSignals.size());
    if (!deferred.chartSignals.isEmpty()) {
        QCOMPARE(restored->result.chartSignals.first().timestamp,
                 deferred.chartSignals.first().timestamp);
        QCOMPARE(restored->result.chartSignals.last().timestamp,
                 deferred.chartSignals.last().timestamp);
    }
    QCOMPARE(computeBacktestDataVersion(
                 restored->result.rawTicks, instrument.symbol),
             deferred.config.dataVersion);
}

void BatchBacktestTest::asyncArchiveLoadsLightRecordAndRawTickPage()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService batchService;
    batchService.setDatabasePath(path);
    QSignalSpy completedSpy(&batchService, &BatchBacktestService::itemCompleted);
    QSignalSpy batchFinishedSpy(&batchService, &BatchBacktestService::finished);
    QVERIFY(batchService.start(rawRequestFor(
        147, QDate(2026, 1, 5), QDate(2026, 1, 5))));
    QTRY_COMPARE_WITH_TIMEOUT(batchFinishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 1);
    const BacktestResult deferred =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));

    AsyncBacktestArchiveService archiveService;
    QVERIFY(archiveService.start(path));
    QSignalSpy saveSpy(&archiveService, &AsyncBacktestArchiveService::finished);
    const QString resultId = QStringLiteral("async-page-archive");
    QVERIFY(archiveService.request(
        801, resultId, QDateTime::currentDateTimeUtc(),
        std::make_shared<const BacktestResult>(deferred)));
    QTRY_COMPARE_WITH_TIMEOUT(saveSpy.count(), 1, 10000);
    QVERIFY(saveSpy.first().at(3).toString().isEmpty());

    QSignalSpy loadSpy(
        &archiveService, &AsyncBacktestArchiveService::loadFinished);
    QVERIFY(archiveService.requestLoad(802, resultId));
    QCOMPARE(loadSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(loadSpy.count(), 1, 10000);
    QCOMPARE(loadSpy.first().at(0).toULongLong(), quint64(802));
    const BacktestArchiveRecord restored =
        qvariant_cast<BacktestArchiveRecord>(loadSpy.first().at(1));
    QCOMPARE(restored.resultId, resultId);
    QVERIFY(restored.result.rawTicks.isEmpty());
    QVERIFY(restored.result.rawTickSnapshotAvailable);
    QVERIFY(!restored.result.rawTickSnapshotLoaded);
    QVERIFY(loadSpy.first().at(2).toString().isEmpty());

    QSignalSpy pageSpy(
        &archiveService, &AsyncBacktestArchiveService::rawTickPageReady);
    QVERIFY(archiveService.requestRawTickPage(803, resultId, 4, 3));
    QCOMPARE(pageSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(pageSpy.count(), 1, 10000);
    QCOMPARE(pageSpy.first().at(0).toULongLong(), quint64(803));
    const BacktestRawTickPage page =
        qvariant_cast<BacktestRawTickPage>(pageSpy.first().at(1));
    QCOMPARE(page.resultId, resultId);
    QCOMPARE(page.offset, 4);
    QCOMPARE(page.totalCount, 60);
    QCOMPARE(page.ticks.size(), 3);
    QCOMPARE(page.ticks.first().sourceLine, 5LL);
    QCOMPARE(page.ticks.last().sourceLine, 7LL);
    QVERIFY(page.hasMore);
    QVERIFY(pageSpy.first().at(2).toString().isEmpty());
}

void BatchBacktestTest::asyncArchiveRejectsChangedIdentityWithoutPartialRecord()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService batchService;
    batchService.setDatabasePath(path);
    QSignalSpy completedSpy(&batchService, &BatchBacktestService::itemCompleted);
    QSignalSpy batchFinishedSpy(&batchService, &BatchBacktestService::finished);
    QVERIFY(batchService.start(rawRequestFor(
        145, QDate(2026, 1, 5), QDate(2026, 1, 5))));
    QTRY_COMPARE_WITH_TIMEOUT(batchFinishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 1);
    const BacktestResult deferred =
        qvariant_cast<BacktestResult>(completedSpy.first().at(2));
    QVERIFY(deferred.rawTickSnapshotDeferred);

    SqliteLocalDataStore mutationStore;
    QVERIFY(mutationStore.initialize(path));
    RawTradeTick appended;
    appended.instrument = datasetFor(0, 0).instrument;
    appended.tradingDate = QDate(2026, 1, 5);
    appended.timestamp = QDateTime(
        appended.tradingDate, QTime(14, 0), QTimeZone("Asia/Shanghai")).toUTC();
    appended.sourceLine = 1001;
    appended.price = 12.35;
    appended.volumeShares = 100.0;
    appended.amount = 1235.0;
    appended.direction = RawTradeDirection::Buy;
    appended.source = QStringLiteral("tickdata_csv_tick");
    const RawTradeIncrementalAppendResult appendResult =
        mutationStore.appendRawTradeTicksIncremental({appended});
    QVERIFY2(appendResult.factCommitted, qPrintable(appendResult.error));

    AsyncBacktestArchiveService archiveService;
    QVERIFY(archiveService.start(path));
    QSignalSpy archiveFinishedSpy(
        &archiveService, &AsyncBacktestArchiveService::finished);
    const QString resultId = QStringLiteral("changed-identity-archive");
    QVERIFY(archiveService.request(
        702, resultId, QDateTime::currentDateTimeUtc(),
        std::make_shared<const BacktestResult>(deferred)));
    QTRY_COMPARE_WITH_TIMEOUT(archiveFinishedSpy.count(), 1, 10000);
    QCOMPARE(archiveFinishedSpy.first().at(0).toULongLong(), quint64(702));
    QVERIFY(archiveFinishedSpy.first().at(2).toString().isEmpty());
    QVERIFY(archiveFinishedSpy.first().at(3).toString().contains(
        QStringLiteral("身份已变化")));

    SqliteLocalDataStore verificationStore;
    QVERIFY(verificationStore.initialize(path));
    QVERIFY(!verificationStore.loadBacktestArchive(resultId).has_value());
}

void BatchBacktestTest::completesMultipleSymbolsAndReportsInsufficientData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 12, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    BatchBacktestRequest request = requestFor(41, 12, 60);
    request.datasets.append(datasetFor(99, 5));
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 13);
    QCOMPARE(finishedSpy.first().at(0).toULongLong(), 41ULL);
    QVERIFY(!finishedSpy.first().at(1).toBool());
    QVERIFY(finishedSpy.first().at(2).toString().isEmpty());

    int succeeded = 0;
    int failed = 0;
    QStringList errors;
    for (const QList<QVariant> &arguments : itemSpy) {
        const BatchBacktestItemResult item =
            qvariant_cast<BatchBacktestItemResult>(arguments.at(1));
        if (item.succeeded) {
            ++succeeded;
            QCOMPARE(item.actualBars, 60);
            QVERIFY(item.dataVersion.startsWith(QStringLiteral("sha256:")));
            QCOMPARE(item.dataVersion.size(), 71);
            QVERIFY(item.datasetVersion.startsWith(QStringLiteral("sha256:")));
            QCOMPARE(item.datasetDayCount, 1);
            QCOMPARE(item.datasetRawTickCount, 60);
        } else {
            ++failed;
            QVERIFY(!item.error.isEmpty());
            errors.append(item.error);
        }
    }
    QVERIFY2(succeeded > 0, qPrintable(errors.join(QStringLiteral(" | "))));
    QCOMPARE(succeeded, 12);
    QCOMPARE(failed, 1);
}

void BatchBacktestTest::identityAndArchiveRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 3, 60);
    QVERIFY(!path.isEmpty());

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(requestFor(81, 3, 60)));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 3);

    BacktestResult retained;
    bool found = false;
    for (const QList<QVariant> &arguments : completedSpy) {
        const QString symbol = arguments.at(1).toString();
        const BacktestResult result =
            qvariant_cast<BacktestResult>(arguments.at(2));
        QVERIFY(result.equityCurve.size() > 0);
        QVERIFY(!result.config.datasetIdentity.isEmpty());
        QVERIFY(result.config.datasetIdentity.datasetVersion.startsWith(
            QStringLiteral("sha256:")));
        if (symbol == QStringLiteral("600001")) {
            retained = result;
            found = true;
        }
    }
    QVERIFY(found);

    // 周期 K 的样本末日买入不能绕过 T+1 强平，归档必须保留实际持仓。
    QCOMPARE(retained.config.dataMode, BacktestDataMode::Candles);
    QVERIFY(retained.finalPosition.quantity > 0.0);
    QVERIFY(retained.equityCurve.last().positionValue > 0.0);
    BacktestArchiveRecord archive;
    archive.resultId = QStringLiteral("batch-archive-1");
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.result = retained;
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QSignalSpy failures(&store, &SqliteLocalDataStore::writeFailed);
        const bool saved = store.saveBacktestArchive(archive);
        QVERIFY2(saved, failures.isEmpty() ? "" : qPrintable(failures.last().first().toString()));
        const auto restored = store.loadBacktestArchive(archive.resultId);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->result.config.datasetIdentity.datasetVersion,
                 retained.config.datasetIdentity.datasetVersion);
        QCOMPARE(restored->result.config.datasetIdentity.days.size(), 1);
        QCOMPARE(restored->result.config.datasetIdentity.rawTickTotal(), 60);
        QCOMPARE(restored->result.finalPosition.quantity, retained.finalPosition.quantity);
        QCOMPARE(restored->result.finalPosition.averagePrice, retained.finalPosition.averagePrice);
        QCOMPARE(restored->result.finalPosition.entryCommission, retained.finalPosition.entryCommission);
        QCOMPARE(restored->result.finalPosition.entryTime, retained.finalPosition.entryTime);
        QCOMPARE(restored->result.finalPosition.entryReason, retained.finalPosition.entryReason);
        QCOMPARE(restored->result.summary.finalEquity, retained.summary.finalEquity);
        QCOMPARE(restored->result.summary.totalCommission, retained.summary.totalCommission);
        QCOMPARE(restored->result.trades.size(), retained.trades.size());
        QCOMPARE(restored->result.equityCurve.last().cash, retained.equityCurve.last().cash);
        QCOMPARE(restored->result.equityCurve.last().positionValue,
                 retained.equityCurve.last().positionValue);
    }

    BatchBacktestResultListModel model;
    BatchBacktestItemResult item;
    item.instrument = datasetFor(0, 60).instrument;
    item.succeeded = true;
    item.datasetVersion = retained.config.datasetIdentity.datasetVersion;
    model.addResult(item);
    model.updateArchiveState(QStringLiteral("600000"),
                             QStringLiteral("batch-archive-1"));
    QCOMPARE(model.indexOfSymbol(QStringLiteral("600000")), 0);
    QVERIFY(model.get(0).value(QStringLiteral("archived")).toBool());
    QCOMPARE(model.get(0).value(QStringLiteral("archivedResultId")).toString(),
             QStringLiteral("batch-archive-1"));
}

void BatchBacktestTest::staleDatasetFailsBatchItem()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 2, 60);
    QVERIFY(!path.isEmpty());

    // 替换 600001 的事实但不刷新派生缓存，制造 stale 数据集。
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        const QDate day(2026, 1, 5);
        const QTimeZone shanghai("Asia/Shanghai");
        const Instrument instrument{QStringLiteral("600001"),
            QStringLiteral("测试1"), Market::AShare, QStringLiteral("上交所")};
        QVector<RawTradeTick> ticks;
        for (int bar = 0; bar < 61; ++bar) {
            RawTradeTick tick;
            tick.instrument = instrument;
            tick.tradingDate = day;
            tick.timestamp = QDateTime(day, QTime(9, 30), shanghai)
                                 .addSecs(bar * 60).toUTC();
            tick.sourceLine = bar + 1;
            tick.price = 10.0 + bar * 0.02;
            tick.volumeShares = 100.0;
            tick.amount = tick.price * 100.0;
            tick.direction = bar % 2 ? RawTradeDirection::Buy
                                     : RawTradeDirection::Sell;
            tick.source = QStringLiteral("tickdata_csv_tick");
            ticks.append(tick);
        }
        QVERIFY(store.beginRawTradeTickImport(
            instrument, day, QStringLiteral("tickdata_csv_tick")));
        QVERIFY(store.appendRawTradeTicks(ticks));
        QVERIFY(store.commitRawTradeTickImport());
        QVERIFY(!store.rawTickCandleCacheFresh(instrument, day, 60));
        const auto identity = store.loadBacktestDatasetIdentity(instrument, 60);
        QVERIFY(identity.has_value());
        QCOMPARE(identity->staleDayCount, 1);
    }
    {
        SqliteLocalDataStore freshStore;
        QVERIFY(freshStore.initialize(path));
        const Instrument instrument{QStringLiteral("600001"),
            QStringLiteral("测试1"), Market::AShare, QStringLiteral("上交所")};
        const auto freshIdentity =
            freshStore.loadBacktestDatasetIdentity(instrument, 60);
        QVERIFY(freshIdentity.has_value());
        QCOMPARE(freshIdentity->staleDayCount, 1);
    }

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(requestFor(82, 2, 60)));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 2);

    int succeeded = 0;
    int staleFailures = 0;
    BatchBacktestItemResult firstResult;
    BatchBacktestItemResult secondResult;
    for (const QList<QVariant> &arguments : itemSpy) {
        const BatchBacktestItemResult item =
            qvariant_cast<BatchBacktestItemResult>(arguments.at(1));
        if (item.instrument.symbol == QStringLiteral("600000")) firstResult = item;
        else secondResult = item;
    }
    QVERIFY(firstResult.succeeded);
    ++succeeded;
    QVERIFY2(!secondResult.succeeded, qPrintable(secondResult.error));
    QVERIFY(secondResult.error.contains(QStringLiteral("缓存过期")));
    QCOMPARE(secondResult.datasetStaleDayCount, 1);
    ++staleFailures;
    QCOMPARE(succeeded, 1);
    QCOMPARE(staleFailures, 1);
}

void BatchBacktestTest::rawTickMissingTradingDayFailsQualityGate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("quality-gap.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        const Instrument value{QStringLiteral("600000"), QStringLiteral("测试0"),
                               Market::AShare, QStringLiteral("上交所")};
        QVERIFY(seedDatasetDay(&store, value, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, value, QDate(2026, 1, 7), 12));
    }

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(rawRequestFor(
        91, QDate(2026, 1, 5), QDate(2026, 1, 7))));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    const auto item = qvariant_cast<BatchBacktestItemResult>(
        itemSpy.first().at(1));
    QVERIFY(!item.succeeded);
    QCOMPARE(item.dataQualityStatus, QStringLiteral("reject"));
    QVERIFY(!item.dataQualityEligible);
    QCOMPARE(item.datasetGapDayCount, 1);
    QVERIFY(item.error.contains(QStringLiteral("数据质量准入拒绝")));
    QCOMPARE(item.dataQualityAssessment.value(
                 QStringLiteral("missingTradingDates")).toList(),
             QVariantList{QDate(2026, 1, 6)});
}

void BatchBacktestTest::segmentedRawTickResearchRunsIndependentCleanSegments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("segmented.db"));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 6), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 8), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 9), 12));
    }

    BatchBacktestRequest request = rawRequestFor(
        912, QDate(2026, 1, 5), QDate(2026, 1, 9));
    request.segmentedResearch = true;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    QCOMPARE(completedSpy.count(), 0);

    const auto item = qvariant_cast<BatchBacktestItemResult>(
        itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QVERIFY(item.segmentedResearch);
    QVERIFY(item.researchOnly);
    QVERIFY(!item.archivable);
    QVERIFY(!item.dataQualityEligible);
    QCOMPARE(item.dataQualityStatus, QStringLiteral("reject"));
    QCOMPARE(item.researchSegments.size(), 2);
    QCOMPARE(item.researchSegments.at(0).startDate, QDate(2026, 1, 5));
    QCOMPARE(item.researchSegments.at(0).endDate, QDate(2026, 1, 6));
    QCOMPARE(item.researchSegments.at(1).startDate, QDate(2026, 1, 8));
    QCOMPARE(item.researchSegments.at(1).endDate, QDate(2026, 1, 9));
    for (const RawTickResearchSegmentResult &segment : item.researchSegments) {
        QVERIFY(segment.qualityEvidenceVersion.startsWith(
            QStringLiteral("sha256:")));
        QCOMPARE(segment.qualityEvidenceVersion.size(), 71);
        QCOMPARE(segment.toVariantMap().value(
                     QStringLiteral("qualityEvidenceVersion")).toString(),
                 segment.qualityEvidenceVersion);
    }
    QVERIFY(item.researchPlanVersion.startsWith(QStringLiteral("sha256:")));
    QCOMPARE(item.researchPlanVersion.size(), 71);
    QCOMPARE(item.datasetVersion, item.researchPlanVersion);
    QCOMPARE(item.toVariantMap().value(
                 QStringLiteral("researchPlanVersion")).toString(),
             item.researchPlanVersion);
    QCOMPARE(item.actualBars, 48);
    QCOMPARE(item.summary.finalEquity, 0.0);
    QCOMPARE(item.summary.totalReturnPercent, 0.0);
    QVERIFY(std::isfinite(item.segmentedMeanReturnPercent));
    QVERIFY(std::isfinite(item.segmentedMedianReturnPercent));
    QVERIFY(item.segmentedWorstReturnPercent
            <= item.segmentedBestReturnPercent);
    QVERIFY(item.datasetVersion.startsWith(QStringLiteral("sha256:")));
    QVERIFY(item.dataVersion.startsWith(QStringLiteral("sha256:")));
}

void BatchBacktestTest::segmentedResearchEvidenceTracksProvenanceNotPresentationText()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("evidence.db"));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    AShareInstrumentDayRecord dayRecord;
    dayRecord.instrument = instrument;
    dayRecord.tradingDate = QDate(2026, 1, 5);
    dayRecord.state = AShareInstrumentDayState::Tradable;
    dayRecord.reason = QStringLiteral("展示原因 A");
    dayRecord.source = QStringLiteral("test-fixture");
    dayRecord.sourceVersion = QStringLiteral("v1");
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 6), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 8), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 9), 12));
        QVERIFY(store.upsertAShareInstrumentDayRecords({dayRecord}));
    }

    const auto runResearch = [&](quint64 requestId) {
        BatchBacktestRequest request = rawRequestFor(
            requestId, QDate(2026, 1, 5), QDate(2026, 1, 9));
        request.segmentedResearch = true;
        BatchBacktestService service;
        service.setDatabasePath(path);
        QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
        QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
        if (!service.start(request)) return BatchBacktestItemResult{};
        if (finishedSpy.isEmpty() && !finishedSpy.wait(10000))
            return BatchBacktestItemResult{};
        if (itemSpy.size() != 1) return BatchBacktestItemResult{};
        return qvariant_cast<BatchBacktestItemResult>(itemSpy.first().at(1));
    };

    const BatchBacktestItemResult original = runResearch(913);
    QVERIFY2(original.succeeded, qPrintable(original.error));
    QCOMPARE(original.researchSegments.size(), 2);
    QVERIFY(!original.researchPlanVersion.isEmpty());

    // 人类可读原因和更新时间都不是质量事实身份。upsert 会同时刷新
    // updated_at_utc，但结构化状态、来源与来源版本保持不变。
    dayRecord.reason = QStringLiteral("展示原因 B（本地化文案变化）");
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.upsertAShareInstrumentDayRecords({dayRecord}));
    }
    const BatchBacktestItemResult presentationChanged = runResearch(914);
    QVERIFY2(presentationChanged.succeeded,
             qPrintable(presentationChanged.error));
    QCOMPARE(presentationChanged.researchSegments.at(0).qualityEvidenceVersion,
             original.researchSegments.at(0).qualityEvidenceVersion);
    QCOMPARE(presentationChanged.researchPlanVersion,
             original.researchPlanVersion);

    // 相同规范化 Tick 内容重新导入会获得新的 SQLite 行号；已有内容 SHA-256
    // 时，数据库自增 id 不能污染数据或质量证据身份。
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 6), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 8), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 9), 12));
    }
    const BatchBacktestItemResult reimported = runResearch(916);
    QVERIFY2(reimported.succeeded, qPrintable(reimported.error));
    QCOMPARE(reimported.researchSegments.at(0).qualityEvidenceVersion,
             original.researchSegments.at(0).qualityEvidenceVersion);
    QCOMPARE(reimported.researchSegments.at(1).qualityEvidenceVersion,
             original.researchSegments.at(1).qualityEvidenceVersion);
    QCOMPARE(reimported.researchPlanVersion, original.researchPlanVersion);

    // 同一状态的新权威来源版本是结构化证据变化，必须得到新身份；未涉及
    // 该日的第二段则保持原证据版本。
    dayRecord.sourceVersion = QStringLiteral("v2");
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.upsertAShareInstrumentDayRecords({dayRecord}));
    }
    const BatchBacktestItemResult provenanceChanged = runResearch(915);
    QVERIFY2(provenanceChanged.succeeded,
             qPrintable(provenanceChanged.error));
    QVERIFY(provenanceChanged.researchSegments.at(0).qualityEvidenceVersion
            != original.researchSegments.at(0).qualityEvidenceVersion);
    QCOMPARE(provenanceChanged.researchSegments.at(1).qualityEvidenceVersion,
             original.researchSegments.at(1).qualityEvidenceVersion);
    QVERIFY(provenanceChanged.researchPlanVersion
            != original.researchPlanVersion);
}

void BatchBacktestTest::availabilityAnalysisIsReadOnly()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("availability.db"));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 6), 12));
    }
    const QByteArray beforeHash = fileSha256(path);
    const qint64 beforeWalSize = QFileInfo(path + QStringLiteral("-wal")).size();
    QVERIFY(!beforeHash.isEmpty());

    AsyncRawTickAvailabilityService service;
    QSignalSpy resultSpy(&service,
                         &AsyncRawTickAvailabilityService::resultReady);
    QVERIFY(service.request(path, 1,
                            QDate(2026, 1, 5), QDate(2026, 1, 6), 1));
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.count(), 1, 10000);
    const QVariantMap result = resultSpy.first().at(1).toMap();
    QCOMPARE(result.value(QStringLiteral("recommendedStartDate")).toString(),
             QStringLiteral("2026-01-05"));
    QCOMPARE(result.value(QStringLiteral("recommendedEndDate")).toString(),
             QStringLiteral("2026-01-06"));

    QVERIFY(service.request(path, 2,
                            QDate(2027, 1, 4), QDate(2027, 1, 4), 1));
    QTRY_COMPARE_WITH_TIMEOUT(resultSpy.count(), 2, 10000);
    const QVariantMap unknownCalendar = resultSpy.at(1).at(1).toMap();
    QVERIFY(!unknownCalendar.value(QStringLiteral("calendarCoverageKnown"))
                 .toBool());
    QVERIFY(!unknownCalendar.value(QStringLiteral("fullRangeEligible")).toBool());
    QVERIFY(unknownCalendar.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("交易日历")));
    QCOMPARE(fileSha256(path), beforeHash);
    QCOMPARE(QFileInfo(path + QStringLiteral("-wal")).size(), beforeWalSize);
}

void BatchBacktestTest::availabilityCancellationTracksPendingWork()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cancel.db"));
    const Instrument instrument{QStringLiteral("600000"), QStringLiteral("测试0"),
                                Market::AShare, QStringLiteral("上交所")};
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
    }

    AsyncRawTickAvailabilityService service;
    QSignalSpy resultSpy(&service,
                         &AsyncRawTickAvailabilityService::resultReady);
    QVERIFY(service.request(path, 41,
                            QDate(2026, 1, 5), QDate(2026, 1, 5), 1));
    QVERIFY(service.isBusy());
    service.cancel();
    QTRY_VERIFY_WITH_TIMEOUT(!service.isBusy(), 10000);
    QCOMPARE(resultSpy.count(), 0);
}

void BatchBacktestTest::rawTickQualityRejectsBeforeFactMaterialization()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("quality-short-circuit.db"));
    const Instrument instrument{QStringLiteral("600000"),
                                QStringLiteral("测试0"),
                                Market::AShare,
                                QStringLiteral("上交所")};
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(seedDatasetDay(&store, instrument, QDate(2026, 1, 5), 12));
    }

    // 保留已经验证的强身份与质量审计，但移除事实行。门禁因 1 月 6 日缺失而
    // 必然拒绝；此时服务不应再触碰事实物化路径，计数应直接来自不可变身份。
    const QString connectionName = QStringLiteral("quality_short_circuit_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
            "delete from raw_trade_ticks where symbol=? and trading_date=?"));
        query.addBindValue(instrument.symbol);
        query.addBindValue(QStringLiteral("2026-01-05"));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        query.prepare(QStringLiteral(
            "update raw_tick_content_fingerprints set dirty=0 "
            "where symbol=? and trading_date=?"));
        query.addBindValue(instrument.symbol);
        query.addBindValue(QStringLiteral("2026-01-05"));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QCOMPARE(query.numRowsAffected(), 1);
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(rawRequestFor(
        911, QDate(2026, 1, 5), QDate(2026, 1, 6))));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(itemSpy.count(), 1);
    const auto item = qvariant_cast<BatchBacktestItemResult>(
        itemSpy.first().at(1));
    QVERIFY(!item.succeeded);
    QCOMPARE(item.dataQualityStatus, QStringLiteral("reject"));
    QCOMPARE(item.actualBars, 12);
    QVERIFY(item.error.contains(QStringLiteral("数据质量准入拒绝")));
    QCOMPARE(item.dataQualityAssessment.value(
                 QStringLiteral("missingTradingDates")).toList(),
             QVariantList{QDate(2026, 1, 6)});
}

void BatchBacktestTest::explicitSuspensionAllowsMissingRawTickDay()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("quality-suspension.db"));
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        const Instrument value{QStringLiteral("600000"), QStringLiteral("测试0"),
                               Market::AShare, QStringLiteral("上交所")};
        QVERIFY(seedDatasetDay(&store, value, QDate(2026, 1, 5), 12));
        QVERIFY(seedDatasetDay(&store, value, QDate(2026, 1, 7), 12));
        AShareInstrumentDayRecord record;
        record.instrument = value;
        record.tradingDate = QDate(2026, 1, 6);
        record.state = AShareInstrumentDayState::Suspended;
        record.reason = QStringLiteral("公告停牌");
        record.source = QStringLiteral("test-fixture");
        record.sourceVersion = QStringLiteral("v1");
        QVERIFY(store.upsertAShareInstrumentDayRecords({record}));
    }

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(rawRequestFor(
        92, QDate(2026, 1, 5), QDate(2026, 1, 7))));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    const auto item = qvariant_cast<BatchBacktestItemResult>(
        itemSpy.first().at(1));
    QVERIFY2(item.succeeded, qPrintable(item.error));
    QCOMPARE(item.dataQualityStatus, QStringLiteral("warning"));
    QVERIFY(item.dataQualityEligible);
    QCOMPARE(item.datasetGapDayCount, 0);
    QCOMPARE(item.dataQualityAssessment.value(
                 QStringLiteral("explainedNoTradeDates")).toList(),
             QVariantList{QDate(2026, 1, 6)});
}

void BatchBacktestTest::excessiveImportedBadRowsFailQualityGate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 1, 60);
    QVERIFY(!path.isEmpty());
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        const Instrument instrument = datasetFor(0, 0).instrument;
        const QDate day(2026, 1, 5);
        QVERIFY(seedDatasetDay(&store, instrument, day, 60, 10));
        const QVector<RawTickImportDayAudit> audits =
            store.loadRawTickImportDayAudits(
                instrument, day, day);
        QCOMPARE(audits.size(), 1);
        QCOMPARE(audits.first().skippedRecords, 10);
    }

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy itemSpy(&service, &BatchBacktestService::itemReady);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(rawRequestFor(
        93, QDate(2026, 1, 5), QDate(2026, 1, 5))));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    const auto item = qvariant_cast<BatchBacktestItemResult>(
        itemSpy.first().at(1));
    QVERIFY(!item.succeeded);
    QCOMPARE(item.dataQualityStatus, QStringLiteral("reject"));
    QCOMPARE(item.dataQualityAssessment.value(
                 QStringLiteral("skippedRecords")).toLongLong(), 10);
    QCOMPARE(item.dataQualityAssessment.value(
                 QStringLiteral("excessiveSkippedDates")).toList(),
             QVariantList{QDate(2026, 1, 5)});
    QVERIFY(item.error.contains(QStringLiteral("坏行率超过")));
}

void BatchBacktestTest::runPersistenceRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("runs.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 25);

    BatchBacktestRunSummary summary;
    summary.runId = QStringLiteral("run-1");
    summary.createdAt = QDateTime::currentDateTimeUtc().addSecs(-60);
    summary.strategyId = QStringLiteral("sma_crossover");
    summary.strategyParameters = {{QStringLiteral("fastPeriod"), 3},
                                  {QStringLiteral("slowPeriod"), 8}};
    summary.frequency = QStringLiteral("tick");
    summary.periodSeconds = 0;
    summary.barCount = 0;
    summary.dataMode = BacktestDataMode::RawTicks;
    summary.startDate = QDate(2026, 1, 5);
    summary.endDate = QDate(2026, 1, 9);
    summary.initialCapital = 1000000.0;
    summary.commissionRate = 0.0003;
    summary.slippageRate = 0.0002;
    summary.lotSize = 100;
    summary.holdoutTradingDays = 2;
    summary.walkForwardFoldCount = 3;
    summary.totalSymbols = 2;
    summary.status = QStringLiteral("running");
    QVERIFY(store.upsertBatchBacktestRun(summary));

    BatchBacktestRunItem first;
    first.runId = summary.runId;
    first.instrument = datasetFor(0, 60).instrument;
    first.status = QStringLiteral("succeeded");
    first.dataVersion = QStringLiteral("sha256:") + QString(64, 'a');
    first.datasetVersion = QStringLiteral("sha256:") + QString(64, 'b');
    QVERIFY(store.upsertBatchBacktestRunItem(first));
    BatchBacktestRunItem second;
    second.runId = summary.runId;
    second.instrument = datasetFor(1, 60).instrument;
    second.status = QStringLiteral("failed");
    second.error = QStringLiteral("派生缓存过期");
    QVERIFY(store.upsertBatchBacktestRunItem(second));

    const QVector<BatchBacktestRunSummary> runs =
        store.loadBatchBacktestRuns();
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs.first().runId, QStringLiteral("run-1"));
    QCOMPARE(runs.first().strategyId, QStringLiteral("sma_crossover"));
    QCOMPARE(runs.first().strategyParameters.value(QStringLiteral("fastPeriod")).toInt(), 3);
    QCOMPARE(runs.first().periodSeconds, 0);
    QCOMPARE(runs.first().dataMode, BacktestDataMode::RawTicks);
    QCOMPARE(runs.first().holdoutTradingDays, 2);
    QCOMPARE(runs.first().walkForwardFoldCount, 3);
    QCOMPARE(runs.first().startDate, QDate(2026, 1, 5));
    QCOMPARE(runs.first().endDate, QDate(2026, 1, 9));
    QCOMPARE(runs.first().totalSymbols, 2);
    QCOMPARE(runs.first().status, QStringLiteral("running"));
    QVERIFY(!runs.first().toVariantMap().value(QStringLiteral("runId")).toString().isEmpty());

    const QVector<BatchBacktestRunItem> items =
        store.loadBatchBacktestRunItems(summary.runId);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).instrument.symbol, QStringLiteral("600000"));
    QCOMPARE(items.at(0).status, QStringLiteral("succeeded"));
    QCOMPARE(items.at(1).status, QStringLiteral("failed"));

    // 运行结束后更新汇总与明细，再次读取保持一致。
    BatchBacktestRunSummary completed = summary;
    completed.completedAt = QDateTime::currentDateTimeUtc();
    completed.completedSymbols = 2;
    completed.succeededSymbols = 1;
    completed.failedSymbols = 1;
    completed.status = QStringLiteral("completed");
    QVERIFY(store.upsertBatchBacktestRun(completed));
    const QVector<BatchBacktestRunSummary> completedRuns =
        store.loadBatchBacktestRuns();
    QCOMPARE(completedRuns.first().status, QStringLiteral("completed"));
    QCOMPARE(completedRuns.first().succeededSymbols, 1);
}

void BatchBacktestTest::rawTickBatchKeepsOneResidentDataset()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 3, 80);
    QVERIFY(!path.isEmpty());

    BatchBacktestRequest request = requestFor(70, 3, 80);
    request.strategyId = QStringLiteral("tick_momentum");
    request.strategyParameters.clear();
    request.dataMode = BacktestDataMode::RawTicks;
    request.periodSeconds = 0;
    request.barCount = 0;
    request.maximumActiveEngines = 20;

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy residencySpy(&service, &BatchBacktestService::rawTickResidencyChanged);
    QSignalSpy completedSpy(&service, &BatchBacktestService::itemCompleted);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(request));
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 3);

    int peakDatasets = 0;
    qint64 peakObservations = 0;
    for (const QList<QVariant> &arguments : residencySpy) {
        QCOMPARE(arguments.at(0).toULongLong(), 70ULL);
        peakDatasets = std::max(peakDatasets, arguments.at(1).toInt());
        peakObservations = std::max(
            peakObservations, arguments.at(2).toLongLong());
    }
    QCOMPARE(peakDatasets, 1);
    QCOMPARE(peakObservations, 80);
    QVERIFY(!residencySpy.isEmpty());
    const QList<QVariant> finalUsage = residencySpy.last();
    QCOMPARE(finalUsage.at(1).toInt(), 0);
    QCOMPARE(finalUsage.at(2).toLongLong(), 0LL);
}

void BatchBacktestTest::retainedResultsEvictOldestWhenByteBudgetIsExceeded()
{
    BatchBacktestResultRetention retention(32, 1);
    BacktestResult first;
    first.config.instrument = datasetFor(0, 0).instrument;
    first.rawTicks.resize(100);
    QCOMPARE(retention.insert(QStringLiteral("600000"), first), QStringList{});
    QVERIFY(retention.contains(QStringLiteral("600000")));
    const std::shared_ptr<const BacktestResult> inFlightArchive =
        retention.sharedResult(QStringLiteral("600000"));
    QVERIFY(inFlightArchive);

    BacktestResult second;
    second.config.instrument = datasetFor(1, 0).instrument;
    second.rawTicks.resize(100);
    QCOMPARE(retention.insert(QStringLiteral("600001"), second),
             QStringList{QStringLiteral("600000")});
    QVERIFY(!retention.contains(QStringLiteral("600000")));
    QCOMPARE(inFlightArchive->rawTicks.size(), 100);
    QVERIFY(retention.contains(QStringLiteral("600001")));
    QCOMPARE(retention.size(), 1);
    QVERIFY(retention.estimatedBytes() > 1);
    QVERIFY(retention.find(QStringLiteral("600001")) != nullptr);
}

void BatchBacktestTest::cancellationAndBusyGate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createDatabase(directory.path(), 40, 300);
    QVERIFY(!path.isEmpty());

    BatchBacktestService service;
    service.setDatabasePath(path);
    QSignalSpy finishedSpy(&service, &BatchBacktestService::finished);
    QVERIFY(service.start(requestFor(71, 40, 300)));
    QVERIFY(!service.start(requestFor(72, 40, 300)));
    service.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(finishedSpy.first().at(0).toULongLong(), 71ULL);
    QVERIFY(finishedSpy.first().at(1).toBool());
    QVERIFY(!service.isRunning());
}

void BatchBacktestTest::resultModelRolesAndOrdering()
{
    BatchBacktestResultListModel model;
    QSignalSpy countSpy(&model, &BatchBacktestResultListModel::countChanged);
    BatchBacktestItemResult second;
    second.instrument = datasetFor(2, 60).instrument;
    second.dataMode = BacktestDataMode::RawTicks;
    second.startDate = QDate(2026, 1, 5);
    second.endDate = QDate(2026, 1, 9);
    second.frequency = QStringLiteral("1m");
    second.succeeded = true;
    second.actualBars = 125;
    second.archivable = true;
    second.summary.totalReturnPercent = 2.5;
    second.holdoutEnabled = true;
    second.holdoutTradingDays = 2;
    second.trainingStartDate = QDate(2026, 1, 5);
    second.trainingEndDate = QDate(2026, 1, 7);
    second.holdoutStartDate = QDate(2026, 1, 8);
    second.holdoutEndDate = QDate(2026, 1, 9);
    second.trainingActualTicks = 90;
    second.holdoutActualTicks = 35;
    second.trainingSummary.totalReturnPercent = 1.25;
    second.holdoutSummary.totalReturnPercent = 2.5;
    second.walkForwardEnabled = true;
    second.walkForwardFoldCount = 3;
    second.researchOnly = true;
    second.walkForwardMeanReturnPercent = 1.25;
    second.walkForwardMedianReturnPercent = 1.0;
    second.walkForwardWorstReturnPercent = -0.5;
    second.walkForwardBestReturnPercent = 3.25;
    RawTickWalkForwardFoldResult fold;
    fold.foldNumber = 1;
    fold.trainingStartDate = QDate(2026, 1, 5);
    fold.trainingEndDate = QDate(2026, 1, 7);
    fold.validationStartDate = QDate(2026, 1, 8);
    fold.validationEndDate = QDate(2026, 1, 9);
    fold.validationSummary.totalReturnPercent = 1.0;
    second.walkForwardFolds = {fold};
    BatchBacktestItemResult first = second;
    first.instrument = datasetFor(1, 60).instrument;
    first.summary.totalReturnPercent = 1.5;
    model.addResult(second);
    model.addResult(first);

    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 2);
    QCOMPARE(model.get(0).value(QStringLiteral("symbol")).toString(), QStringLiteral("600001"));
    QCOMPARE(model.get(0).value(QStringLiteral("totalReturnPercent")).toDouble(), 1.5);
    QCOMPARE(model.get(0).value(QStringLiteral("dataMode")).toString(),
             QStringLiteral("raw_ticks"));
    QCOMPARE(model.get(0).value(QStringLiteral("startDate")).toDate(), QDate(2026, 1, 5));
    QCOMPARE(model.get(0).value(QStringLiteral("endDate")).toDate(), QDate(2026, 1, 9));
    QVERIFY(model.get(0).value(QStringLiteral("archivable")).toBool());
    QVERIFY(model.get(0).value(QStringLiteral("holdoutEnabled")).toBool());
    QCOMPARE(model.get(0).value(QStringLiteral("holdoutTradingDays")).toInt(), 2);
    QCOMPARE(model.get(0).value(QStringLiteral("trainingActualTicks")).toInt(), 90);
    QCOMPARE(model.get(0).value(
                 QStringLiteral("trainingTotalReturnPercent")).toDouble(), 1.25);
    QCOMPARE(model.get(0).value(
                 QStringLiteral("holdoutTotalReturnPercent")).toDouble(), 2.5);
    QVERIFY(model.get(0).value(QStringLiteral("walkForwardEnabled")).toBool());
    QCOMPARE(model.get(0).value(QStringLiteral("walkForwardFoldCount")).toInt(), 3);
    QVERIFY(model.get(0).value(QStringLiteral("researchOnly")).toBool());
    QCOMPARE(model.get(0).value(QStringLiteral("walkForwardFolds")).toList().size(), 1);
    QCOMPARE(model.get(0).value(
                 QStringLiteral("walkForwardMeanReturnPercent")).toDouble(), 1.25);
    model.updateArchivableState(QStringLiteral("600001"), false);
    QVERIFY(!model.get(0).value(QStringLiteral("archivable")).toBool());
    QCOMPARE(model.indexOfSymbol(QStringLiteral("600002")), 1);
    QCOMPARE(model.indexOfSymbol(QStringLiteral("999999")), -1);
    QVERIFY(model.get(-1).isEmpty());
    model.clear();
    QCOMPARE(model.count(), 0);
}

QTEST_GUILESS_MAIN(BatchBacktestTest)

#include "tst_batch_backtest.moc"
