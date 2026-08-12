#include "domain/BatchBacktestTypes.h"

namespace pp {

QVariantMap BatchBacktestItemResult::toVariantMap() const
{
    return {{QStringLiteral("requestId"), QVariant::fromValue(requestId)},
            {QStringLiteral("symbol"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
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
            {QStringLiteral("archived"), archived},
            {QStringLiteral("archivedResultId"), archivedResultId},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("elapsedMilliseconds"), elapsedMilliseconds}};
}

QVariantMap BatchBacktestRunSummary::toVariantMap() const
{
    return {{QStringLiteral("runId"), runId},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("completedAt"), completedAt},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("strategyParameters"), strategyParameters},
            {QStringLiteral("frequency"), frequency},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("barCount"), barCount},
            {QStringLiteral("initialCapital"), initialCapital},
            {QStringLiteral("commissionRate"), commissionRate},
            {QStringLiteral("slippageRate"), slippageRate},
            {QStringLiteral("lotSize"), lotSize},
            {QStringLiteral("totalSymbols"), totalSymbols},
            {QStringLiteral("completedSymbols"), completedSymbols},
            {QStringLiteral("succeededSymbols"), succeededSymbols},
            {QStringLiteral("failedSymbols"), failedSymbols},
            {QStringLiteral("status"), status},
            {QStringLiteral("error"), error}};
}

} // namespace pp
