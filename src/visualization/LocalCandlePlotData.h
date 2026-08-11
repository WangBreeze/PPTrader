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
};

class LocalCandlePlotData final {
public:
    static LocalCandlePlotRange priceRange(const QVector<LocalCandleSeriesPoint> &points);
    static LocalCandlePlotRange priceRange(const QVector<LocalCandleSeriesPoint> &points,
                                           const LocalCandleViewport &viewport);
    static LocalCandleViewport normalizeViewport(int totalCount, int start, int count);
    static LocalCandleViewport zoomViewport(int totalCount,
                                            const LocalCandleViewport &viewport,
                                            qreal anchorRatio, qreal factor,
                                            int minimumCount = 10);
    static LocalCandleViewport panViewport(int totalCount,
                                           const LocalCandleViewport &viewport,
                                           int barDelta);
    static qreal xFor(int index, int count, qreal width);
    static qreal yFor(double price, const LocalCandlePlotRange &range, qreal height);
    static qreal bodyWidth(int count, qreal width);
    static int nearestIndex(int count, qreal width, qreal x);
    static int wickVertexCount(int count);
    static int bodyVertexCount(int count);
};

} // namespace pp
