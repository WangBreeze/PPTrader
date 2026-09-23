#pragma once

#include "domain/BacktestTypes.h"
#include "domain/LocalHistoryTypes.h"

#include <QVariantMap>

namespace pp {

// 一折扩展窗 walk-forward 的不可变研究摘要。每折训练和验证均使用全新引擎，
// 只报告事实范围与绩效，不把多折结果伪装成一条可归档回测曲线。
struct RawTickWalkForwardFoldResult {
    int foldNumber = 0;
    QDate trainingStartDate;
    QDate trainingEndDate;
    QDate validationStartDate;
    QDate validationEndDate;
    int trainingActualTicks = 0;
    int validationActualTicks = 0;
    QString trainingDataVersion;
    QString validationDataVersion;
    QString trainingDatasetVersion;
    QString validationDatasetVersion;
    PerformanceSummary trainingSummary;
    PerformanceSummary validationSummary;

    QVariantMap toVariantMap() const;
};

// 永久缺口研究模式的独立区间结果。每段使用全新资金和策略状态，不能把
// 多段结果解释为一条连续权益曲线，也不能作为单次回测归档。
struct RawTickResearchSegmentResult {
    int segmentNumber = 0;
    QDate startDate;
    QDate endDate;
    int tradingDayCount = 0;
    int actualTicks = 0;
    QString dataVersion;
    QString datasetVersion;
    // 只由规范化数据身份、日历/停牌来源和结构化导入质量事实组成；
    // 展示文案、审计时间与本机路径不参与身份。
    QString qualityEvidenceVersion;
    QVariantMap qualityAssessment;
    PerformanceSummary summary;
    BacktestPosition endingPosition;

    QVariantMap toVariantMap() const;
};

struct BatchBacktestRequest {
    quint64 requestId = 0;
    QVector<LocalHistoryDataset> datasets;
    QString strategyId;
    QVariantMap strategyParameters;
    BacktestDataMode dataMode = BacktestDataMode::RawTicks;
    QDate startDate;
    QDate endDate;
    int periodSeconds = 0;
    int barCount = 0;
    double initialCapital = 0.0;
    double commissionRate = 0.0;
    double slippageRate = 0.0;
    int lotSize = 100;
    int maximumActiveEngines = 20;
    // 原始 Tick 固定参数的时序样本外验证。0 表示关闭；开启时至少为 2，
    // 并将最后 N 个实际观测到的上海交易日作为完全独立的验证窗。
    int holdoutTradingDays = 0;
    // 0/1 保持单次尾部留出；2–20 表示扩展训练窗、互不重叠验证窗的多折研究。
    int walkForwardFoldCount = 0;
    // 仅适用于原始 Tick 且不能与 holdout/walk-forward 同时启用。开启后，
    // 质量缺口会切成独立、完整的研究段；严格连续模式仍是默认值。
    bool segmentedResearch = false;
};

struct BatchBacktestItemResult {
    quint64 requestId = 0;
    Instrument instrument;
    BacktestDataMode dataMode = BacktestDataMode::Candles;
    QDate startDate;
    QDate endDate;
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
    QString dataQualityStatus = QStringLiteral("unknown");
    bool dataQualityEligible = false;
    QVariantMap dataQualityAssessment;
    bool archived = false;
    bool archivable = false;
    QString archivedResultId;
    PerformanceSummary summary;
    BacktestPosition finalPosition;
    qint64 elapsedMilliseconds = 0;
    bool holdoutEnabled = false;
    int holdoutTradingDays = 0;
    QDate trainingStartDate;
    QDate trainingEndDate;
    QDate holdoutStartDate;
    QDate holdoutEndDate;
    int trainingActualTicks = 0;
    int holdoutActualTicks = 0;
    QString trainingDataVersion;
    QString holdoutDataVersion;
    QString trainingDatasetVersion;
    QString holdoutDatasetVersion;
    PerformanceSummary trainingSummary;
    PerformanceSummary holdoutSummary;
    bool walkForwardEnabled = false;
    int walkForwardFoldCount = 0;
    bool researchOnly = false;
    QVector<RawTickWalkForwardFoldResult> walkForwardFolds;
    double walkForwardMeanReturnPercent = 0.0;
    double walkForwardMedianReturnPercent = 0.0;
    double walkForwardWorstReturnPercent = 0.0;
    double walkForwardBestReturnPercent = 0.0;
    bool segmentedResearch = false;
    // 独立分段的稳定研究计划身份，由有序区间及其质量证据版本组成。
    QString researchPlanVersion;
    QVector<RawTickResearchSegmentResult> researchSegments;
    double segmentedMeanReturnPercent = 0.0;
    double segmentedMedianReturnPercent = 0.0;
    double segmentedWorstReturnPercent = 0.0;
    double segmentedBestReturnPercent = 0.0;
    double segmentedWorstDrawdownPercent = 0.0;

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
    BacktestDataMode dataMode = BacktestDataMode::Candles;
    QDate startDate;
    QDate endDate;
    QString frequency;
    int periodSeconds = 0;
    int barCount = 0;
    double initialCapital = 0.0;
    double commissionRate = 0.0;
    double slippageRate = 0.0;
    int lotSize = 100;
    int holdoutTradingDays = 0;
    int walkForwardFoldCount = 0;
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
Q_DECLARE_METATYPE(pp::RawTickResearchSegmentResult)
