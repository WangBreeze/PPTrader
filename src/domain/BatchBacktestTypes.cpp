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
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("elapsedMilliseconds"), elapsedMilliseconds}};
}

} // namespace pp

