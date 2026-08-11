#pragma once

#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace pp {

struct RealtimeMinuteSeriesResult {
    Instrument instrument;
    QVector<Candle> candles;
    QDateTime requestedAt;
    QDateTime fetchedAt;
    QDateTime lastBarTime;
    QString providerId;
    QString error;
    bool succeeded = false;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::RealtimeMinuteSeriesResult)
