#include "services/RealtimeProviderRouter.h"
#include "services/ManagedProcessTree.h"
#include "services/MootdxCompactSnapshotParser.h"
#include "services/TencentQuoteParser.h"
#include "services/TencentMinuteBarService.h"
#include "services/TencentMarketDataProvider.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QProcess>
#include <QTest>
#include <QTimer>
#include <QTimeZone>

namespace pp {

namespace {

class FakeRealtimeProvider final : public MarketDataProvider {
public:
    void start(const QVector<Instrument> &instruments) override
    {
        ++startCount;
        lastUniverse = instruments;
    }

    void stop() override { ++stopCount; }

    void publish(int health, const QString &batchId = {}, int expected = 5000,
                 int received = 5000, const QString &error = {})
    {
        emit providerStateChanged(health, QStringLiteral("fake state"), error,
                                  expected, received, batchId,
                                  QDateTime::currentDateTimeUtc());
    }

    void publishTick(double price)
    {
        Tick tick;
        tick.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                           Market::AShare, QStringLiteral("上交所")};
        tick.timestamp = QDateTime::currentDateTimeUtc();
        tick.price = price;
        emit tickReceived(tick);
    }

    void publishBatchTick(double price, const QString &batchId)
    {
        Tick tick;
        tick.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                           Market::AShare, QStringLiteral("上交所")};
        tick.timestamp = QDateTime::currentDateTimeUtc();
        tick.batchId = batchId;
        tick.price = price;
        emit tickReceived(tick);
    }

    void publishSnapshot(const QString &batchId)
    {
        MarketSnapshot snapshot;
        snapshot.market = Market::AShare;
        snapshot.batchId = batchId;
        snapshot.expectedCount = 1;
        snapshot.timestamp = QDateTime::currentDateTimeUtc();
        snapshot.completedAt = snapshot.timestamp;
        CrossSectionalQuote quote;
        quote.instrument = {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
                            Market::AShare, QStringLiteral("上交所")};
        quote.timestamp = snapshot.timestamp;
        quote.price = 100.0;
        snapshot.quotes.append(quote);
        emit snapshotReceived(snapshot);
    }

    int startCount = 0;
    int stopCount = 0;
    QVector<Instrument> lastUniverse;
};

RealtimeProviderCapability capability(const QString &id, int priority,
                                      bool configured = true)
{
    RealtimeProviderCapability result;
    result.providerId = id;
    result.displayName = id;
    result.transport = QStringLiteral("fake");
    result.priority = priority;
    result.configured = configured;
    result.fullMarket = true;
    return result;
}

QVector<Instrument> universe()
{
    return {{QStringLiteral("600519"), QStringLiteral("贵州茅台"),
             Market::AShare, QStringLiteral("上交所")}};
}

} // namespace

class RealtimeProviderRouterTest final : public QObject {
    Q_OBJECT

private slots:
    void selectsConfiguredPrimaryAndPublishesCapabilities();
    void errorFailsOverAndIgnoresStalePrimaryTicks();
    void batchTicksAreCoalescedIntoOneSnapshotEvent();
    void repeatedDegradedBatchesTripCircuit();
    void highCompletenessDegradedBatchRemainsAvailable();
    void watchdogSilenceFailsOver();
    void controlledStopDoesNotWakeFallback();
    void tickActivityKeepsLongBatchAlive();
    void providerSpecificWatchdogSupportsMinutePolling();
    void allCircuitsAutomaticallyRetryAfterCooldown();
    void reactivationClearsPreviousFailureStreak();
    void tencentWorkersAreProviderChildren();
    void healthScoreIsBoundedAndQualitySensitive();
    void realtimePollingNeverRunsFasterThanOneMinute();
    void parsesCompactMootdxSnapshotChunk();
    void rejectsMalformedCompactMootdxSnapshotChunk();
    void parsesTencentQuoteAndMapsMinuteCapability();
    void parsesLatestTencentMinuteTradingDayInMemory();
    void alignsRealtimeMinuteRefreshToNaturalMinute();
    void aggregatesCommonPeriodsWithoutCrossingLunch();
    void realtimeMinuteRequestGateRejectsOldResponse();
    void realtimeSessionKeepsMinuteRefreshAcrossProviderRecovery();
    void stoppingProcessTreeTerminatesDescendant();
};

void RealtimeProviderRouterTest::selectsConfiguredPrimaryAndPublishesCapabilities()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    QSignalSpy routes(&router, &RealtimeProviderRouter::routeStateChanged);
    router.start(universe());
    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
    QCOMPARE(primary.startCount, 1);
    QCOMPARE(backup.startCount, 0);
    QCOMPARE(router.providerCapabilities().size(), 2);
    QVERIFY(!routes.isEmpty());
}

void RealtimeProviderRouterTest::errorFailsOverAndIgnoresStalePrimaryTicks()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    QSignalSpy ticks(&router, &MarketDataProvider::tickReceived);
    router.start(universe());
    primary.publish(static_cast<int>(ProviderHealthState::Error), {}, 5000, 0,
                    QStringLiteral("受控错误"));
    QCOMPARE(router.activeProviderId(), QStringLiteral("backup"));
    QCOMPARE(router.failoverCount(), 1);
    QVERIFY(router.fallbackUsed());
    QCOMPARE(primary.stopCount, 1);
    QCOMPARE(backup.startCount, 1);
    primary.publishTick(99.0);
    QCOMPARE(ticks.size(), 0);
    backup.publishTick(100.0);
    QCOMPARE(ticks.size(), 1);
}

void RealtimeProviderRouterTest::batchTicksAreCoalescedIntoOneSnapshotEvent()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    QSignalSpy ticks(&router, &MarketDataProvider::tickReceived);
    QSignalSpy snapshots(&router, &MarketDataProvider::snapshotReceived);
    router.start(universe());

    for (int index = 0; index < 5207; ++index) {
        primary.publishBatchTick(100.0 + index * 0.001,
                                 QStringLiteral("batch-1"));
    }
    primary.publishSnapshot(QStringLiteral("batch-1"));

    QCOMPARE(ticks.size(), 0);
    QCOMPARE(snapshots.size(), 1);
}

void RealtimeProviderRouterTest::repeatedDegradedBatchesTripCircuit()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 10000, 1000);
    router.start(universe());
    for (int index = 0; index < 3; ++index) {
        primary.publish(static_cast<int>(ProviderHealthState::Degraded),
                        QStringLiteral("batch-%1").arg(index), 5000, 4000,
                        QStringLiteral("缺失"));
    }
    QCOMPARE(router.activeProviderId(), QStringLiteral("backup"));
    QCOMPARE(router.failoverCount(), 1);
}

void RealtimeProviderRouterTest::highCompletenessDegradedBatchRemainsAvailable()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 10000, 1000);
    router.start(universe());
    for (int index = 0; index < 5; ++index) {
        primary.publish(static_cast<int>(ProviderHealthState::Degraded),
                        QStringLiteral("partial-%1").arg(index), 5000, 4900,
                        QStringLiteral("少量缺失"));
    }
    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
    QCOMPARE(router.failoverCount(), 0);
    QVERIFY(router.activeProviderScore() > 0);
}

void RealtimeProviderRouterTest::watchdogSilenceFailsOver()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 40, 1000);
    router.start(universe());
    QTRY_COMPARE_WITH_TIMEOUT(router.activeProviderId(), QStringLiteral("backup"), 500);
    QCOMPARE(router.failoverCount(), 1);
}

void RealtimeProviderRouterTest::controlledStopDoesNotWakeFallback()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 40, 1000);
    router.start(universe());

    primary.publish(static_cast<int>(ProviderHealthState::Stopped));
    QTest::qWait(140);

    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
    QCOMPARE(router.failoverCount(), 0);
    QCOMPARE(backup.startCount, 0);
}

void RealtimeProviderRouterTest::tickActivityKeepsLongBatchAlive()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 40, 1000);
    router.start(universe());
    QTimer activity;
    activity.setInterval(10);
    connect(&activity, &QTimer::timeout, &primary,
            [&primary]() { primary.publishTick(100.0); });
    activity.start();
    QTest::qWait(140);
    activity.stop();
    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
    QCOMPARE(router.failoverCount(), 0);
}

void RealtimeProviderRouterTest::providerSpecificWatchdogSupportsMinutePolling()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    RealtimeProviderCapability minuteCapability = capability(QStringLiteral("minute"), 10);
    minuteCapability.minimumPeriodSeconds = 60;
    minuteCapability.pollingIntervalSeconds = 60;
    minuteCapability.watchdogTimeoutMilliseconds = 200;
    router.addProvider(&primary, minuteCapability);
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 40, 1000);
    router.start(universe());
    QTest::qWait(120);
    QCOMPARE(router.activeProviderId(), QStringLiteral("minute"));
    QCOMPARE(router.failoverCount(), 0);
    const QVariantMap exposed = router.providerCapabilities().first().toMap();
    QCOMPARE(exposed.value(QStringLiteral("minimumPeriodSeconds")).toInt(), 60);
    QCOMPARE(exposed.value(QStringLiteral("watchdogTimeoutMilliseconds")).toInt(), 200);
}

void RealtimeProviderRouterTest::allCircuitsAutomaticallyRetryAfterCooldown()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(3, 1000, 40);
    router.start(universe());
    primary.publish(static_cast<int>(ProviderHealthState::Error));
    QCOMPARE(router.activeProviderId(), QStringLiteral("backup"));
    backup.publish(static_cast<int>(ProviderHealthState::Error));
    QVERIFY(router.activeProviderId().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(primary.startCount >= 2, 300);
    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
}

void RealtimeProviderRouterTest::reactivationClearsPreviousFailureStreak()
{
    RealtimeProviderRouter router;
    FakeRealtimeProvider primary;
    FakeRealtimeProvider backup;
    router.addProvider(&primary, capability(QStringLiteral("primary"), 10));
    router.addProvider(&backup, capability(QStringLiteral("backup"), 20));
    router.setFailurePolicy(2, 1000, 40);
    router.start(universe());
    primary.publish(static_cast<int>(ProviderHealthState::Degraded),
                    QStringLiteral("bad-1"), 5000, 4000);
    primary.publish(static_cast<int>(ProviderHealthState::Degraded),
                    QStringLiteral("bad-2"), 5000, 4000);
    QCOMPARE(router.activeProviderId(), QStringLiteral("backup"));
    backup.publish(static_cast<int>(ProviderHealthState::Error));
    QVERIFY(router.activeProviderId().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(router.activeProviderId(), QStringLiteral("primary"), 300);

    const int failoversBeforeStarting = router.failoverCount();
    primary.publish(static_cast<int>(ProviderHealthState::Starting));
    QCOMPARE(router.activeProviderId(), QStringLiteral("primary"));
    QCOMPARE(router.failoverCount(), failoversBeforeStarting);
}

void RealtimeProviderRouterTest::tencentWorkersAreProviderChildren()
{
    TencentMarketDataProvider provider;
    QNetworkAccessManager *network = provider.findChild<QNetworkAccessManager *>();
    QTimer *pollTimer = provider.findChild<QTimer *>();
    QVERIFY(network);
    QVERIFY(pollTimer);
    QCOMPARE(network->parent(), &provider);
    QCOMPARE(pollTimer->parent(), &provider);

}

void RealtimeProviderRouterTest::healthScoreIsBoundedAndQualitySensitive()
{
    const int healthy = realtimeProviderHealthScore(
        static_cast<int>(ProviderHealthState::Healthy), 5000, 5000, 0, 1000);
    const int incomplete = realtimeProviderHealthScore(
        static_cast<int>(ProviderHealthState::Degraded), 5000, 4500, 1, 1000);
    const int silent = realtimeProviderHealthScore(
        static_cast<int>(ProviderHealthState::Healthy), 5000, 5000, 0, 40000);
    QCOMPARE(healthy, 100);
    QVERIFY(incomplete < healthy);
    QVERIFY(silent < healthy);
    QVERIFY(incomplete >= 0 && incomplete <= 100);
}

void RealtimeProviderRouterTest::realtimePollingNeverRunsFasterThanOneMinute()
{
    QCOMPARE(normalizedRealtimePollingIntervalSeconds(0), 60);
    QCOMPARE(normalizedRealtimePollingIntervalSeconds(3), 60);
    QCOMPARE(normalizedRealtimePollingIntervalSeconds(60), 60);
    QCOMPARE(normalizedRealtimePollingIntervalSeconds(120), 120);
}

void RealtimeProviderRouterTest::parsesCompactMootdxSnapshotChunk()
{
    const QByteArray line =
        "S\ttdx-1\t2026-08-27T03:30:01.123+00:00\t"
        "600519,1300.5,1200,1300.4,1300.6,1299,1302,1298,1297.5,1560600;"
        "300750,210.25,800,210.2,210.3,208,212,207.5,209,168200";
    const MootdxCompactSnapshotChunk chunk =
        MootdxCompactSnapshotParser::parse(line);
    QVERIFY2(chunk.isValid(), qPrintable(chunk.error));
    QCOMPARE(chunk.batchId, QStringLiteral("tdx-1"));
    QCOMPARE(chunk.timestamp.toUTC().toString(Qt::ISODateWithMs),
             QStringLiteral("2026-08-27T03:30:01.123Z"));
    QCOMPARE(chunk.quotes.size(), 2);
    QCOMPARE(chunk.quotes.first().symbolCode, 600519);
    QCOMPARE(chunk.quotes.first().price, 1300.5);
    QCOMPARE(chunk.quotes.first().previousClose, 1297.5);
    QCOMPARE(chunk.quotes.last().amount, 168200.0);
}

void RealtimeProviderRouterTest::rejectsMalformedCompactMootdxSnapshotChunk()
{
    const auto malformedNumber = MootdxCompactSnapshotParser::parse(
        QByteArrayLiteral("S\ttdx-1\t2026-08-27T03:30:01.123+00:00\t"
                          "600519,not-a-price,1200,1,1,1,1,1,1,1"));
    QVERIFY(!malformedNumber.isValid());
    QVERIFY(malformedNumber.error.contains(QStringLiteral("数值无效")));
    const auto missingField = MootdxCompactSnapshotParser::parse(
        QByteArrayLiteral("S\ttdx-1\t2026-08-27T03:30:01.123+00:00\t"
                          "600519,1,2,3"));
    QVERIFY(!missingField.isValid());
    QVERIFY(missingField.error.contains(QStringLiteral("字段不足")));
}

void RealtimeProviderRouterTest::parsesTencentQuoteAndMapsMinuteCapability()
{
    QList<QByteArray> fields;
    for (int index = 0; index < 80; ++index) fields.append(QByteArray{});
    fields[0] = "1";
    fields[2] = "600519";
    fields[3] = "1309.22";
    fields[4] = "1308.55";
    fields[5] = "1308.66";
    fields[6] = "24976";
    fields[9] = "1309.21";
    fields[19] = "1309.23";
    fields[30] = "20260807161437";
    fields[33] = "1315.28";
    fields[34] = "1301.00";
    fields[35] = "1309.22/24976/3266919421";
    const QByteArray payload = "v_sh600519=\"" + fields.join('~') + "\";\r\n";
    QHash<QString, Instrument> instruments;
    instruments.insert(QStringLiteral("600519"), universe().first());
    const QVector<Tick> ticks = TencentQuoteParser::parse(
        payload, instruments, QStringLiteral("batch-1"));
    QCOMPARE(ticks.size(), 1);
    const Tick &tick = ticks.first();
    QCOMPARE(tick.instrument.symbol, QStringLiteral("600519"));
    QCOMPARE(tick.batchId, QStringLiteral("batch-1"));
    QCOMPARE(tick.price, 1309.22);
    QCOMPARE(tick.volume, 2497600.0);
    QCOMPARE(tick.amount, 3266919421.0);
    QCOMPARE(tick.timestamp.toUTC().toString(Qt::ISODate),
             QStringLiteral("2026-08-07T08:14:37Z"));
    QCOMPARE(TencentQuoteParser::providerCode(universe().first()),
             QStringLiteral("sh600519"));
    const Instrument beijing {QStringLiteral("920001"), QStringLiteral("北交样本"),
                              Market::AShare, QStringLiteral("北交所")};
    QVERIFY(TencentQuoteParser::providerCode(beijing).isEmpty());
}

void RealtimeProviderRouterTest::parsesLatestTencentMinuteTradingDayInMemory()
{
    const QByteArray payload = R"json({
        "code": 0,
        "msg": "",
        "data": {
            "sh600519": {
                "m1": [
                    ["202608061500", "1300.00", "1301.00", "1302.00", "1299.00", "10.00"],
                    ["202608070930", "1308.66", "1309.00", "1310.00", "1308.00", "12.00"],
                    ["202608070931", "1309.00", "1308.50", "1309.50", "1308.20", "8.00"],
                    ["202608070931", "1309.00", "1308.75", "1309.60", "1308.20", "9.00"]
                ]
            }
        }
    })json";
    const Instrument instrument = universe().first();
    const QDateTime requestedAt = QDateTime::fromString(
        QStringLiteral("2026-08-09T04:00:00Z"), Qt::ISODate);
    const RealtimeMinuteSeriesResult result = TencentMinuteBarService::parseResponse(
        payload, instrument, requestedAt, requestedAt.addMSecs(200));
    QVERIFY(result.succeeded);
    QCOMPARE(result.candles.size(), 2);
    QCOMPARE(result.candles.first().periodSeconds, 60);
    QCOMPARE(result.candles.first().source, QStringLiteral("tencent_public_m1"));
    QCOMPARE(result.candles.first().volume, 1200.0);
    QCOMPARE(result.candles.first().startTime.toString(Qt::ISODate),
             QStringLiteral("2026-08-07T01:30:00Z"));
    QCOMPARE(result.lastBarTime.toString(Qt::ISODate),
             QStringLiteral("2026-08-07T01:31:00Z"));
    QCOMPARE(result.candles.last().close, 1308.75);
    QCOMPARE(result.candles.last().volume, 900.0);
}

void RealtimeProviderRouterTest::alignsRealtimeMinuteRefreshToNaturalMinute()
{
    const QDateTime minute = QDateTime::fromString(
        QStringLiteral("2026-09-01T01:30:00.000Z"), Qt::ISODateWithMs);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(minute), 61500);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
                 minute.addMSecs(1000)), 60500);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
                 minute.addMSecs(1500)), 60000);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
                 minute.addMSecs(52250)), 9250);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
                 minute.addMSecs(59900)), 1600);
    QCOMPARE(TencentMinuteBarService::millisecondsUntilNextMinuteRefresh({}), 60000);
}

void RealtimeProviderRouterTest::aggregatesCommonPeriodsWithoutCrossingLunch()
{
    const Instrument value = universe().first();
    const QTimeZone shanghai("Asia/Shanghai");
    const QDate day(2026, 8, 24);
    const auto minute = [&](const QTime &time, double price) {
        Candle candle;
        candle.instrument = value;
        candle.startTime = QDateTime(day, time, shanghai).toUTC();
        candle.periodSeconds = 60;
        candle.open = price;
        candle.high = price + 1.0;
        candle.low = price - 1.0;
        candle.close = price + 0.5;
        candle.volume = 100.0;
        candle.source = QStringLiteral("tencent_public_m1");
        return candle;
    };
    const QVector<Candle> input {
        minute(QTime(9, 30), 10.0), minute(QTime(9, 31), 11.0),
        minute(QTime(11, 30), 12.0), minute(QTime(13, 0), 20.0),
        minute(QTime(13, 1), 21.0), minute(QTime(15, 0), 22.0),
        minute(QTime(15, 5), 23.0), minute(QTime(15, 30), 24.0),
        minute(QTime(15, 31), 25.0)};
    const QVector<Candle> fiveMinute =
        TencentMinuteBarService::aggregateCandles(input, 300);
    QCOMPARE(fiveMinute.size(), 6);
    QCOMPARE(fiveMinute.at(0).periodSeconds, 300);
    QCOMPARE(fiveMinute.at(0).open, 10.0);
    QCOMPARE(fiveMinute.at(0).close, 11.5);
    QCOMPARE(fiveMinute.at(0).volume, 200.0);
    QCOMPARE(fiveMinute.at(2).startTime.toTimeZone(shanghai).time(), QTime(13, 0));
    QCOMPARE(fiveMinute.at(3).startTime.toTimeZone(shanghai).time(), QTime(15, 0));
    QCOMPARE(fiveMinute.at(4).startTime.toTimeZone(shanghai).time(), QTime(15, 5));
    QCOMPARE(fiveMinute.at(5).startTime.toTimeZone(shanghai).time(), QTime(15, 25));
    QVERIFY(TencentMinuteBarService::supportsPeriod(3600));
    QVERIFY(!TencentMinuteBarService::supportsPeriod(120));
    QVERIFY(TencentMinuteBarService::aggregateCandles(input, 120).isEmpty());
}

void RealtimeProviderRouterTest::realtimeMinuteRequestGateRejectsOldResponse()
{
    RealtimeMinuteRequestGate gate;
    const quint64 first = gate.begin();
    const quint64 second = gate.begin();
    QVERIFY(!gate.finish(first));
    QVERIFY(gate.loading());
    QVERIFY(gate.finish(second));
    QVERIFY(!gate.loading());
    QCOMPARE(gate.resolvedRequestId(), second);
    gate.begin();
    gate.cancel();
    QVERIFY(!gate.loading());
}

void RealtimeProviderRouterTest::realtimeSessionKeepsMinuteRefreshAcrossProviderRecovery()
{
    RealtimeLiveSessionState state;
    QVERIFY(state.requestStart());
    QVERIFY(state.minuteRefreshEnabled());
    state.updateProviderHealth(static_cast<int>(ProviderHealthState::Error));
    QVERIFY(state.requested());
    QVERIFY(state.minuteRefreshEnabled());
    QVERIFY(!state.providerActive());
    state.updateProviderHealth(static_cast<int>(ProviderHealthState::Healthy));
    QVERIFY(state.requested());
    QVERIFY(state.minuteRefreshEnabled());
    QVERIFY(state.providerActive());
}

void RealtimeProviderRouterTest::stoppingProcessTreeTerminatesDescendant()
{
#ifndef Q_OS_WIN
    QSKIP("进程树回归只适用于 Windows 行情桥");
#else
    QProcess process;
    const QString command = qEnvironmentVariable(
        "ComSpec", QStringLiteral("C:/Windows/System32/cmd.exe"));
    process.start(command, {QStringLiteral("/d"), QStringLiteral("/c"),
                            QStringLiteral("ping"), QStringLiteral("-t"),
                            QStringLiteral("127.0.0.1")});
    QVERIFY(process.waitForStarted(2000));
    QList<qint64> descendants;
    QTRY_VERIFY_WITH_TIMEOUT(
        !(descendants = ManagedProcessTree::descendantProcessIds(
              process.processId())).isEmpty(), 2000);
    // cmd 启动后会先出现控制台宿主，再创建 ping；等待进程树稳定并重新
    // 取样，避免只捕获到启动瞬间的第一个子进程而漏掉真正的工作进程。
    QTest::qWait(250);
    descendants = ManagedProcessTree::descendantProcessIds(process.processId());
    QVERIFY(!descendants.isEmpty());

    ManagedProcessTree::stop(&process, 1000, 2000);
    bool descendantsStopped = false;
    QTRY_VERIFY_WITH_TIMEOUT((descendantsStopped = std::all_of(
        descendants.cbegin(), descendants.cend(), [](qint64 processId) {
            return !ManagedProcessTree::isProcessRunning(processId);
        })), 3000);
    for (const qint64 processId : std::as_const(descendants)) {
        if (!ManagedProcessTree::isProcessRunning(processId)) continue;
        QProcess::execute(QStringLiteral("taskkill"),
                          {QStringLiteral("/PID"), QString::number(processId),
                           QStringLiteral("/T"), QStringLiteral("/F")});
    }
    QVERIFY2(descendantsStopped,
             "停止行情桥父进程后仍残留子进程");
#endif
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::RealtimeProviderRouterTest)

#include "tst_realtime_provider_router.moc"
