#include "visualization/LocalCandlePlotData.h"

#include <algorithm>
#include <cmath>

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

} // namespace pp
