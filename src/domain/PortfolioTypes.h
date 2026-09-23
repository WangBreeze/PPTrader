#pragma once

#include "domain/BacktestTypes.h"
#include "domain/AShareIndustryTypes.h"
#include "domain/FactorResearchTypes.h"
#include "domain/MarketTypes.h"

#include <QDateTime>
#include <QVariantMap>
#include <QVector>

namespace pp {

struct PortfolioLeg {
    Instrument instrument;
    double targetWeight = 0.0; // 0 表示等权均分
};

struct PortfolioDefinition {
    QString portfolioId;
    QString name;
    QVector<PortfolioLeg> legs;
    int periodSeconds = 0;
    int rebalanceEveryTradingDays = 1;
    double initialCapital = 1000000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
    int lotSize = 100;
    bool tPlusOne = true;
    double maximumLegWeight = 1.0;
    int maximumActiveLegs = 0;
    int maximumConsecutiveMissingBars = 0; // 0 为关闭；恢复报价时按真实价退出
    double maximumPortfolioLossPercent = 0.0; // 0 为关闭；相对初始资金
    double maximumPortfolioDrawdownPercent = 0.0; // 0 为关闭；相对运行峰值
    double maximumIndustryWeight = 0.0; // 0 为关闭；约束调仓目标权重
    QString industryDatasetId; // 开启行业约束或研究中性化时固定不可变数据集
    QString factorExposureAlias; // DSL v3 crossIndicators 中的不可变别名
    double maximumAbsoluteFactorExposure = 0.0; // 0 为关闭；总体 Z 分数暴露
    QString factorResearchAlias; // 空表示不计算 IC；只研究下一对齐帧收益
    QString factorDefinitionId;
    int factorDefinitionVersion = 0;
    QString factorDefinitionSha256;
    FactorPreprocessingPolicy factorPreprocessing;
    FactorResearchCostPolicy factorResearchCost;
    FactorNeutralizationPolicy factorNeutralization;

    bool isValid(QString *error = nullptr) const;
    QVariantMap toVariantMap() const;
    QVector<Instrument> instruments() const;
};

struct PortfolioPosition {
    Instrument instrument;
    double quantity = 0.0;
    double lastPrice = 0.0;
    double marketValue = 0.0;
    double weight = 0.0;
    bool priceStale = false; // 当日无报价且持仓使用最近已知收盘价估值
    QString marketStatus = QStringLiteral("quoted"); // quoted/suspended/missing_unknown
};

struct PortfolioBar {
    QDateTime timestamp;
    double equity = 0.0;
    double cash = 0.0;
    QVector<PortfolioPosition> positions;
};

struct PortfolioTrade {
    Instrument instrument;
    QDateTime timestamp;
    QString side; // buy / sell
    double quantity = 0.0;
    double price = 0.0;
    double commission = 0.0;
    double amount = 0.0;
    QString reason;
};

struct PortfolioRiskSummary {
    int constrainedFrameCount = 0;
    int staleValuationPointCount = 0;
    int maximumObservedActiveLegs = 0;
    double maximumObservedLegWeight = 0.0;
    double maximumObservedTop3Weight = 0.0;
    int maximumObservedConsecutiveMissingBars = 0;
    int completedMissingRiskExitCount = 0;
    int pendingMissingRiskExitCount = 0;
    double maximumObservedLossPercent = 0.0;
    double maximumObservedDrawdownPercent = 0.0;
    bool circuitBreakerTriggered = false;
    QString circuitBreakerReason;
    QDateTime circuitBreakerTriggeredAt;
    double circuitBreakerEquity = 0.0;
    double circuitBreakerLossPercent = 0.0;
    double circuitBreakerDrawdownPercent = 0.0;
    int completedCircuitBreakerExitCount = 0;
    int pendingCircuitBreakerExitCount = 0;
    int calendarOnlyValuationPointCount = 0;
    int authoritativeSuspensionPointCount = 0;
    int unknownMissingQuotePointCount = 0;
    bool marketCalendarCoverageComplete = false;
    bool instrumentStatusCoverageComplete = false;
    QString calendarSnapshotVersion;
    int industryConstrainedGroupCount = 0;
    int industryLimitBreachFrameCount = 0;
    double maximumObservedIndustryWeight = 0.0;
    bool industryCoverageComplete = false;
    QString industrySnapshotVersion;
    QString factorExposureAlias;
    bool factorExposureCoverageComplete = false;
    int factorExposureConstrainedFrameCount = 0;
    int factorExposureUnmeasuredFrameCount = 0;
    int factorExposureLimitBreachFrameCount = 0;
    double maximumRequestedAbsoluteFactorExposure = 0.0;
    double maximumObservedAbsoluteFactorExposure = 0.0;

    QVariantMap toVariantMap() const;
    static PortfolioRiskSummary fromVariantMap(const QVariantMap &map);
};

struct PortfolioResult {
    PortfolioDefinition definition;
    int periodSeconds = 0;
    QString resultKind = QStringLiteral("fixed_portfolio");
    QStringList datasetVersions;
    QVector<BacktestDatasetIdentity> datasetIdentities;
    QVariantMap immutableIdentity;
    AShareIndustrySnapshot industrySnapshot;
    PerformanceSummary summary;
    PortfolioRiskSummary riskSummary;
    FactorResearchSummary factorResearchSummary;
    QVector<PortfolioBar> equityCurve;
    QVector<PortfolioTrade> trades;
};

struct PortfolioArchiveSummary {
    QString resultId;
    QString resultKind = QStringLiteral("fixed_portfolio");
    QString portfolioId;
    QString name;
    QString strategyId;
    int strategyVersion = 0;
    QDateTime completedAt;
    QStringList symbols;
    int legCount = 0;
    int periodSeconds = 0;
    int barCount = 0;
    QStringList datasetVersions;
    PerformanceSummary summary;

    QVariantMap toVariantMap() const;
};

struct PortfolioArchiveRecord {
    QString resultId;
    QDateTime completedAt;
    PortfolioResult result;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::PortfolioDefinition)
Q_DECLARE_METATYPE(pp::PortfolioResult)
Q_DECLARE_METATYPE(pp::PortfolioArchiveSummary)
Q_DECLARE_METATYPE(pp::PortfolioArchiveRecord)
