#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"

#include <QString>
#include <QVector>

namespace pp {

QString computeBacktestDataVersion(const QVector<Candle> &candles,
                                   const QString &symbol,
                                   int periodSeconds);

// v2 数据集身份哈希纳入逐日规范化 Tick 内容 SHA-256。旧归档没有内容摘要时继续使用
// v1 的 date/count/maximumId 算法，派生缓存是否新鲜不参与哈希。
QString computeBacktestDatasetVersion(const BacktestDatasetIdentity &identity);

} // namespace pp
