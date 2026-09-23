#include "visualization/LocalCandlePlotData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

LocalCandlePlotRange LocalCandlePlotData::priceRange(
    const QVector<LocalCandleSeriesPoint> &points)
{
    return priceRange(points, {0, int(points.size())});
}

LocalCandlePlotRange LocalCandlePlotData::priceRange(
    const QVector<LocalCandleSeriesPoint> &points, const LocalCandleViewport &viewport)
{
    LocalCandlePlotRange range;
    const LocalCandleViewport normalized = normalizeViewport(
        points.size(), viewport.start, viewport.count);
    for (int index = normalized.start;
         index < normalized.start + normalized.count; ++index) {
        const LocalCandleSeriesPoint &point = points.at(index);
        if (!std::isfinite(point.candle.low) || !std::isfinite(point.candle.high)
            || point.candle.low <= 0.0 || point.candle.high < point.candle.low) continue;
        if (!range.valid) {
            range.minimum = point.candle.low;
            range.maximum = point.candle.high;
            range.valid = true;
        } else {
            range.minimum = std::min(range.minimum, point.candle.low);
            range.maximum = std::max(range.maximum, point.candle.high);
        }
    }
    if (range.valid && range.maximum <= range.minimum) {
        const double padding = std::max(0.01, range.maximum * 0.005);
        range.minimum -= padding;
        range.maximum += padding;
    }
    return range;
}

LocalCandlePlotRange LocalCandlePlotData::scaledPriceRange(
    const LocalCandlePlotRange &automaticRange, qreal spanFactor)
{
    if (!automaticRange.valid || !std::isfinite(spanFactor)
        || spanFactor <= 0.0) return automaticRange;
    const double factor = std::clamp(double(spanFactor), 0.05, 8.0);
    const double center = (automaticRange.minimum + automaticRange.maximum) * 0.5;
    const double halfSpan = (automaticRange.maximum - automaticRange.minimum)
        * 0.5 * factor;
    LocalCandlePlotRange result {center - halfSpan, center + halfSpan, true};
    if (result.minimum <= 0.0) {
        result.maximum += 0.0001 - result.minimum;
        result.minimum = 0.0001;
    }
    return result;
}

QVector<double> LocalCandlePlotData::simpleMovingAverage(
    const QVector<LocalCandleSeriesPoint> &points, int period)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    QVector<double> result(points.size(), missing);
    if (period <= 0 || points.isEmpty()) return result;

    double sum = 0.0;
    int invalidCount = 0;
    for (int index = 0; index < points.size(); ++index) {
        const double close = points.at(index).candle.close;
        if (std::isfinite(close)) sum += close;
        else ++invalidCount;

        if (index >= period) {
            const double removed = points.at(index - period).candle.close;
            if (std::isfinite(removed)) sum -= removed;
            else --invalidCount;
        }
        if (index + 1 >= period && invalidCount == 0)
            result[index] = sum / double(period);
    }
    return result;
}

LocalCandleBollingerBands LocalCandlePlotData::bollingerBands(
    const QVector<LocalCandleSeriesPoint> &points, int period,
    double standardDeviations)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    LocalCandleBollingerBands result;
    result.upper.fill(missing, points.size());
    result.middle.fill(missing, points.size());
    result.lower.fill(missing, points.size());
    if (period <= 0 || points.isEmpty() || !std::isfinite(standardDeviations)
        || standardDeviations < 0.0) return result;

    double sum = 0.0;
    double squaredSum = 0.0;
    int invalidCount = 0;
    for (int index = 0; index < points.size(); ++index) {
        const double close = points.at(index).candle.close;
        if (std::isfinite(close)) {
            sum += close;
            squaredSum += close * close;
        } else {
            ++invalidCount;
        }
        if (index >= period) {
            const double removed = points.at(index - period).candle.close;
            if (std::isfinite(removed)) {
                sum -= removed;
                squaredSum -= removed * removed;
            } else {
                --invalidCount;
            }
        }
        if (index + 1 < period || invalidCount != 0) continue;
        const double mean = sum / double(period);
        const double variance = std::max(0.0, squaredSum / double(period) - mean * mean);
        const double offset = standardDeviations * std::sqrt(variance);
        result.middle[index] = mean;
        result.upper[index] = mean + offset;
        result.lower[index] = mean - offset;
    }
    return result;
}

QVector<double> LocalCandlePlotData::relativeStrengthIndex(
    const QVector<LocalCandleSeriesPoint> &points, int period)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    QVector<double> result(points.size(), missing);
    if (period <= 1 || points.size() <= period) return result;

    double gainSum = 0.0;
    double lossSum = 0.0;
    for (int index = 1; index <= period; ++index) {
        const double previous = points.at(index - 1).candle.close;
        const double current = points.at(index).candle.close;
        if (!std::isfinite(previous) || !std::isfinite(current)) return result;
        const double change = current - previous;
        gainSum += std::max(0.0, change);
        lossSum += std::max(0.0, -change);
    }

    double averageGain = gainSum / double(period);
    double averageLoss = lossSum / double(period);
    const auto rsiValue = [](double gain, double loss) {
        if (gain <= 0.0 && loss <= 0.0) return 50.0;
        if (loss <= 0.0) return 100.0;
        if (gain <= 0.0) return 0.0;
        const double relativeStrength = gain / loss;
        return 100.0 - 100.0 / (1.0 + relativeStrength);
    };
    result[period] = rsiValue(averageGain, averageLoss);

    for (int index = period + 1; index < points.size(); ++index) {
        const double previous = points.at(index - 1).candle.close;
        const double current = points.at(index).candle.close;
        if (!std::isfinite(previous) || !std::isfinite(current)) {
            averageGain = 0.0;
            averageLoss = 0.0;
            continue;
        }
        const double change = current - previous;
        averageGain = (averageGain * double(period - 1)
                       + std::max(0.0, change)) / double(period);
        averageLoss = (averageLoss * double(period - 1)
                       + std::max(0.0, -change)) / double(period);
        result[index] = rsiValue(averageGain, averageLoss);
    }
    return result;
}

LocalCandleMacdValues LocalCandlePlotData::macd(
    const QVector<LocalCandleSeriesPoint> &points, int fastPeriod,
    int slowPeriod, int signalPeriod)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    LocalCandleMacdValues result;
    result.line.fill(missing, points.size());
    result.signal.fill(missing, points.size());
    result.histogram.fill(missing, points.size());
    if (fastPeriod <= 0 || slowPeriod <= fastPeriod || signalPeriod <= 0
        || points.isEmpty()) return result;

    const auto closeEma = [&points, missing](int period) {
        QVector<double> values(points.size(), missing);
        if (period <= 0 || points.size() < period) return values;
        double seed = 0.0;
        for (int index = 0; index < period; ++index) {
            const double close = points.at(index).candle.close;
            if (!std::isfinite(close)) return values;
            seed += close;
        }
        values[period - 1] = seed / double(period);
        const double multiplier = 2.0 / double(period + 1);
        for (int index = period; index < points.size(); ++index) {
            const double close = points.at(index).candle.close;
            if (!std::isfinite(close)) continue;
            values[index] = (close - values.at(index - 1)) * multiplier
                + values.at(index - 1);
        }
        return values;
    };

    const QVector<double> fast = closeEma(fastPeriod);
    const QVector<double> slow = closeEma(slowPeriod);
    for (int index = slowPeriod - 1; index < points.size(); ++index) {
        if (std::isfinite(fast.at(index)) && std::isfinite(slow.at(index)))
            result.line[index] = fast.at(index) - slow.at(index);
    }

    const int firstSignalIndex = slowPeriod - 1;
    if (points.size() < firstSignalIndex + signalPeriod) return result;
    double signalSeed = 0.0;
    for (int index = firstSignalIndex;
         index < firstSignalIndex + signalPeriod; ++index) {
        if (!std::isfinite(result.line.at(index))) return result;
        signalSeed += result.line.at(index);
    }
    const int seedIndex = firstSignalIndex + signalPeriod - 1;
    result.signal[seedIndex] = signalSeed / double(signalPeriod);
    result.histogram[seedIndex] = result.line.at(seedIndex)
        - result.signal.at(seedIndex);
    const double multiplier = 2.0 / double(signalPeriod + 1);
    for (int index = seedIndex + 1; index < points.size(); ++index) {
        if (!std::isfinite(result.line.at(index))) continue;
        result.signal[index] = (result.line.at(index) - result.signal.at(index - 1))
            * multiplier + result.signal.at(index - 1);
        result.histogram[index] = result.line.at(index) - result.signal.at(index);
    }
    return result;
}

LocalCandlePlotRange LocalCandlePlotData::valueRange(
    const QVector<QVector<double>> &series,
    const LocalCandleViewport &viewport, bool includeZero)
{
    LocalCandlePlotRange range;
    if (includeZero) range = {0.0, 0.0, true};
    for (const QVector<double> &values : series) {
        const int start = std::clamp(viewport.start, 0, int(values.size()));
        const int end = std::clamp(viewport.start + viewport.count,
                                   start, int(values.size()));
        for (int index = start; index < end; ++index) {
            const double value = values.at(index);
            if (!std::isfinite(value)) continue;
            if (!range.valid) range = {value, value, true};
            else {
                range.minimum = std::min(range.minimum, value);
                range.maximum = std::max(range.maximum, value);
            }
        }
    }
    if (range.valid && range.maximum <= range.minimum) {
        const double padding = std::max(0.0001, std::abs(range.maximum) * 0.05);
        range.minimum -= padding;
        range.maximum += padding;
    }
    return range;
}

QVector<double> LocalCandlePlotData::nicePriceTicks(
    const LocalCandlePlotRange &range, int targetCount)
{
    QVector<double> ticks;
    if (!range.valid || !std::isfinite(range.minimum)
        || !std::isfinite(range.maximum) || range.maximum <= range.minimum
        || targetCount < 2) return ticks;

    const double roughStep = (range.maximum - range.minimum)
        / double(targetCount - 1);
    const double magnitude = std::pow(10.0, std::floor(std::log10(roughStep)));
    const double fraction = roughStep / magnitude;
    const double niceFraction = fraction < 1.5 ? 1.0
        : fraction < 3.0 ? 2.0 : fraction < 7.0 ? 5.0 : 10.0;
    const double step = niceFraction * magnitude;
    const double epsilon = step * 1e-9;
    const double first = std::ceil((range.minimum - epsilon) / step) * step;
    const double last = std::floor((range.maximum + epsilon) / step) * step;
    for (double value = first; value <= last + epsilon; value += step) {
        const double normalized = std::abs(value) < epsilon ? 0.0 : value;
        ticks.append(normalized);
        if (ticks.size() > 32) break;
    }
    return ticks;
}

QVector<int> LocalCandlePlotData::timeTickIndices(
    const LocalCandleViewport &viewport, int targetCount)
{
    QVector<int> indices;
    if (viewport.count <= 0 || targetCount <= 0) return indices;
    const int count = std::min(viewport.count, targetCount);
    if (count == 1) return {viewport.start};
    indices.reserve(count);
    for (int tick = 0; tick < count; ++tick) {
        const int offset = int(std::round(
            double(tick) * double(viewport.count - 1) / double(count - 1)));
        const int index = viewport.start + offset;
        if (indices.isEmpty() || indices.last() != index) indices.append(index);
    }
    return indices;
}

LocalCandleViewport LocalCandlePlotData::normalizeViewport(
    int totalCount, int start, int count)
{
    if (totalCount <= 0) return {};
    const int normalizedCount = std::clamp(count <= 0 ? totalCount : count, 1, totalCount);
    const int normalizedStart = std::clamp(start, 0, totalCount - normalizedCount);
    return {normalizedStart, normalizedCount};
}

LocalCandleViewport LocalCandlePlotData::zoomViewport(
    int totalCount, const LocalCandleViewport &viewport, qreal anchorRatio,
    qreal factor, int minimumCount)
{
    const LocalCandleViewport current = normalizeViewport(
        totalCount, viewport.start, viewport.count);
    if (current.count <= 0 || !std::isfinite(factor) || factor <= 0.0) return current;
    const int minimum = std::clamp(minimumCount, 1, totalCount);
    const int nextCount = std::clamp(
        int(std::round(qreal(current.count) / factor)), minimum, totalCount);
    const qreal anchor = std::clamp(anchorRatio, qreal(0.0), qreal(1.0));
    const qreal anchoredIndex = qreal(current.start) + anchor * qreal(current.count - 1);
    const int nextStart = int(std::round(anchoredIndex - anchor * qreal(nextCount - 1)));
    return normalizeViewport(totalCount, nextStart, nextCount);
}

LocalCandleViewport LocalCandlePlotData::panViewport(
    int totalCount, const LocalCandleViewport &viewport, int barDelta)
{
    const LocalCandleViewport current = normalizeViewport(
        totalCount, viewport.start, viewport.count);
    return normalizeViewport(totalCount, current.start + barDelta, current.count);
}

LocalCandleViewport LocalCandlePlotData::viewportAfterDataChange(
    int totalCount, int previousTotalCount,
    const LocalCandleViewport &previousViewport, bool wasAtRightEdge)
{
    if (totalCount <= 0) return {};
    if (previousTotalCount <= 0 || previousViewport.count <= 0)
        return normalizeViewport(totalCount, 0, totalCount);
    const LocalCandleViewport previous = normalizeViewport(
        previousTotalCount, previousViewport.start, previousViewport.count);
    const bool showedEntirePreviousSeries = previous.start == 0
        && previous.count == previousTotalCount;
    if (showedEntirePreviousSeries)
        return normalizeViewport(totalCount, 0, totalCount);
    const int count = std::min(previous.count, totalCount);
    if (wasAtRightEdge)
        return normalizeViewport(totalCount, totalCount - count, count);
    return normalizeViewport(totalCount, previous.start, count);
}

LocalCandleViewport LocalCandlePlotData::viewportAfterPrepend(
    int totalCount, const LocalCandleViewport &previousViewport,
    int insertedCount)
{
    if (totalCount <= 0) return {};
    if (previousViewport.count <= 0 || insertedCount <= 0)
        return normalizeViewport(totalCount, previousViewport.start,
                                 previousViewport.count);
    return normalizeViewport(totalCount, previousViewport.start + insertedCount,
                             previousViewport.count);
}

int LocalCandlePlotData::renderBucketCapacity(qreal width, qreal samplesPerPixel)
{
    if (!std::isfinite(width) || width <= 0.0
        || !std::isfinite(samplesPerPixel) || samplesPerPixel <= 0.0) {
        return 0;
    }
    return std::max(1, int(std::ceil(width * samplesPerPixel)));
}

QVector<LocalCandleRenderBucket> LocalCandlePlotData::renderBuckets(
    const QVector<LocalCandleSeriesPoint> &points,
    const LocalCandleViewport &viewport, qreal width, qreal samplesPerPixel)
{
    QVector<LocalCandleRenderBucket> buckets;
    const LocalCandleViewport normalized = normalizeViewport(
        points.size(), viewport.start, viewport.count);
    const int capacity = renderBucketCapacity(width, samplesPerPixel);
    if (normalized.count <= 0 || capacity <= 0) return buckets;
    const int bucketCount = std::min(normalized.count, capacity);
    buckets.reserve(bucketCount);
    for (int bucketIndex = 0; bucketIndex < bucketCount; ++bucketIndex) {
        const int first = normalized.start + int(
            qint64(bucketIndex) * normalized.count / bucketCount);
        const int end = normalized.start + int(
            qint64(bucketIndex + 1) * normalized.count / bucketCount);
        const int last = std::max(first, end - 1);
        LocalCandleRenderBucket bucket;
        bucket.firstIndex = first;
        bucket.lastIndex = last;
        bucket.open = points.at(first).candle.open;
        bucket.close = points.at(last).candle.close;
        bucket.high = -std::numeric_limits<double>::infinity();
        bucket.low = std::numeric_limits<double>::infinity();
        for (int index = first; index <= last; ++index) {
            const Candle &candle = points.at(index).candle;
            if (std::isfinite(candle.high))
                bucket.high = std::max(bucket.high, candle.high);
            if (std::isfinite(candle.low))
                bucket.low = std::min(bucket.low, candle.low);
            if (std::isfinite(candle.volume))
                bucket.peakVolume = std::max(bucket.peakVolume, candle.volume);
        }
        if (!std::isfinite(bucket.high))
            bucket.high = std::max(bucket.open, bucket.close);
        if (!std::isfinite(bucket.low))
            bucket.low = std::min(bucket.open, bucket.close);
        buckets.append(bucket);
    }
    return buckets;
}

qreal LocalCandlePlotData::xFor(int index, int count, qreal width)
{
    if (count <= 0 || width <= 0.0) return 0.0;
    return (qreal(index) + 0.5) * width / qreal(count);
}

qreal LocalCandlePlotData::yFor(double price, const LocalCandlePlotRange &range,
                                qreal height)
{
    if (!range.valid || height <= 0.0) return height * 0.5;
    const double ratio = (price - range.minimum) / (range.maximum - range.minimum);
    return qreal((1.0 - std::clamp(ratio, 0.0, 1.0)) * height);
}

qreal LocalCandlePlotData::bodyWidth(int count, qreal width)
{
    if (count <= 0 || width <= 0.0) return 1.0;
    return std::clamp(width / qreal(count) * 0.66, qreal(1.0), qreal(10.0));
}

int LocalCandlePlotData::nearestIndex(int count, qreal width, qreal x)
{
    if (count <= 0 || width <= 0.0 || x < 0.0 || x > width) return -1;
    return std::clamp(int(std::floor(x / width * count)), 0, count - 1);
}

int LocalCandlePlotData::wickVertexCount(int count) { return std::max(0, count) * 2; }
int LocalCandlePlotData::bodyVertexCount(int count) { return std::max(0, count) * 6; }
int LocalCandlePlotData::signalMarkerVertexCount(int count)
{
    return std::max(0, count) * 3;
}

} // namespace pp
