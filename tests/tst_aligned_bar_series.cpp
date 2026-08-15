#include "services/AlignedBarSeries.h"

#include <QTest>

namespace pp {

namespace {

Candle makeCandle(const QString &symbol, const QDateTime &startTime,
                  double close, int periodSeconds = 60)
{
    Candle candle;
    candle.instrument.symbol = symbol;
    candle.instrument.name = symbol;
    candle.instrument.market = Market::AShare;
    candle.startTime = startTime;
    candle.periodSeconds = periodSeconds;
    candle.open = candle.high = candle.low = candle.close = close;
    candle.volume = 1000.0;
    candle.source = QStringLiteral("raw_tick_cache_v1");
    return candle;
}

QDateTime at(int day, int hour, int minute)
{
    return QDateTime(QDate(2026, 8, day), QTime(hour, minute));
}

} // namespace

class AlignedBarSeriesTest final : public QObject {
    Q_OBJECT

private slots:
    void singleSeriesBuildsFramePerBar();
    void framesAreSortedByTime();
    void replayBarOutOfRangeReturnsNull();
    void emptyInputRejected();
    void twoSeriesAlignMissingBarsAbsent();
    void mixedPeriodsRejected();
};

void AlignedBarSeriesTest::singleSeriesBuildsFramePerBar()
{
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {makeCandle(QStringLiteral("600001"), at(1, 9, 30), 10.0),
          makeCandle(QStringLiteral("600001"), at(1, 9, 31), 11.0),
          makeCandle(QStringLiteral("600001"), at(1, 9, 32), 12.0)}}};

    QString error;
    const AlignedBarSeries aligned = AlignedBarSeries::build(series, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(aligned.frameCount(), 3);
    QCOMPARE(aligned.periodSeconds(), 60);
    for (int index = 0; index < aligned.frameCount(); ++index) {
        const AlignedBarFrame *frame = aligned.frameAt(index);
        QVERIFY(frame != nullptr);
        QVERIFY(frame->hasBar(QStringLiteral("600001")));
        QCOMPARE(frame->bars.size(), 1);
    }
}

void AlignedBarSeriesTest::framesAreSortedByTime()
{
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {makeCandle(QStringLiteral("600001"), at(1, 9, 32), 12.0),
          makeCandle(QStringLiteral("600001"), at(1, 9, 30), 10.0),
          makeCandle(QStringLiteral("600001"), at(1, 9, 31), 11.0)}}};

    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 3);
    QCOMPARE(aligned.frameAt(0)->timestamp, at(1, 9, 30));
    QCOMPARE(aligned.frameAt(1)->timestamp, at(1, 9, 31));
    QCOMPARE(aligned.frameAt(2)->timestamp, at(1, 9, 32));
}

void AlignedBarSeriesTest::replayBarOutOfRangeReturnsNull()
{
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {makeCandle(QStringLiteral("600001"), at(1, 9, 30), 10.0)}}};

    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 1);
    QVERIFY(aligned.replayBar(0) != nullptr);
    QVERIFY(aligned.replayBar(-1) == nullptr);
    QVERIFY(aligned.replayBar(1) == nullptr);
}

void AlignedBarSeriesTest::emptyInputRejected()
{
    QString error;
    const AlignedBarSeries aligned = AlignedBarSeries::build({}, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(aligned.isEmpty());
    QCOMPARE(aligned.frameCount(), 0);
}

void AlignedBarSeriesTest::twoSeriesAlignMissingBarsAbsent()
{
    // 600001 在 9:30/9:32 有 bar，600002 在 9:31/9:32 有 bar。
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {makeCandle(QStringLiteral("600001"), at(1, 9, 30), 10.0),
          makeCandle(QStringLiteral("600001"), at(1, 9, 32), 12.0)}},
        {QStringLiteral("600002"),
         {makeCandle(QStringLiteral("600002"), at(1, 9, 31), 20.0),
          makeCandle(QStringLiteral("600002"), at(1, 9, 32), 22.0)}}};

    const AlignedBarSeries aligned = AlignedBarSeries::build(series);
    QCOMPARE(aligned.frameCount(), 3);

    const AlignedBarFrame *frame = aligned.frameAt(0);
    QCOMPARE(frame->timestamp, at(1, 9, 30));
    QVERIFY(frame->hasBar(QStringLiteral("600001")));
    QVERIFY(!frame->hasBar(QStringLiteral("600002")));

    frame = aligned.frameAt(1);
    QCOMPARE(frame->timestamp, at(1, 9, 31));
    QVERIFY(!frame->hasBar(QStringLiteral("600001")));
    QVERIFY(frame->hasBar(QStringLiteral("600002")));

    frame = aligned.frameAt(2);
    QCOMPARE(frame->timestamp, at(1, 9, 32));
    QVERIFY(frame->hasBar(QStringLiteral("600001")));
    QVERIFY(frame->hasBar(QStringLiteral("600002")));
}

void AlignedBarSeriesTest::mixedPeriodsRejected()
{
    const QHash<QString, QVector<Candle>> series{
        {QStringLiteral("600001"),
         {makeCandle(QStringLiteral("600001"), at(1, 9, 30), 10.0, 60)}},
        {QStringLiteral("600002"),
         {makeCandle(QStringLiteral("600002"), at(1, 9, 30), 20.0, 300)}}};

    QString error;
    const AlignedBarSeries aligned = AlignedBarSeries::build(series, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(aligned.isEmpty());
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::AlignedBarSeriesTest)

#include "tst_aligned_bar_series.moc"
