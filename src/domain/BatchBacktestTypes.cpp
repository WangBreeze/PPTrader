#include "domain/BatchBacktestTypes.h"

namespace pp {

namespace {

QVariantMap performanceMap(const PerformanceSummary &summary)
{
    return {{QStringLiteral("initialCapital"), summary.initialCapital},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades}};
}

} // namespace

QVariantMap RawTickWalkForwardFoldResult::toVariantMap() const
{
    return {{QStringLiteral("foldNumber"), foldNumber},
            {QStringLiteral("trainingStartDate"), trainingStartDate},
            {QStringLiteral("trainingEndDate"), trainingEndDate},
            {QStringLiteral("validationStartDate"), validationStartDate},
            {QStringLiteral("validationEndDate"), validationEndDate},
            {QStringLiteral("trainingActualTicks"), trainingActualTicks},
            {QStringLiteral("validationActualTicks"), validationActualTicks},
            {QStringLiteral("trainingDataVersion"), trainingDataVersion},
            {QStringLiteral("validationDataVersion"), validationDataVersion},
            {QStringLiteral("trainingDatasetVersion"), trainingDatasetVersion},
            {QStringLiteral("validationDatasetVersion"), validationDatasetVersion},
            {QStringLiteral("trainingSummary"), performanceMap(trainingSummary)},
            {QStringLiteral("validationSummary"), performanceMap(validationSummary)},
            {QStringLiteral("trainingTotalReturnPercent"),
             trainingSummary.totalReturnPercent},
            {QStringLiteral("validationTotalReturnPercent"),
             validationSummary.totalReturnPercent},
            {QStringLiteral("validationMaxDrawdownPercent"),
             validationSummary.maxDrawdownPercent},
            {QStringLiteral("validationTotalTrades"),
             validationSummary.totalTrades}};
}

QVariantMap RawTickResearchSegmentResult::toVariantMap() const
{
    return {{QStringLiteral("segmentNumber"), segmentNumber},
            {QStringLiteral("startDate"), startDate},
            {QStringLiteral("endDate"), endDate},
            {QStringLiteral("tradingDayCount"), tradingDayCount},
            {QStringLiteral("actualTicks"), actualTicks},
            {QStringLiteral("dataVersion"), dataVersion},
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("qualityEvidenceVersion"),
             qualityEvidenceVersion},
            {QStringLiteral("qualityAssessment"), qualityAssessment},
            {QStringLiteral("summary"), performanceMap(summary)},
            {QStringLiteral("totalReturnPercent"),
             summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"),
             summary.maxDrawdownPercent},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("hasEndingOpenPosition"),
             endingPosition.quantity > 0.0},
            {QStringLiteral("endingPositionQuantity"),
             endingPosition.quantity},
            {QStringLiteral("endingPositionAveragePrice"),
             endingPosition.averagePrice},
            {QStringLiteral("endingPositionEntryTime"),
             endingPosition.entryTime},
            {QStringLiteral("endingPositionEntryReason"),
             endingPosition.entryReason}};
}

QVariantMap BatchBacktestItemResult::toVariantMap() const
{
    QVariantList foldMaps;
    foldMaps.reserve(walkForwardFolds.size());
    for (const RawTickWalkForwardFoldResult &fold : walkForwardFolds) {
        foldMaps.append(fold.toVariantMap());
    }
    QVariantList segmentMaps;
    segmentMaps.reserve(researchSegments.size());
    for (const RawTickResearchSegmentResult &segment : researchSegments) {
        segmentMaps.append(segment.toVariantMap());
    }
    return {{QStringLiteral("requestId"), QVariant::fromValue(requestId)},
            {QStringLiteral("symbol"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("dataMode"), backtestDataModeCode(dataMode)},
            {QStringLiteral("startDate"), startDate},
            {QStringLiteral("endDate"), endDate},
            {QStringLiteral("frequency"), frequency},
            {QStringLiteral("requestedBars"), requestedBars},
            {QStringLiteral("actualBars"), actualBars},
            {QStringLiteral("succeeded"), succeeded},
            {QStringLiteral("error"), error},
            {QStringLiteral("dataVersion"), dataVersion},
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("datasetDayCount"), datasetDayCount},
            {QStringLiteral("datasetRawTickCount"), datasetRawTickCount},
            {QStringLiteral("datasetGapDayCount"), datasetGapDayCount},
            {QStringLiteral("datasetStaleDayCount"), datasetStaleDayCount},
            {QStringLiteral("dataQualityStatus"), dataQualityStatus},
            {QStringLiteral("dataQualityEligible"), dataQualityEligible},
            {QStringLiteral("dataQualityAssessment"), dataQualityAssessment},
            {QStringLiteral("archived"), archived},
            {QStringLiteral("archivable"), archivable},
            {QStringLiteral("archivedResultId"), archivedResultId},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("hasOpenPosition"), finalPosition.quantity > 0.0},
            {QStringLiteral("openPositionQuantity"), finalPosition.quantity},
            {QStringLiteral("openPositionAveragePrice"), finalPosition.averagePrice},
            {QStringLiteral("openPositionEntryTime"), finalPosition.entryTime},
            {QStringLiteral("openPositionEntryReason"), finalPosition.entryReason},
            {QStringLiteral("elapsedMilliseconds"), elapsedMilliseconds},
            {QStringLiteral("holdoutEnabled"), holdoutEnabled},
            {QStringLiteral("holdoutTradingDays"), holdoutTradingDays},
            {QStringLiteral("trainingStartDate"), trainingStartDate},
            {QStringLiteral("trainingEndDate"), trainingEndDate},
            {QStringLiteral("holdoutStartDate"), holdoutStartDate},
            {QStringLiteral("holdoutEndDate"), holdoutEndDate},
            {QStringLiteral("trainingActualTicks"), trainingActualTicks},
            {QStringLiteral("holdoutActualTicks"), holdoutActualTicks},
            {QStringLiteral("trainingDataVersion"), trainingDataVersion},
            {QStringLiteral("holdoutDataVersion"), holdoutDataVersion},
            {QStringLiteral("trainingDatasetVersion"), trainingDatasetVersion},
            {QStringLiteral("holdoutDatasetVersion"), holdoutDatasetVersion},
            {QStringLiteral("trainingTotalReturnPercent"),
             trainingSummary.totalReturnPercent},
            {QStringLiteral("trainingMaxDrawdownPercent"),
             trainingSummary.maxDrawdownPercent},
            {QStringLiteral("trainingTotalTrades"), trainingSummary.totalTrades},
            {QStringLiteral("holdoutTotalReturnPercent"),
             holdoutSummary.totalReturnPercent},
            {QStringLiteral("holdoutMaxDrawdownPercent"),
             holdoutSummary.maxDrawdownPercent},
            {QStringLiteral("holdoutTotalTrades"), holdoutSummary.totalTrades},
            {QStringLiteral("walkForwardEnabled"), walkForwardEnabled},
            {QStringLiteral("walkForwardFoldCount"), walkForwardFoldCount},
            {QStringLiteral("researchOnly"), researchOnly},
            {QStringLiteral("walkForwardFolds"), foldMaps},
            {QStringLiteral("walkForwardMeanReturnPercent"),
             walkForwardMeanReturnPercent},
            {QStringLiteral("walkForwardMedianReturnPercent"),
             walkForwardMedianReturnPercent},
            {QStringLiteral("walkForwardWorstReturnPercent"),
             walkForwardWorstReturnPercent},
            {QStringLiteral("walkForwardBestReturnPercent"),
             walkForwardBestReturnPercent},
            {QStringLiteral("segmentedResearch"), segmentedResearch},
            {QStringLiteral("researchPlanVersion"), researchPlanVersion},
            {QStringLiteral("researchSegments"), segmentMaps},
            {QStringLiteral("researchSegmentCount"), segmentMaps.size()},
            {QStringLiteral("segmentedMeanReturnPercent"),
             segmentedMeanReturnPercent},
            {QStringLiteral("segmentedMedianReturnPercent"),
             segmentedMedianReturnPercent},
            {QStringLiteral("segmentedWorstReturnPercent"),
             segmentedWorstReturnPercent},
            {QStringLiteral("segmentedBestReturnPercent"),
             segmentedBestReturnPercent},
            {QStringLiteral("segmentedWorstDrawdownPercent"),
             segmentedWorstDrawdownPercent}};
}

QVariantMap BatchBacktestRunSummary::toVariantMap() const
{
    return {{QStringLiteral("runId"), runId},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("completedAt"), completedAt},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("strategyParameters"), strategyParameters},
            {QStringLiteral("dataMode"), backtestDataModeCode(dataMode)},
            {QStringLiteral("startDate"), startDate},
            {QStringLiteral("endDate"), endDate},
            {QStringLiteral("frequency"), frequency},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("barCount"), barCount},
            {QStringLiteral("initialCapital"), initialCapital},
            {QStringLiteral("commissionRate"), commissionRate},
            {QStringLiteral("slippageRate"), slippageRate},
            {QStringLiteral("lotSize"), lotSize},
            {QStringLiteral("holdoutTradingDays"), holdoutTradingDays},
            {QStringLiteral("walkForwardFoldCount"), walkForwardFoldCount},
            {QStringLiteral("totalSymbols"), totalSymbols},
            {QStringLiteral("completedSymbols"), completedSymbols},
            {QStringLiteral("succeededSymbols"), succeededSymbols},
            {QStringLiteral("failedSymbols"), failedSymbols},
            {QStringLiteral("status"), status},
            {QStringLiteral("error"), error}};
}

} // namespace pp
