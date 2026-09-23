#pragma once

#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QVariantList>

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
    QString payloadSha256;
    QVariantList providerTailRows;
    QString httpDate;
    QString httpAge;
    int responseRowCount = 0;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::RealtimeMinuteSeriesResult)
