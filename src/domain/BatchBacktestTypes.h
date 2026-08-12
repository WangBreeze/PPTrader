#pragma once

#include "domain/BacktestTypes.h"
#include "domain/LocalHistoryTypes.h"

#include <QVariantMap>

namespace pp {

struct BatchBacktestRequest {
    quint64 requestId = 0;
    QVector<LocalHistoryDataset> datasets;
    QString strategyId;
    QVariantMap strategyParameters;
    int barCount = 0;
    double initialCapital = 0.0;
    double commissionRate = 0.0;
    double slippageRate = 0.0;
    int lotSize = 100;
    int maximumActiveEngines = 20;
};

struct BatchBacktestItemResult {
    quint64 requestId = 0;
    Instrument instrument;
    QString frequency;
    int requestedBars = 0;
    int actualBars = 0;
    bool succeeded = false;
    QString error;
    QString dataVersion;
    QString datasetVersion;
    int datasetDayCount = 0;
    int datasetRawTickCount = 0;
    int datasetGapDayCount = 0;
    int datasetStaleDayCount = 0;
    bool archived = false;
    QString archivedResultId;
    PerformanceSummary summary;
    qint64 elapsedMilliseconds = 0;

    QVariantMap toVariantMap() const;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BatchBacktestRequest)
Q_DECLARE_METATYPE(pp::BatchBacktestItemResult)
