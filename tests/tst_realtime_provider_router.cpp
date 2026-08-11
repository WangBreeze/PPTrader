#include "services/RealtimeProviderRouter.h"
#include "services/TencentQuoteParser.h"
#include "services/TencentMinuteBarService.h"

#include <QSignalSpy>
#include <QTest>

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
    void repeatedDegradedBatchesTripCircuit();
    void highCompletenessDegradedBatchRemainsAvailable();
    void watchdogSilenceFailsOver();
    void tickActivityKeepsLongBatchAlive();
    void providerSpecificWatchdogSupportsMinutePolling();
    void allCircuitsAutomaticallyRetryAfterCooldown();
    void healthScoreIsBoundedAndQualitySensitive();
    void parsesTencentQuoteAndMapsMinuteCapability();
    void parsesLatestTencentMinuteTradingDayInMemory();
    void realtimeMinuteRequestGateRejectsOldResponse();
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
                    ["202608070931", "1309.00", "1308.50", "1309.50", "1308.20", "8.00"]
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

} // namespace pp

QTEST_GUILESS_MAIN(pp::RealtimeProviderRouterTest)

#include "tst_realtime_provider_router.moc"
