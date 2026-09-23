#include "visualization/LocalTickPlotData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

LocalTickPlotRange LocalTickPlotData::priceRange(const QVector<LocalTickSeriesPoint> &points)
{ return priceRange(points, {0, int(points.size())}); }
LocalTickPlotRange LocalTickPlotData::priceRange(
    const QVector<LocalTickSeriesPoint> &points, const LocalTickViewport &viewport)
{
    const auto normalized = normalizeViewport(points.size(), viewport.start, viewport.count);
    if (!normalized.count) return {};
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (int i = normalized.start; i < normalized.start + normalized.count; ++i) {
        low = std::min(low, points.at(i).tick.price);
        high = std::max(high, points.at(i).tick.price);
    }
    if (!std::isfinite(low) || !std::isfinite(high)) return {};
    if (high <= low) { const double pad = std::max(0.01, std::abs(low) * 0.001); low -= pad; high += pad; }
    return {low, high, true};
}
LocalTickViewport LocalTickPlotData::normalizeViewport(int total, int start, int count)
{
    if (total <= 0) return {};
    count = std::clamp(count, 1, total);
    start = std::clamp(start, 0, total - count);
    return {start, count};
}
LocalTickViewport LocalTickPlotData::zoomViewport(
    int total, const LocalTickViewport &viewport, qreal anchorRatio, qreal factor, int minimumCount)
{
    if (total <= 0 || factor <= 0.0) return {};
    const auto current = normalizeViewport(total, viewport.start, viewport.count);
    const int newCount = std::clamp(int(std::round(current.count / factor)),
                                    std::min(minimumCount, total), total);
    anchorRatio = std::clamp(anchorRatio, qreal(0), qreal(1));
    const qreal anchor = current.start + anchorRatio * qreal(current.count - 1);
    return normalizeViewport(total, int(std::round(anchor - anchorRatio * (newCount - 1))), newCount);
}
LocalTickViewport LocalTickPlotData::panViewport(
    int total, const LocalTickViewport &viewport, int delta)
{ const auto current = normalizeViewport(total, viewport.start, viewport.count); return normalizeViewport(total, current.start + delta, current.count); }
qreal LocalTickPlotData::xFor(int index, int count, qreal width)
{ return count <= 1 ? width * 0.5 : qreal(index) * width / qreal(count - 1); }
qreal LocalTickPlotData::yFor(double price, const LocalTickPlotRange &range, qreal height)
{ return !range.valid ? height * 0.5 : (range.maximum - price) * height / (range.maximum - range.minimum); }
int LocalTickPlotData::nearestIndex(int count, qreal width, qreal x)
{ if (count <= 0 || width <= 0) return -1; return std::clamp(int(std::round(std::clamp(x / width, qreal(0), qreal(1)) * (count - 1))), 0, count - 1); }

} // namespace pp
