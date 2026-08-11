#include "visualization/CrossSectionPlotData.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

int CrossSectionPlotData::boardBit(const QString &board)
{
    if (board == QStringLiteral("主板")) return MainBoard;
    if (board == QStringLiteral("深市主板")) return ShenzhenMain;
    if (board == QStringLiteral("创业板")) return GrowthBoard;
    if (board == QStringLiteral("科创板")) return StarMarket;
    if (board == QStringLiteral("北交所")) return Bse;
    return 0;
}

bool CrossSectionPlotData::isVisible(const CrossSectionPlotPoint &point, int boardMask)
{
    return point.valid && std::isfinite(point.price) && point.price > 0.0
        && point.boardBit != 0 && (boardMask & point.boardBit) != 0;
}

qreal CrossSectionPlotData::xFor(const CrossSectionPlotPoint &point, int totalCount,
                                 qreal width)
{
    if (totalCount <= 1) return width / 2.0;
    const int index = std::clamp(point.displayIndex - 1, 0, totalCount - 1);
    return static_cast<qreal>(index) / static_cast<qreal>(totalCount - 1) * width;
}

qreal CrossSectionPlotData::yFor(double price, bool logarithmic, double maximum,
                                 qreal height)
{
    if (height <= 0.0) return 0.0;
    if (logarithmic) {
        const double value = std::clamp(price, 1.0, 2000.0);
        return height - std::log(value) / std::log(2000.0) * height;
    }
    const double upperBound = std::max(1.0, maximum);
    const double value = std::clamp(price, 0.0, upperBound);
    return height - value / upperBound * height;
}

CrossSectionPlotSummary CrossSectionPlotData::summarize(
    const QVector<CrossSectionPlotPoint> &points, int boardMask)
{
    CrossSectionPlotSummary result;
    QVector<double> prices;
    prices.reserve(points.size());
    QVector<int> ranked;
    ranked.reserve(points.size());
    double sum = 0.0;

    for (int index = 0; index < points.size(); ++index) {
        const CrossSectionPlotPoint &point = points.at(index);
        switch (point.boardBit) {
        case MainBoard: ++result.mainBoardCount; break;
        case ShenzhenMain: ++result.shenzhenMainCount; break;
        case GrowthBoard: ++result.growthBoardCount; break;
        case StarMarket: ++result.starMarketCount; break;
        case Bse: ++result.bseCount; break;
        default: ++result.unknownCount; break;
        }

        if (isVisible(point, boardMask)) {
            prices.append(point.price);
            ranked.append(index);
            sum += point.price;
        } else if (point.boardBit != 0 && (point.boardBit & boardMask) != 0) {
            ++result.invalidCount;
        }
    }

    std::sort(prices.begin(), prices.end());
    std::sort(ranked.begin(), ranked.end(), [&points](int left, int right) {
        const CrossSectionPlotPoint &leftPoint = points.at(left);
        const CrossSectionPlotPoint &rightPoint = points.at(right);
        if (leftPoint.price != rightPoint.price) return leftPoint.price > rightPoint.price;
        return leftPoint.symbol < rightPoint.symbol;
    });

    result.validCount = prices.size();
    if (!prices.isEmpty()) {
        result.minimum = prices.constFirst();
        result.maximum = prices.constLast();
        result.average = sum / prices.size();
        const int middle = prices.size() / 2;
        result.median = prices.size() % 2 != 0
            ? prices.at(middle)
            : (prices.at(middle - 1) + prices.at(middle)) / 2.0;
    }
    result.topIndices = ranked.mid(0, 3);
    return result;
}

int CrossSectionPlotData::nearestPoint(const QVector<CrossSectionPlotPoint> &points,
                                       int boardMask, bool logarithmic,
                                       double maximum, qreal width, qreal height,
                                       const QPointF &position, qreal radius)
{
    if (points.isEmpty() || width <= 0.0 || height <= 0.0 || radius <= 0.0) return -1;

    const int pointCount = static_cast<int>(points.size());
    const int approximate = std::clamp(
        qRound(position.x() / width * (pointCount - 1)), 0, pointCount - 1);
    const qreal spacing = width / std::max(1, pointCount - 1);
    const int span = std::max(8, qCeil(radius / std::max<qreal>(0.1, spacing)) + 2);
    const qreal radiusSquared = radius * radius;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    int bestIndex = -1;

    for (int index = std::max(0, approximate - span);
         index <= std::min(pointCount - 1, approximate + span); ++index) {
        const CrossSectionPlotPoint &point = points.at(index);
        if (!isVisible(point, boardMask)) continue;
        const qreal dx = xFor(point, points.size(), width) - position.x();
        const qreal dy = yFor(point.price, logarithmic, maximum, height) - position.y();
        const qreal distance = dx * dx + dy * dy;
        if (distance <= radiusSquared && distance < bestDistance) {
            bestDistance = distance;
            bestIndex = index;
        }
    }
    return bestIndex;
}

} // namespace pp
