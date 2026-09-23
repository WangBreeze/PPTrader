#include "visualization/FrameTimingStatistics.h"

#include <QJsonObject>
#include <QtTest>

using namespace pp;

class FrameTimingStatisticsTest final : public QObject {
    Q_OBJECT

private slots:
    void emptySamplesReturnZeroSummary();
    void nearestRankPercentilesAndBudgetsAreStable();
    void percentileFractionIsClamped();
    void memoryGrowthAndSlopeAreReported();
};

void FrameTimingStatisticsTest::emptySamplesReturnZeroSummary()
{
    const FrameTimingSummary summary = summarizeFrameTimings({});
    QCOMPARE(summary.sampleCount, 0);
    QCOMPARE(summary.p95Milliseconds, 0.0);
    QCOMPARE(summary.framesOver16_7Milliseconds, 0);
    QCOMPARE(summary.toJson().value(QStringLiteral("sampleCount")).toInt(), 0);
}

void FrameTimingStatisticsTest::nearestRankPercentilesAndBudgetsAreStable()
{
    const QVector<qint64> samples {
        10'000'000, 16'700'000, 16'700'001, 25'000'001, 40'000'000
    };
    const FrameTimingSummary summary = summarizeFrameTimings(samples);
    QCOMPARE(summary.sampleCount, 5);
    QCOMPARE(summary.minimumMilliseconds, 10.0);
    QCOMPARE(summary.maximumMilliseconds, 40.0);
    QCOMPARE(summary.p50Milliseconds, 16.700001);
    QCOMPARE(summary.p95Milliseconds, 40.0);
    QCOMPARE(summary.p99Milliseconds, 40.0);
    QCOMPARE(summary.framesOver16_7Milliseconds, 3);
    QCOMPARE(summary.framesOver25Milliseconds, 2);
    QCOMPARE(summary.framesOver33_3Milliseconds, 1);

    const QJsonObject json = summary.toJson();
    QCOMPARE(json.value(QStringLiteral("framesOver25Ms")).toInt(), 2);
}

void FrameTimingStatisticsTest::percentileFractionIsClamped()
{
    const QVector<qint64> samples {30'000'000, 10'000'000, 20'000'000};
    QCOMPARE(timingPercentileMilliseconds(samples, -1.0), 10.0);
    QCOMPARE(timingPercentileMilliseconds(samples, 2.0), 30.0);
    QCOMPARE(countTimingSamplesOver(samples, 20.0), 1);
}

void FrameTimingStatisticsTest::memoryGrowthAndSlopeAreReported()
{
    const QVector<ProcessMemorySample> samples {
        {0, 1000, 2000},
        {60000, 1100, 2200},
        {120000, 1200, 2400}
    };
    const ProcessMemorySummary summary = summarizeProcessMemory(samples);
    QCOMPARE(summary.sampleCount, 3);
    QCOMPARE(summary.workingSetGrowthBytes, qint64(200));
    QCOMPARE(summary.privateGrowthBytes, qint64(400));
    QCOMPARE(summary.peakWorkingSetBytes, qint64(1200));
    QCOMPARE(summary.peakPrivateBytes, qint64(2400));
    QCOMPARE(summary.workingSetSlopeBytesPerMinute, 100.0);
    QCOMPARE(summary.privateSlopeBytesPerMinute, 200.0);
    QCOMPARE(summary.toJson().value(
        QStringLiteral("privateGrowthBytes")).toInteger(), qint64(400));
    const ProcessMemorySummary emptySummary = summarizeProcessMemory({});
    QCOMPARE(emptySummary.sampleCount, 0);
    QCOMPARE(emptySummary.toJson().value(
        QStringLiteral("privateSlopeBytesPerMinute")).toDouble(), 0.0);
}

QTEST_APPLESS_MAIN(FrameTimingStatisticsTest)
#include "tst_frame_timing_statistics.moc"
