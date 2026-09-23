#pragma once

#include "domain/LocalTickSeriesTypes.h"

namespace pp {

struct LocalTickPlotRange { double minimum = 0.0; double maximum = 0.0; bool valid = false; };
struct LocalTickViewport { int start = 0; int count = 0; };

class LocalTickPlotData final {
public:
    static LocalTickPlotRange priceRange(const QVector<LocalTickSeriesPoint> &points);
    static LocalTickPlotRange priceRange(const QVector<LocalTickSeriesPoint> &points,
                                         const LocalTickViewport &viewport);
    static LocalTickViewport normalizeViewport(int total, int start, int count);
    static LocalTickViewport zoomViewport(int total, const LocalTickViewport &viewport,
                                          qreal anchorRatio, qreal factor, int minimumCount = 20);
    static LocalTickViewport panViewport(int total, const LocalTickViewport &viewport, int delta);
    static qreal xFor(int index, int count, qreal width);
    static qreal yFor(double price, const LocalTickPlotRange &range, qreal height);
    static int nearestIndex(int count, qreal width, qreal x);
};

} // namespace pp
