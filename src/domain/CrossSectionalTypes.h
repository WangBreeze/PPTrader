#pragma once

#include "domain/MarketTypes.h"

#include <QVector>

namespace pp {

struct CrossSectionalQuote {
    Instrument instrument;
    QDateTime timestamp;
    double price = 0.0;
    double previousClose = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double volume = 0.0;
    double amount = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    bool suspended = false;
    bool stale = false;
};

struct MarketSnapshot {
    QString batchId;
    Market market = Market::AShare;
    QDateTime timestamp;
    QDateTime completedAt;
    qint64 collectionDurationMilliseconds = -1;
    int expectedCount = 0;
    QVector<CrossSectionalQuote> quotes;

    double completenessRatio() const
    {
        return expectedCount > 0
            ? static_cast<double>(quotes.size()) / static_cast<double>(expectedCount)
            : 0.0;
    }

    qint64 durationMilliseconds() const
    {
        if (collectionDurationMilliseconds >= 0)
            return collectionDurationMilliseconds;
        return timestamp.isValid() && completedAt.isValid()
            ? timestamp.msecsTo(completedAt) : 0;
    }
};

struct FactorValue {
    Instrument instrument;
    double sourcePrice = 0.0;
    double sourcePreviousClose = 0.0;
    double rawValue = 0.0;
    double zScore = 0.0;
    double percentile = 0.0;
    int rank = 0;
    bool valid = false;
    QString invalidReason;
};

struct FactorFrame {
    QString indicatorId;
    QString indicatorName;
    Market market = Market::AShare;
    QDateTime timestamp;
    QString batchId;
    QVector<FactorValue> values;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::CrossSectionalQuote)
Q_DECLARE_METATYPE(pp::MarketSnapshot)
Q_DECLARE_METATYPE(pp::FactorValue)
Q_DECLARE_METATYPE(pp::FactorFrame)
