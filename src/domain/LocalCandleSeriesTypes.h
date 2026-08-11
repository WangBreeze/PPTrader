#pragma once

#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace pp {

struct LocalCandleSeriesQuery {
    Instrument instrument;
    int periodSeconds = 60;
    int page = 0;
    int pageSize = 1000;
    int maximumPoints = 800;
    bool latestTradingDayOnly = false;
};

struct LocalCandleSeriesPoint {
    Candle candle;
    QDateTime endTime;
    int sourceCount = 1;
};

struct LocalCandleSeriesResult {
    LocalCandleSeriesQuery query;
    QVector<LocalCandleSeriesPoint> points;
    bool succeeded = false;
    bool downsampled = false;
    int totalBars = 0;
    int pageCount = 0;
    int rawBarCount = 0;
    int bucketSize = 1;
    int regeneratedCacheDays = 0;
    bool rawTickBacked = false;
    QDate resolvedTradingDate;
    QString error;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::LocalCandleSeriesResult)
