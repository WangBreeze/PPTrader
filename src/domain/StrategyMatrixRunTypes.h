#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace pp {

struct StrategyMatrixRunRecord {
    QString runId;
    QDateTime createdAt;
    QDateTime completedAt;
    QString requestJson;
    QString status;
    QString error;
    int totalTasks = 0;
};

struct StrategyMatrixTaskRecord {
    QString runId;
    QString taskId;
    QString strategyId;
    QString profileLabel;
    int strategyVersion = 0;
    QString parameterFingerprint;
    QString symbol;
    QString datasetVersion;
    QString dataVersion;
    QString status;
    QString error;
    QString resultId;
    int actualBars = 0;
    qint64 elapsedMilliseconds = 0;
    double totalReturnPercent = 0.0;
    double maxDrawdownPercent = 0.0;
    int totalTrades = 0;
    double totalCommission = 0.0;
    double finalEquity = 0.0;
};

} // namespace pp
