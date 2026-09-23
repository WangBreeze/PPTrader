#include "domain/PortfolioTypes.h"

#include <cmath>

namespace pp {

bool PortfolioDefinition::isValid(QString *error) const
{
    if (portfolioId.trimmed().isEmpty() || legs.isEmpty()) {
        if (error) *error = QStringLiteral("组合 ID 与腿列表不能为空");
        return false;
    }
    if (periodSeconds <= 0 || periodSeconds > 86400) {
        if (error) *error = QStringLiteral("组合周期必须在 1–86400 秒之间");
        return false;
    }
    if (!std::isfinite(maximumPortfolioLossPercent)
        || maximumPortfolioLossPercent < 0.0
        || maximumPortfolioLossPercent > 100.0) {
        if (error) *error = QStringLiteral("组合累计亏损熔断阈值必须在 0–100% 之间");
        return false;
    }
    if (!std::isfinite(maximumPortfolioDrawdownPercent)
        || maximumPortfolioDrawdownPercent < 0.0
        || maximumPortfolioDrawdownPercent > 100.0) {
        if (error) *error = QStringLiteral("组合峰值回撤熔断阈值必须在 0–100% 之间");
        return false;
    }
    if (!std::isfinite(maximumIndustryWeight)
        || maximumIndustryWeight < 0.0 || maximumIndustryWeight > 1.0) {
        if (error) *error = QStringLiteral("组合行业权重上限必须在 0–1 之间");
        return false;
    }
    if (maximumIndustryWeight > 0.0
        && industryDatasetId.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("开启行业约束必须固定行业数据集");
        return false;
    }
    if (!std::isfinite(maximumAbsoluteFactorExposure)
        || maximumAbsoluteFactorExposure < 0.0
        || maximumAbsoluteFactorExposure > 10.0) {
        if (error) *error = QStringLiteral("因子绝对暴露上限必须在 0–10 之间");
        return false;
    }
    if (maximumAbsoluteFactorExposure > 0.0
        && factorExposureAlias.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("开启因子暴露约束必须指定横截面指标别名");
        return false;
    }
    if (!factorResearchAlias.trimmed().isEmpty()) {
        QString preprocessingError;
        QString costPolicyError;
        QString neutralizationError;
        if (factorDefinitionId.trimmed().isEmpty()
            || factorDefinitionVersion < 1
            || !factorDefinitionSha256.startsWith(QStringLiteral("sha256:"))
            || factorDefinitionSha256.size() != 71
            || !factorPreprocessing.isValid(&preprocessingError)
            || !factorResearchCost.isValid(&costPolicyError)
            || !factorNeutralization.isValid(&neutralizationError)
            || (factorNeutralization.enabled()
                && industryDatasetId.trimmed().isEmpty())) {
            if (error) {
                *error = !preprocessingError.isEmpty()
                    ? preprocessingError
                    : !costPolicyError.isEmpty()
                        ? costPolicyError
                        : !neutralizationError.isEmpty()
                            ? neutralizationError
                            : factorNeutralization.enabled()
                                  && industryDatasetId.trimmed().isEmpty()
                                ? QStringLiteral(
                                      "行业中性化必须固定不可变行业数据集")
                                : QStringLiteral(
                                      "因子研究需要完整的不可变因子版本身份");
            }
            return false;
        }
    }
    if (initialCapital <= 0.0 || commissionRate < 0.0 || commissionRate > 0.1
        || slippageRate < 0.0 || slippageRate > 0.1 || lotSize <= 0
        || rebalanceEveryTradingDays < 1 || maximumLegWeight <= 0.0
        || maximumLegWeight > 1.0 || maximumActiveLegs < 0
        || maximumConsecutiveMissingBars < 0
        || !std::isfinite(initialCapital) || !std::isfinite(commissionRate)
        || !std::isfinite(slippageRate)) {
        if (error) *error = QStringLiteral("组合资金、费率、周期或风险参数无效");
        return false;
    }
    for (const PortfolioLeg &leg : legs) {
        if (leg.instrument.symbol.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("组合腿包含空标的");
            return false;
        }
        if (leg.targetWeight < 0.0 || !std::isfinite(leg.targetWeight)
            || leg.targetWeight > 1.0) {
            if (error) *error = QStringLiteral("组合腿权重必须在 0–1 之间");
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

QVariantMap PortfolioDefinition::toVariantMap() const
{
    QVariantList legList;
    for (const PortfolioLeg &leg : legs) {
        legList.append(QVariantMap{
            {QStringLiteral("symbol"), leg.instrument.symbol},
            {QStringLiteral("name"), leg.instrument.name},
            {QStringLiteral("targetWeight"), leg.targetWeight}});
    }
    return {{QStringLiteral("portfolioId"), portfolioId},
            {QStringLiteral("name"), name},
            {QStringLiteral("legs"), legList},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("rebalanceEveryTradingDays"),
             rebalanceEveryTradingDays},
            {QStringLiteral("initialCapital"), initialCapital},
            {QStringLiteral("commissionRate"), commissionRate},
            {QStringLiteral("slippageRate"), slippageRate},
            {QStringLiteral("lotSize"), lotSize},
            {QStringLiteral("tPlusOne"), tPlusOne},
            {QStringLiteral("maximumLegWeight"), maximumLegWeight},
            {QStringLiteral("maximumActiveLegs"), maximumActiveLegs},
            {QStringLiteral("maximumConsecutiveMissingBars"),
             maximumConsecutiveMissingBars},
            {QStringLiteral("maximumPortfolioLossPercent"),
             maximumPortfolioLossPercent},
            {QStringLiteral("maximumPortfolioDrawdownPercent"),
             maximumPortfolioDrawdownPercent},
            {QStringLiteral("maximumIndustryWeight"), maximumIndustryWeight},
            {QStringLiteral("industryDatasetId"), industryDatasetId},
            {QStringLiteral("factorExposureAlias"), factorExposureAlias},
            {QStringLiteral("maximumAbsoluteFactorExposure"),
             maximumAbsoluteFactorExposure},
            {QStringLiteral("factorResearchAlias"), factorResearchAlias},
            {QStringLiteral("factorDefinitionId"), factorDefinitionId},
            {QStringLiteral("factorDefinitionVersion"), factorDefinitionVersion},
            {QStringLiteral("factorDefinitionSha256"), factorDefinitionSha256},
            {QStringLiteral("factorPreprocessing"),
             factorPreprocessing.toVariantMap()},
            {QStringLiteral("factorResearchCost"),
             factorResearchCost.toVariantMap()},
            {QStringLiteral("factorNeutralization"),
             factorNeutralization.toVariantMap()}};
}

QVector<Instrument> PortfolioDefinition::instruments() const
{
    QVector<Instrument> result;
    result.reserve(legs.size());
    for (const PortfolioLeg &leg : legs) result.append(leg.instrument);
    return result;
}

QVariantMap PortfolioRiskSummary::toVariantMap() const
{
    return {{QStringLiteral("constrainedFrameCount"), constrainedFrameCount},
            {QStringLiteral("staleValuationPointCount"),
             staleValuationPointCount},
            {QStringLiteral("maximumObservedActiveLegs"),
             maximumObservedActiveLegs},
            {QStringLiteral("maximumObservedLegWeight"),
             maximumObservedLegWeight},
            {QStringLiteral("maximumObservedTop3Weight"),
             maximumObservedTop3Weight},
            {QStringLiteral("maximumObservedConsecutiveMissingBars"),
             maximumObservedConsecutiveMissingBars},
            {QStringLiteral("completedMissingRiskExitCount"),
             completedMissingRiskExitCount},
            {QStringLiteral("pendingMissingRiskExitCount"),
             pendingMissingRiskExitCount},
            {QStringLiteral("maximumObservedLossPercent"),
             maximumObservedLossPercent},
            {QStringLiteral("maximumObservedDrawdownPercent"),
             maximumObservedDrawdownPercent},
            {QStringLiteral("circuitBreakerTriggered"),
             circuitBreakerTriggered},
            {QStringLiteral("circuitBreakerReason"), circuitBreakerReason},
            {QStringLiteral("circuitBreakerTriggeredAt"),
             circuitBreakerTriggeredAt},
            {QStringLiteral("circuitBreakerEquity"), circuitBreakerEquity},
            {QStringLiteral("circuitBreakerLossPercent"),
             circuitBreakerLossPercent},
            {QStringLiteral("circuitBreakerDrawdownPercent"),
             circuitBreakerDrawdownPercent},
            {QStringLiteral("completedCircuitBreakerExitCount"),
             completedCircuitBreakerExitCount},
            {QStringLiteral("pendingCircuitBreakerExitCount"),
             pendingCircuitBreakerExitCount},
            {QStringLiteral("calendarOnlyValuationPointCount"),
             calendarOnlyValuationPointCount},
            {QStringLiteral("authoritativeSuspensionPointCount"),
             authoritativeSuspensionPointCount},
            {QStringLiteral("unknownMissingQuotePointCount"),
             unknownMissingQuotePointCount},
            {QStringLiteral("marketCalendarCoverageComplete"),
             marketCalendarCoverageComplete},
            {QStringLiteral("instrumentStatusCoverageComplete"),
             instrumentStatusCoverageComplete},
            {QStringLiteral("calendarSnapshotVersion"),
             calendarSnapshotVersion},
            {QStringLiteral("industryConstrainedGroupCount"),
             industryConstrainedGroupCount},
            {QStringLiteral("industryLimitBreachFrameCount"),
             industryLimitBreachFrameCount},
            {QStringLiteral("maximumObservedIndustryWeight"),
             maximumObservedIndustryWeight},
            {QStringLiteral("industryCoverageComplete"),
             industryCoverageComplete},
            {QStringLiteral("industrySnapshotVersion"),
             industrySnapshotVersion},
            {QStringLiteral("factorExposureAlias"), factorExposureAlias},
            {QStringLiteral("factorExposureCoverageComplete"),
             factorExposureCoverageComplete},
            {QStringLiteral("factorExposureConstrainedFrameCount"),
             factorExposureConstrainedFrameCount},
            {QStringLiteral("factorExposureUnmeasuredFrameCount"),
             factorExposureUnmeasuredFrameCount},
            {QStringLiteral("factorExposureLimitBreachFrameCount"),
             factorExposureLimitBreachFrameCount},
            {QStringLiteral("maximumRequestedAbsoluteFactorExposure"),
             maximumRequestedAbsoluteFactorExposure},
            {QStringLiteral("maximumObservedAbsoluteFactorExposure"),
             maximumObservedAbsoluteFactorExposure}};
}

PortfolioRiskSummary PortfolioRiskSummary::fromVariantMap(const QVariantMap &map)
{
    PortfolioRiskSummary result;
    result.constrainedFrameCount =
        map.value(QStringLiteral("constrainedFrameCount")).toInt();
    result.staleValuationPointCount =
        map.value(QStringLiteral("staleValuationPointCount")).toInt();
    result.maximumObservedActiveLegs =
        map.value(QStringLiteral("maximumObservedActiveLegs")).toInt();
    result.maximumObservedLegWeight =
        map.value(QStringLiteral("maximumObservedLegWeight")).toDouble();
    result.maximumObservedTop3Weight =
        map.value(QStringLiteral("maximumObservedTop3Weight")).toDouble();
    result.maximumObservedConsecutiveMissingBars = map
        .value(QStringLiteral("maximumObservedConsecutiveMissingBars")).toInt();
    result.completedMissingRiskExitCount =
        map.value(QStringLiteral("completedMissingRiskExitCount")).toInt();
    result.pendingMissingRiskExitCount =
        map.value(QStringLiteral("pendingMissingRiskExitCount")).toInt();
    result.maximumObservedLossPercent =
        map.value(QStringLiteral("maximumObservedLossPercent")).toDouble();
    result.maximumObservedDrawdownPercent = map
        .value(QStringLiteral("maximumObservedDrawdownPercent")).toDouble();
    result.circuitBreakerTriggered =
        map.value(QStringLiteral("circuitBreakerTriggered")).toBool();
    result.circuitBreakerReason =
        map.value(QStringLiteral("circuitBreakerReason")).toString();
    result.circuitBreakerTriggeredAt =
        map.value(QStringLiteral("circuitBreakerTriggeredAt")).toDateTime();
    result.circuitBreakerEquity =
        map.value(QStringLiteral("circuitBreakerEquity")).toDouble();
    result.circuitBreakerLossPercent =
        map.value(QStringLiteral("circuitBreakerLossPercent")).toDouble();
    result.circuitBreakerDrawdownPercent = map
        .value(QStringLiteral("circuitBreakerDrawdownPercent")).toDouble();
    result.completedCircuitBreakerExitCount = map
        .value(QStringLiteral("completedCircuitBreakerExitCount")).toInt();
    result.pendingCircuitBreakerExitCount = map
        .value(QStringLiteral("pendingCircuitBreakerExitCount")).toInt();
    result.calendarOnlyValuationPointCount = map
        .value(QStringLiteral("calendarOnlyValuationPointCount")).toInt();
    result.authoritativeSuspensionPointCount = map
        .value(QStringLiteral("authoritativeSuspensionPointCount")).toInt();
    result.unknownMissingQuotePointCount = map
        .value(QStringLiteral("unknownMissingQuotePointCount")).toInt();
    result.marketCalendarCoverageComplete = map
        .value(QStringLiteral("marketCalendarCoverageComplete")).toBool();
    result.instrumentStatusCoverageComplete = map
        .value(QStringLiteral("instrumentStatusCoverageComplete")).toBool();
    result.calendarSnapshotVersion = map
        .value(QStringLiteral("calendarSnapshotVersion")).toString();
    result.industryConstrainedGroupCount = map
        .value(QStringLiteral("industryConstrainedGroupCount")).toInt();
    result.industryLimitBreachFrameCount = map
        .value(QStringLiteral("industryLimitBreachFrameCount")).toInt();
    result.maximumObservedIndustryWeight = map
        .value(QStringLiteral("maximumObservedIndustryWeight")).toDouble();
    result.industryCoverageComplete = map
        .value(QStringLiteral("industryCoverageComplete")).toBool();
    result.industrySnapshotVersion = map
        .value(QStringLiteral("industrySnapshotVersion")).toString();
    result.factorExposureAlias = map
        .value(QStringLiteral("factorExposureAlias")).toString();
    result.factorExposureCoverageComplete = map
        .value(QStringLiteral("factorExposureCoverageComplete")).toBool();
    result.factorExposureConstrainedFrameCount = map
        .value(QStringLiteral("factorExposureConstrainedFrameCount")).toInt();
    result.factorExposureUnmeasuredFrameCount = map
        .value(QStringLiteral("factorExposureUnmeasuredFrameCount")).toInt();
    result.factorExposureLimitBreachFrameCount = map
        .value(QStringLiteral("factorExposureLimitBreachFrameCount")).toInt();
    result.maximumRequestedAbsoluteFactorExposure = map
        .value(QStringLiteral("maximumRequestedAbsoluteFactorExposure")).toDouble();
    result.maximumObservedAbsoluteFactorExposure = map
        .value(QStringLiteral("maximumObservedAbsoluteFactorExposure")).toDouble();
    return result;
}

QVariantMap PortfolioArchiveSummary::toVariantMap() const
{
    return {{QStringLiteral("resultId"), resultId},
            {QStringLiteral("resultKind"), resultKind},
            {QStringLiteral("portfolioId"), portfolioId},
            {QStringLiteral("name"), name},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("strategyVersion"), strategyVersion},
            {QStringLiteral("completedAt"), completedAt},
            {QStringLiteral("symbols"), symbols},
            {QStringLiteral("legCount"), legCount},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("barCount"), barCount},
            {QStringLiteral("datasetVersions"), datasetVersions},
            {QStringLiteral("initialCapital"), summary.initialCapital},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades}};
}

} // namespace pp
