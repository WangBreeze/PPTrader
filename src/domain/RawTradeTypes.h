#pragma once

#include "domain/MarketTypes.h"

#include <QDate>
#include <QDateTime>
#include <QVector>

namespace pp {

enum class RawTradeDirection {
    Buy,
    Sell,
    Neutral
};

struct RawTradeTick {
    Instrument instrument;
    QDateTime timestamp;
    QDate tradingDate;
    qint64 sourceLine = 0;
    double price = 0.0;
    double volumeShares = 0.0;
    double amount = 0.0;
    RawTradeDirection direction = RawTradeDirection::Neutral;
    QString source = QStringLiteral("tickdata_csv_tick");
};

struct RawTradeFingerprint {
    qint64 count = 0;
    qint64 maximumId = 0;
};

// 盘中逐笔增量批次的落库与局部缓存刷新结果。sourceLine 在实时接入时
// 表示行情源提供的稳定成交序号，而不是接收端临时生成的行号。
struct RawTradeIncrementalAppendResult {
    bool factCommitted = false;
    qint64 receivedCount = 0;
    qint64 insertedCount = 0;
    qint64 duplicateCount = 0;
    Instrument instrument;
    QDate tradingDate;
    QString source;
    QVector<int> invalidatedPeriods;
    QVector<int> refreshedPeriods;
    QString error;
    QString cacheRefreshError;
};

} // namespace pp
