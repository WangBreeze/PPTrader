#include "services/HistoricalDataProviderRouter.h"

#include <QSignalSpy>
#include <QtTest>

using namespace pp;

namespace {

class FakeProvider final : public HistoricalDataProvider {
    Q_OBJECT

public:
    explicit FakeProvider(HistoricalProviderCapability descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }

    HistoricalProviderCapability capability() const override
    {
        return m_descriptor;
    }

    void fetch(quint64 requestId, const Instrument &instrument,
               const QString &frequency, int count) override
    {
        fetchAdjusted(requestId, instrument, frequency, count,
                      QStringLiteral("none"));
    }

    void fetchAdjusted(quint64 requestId, const Instrument &instrument,
                       const QString &frequency, int count,
                       const QString &adjustment) override
    {
        requestIds.append(requestId);
        instruments.append(instrument);
        frequencies.append(frequency);
        counts.append(count);
        adjustments.append(adjustment);
    }

    void succeed(quint64 requestId, double close = 10.0)
    {
        Candle candle;
        candle.instrument = instruments.last();
        candle.periodSeconds = 86400;
        candle.startTime = QDateTime(QDate(2026, 8, 7), QTime(0, 0),
                                     QTimeZone("Asia/Shanghai"));
        candle.open = close;
        candle.high = close;
        candle.low = close;
        candle.close = close;
        candle.volume = 100.0;
        candle.source = m_descriptor.providerId;
        candle.adjustment = adjustments.last();
        emit candlesReady(requestId, {candle});
    }

    void fail(quint64 requestId, const QString &message)
    {
        emit fetchError(requestId, message);
    }

    HistoricalProviderCapability m_descriptor;
    QVector<quint64> requestIds;
    QVector<Instrument> instruments;
    QStringList frequencies;
    QVector<int> counts;
    QStringList adjustments;
};

HistoricalProviderCapability descriptor(
    const QString &id, int priority, const QStringList &adjustments,
    bool configured = true)
{
    HistoricalProviderCapability result;
    result.providerId = id;
    result.displayName = id;
    result.markets = {Market::AShare};
    result.frequencies = {QStringLiteral("day")};
    result.adjustments = adjustments;
    result.priority = priority;
    result.configured = configured;
    return result;
}

Instrument instrument()
{
    return {QStringLiteral("600519"), QStringLiteral("贵州茅台"),
            Market::AShare, QStringLiteral("上交所")};
}

} // namespace

class HistoricalProviderRouterTest final : public QObject {
    Q_OBJECT

private slots:
    void priorityAndFallback();
    void adjustmentCapabilityRoutesDirectly();
    void staleProviderResponseCannotWin();
    void unavailableCapabilityFailsClearly();
    void venueCapabilityExcludesBeijingForNow();
};

void HistoricalProviderRouterTest::priorityAndFallback()
{
    HistoricalDataProviderRouter router;
    FakeProvider primary(descriptor(QStringLiteral("primary"), 10,
                                    {QStringLiteral("none")}));
    FakeProvider fallback(descriptor(QStringLiteral("fallback"), 20,
                                     {QStringLiteral("none")}));
    router.addProvider(&fallback);
    router.addProvider(&primary);
    QSignalSpy resolved(&router, &HistoricalDataProviderRouter::routeResolved);
    QSignalSpy ready(&router, &HistoricalDataProvider::candlesReady);

    router.fetch(7, instrument(), QStringLiteral("day"), 500);
    QCOMPARE(primary.requestIds, QVector<quint64>{7});
    QVERIFY(fallback.requestIds.isEmpty());
    primary.fail(7, QStringLiteral("受控失败"));
    QCOMPARE(fallback.requestIds, QVector<quint64>{7});
    fallback.succeed(7);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(resolved.size(), 1);
    QCOMPARE(resolved.first().at(1).toString(), QStringLiteral("fallback"));
    QVERIFY(resolved.first().at(2).toBool());
}

void HistoricalProviderRouterTest::adjustmentCapabilityRoutesDirectly()
{
    HistoricalDataProviderRouter router;
    FakeProvider unadjusted(descriptor(QStringLiteral("none_only"), 10,
                                       {QStringLiteral("none")}));
    FakeProvider adjusted(descriptor(QStringLiteral("adjusted"), 20,
                                     {QStringLiteral("none"), QStringLiteral("qfq")}));
    router.addProvider(&unadjusted);
    router.addProvider(&adjusted);
    router.fetchAdjusted(8, instrument(), QStringLiteral("day"), 100,
                         QStringLiteral("qfq"));
    QVERIFY(unadjusted.requestIds.isEmpty());
    QCOMPARE(adjusted.requestIds, QVector<quint64>{8});
    QCOMPARE(adjusted.adjustments, QStringList{QStringLiteral("qfq")});
}

void HistoricalProviderRouterTest::staleProviderResponseCannotWin()
{
    HistoricalDataProviderRouter router;
    FakeProvider primary(descriptor(QStringLiteral("primary"), 10,
                                    {QStringLiteral("none")}));
    FakeProvider fallback(descriptor(QStringLiteral("fallback"), 20,
                                     {QStringLiteral("none")}));
    router.addProvider(&primary);
    router.addProvider(&fallback);
    QSignalSpy ready(&router, &HistoricalDataProvider::candlesReady);
    router.fetch(9, instrument(), QStringLiteral("day"), 100);
    primary.fail(9, QStringLiteral("失败后仍迟到"));
    primary.succeed(9, 99.0);
    QCOMPARE(ready.size(), 0);
    fallback.succeed(9, 10.0);
    QCOMPARE(ready.size(), 1);
}

void HistoricalProviderRouterTest::unavailableCapabilityFailsClearly()
{
    HistoricalDataProviderRouter router;
    FakeProvider unavailable(descriptor(QStringLiteral("disabled"), 10,
                                         {QStringLiteral("none")}, false));
    router.addProvider(&unavailable);
    QSignalSpy errors(&router, &HistoricalDataProvider::fetchError);
    router.fetchAdjusted(10, instrument(), QStringLiteral("day"), 100,
                         QStringLiteral("hfq"));
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().at(1).toString().contains(QStringLiteral("没有已配置")));
}

void HistoricalProviderRouterTest::venueCapabilityExcludesBeijingForNow()
{
    HistoricalProviderCapability capability = descriptor(
        QStringLiteral("sh_sz_only"), 10, {QStringLiteral("none")});
    capability.venues = {QStringLiteral("上交所"), QStringLiteral("深交所")};
    Instrument beijing{QStringLiteral("920001"), QStringLiteral("北交所样本"),
                       Market::AShare, QStringLiteral("北交所")};
    QVERIFY(!capability.supports(beijing, QStringLiteral("day"),
                                 QStringLiteral("none")));
    QVERIFY(capability.supports(instrument(), QStringLiteral("day"),
                                QStringLiteral("none")));
}

QTEST_GUILESS_MAIN(HistoricalProviderRouterTest)

#include "tst_historical_provider_router.moc"
