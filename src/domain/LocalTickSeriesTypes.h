#pragma once

#include "domain/RawTradeTypes.h"

#include <QMetaType>

namespace pp {

struct LocalTickSeriesQuery {
    Instrument instrument;
    QDate tradingDate;
    int maximumPoints = 100000;
};

struct LocalTickSeriesPoint {
    RawTradeTick tick;
    qint64 eventIndex = 0;
    qint64 representedCount = 1;
};

struct LocalTickSeriesResult {
    LocalTickSeriesQuery query;
    QVector<LocalTickSeriesPoint> points;
    QDate resolvedTradingDate;
    qint64 rawTickCount = 0;
    bool downsampled = false;
    bool succeeded = false;
    QString error;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::LocalTickSeriesResult)
