#include "services/PortfolioBacktestEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QDate>
#include <QHash>
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

} // namespace

class PortfolioBacktestStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void archiveRoundTripAndMigrationVersion();
    void archiveRejectsInvalidResult();
};

void PortfolioBacktestStoreTest::archiveRoundTripAndMigrationVersion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("portfolio.db"));
    SqliteLocalDataStore store;
    QVERIFY(store.initialize(path));
    QCOMPARE(store.schemaVersion(), 7);

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
    QCOMPARE(restored->result.trades.size(), result.trades.size());
    QCOMPARE(restored->result.trades.first().side,
             result.trades.first().side);
    QCOMPARE(restored->result.datasetVersions, result.datasetVersions);
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

} // namespace pp

QTEST_GUILESS_MAIN(pp::PortfolioBacktestStoreTest)

#include "tst_portfolio_backtest_store.moc"
