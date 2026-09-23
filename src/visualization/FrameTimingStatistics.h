#pragma once

#include <QJsonObject>
#include <QVector>

namespace pp {

struct FrameTimingSummary {
    int sampleCount = 0;
    double minimumMilliseconds = 0.0;
    double maximumMilliseconds = 0.0;
    double meanMilliseconds = 0.0;
    double p50Milliseconds = 0.0;
    double p95Milliseconds = 0.0;
    double p99Milliseconds = 0.0;
    int framesOver16_7Milliseconds = 0;
    int framesOver25Milliseconds = 0;
    int framesOver33_3Milliseconds = 0;

    QJsonObject toJson() const;
};

struct ProcessMemorySample {
    qint64 elapsedMilliseconds = 0;
    qint64 workingSetBytes = 0;
    qint64 privateBytes = 0;
};

struct ProcessMemorySummary {
    int sampleCount = 0;
    qint64 initialWorkingSetBytes = 0;
    qint64 finalWorkingSetBytes = 0;
    qint64 peakWorkingSetBytes = 0;
    qint64 workingSetGrowthBytes = 0;
    qint64 initialPrivateBytes = 0;
    qint64 finalPrivateBytes = 0;
    qint64 peakPrivateBytes = 0;
    qint64 privateGrowthBytes = 0;
    double workingSetSlopeBytesPerMinute = 0.0;
    double privateSlopeBytesPerMinute = 0.0;

    QJsonObject toJson() const;
};

double timingPercentileMilliseconds(const QVector<qint64> &nanoseconds,
                                    double fraction);
int countTimingSamplesOver(const QVector<qint64> &nanoseconds,
                           double milliseconds);
FrameTimingSummary summarizeFrameTimings(const QVector<qint64> &nanoseconds);
ProcessMemorySummary summarizeProcessMemory(
    const QVector<ProcessMemorySample> &samples);

} // namespace pp
