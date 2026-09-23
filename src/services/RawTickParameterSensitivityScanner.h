#pragma once

#include "domain/BacktestTypes.h"
#include "services/ParameterGridScanner.h"

#include <QDate>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <functional>
#include <optional>

namespace pp {

struct RawTickParameterSensitivityCandidate {
    int rank = 0;
    QVariantMap parameters;
    PerformanceSummary trainingSummary;

    QVariantMap toVariantMap() const;
};

struct RawTickParameterSensitivityRequest {
    QString strategyId;
    QVariantMap baseParameters;
    QVector<ParameterGridAxis> axes;
    BacktestConfig baseConfig;
    QString datasetVersion;
    int validationTradingDays = 2;
    int walkForwardFoldCount = 1;
    int maximumCombinations = 64;
};

// 嵌套 walk-forward 的一个外层折。每折都在当折训练段重新扫描
// 完整候选集，然后只在严格更晚的验证段评价当折胜出参数。
struct RawTickParameterSensitivityFold {
    int foldNumber = 0;
    QDate trainingStartDate;
    QDate trainingEndDate;
    QDate validationStartDate;
    QDate validationEndDate;
    int trainingActualTicks = 0;
    int validationActualTicks = 0;
    QString trainingDataVersion;
    QString validationDataVersion;
    QVariantMap selectedParameters;
    PerformanceSummary validationSummary;
    int validationWinningTrades = 0;
    int validationLosingTrades = 0;
    double validationGrossProfit = 0.0;
    double validationGrossLossAbsolute = 0.0;
    double validationAverageWinningNetPnl = 0.0;
    double validationAverageLosingNetPnlAbsolute = 0.0;
    double validationPayoffRatio = 0.0;
    bool validationPayoffRatioAvailable = false;
    QVector<RawTickParameterSensitivityCandidate> candidates;

    QVariantMap toVariantMap() const;
    static std::optional<RawTickParameterSensitivityFold> fromVariantMap(
        const QVariantMap &value);
};

struct RawTickParameterSensitivityResult {
    QString resultId;
    QDateTime completedAt;
    QString analysisMode = QStringLiteral("single_holdout");
    int walkForwardFoldCount = 1;
    QString strategyId;
    Instrument instrument;
    QVariantMap baseParameters;
    double initialCapital = 0.0;
    double commissionRate = 0.0;
    double slippageRate = 0.0;
    int lotSize = 100;
    QDate trainingStartDate;
    QDate trainingEndDate;
    QDate validationStartDate;
    QDate validationEndDate;
    int trainingActualTicks = 0;
    int validationActualTicks = 0;
    qint64 loadMilliseconds = 0;
    qint64 scanMilliseconds = 0;
    QString trainingDataVersion;
    QString validationDataVersion;
    QString datasetVersion;
    QString candidateSetSha256;
    QString identitySha256;
    QString selectionCriterion;
    QVariantMap selectedParameters;
    PerformanceSummary validationSummary;
    QVector<RawTickParameterSensitivityCandidate> candidates;
    QVector<RawTickParameterSensitivityFold> folds;
    double meanValidationReturnPercent = 0.0;
    double medianValidationReturnPercent = 0.0;
    double minimumValidationReturnPercent = 0.0;
    double maximumValidationReturnPercent = 0.0;
    int positiveValidationFoldCount = 0;
    int nonPositiveValidationFoldCount = 0;
    int validationWinningTrades = 0;
    int validationLosingTrades = 0;
    int validationTotalTrades = 0;
    double validationGrossProfit = 0.0;
    double validationGrossLossAbsolute = 0.0;
    double validationAverageWinningNetPnl = 0.0;
    double validationAverageLosingNetPnlAbsolute = 0.0;
    double validationPayoffRatio = 0.0;
    bool validationPayoffRatioAvailable = false;
    int parameterSelectionVariantCount = 0;
    QVariantList parameterSelectionDistribution;
    bool researchOnly = true;

    QVariantMap toVariantMap() const;
    static std::optional<RawTickParameterSensitivityResult> fromVariantMap(
        const QVariantMap &value);
};

class RawTickParameterSensitivityScanner {
public:
    using SegmentRunner = std::function<std::optional<BacktestResult>(
        BacktestConfig config, const QDate &firstDate, const QDate &lastDate,
        QString *error)>;

    static std::optional<RawTickParameterSensitivityResult> scan(
        const RawTickParameterSensitivityRequest &request,
        QVector<RawTradeTick> ticks,
        const std::atomic_bool *cancelFlag = nullptr,
        QString *error = nullptr);

    // 使用不可变的逐日身份规划训练/验证窗口；每个候选的实际数据读取由
    // segmentRunner 完成，扫描器本身不持有整段原始 Tick。
    static std::optional<RawTickParameterSensitivityResult> scanSegments(
        const RawTickParameterSensitivityRequest &request,
        QVector<RawTickDayFingerprint> days,
        const SegmentRunner &segmentRunner,
        const std::atomic_bool *cancelFlag = nullptr,
        QString *error = nullptr);
};

} // namespace pp

Q_DECLARE_METATYPE(pp::RawTickParameterSensitivityCandidate)
Q_DECLARE_METATYPE(pp::RawTickParameterSensitivityFold)
Q_DECLARE_METATYPE(pp::RawTickParameterSensitivityResult)
