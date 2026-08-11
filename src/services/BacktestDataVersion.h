#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"

#include <QString>
#include <QVector>

namespace pp {

QString computeBacktestDataVersion(const QVector<Candle> &candles,
                                   const QString &symbol,
                                   int periodSeconds);

// 数据集身份哈希：symbol + periodSeconds + source + aggregatorVersion，再按交易日升序
// 逐日追加 date/count/maximumId。派生缓存是否新鲜不参与哈希。
QString computeBacktestDatasetVersion(const BacktestDatasetIdentity &identity);

} // namespace pp
