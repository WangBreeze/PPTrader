#include "domain/BatchBacktestTypes.h"
#include "domain/RawTradeTypes.h"
#include "models/BatchBacktestResultListModel.h"
#include "services/BatchBacktestService.h"
#include "services/SqliteLocalDataStore.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

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
        if (!store.commitRawTradeTickImport()) return {};

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
    request.barCount = bars;
    request.initialCapital = 1000000.0;
    request.commissionRate = 0.0003;
    request.slippageRate = 0.0002;
    request.maximumActiveEngines = 4;
    return request;
}

} // namespace

class BatchBacktestTest final : public QObject {
    Q_OBJECT

private slots:
    void completesMultipleSymbolsAndReportsInsufficientData();
    void identityAndArchiveRoundTrip();
    void staleDatasetFailsBatchItem();
    void runPersistenceRoundTrip();
    void cancellationAndBusyGate();
    void resultModelRolesAndOrdering();
};

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

    BacktestArchiveRecord archive;
    archive.resultId = QStringLiteral("batch-archive-1");
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.result = retained;
    {
        SqliteLocalDataStore store;
        QVERIFY(store.initialize(path));
        QVERIFY(store.saveBacktestArchive(archive));
        const auto restored = store.loadBacktestArchive(archive.resultId);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->result.config.datasetIdentity.datasetVersion,
                 retained.config.datasetIdentity.datasetVersion);
        QCOMPARE(restored->result.config.datasetIdentity.days.size(), 1);
        QCOMPARE(restored->result.config.datasetIdentity.rawTickTotal(), 60);
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

void BatchBacktestTest::runPersistenceRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("runs.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 5);

    BatchBacktestRunSummary summary;
    summary.runId = QStringLiteral("run-1");
    summary.createdAt = QDateTime::currentDateTimeUtc().addSecs(-60);
    summary.strategyId = QStringLiteral("sma_crossover");
    summary.strategyParameters = {{QStringLiteral("fastPeriod"), 3},
                                  {QStringLiteral("slowPeriod"), 8}};
    summary.frequency = QStringLiteral("1m");
    summary.periodSeconds = 60;
    summary.barCount = 60;
    summary.initialCapital = 1000000.0;
    summary.commissionRate = 0.0003;
    summary.slippageRate = 0.0002;
    summary.lotSize = 100;
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
    QCOMPARE(runs.first().periodSeconds, 60);
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
    second.frequency = QStringLiteral("1m");
    second.succeeded = true;
    second.summary.totalReturnPercent = 2.5;
    BatchBacktestItemResult first = second;
    first.instrument = datasetFor(1, 60).instrument;
    first.summary.totalReturnPercent = 1.5;
    model.addResult(second);
    model.addResult(first);

    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 2);
    QCOMPARE(model.get(0).value(QStringLiteral("symbol")).toString(), QStringLiteral("600001"));
    QCOMPARE(model.get(0).value(QStringLiteral("totalReturnPercent")).toDouble(), 1.5);
    QCOMPARE(model.indexOfSymbol(QStringLiteral("600002")), 1);
    QCOMPARE(model.indexOfSymbol(QStringLiteral("999999")), -1);
    QVERIFY(model.get(-1).isEmpty());
    model.clear();
    QCOMPARE(model.count(), 0);
}

QTEST_GUILESS_MAIN(BatchBacktestTest)

#include "tst_batch_backtest.moc"
