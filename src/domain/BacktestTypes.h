#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"
#include "domain/RawTradeTypes.h"

#include <QVariantMap>
#include <QVector>

namespace pp {

enum class BacktestDataMode {
    RawTicks = 0,
    Candles = 1
};

enum class BacktestArchiveLoadMode {
    Display = 0, // 身份、摘要、权益、成交、信号；raw Tick 保持按需
    CompleteInputSnapshot = 1 // 显式恢复完整不可变输入，可能占用大量内存
};

inline QString backtestDataModeCode(BacktestDataMode mode)
{
    return mode == BacktestDataMode::RawTicks
        ? QStringLiteral("raw_ticks") : QStringLiteral("candles");
}

struct BacktestConfig {
    QString taskId;
    QString strategyId = QStringLiteral("sma_crossover");
    int strategyVersion = 0;
    QVariantMap strategyParameters;
    QVariantMap ruleSnapshot;
    QString dataVersion;
    BacktestDatasetIdentity datasetIdentity;
    BacktestDataMode dataMode = BacktestDataMode::Candles;
    Instrument instrument;
    QString frequency = QStringLiteral("day");
    int periodSeconds = 86400;
    int requestedBars = 500;
    int actualBars = 0;
    QDateTime from;
    QDateTime to;
    QDateTime sampleFrom; // 请求样本窗起点，无效表示不限制
    QDateTime sampleTo;   // 请求样本窗终点，无效表示不限制
    bool sampleWindowKnown = true; // 旧归档未记录时为 false，不得解释为明确不限日期
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
    BacktestPosition finalPosition; // 样本结束时仍未平仓的真实持仓；quantity=0 表示空仓
    QVector<BacktestTrade> trades;
    QVector<EquityPoint> equityCurve;
    qint64 equityObservationCount = 0; // 精确绩效计算覆盖的全部观测数
    bool equityCurveSampled = false; // equityCurve 是否仅为有界展示序列
    QVector<Candle> candles; // 回测用 K 线，供 K 线图叠加买卖点
    QVector<RawTradeTick> rawTicks; // 原始逐笔模式的不可变输入快照
    // 执行结果只保留源事实身份时为 true；它与“归档已保存但当前未分页读取”不同。
    bool rawTickSnapshotDeferred = false;
    bool rawTickSnapshotAvailable = false; // SQLite 归档中存在完整原始 Tick 快照
    bool rawTickSnapshotLoaded = false; // 当前结果对象是否已载入该完整快照
    QVector<TradeSignal> chartSignals; // 与 K 线时刻一一对齐的图表信号快照
    QVariantMap strategyEvaluationFacts; // 策略观察事实；不等同于成交或归档
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

struct BacktestRawTickPage {
    QString resultId;
    Instrument instrument;
    int offset = 0;
    int totalCount = 0;
    QVector<RawTradeTick> ticks;
    bool hasMore = false;
};

// 回测验证记录：保存一次验证回测的配置与摘要，供按收益率排序与重跑。
struct ValidationRun {
    QString name;
    QString strategyId;
    int strategyVersion = 0;
    QString instrumentSymbol;
    int periodSeconds = 0;
    QDateTime sampleFrom;
    QDateTime sampleTo;
    int requestedBars = 0;
    double totalReturnPercent = 0.0;
    double maxDrawdownPercent = 0.0;
    double sharpeRatio = 0.0;
    int totalTrades = 0;
    QDateTime completedAt;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("name"), name},
                {QStringLiteral("strategyId"), strategyId},
                {QStringLiteral("strategyVersion"), strategyVersion},
                {QStringLiteral("instrumentSymbol"), instrumentSymbol},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("sampleFrom"), sampleFrom},
                {QStringLiteral("sampleTo"), sampleTo},
                {QStringLiteral("requestedBars"), requestedBars},
                {QStringLiteral("totalReturnPercent"), totalReturnPercent},
                {QStringLiteral("maxDrawdownPercent"), maxDrawdownPercent},
                {QStringLiteral("sharpeRatio"), sharpeRatio},
                {QStringLiteral("totalTrades"), totalTrades},
                {QStringLiteral("completedAt"), completedAt}};
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::BacktestConfig)
Q_DECLARE_METATYPE(pp::BacktestDataMode)
Q_DECLARE_METATYPE(pp::BacktestArchiveLoadMode)
Q_DECLARE_METATYPE(pp::BacktestTrade)
Q_DECLARE_METATYPE(pp::EquityPoint)
Q_DECLARE_METATYPE(pp::PerformanceSummary)
Q_DECLARE_METATYPE(pp::BacktestResult)
Q_DECLARE_METATYPE(pp::BacktestArchiveSummary)
Q_DECLARE_METATYPE(pp::BacktestArchiveRecord)
Q_DECLARE_METATYPE(pp::BacktestRawTickPage)
