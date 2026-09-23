#include "visualization/FrameTimingStatistics.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace pp {

namespace {

double toMilliseconds(qint64 nanoseconds)
{
    return double(nanoseconds) / 1'000'000.0;
}

double memorySlopeBytesPerMinute(
    const QVector<ProcessMemorySample> &samples,
    const std::function<qint64(const ProcessMemorySample &)> &value)
{
    if (samples.size() < 2) return 0.0;
    long double xMean = 0.0;
    long double yMean = 0.0;
    for (const ProcessMemorySample &sample : samples) {
        xMean += sample.elapsedMilliseconds / 60000.0L;
        yMean += value(sample);
    }
    xMean /= samples.size();
    yMean /= samples.size();
    long double numerator = 0.0;
    long double denominator = 0.0;
    for (const ProcessMemorySample &sample : samples) {
        const long double x = sample.elapsedMilliseconds / 60000.0L - xMean;
        const long double y = value(sample) - yMean;
        numerator += x * y;
        denominator += x * x;
    }
    return denominator > 0.0 ? double(numerator / denominator) : 0.0;
}

} // namespace

QJsonObject FrameTimingSummary::toJson() const
{
    return {
        {QStringLiteral("sampleCount"), sampleCount},
        {QStringLiteral("minimumMs"), minimumMilliseconds},
        {QStringLiteral("maximumMs"), maximumMilliseconds},
        {QStringLiteral("meanMs"), meanMilliseconds},
        {QStringLiteral("p50Ms"), p50Milliseconds},
        {QStringLiteral("p95Ms"), p95Milliseconds},
        {QStringLiteral("p99Ms"), p99Milliseconds},
        {QStringLiteral("framesOver16_7Ms"), framesOver16_7Milliseconds},
        {QStringLiteral("framesOver25Ms"), framesOver25Milliseconds},
        {QStringLiteral("framesOver33_3Ms"), framesOver33_3Milliseconds}
    };
}

QJsonObject ProcessMemorySummary::toJson() const
{
    return {
        {QStringLiteral("sampleCount"), sampleCount},
        {QStringLiteral("initialWorkingSetBytes"), initialWorkingSetBytes},
        {QStringLiteral("finalWorkingSetBytes"), finalWorkingSetBytes},
        {QStringLiteral("peakWorkingSetBytes"), peakWorkingSetBytes},
        {QStringLiteral("workingSetGrowthBytes"), workingSetGrowthBytes},
        {QStringLiteral("initialPrivateBytes"), initialPrivateBytes},
        {QStringLiteral("finalPrivateBytes"), finalPrivateBytes},
        {QStringLiteral("peakPrivateBytes"), peakPrivateBytes},
        {QStringLiteral("privateGrowthBytes"), privateGrowthBytes},
        {QStringLiteral("workingSetSlopeBytesPerMinute"),
         workingSetSlopeBytesPerMinute},
        {QStringLiteral("privateSlopeBytesPerMinute"),
         privateSlopeBytesPerMinute}
    };
}

double timingPercentileMilliseconds(const QVector<qint64> &nanoseconds,
                                    double fraction)
{
    if (nanoseconds.isEmpty()) return 0.0;
    QVector<qint64> sorted = nanoseconds;
    std::sort(sorted.begin(), sorted.end());
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const int index = std::clamp(
        int(std::ceil(clamped * double(sorted.size()))) - 1,
        0, int(sorted.size()) - 1);
    return toMilliseconds(sorted.at(index));
}

int countTimingSamplesOver(const QVector<qint64> &nanoseconds,
                           double milliseconds)
{
    const qint64 threshold = qint64(std::llround(milliseconds * 1'000'000.0));
    return int(std::count_if(nanoseconds.cbegin(), nanoseconds.cend(),
                             [threshold](qint64 value) {
        return value > threshold;
    }));
}

FrameTimingSummary summarizeFrameTimings(const QVector<qint64> &nanoseconds)
{
    FrameTimingSummary summary;
    summary.sampleCount = int(nanoseconds.size());
    if (nanoseconds.isEmpty()) return summary;
    const auto [minimum, maximum] = std::minmax_element(
        nanoseconds.cbegin(), nanoseconds.cend());
    const long double total = std::accumulate(
        nanoseconds.cbegin(), nanoseconds.cend(), static_cast<long double>(0));
    summary.minimumMilliseconds = toMilliseconds(*minimum);
    summary.maximumMilliseconds = toMilliseconds(*maximum);
    summary.meanMilliseconds = double(total / nanoseconds.size()) / 1'000'000.0;
    summary.p50Milliseconds = timingPercentileMilliseconds(nanoseconds, 0.50);
    summary.p95Milliseconds = timingPercentileMilliseconds(nanoseconds, 0.95);
    summary.p99Milliseconds = timingPercentileMilliseconds(nanoseconds, 0.99);
    summary.framesOver16_7Milliseconds = countTimingSamplesOver(nanoseconds, 16.7);
    summary.framesOver25Milliseconds = countTimingSamplesOver(nanoseconds, 25.0);
    summary.framesOver33_3Milliseconds = countTimingSamplesOver(nanoseconds, 33.3);
    return summary;
}

ProcessMemorySummary summarizeProcessMemory(
    const QVector<ProcessMemorySample> &samples)
{
    ProcessMemorySummary summary;
    summary.sampleCount = int(samples.size());
    if (samples.isEmpty()) return summary;
    summary.initialWorkingSetBytes = samples.first().workingSetBytes;
    summary.finalWorkingSetBytes = samples.last().workingSetBytes;
    summary.initialPrivateBytes = samples.first().privateBytes;
    summary.finalPrivateBytes = samples.last().privateBytes;
    for (const ProcessMemorySample &sample : samples) {
        summary.peakWorkingSetBytes = std::max(
            summary.peakWorkingSetBytes, sample.workingSetBytes);
        summary.peakPrivateBytes = std::max(
            summary.peakPrivateBytes, sample.privateBytes);
    }
    summary.workingSetGrowthBytes = summary.finalWorkingSetBytes
        - summary.initialWorkingSetBytes;
    summary.privateGrowthBytes = summary.finalPrivateBytes
        - summary.initialPrivateBytes;
    summary.workingSetSlopeBytesPerMinute = memorySlopeBytesPerMinute(
        samples, [](const ProcessMemorySample &sample) {
            return sample.workingSetBytes;
        });
    summary.privateSlopeBytesPerMinute = memorySlopeBytesPerMinute(
        samples, [](const ProcessMemorySample &sample) {
            return sample.privateBytes;
        });
    return summary;
}

} // namespace pp
