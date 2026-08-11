#pragma once

#include "domain/MarketTypes.h"

#include <QString>
#include <QVector>

namespace pp {

QString computeBacktestDataVersion(const QVector<Candle> &candles,
                                   const QString &symbol,
                                   int periodSeconds);

} // namespace pp
