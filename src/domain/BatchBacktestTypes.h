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

// 批量回测运行摘要与逐项明细；只保存代码、周期、策略、参数和状态，
// 不保存任何路径或凭据。恢复只重试 failed/cancelled 项。
struct BatchBacktestRunSummary {
    QString runId;
    QDateTime createdAt;
    QDateTime completedAt;
    QString strategyId;
    QVariantMap strategyParameters;
    QString frequency;
    int periodSeconds = 0;
    int barCount = 0;
    double initialCapital = 0.0;
    double commissionRate = 0.0;
    double slippageRate = 0.0;
    int lotSize = 100;
    int totalSymbols = 0;
    int completedSymbols = 0;
    int succeededSymbols = 0;
    int failedSymbols = 0;
    QString status;
    QString error;

    QVariantMap toVariantMap() const;
};

struct BatchBacktestRunItem {
    QString runId;
    Instrument instrument;
    QString status;
    QString error;
    QString dataVersion;
    QString datasetVersion;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BatchBacktestRequest)
Q_DECLARE_METATYPE(pp::BatchBacktestItemResult)
Q_DECLARE_METATYPE(pp::BatchBacktestRunSummary)
Q_DECLARE_METATYPE(pp::BatchBacktestRunItem)
