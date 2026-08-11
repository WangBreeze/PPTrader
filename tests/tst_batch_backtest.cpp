#include "domain/BatchBacktestTypes.h"
#include "models/BatchBacktestResultListModel.h"
#include "services/BatchBacktestService.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <QUuid>

using namespace pp;

namespace {

QString createDatabase(const QString &directory, int symbolCount, int barsPerSymbol)
{
    const QString path = directory + QStringLiteral("/batch.db");
    const QString connectionName = QStringLiteral("batch-test-create-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        if (!database.open()) return {};
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral(
                "create table candles ("
                "symbol text not null, name text, market integer, venue text, "
                "start_time_utc text not null, period_seconds integer not null, "
                "open real, high real, low real, close real, volume real, "
                "buy_volume real, sell_volume real, neutral_volume real, "
                "directional_volume_known integer, source text, adjustment text, "
                "downloaded_at_utc text, primary key(symbol, period_seconds, start_time_utc))"))) {
            return {};
        }
        if (!database.transaction()) return {};
        query.prepare(QStringLiteral(
            "insert into candles values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        const QDateTime start(QDate(2024, 1, 2), QTime(1, 30), Qt::UTC);
        for (int symbolIndex = 0; symbolIndex < symbolCount; ++symbolIndex) {
            const QString symbol = QStringLiteral("%1").arg(600000 + symbolIndex, 6, 10, QLatin1Char('0'));
            for (int bar = 0; bar < barsPerSymbol; ++bar) {
                const double close = 10.0 + symbolIndex * 0.01 + bar * 0.02
                                     + (bar % 7 == 0 ? -0.03 : 0.0);
                query.addBindValue(symbol);
                query.addBindValue(QStringLiteral("测试%1").arg(symbolIndex));
                query.addBindValue(static_cast<int>(Market::AShare));
                query.addBindValue(QStringLiteral("上交所"));
                query.addBindValue(start.addSecs(bar * 60).toString(Qt::ISODateWithMs));
                query.addBindValue(60);
                query.addBindValue(close - 0.01);
                query.addBindValue(close + 0.03);
                query.addBindValue(close - 0.04);
                query.addBindValue(close);
                query.addBindValue(10000.0 + bar * 100.0);
                query.addBindValue(5000.0);
                query.addBindValue(4000.0);
                query.addBindValue(1000.0);
                query.addBindValue(1);
                query.addBindValue(QStringLiteral("raw_tick_cache_v1"));
                query.addBindValue(QStringLiteral("none"));
                query.addBindValue(start.toString(Qt::ISODateWithMs));
                if (!query.exec()) return {};
                query.finish();
            }
        }
        if (!database.commit()) return {};
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
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
