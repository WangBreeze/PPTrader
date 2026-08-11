#include "domain/HistoricalProviderTypes.h"

namespace pp {

bool HistoricalProviderCapability::supports(
    const Instrument &instrument, const QString &frequency,
    const QString &adjustment) const
{
    return configured && markets.contains(instrument.market)
        && (venues.isEmpty() || venues.contains(instrument.venue))
        && frequencies.contains(frequency)
        && adjustments.contains(adjustment.isEmpty() ? QStringLiteral("none")
                                                      : adjustment);
}

QVariantMap HistoricalProviderCapability::toVariantMap() const
{
    QStringList marketNames;
    for (const Market market : markets) marketNames.append(marketName(market));
    return {{QStringLiteral("providerId"), providerId},
            {QStringLiteral("displayName"), displayName},
            {QStringLiteral("markets"), marketNames},
            {QStringLiteral("venues"), venues},
            {QStringLiteral("frequencies"), frequencies},
            {QStringLiteral("adjustments"), adjustments},
            {QStringLiteral("earliestDate"), earliestDate},
            {QStringLiteral("maximumBarsPerRequest"), maximumBarsPerRequest},
            {QStringLiteral("rateLimitPerMinute"), rateLimitPerMinute},
            {QStringLiteral("priority"), priority},
            {QStringLiteral("authenticationRequired"), authenticationRequired},
            {QStringLiteral("configured"), configured},
            {QStringLiteral("supportsSuspended"), supportsSuspended},
            {QStringLiteral("supportsDelisted"), supportsDelisted},
            {QStringLiteral("configurationMessage"), configurationMessage}};
}

QString historicalAdjustmentName(const QString &adjustment)
{
    if (adjustment == QStringLiteral("qfq")) return QStringLiteral("前复权");
    if (adjustment == QStringLiteral("hfq")) return QStringLiteral("后复权");
    return QStringLiteral("不复权");
}

} // namespace pp
