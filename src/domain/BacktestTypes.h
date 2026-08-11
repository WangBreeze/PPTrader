#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"

#include <QVariantMap>
#include <QVector>

namespace pp {

struct BacktestConfig {
    QString taskId;
    QString strategyId = QStringLiteral("sma_crossover");
    int strategyVersion = 0;
    QVariantMap strategyParameters;
    QVariantMap ruleSnapshot;
    QString dataVersion;
    BacktestDatasetIdentity datasetIdentity;
    Instrument instrument;
    QString frequency = QStringLiteral("day");
    int periodSeconds = 86400;
    int requestedBars = 500;
    int actualBars = 0;
    QDateTime from;
    QDateTime to;
    double initialCapital = 1000000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
    int lotSize = 100;
};

struct BacktestPosition {
    double quantity = 0.0;
    double averagePrice = 0.0;
    double entryCommission = 0.0;
    QDateTime entryTime;
    QString entryReason;
};

struct BacktestTrade {
    Instrument instrument;
    QDateTime entryTime;
    QDateTime exitTime;
    double entryPrice = 0.0;
    double exitPrice = 0.0;
    double quantity = 0.0;
    double grossPnl = 0.0;
    double netPnl = 0.0;
    double commission = 0.0;
    double returnPercent = 0.0;
    QString entryReason;
    QString exitReason;
};

struct EquityPoint {
    QDateTime timestamp;
    double equity = 0.0;
    double cash = 0.0;
    double positionValue = 0.0;
    double drawdownPercent = 0.0;
};

struct PerformanceSummary {
    double initialCapital = 0.0;
    double finalEquity = 0.0;
    double totalReturnPercent = 0.0;
    double maxDrawdownPercent = 0.0;
    double winRatePercent = 0.0;
    double sharpeRatio = 0.0;
    double totalCommission = 0.0;
    int totalTrades = 0;
    int winningTrades = 0;
    int losingTrades = 0;
};

struct BacktestResult {
    BacktestConfig config;
    PerformanceSummary summary;
    QVector<BacktestTrade> trades;
    QVector<EquityPoint> equityCurve;
};

struct BacktestArchiveSummary {
    QString resultId;
    QString taskId;
    QDateTime completedAt;
    QString strategyId;
    int strategyVersion = 0;
    Instrument instrument;
    QString frequency;
    int requestedBars = 0;
    int actualBars = 0;
    QString dataVersion;
    QString datasetVersion;
    QDate datasetFirstDate;
    QDate datasetLastDate;
    int datasetDayCount = 0;
    int datasetRawTickCount = 0;
    int datasetDerivedBarCount = 0;
    int datasetGapDayCount = 0;
    int datasetStaleDayCount = 0;
    QString datasetCalendarSource;
    bool datasetCalendarCoverageKnown = false;
    PerformanceSummary summary;
};

struct BacktestArchiveRecord {
    QString resultId;
    QDateTime completedAt;
    BacktestResult result;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BacktestConfig)
Q_DECLARE_METATYPE(pp::BacktestTrade)
Q_DECLARE_METATYPE(pp::EquityPoint)
Q_DECLARE_METATYPE(pp::PerformanceSummary)
Q_DECLARE_METATYPE(pp::BacktestResult)
Q_DECLARE_METATYPE(pp::BacktestArchiveSummary)
Q_DECLARE_METATYPE(pp::BacktestArchiveRecord)
