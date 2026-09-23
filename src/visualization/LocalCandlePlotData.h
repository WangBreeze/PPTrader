#pragma once

#include "domain/LocalCandleSeriesTypes.h"

#include <QPointF>

namespace pp {

struct LocalCandlePlotRange {
    double minimum = 0.0;
    double maximum = 0.0;
    bool valid = false;
};

struct LocalCandleViewport {
    int start = 0;
    int count = 0;

    bool operator==(const LocalCandleViewport &) const = default;
};

struct LocalCandleBollingerBands {
    QVector<double> upper;
    QVector<double> middle;
    QVector<double> lower;
};

struct LocalCandleMacdValues {
    QVector<double> line;
    QVector<double> signal;
    QVector<double> histogram;
};

struct LocalCandleRenderBucket {
    int firstIndex = -1;
    int lastIndex = -1;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double peakVolume = 0.0;

    int sourceCount() const { return lastIndex - firstIndex + 1; }
};

class LocalCandlePlotData final {
public:
    static LocalCandlePlotRange priceRange(const QVector<LocalCandleSeriesPoint> &points);
    static LocalCandlePlotRange priceRange(const QVector<LocalCandleSeriesPoint> &points,
                                           const LocalCandleViewport &viewport);
    static LocalCandlePlotRange scaledPriceRange(
        const LocalCandlePlotRange &automaticRange, qreal spanFactor);
    static QVector<double> simpleMovingAverage(
        const QVector<LocalCandleSeriesPoint> &points, int period);
    static LocalCandleBollingerBands bollingerBands(
        const QVector<LocalCandleSeriesPoint> &points, int period,
        double standardDeviations);
    static QVector<double> relativeStrengthIndex(
        const QVector<LocalCandleSeriesPoint> &points, int period);
    static LocalCandleMacdValues macd(
        const QVector<LocalCandleSeriesPoint> &points, int fastPeriod,
        int slowPeriod, int signalPeriod);
    static LocalCandlePlotRange valueRange(
        const QVector<QVector<double>> &series,
        const LocalCandleViewport &viewport, bool includeZero = false);
    static QVector<double> nicePriceTicks(
        const LocalCandlePlotRange &range, int targetCount = 6);
    static QVector<int> timeTickIndices(
        const LocalCandleViewport &viewport, int targetCount = 6);
    static LocalCandleViewport normalizeViewport(int totalCount, int start, int count);
    static LocalCandleViewport zoomViewport(int totalCount,
                                            const LocalCandleViewport &viewport,
                                            qreal anchorRatio, qreal factor,
                                            int minimumCount = 10);
    static LocalCandleViewport panViewport(int totalCount,
                                           const LocalCandleViewport &viewport,
                                           int barDelta);
    static LocalCandleViewport viewportAfterDataChange(
        int totalCount, int previousTotalCount,
        const LocalCandleViewport &previousViewport, bool wasAtRightEdge);
    static LocalCandleViewport viewportAfterPrepend(
        int totalCount, const LocalCandleViewport &previousViewport,
        int insertedCount);
    static int renderBucketCapacity(qreal width, qreal samplesPerPixel = 2.0);
    static QVector<LocalCandleRenderBucket> renderBuckets(
        const QVector<LocalCandleSeriesPoint> &points,
        const LocalCandleViewport &viewport, qreal width,
        qreal samplesPerPixel = 2.0);
    static qreal xFor(int index, int count, qreal width);
    static qreal yFor(double price, const LocalCandlePlotRange &range, qreal height);
    static qreal bodyWidth(int count, qreal width);
    static int nearestIndex(int count, qreal width, qreal x);
    static int wickVertexCount(int count);
    static int bodyVertexCount(int count);
    static int signalMarkerVertexCount(int count);
};

} // namespace pp
