#include "domain/RealtimeProviderTypes.h"

#include "services/MarketDataProvider.h"

#include <algorithm>
#include <cmath>

namespace pp {

QVariantMap RealtimeProviderCapability::toVariantMap() const
{
    return {{QStringLiteral("providerId"), providerId},
            {QStringLiteral("displayName"), displayName},
            {QStringLiteral("transport"), transport},
            {QStringLiteral("priority"), priority},
            {QStringLiteral("configured"), configured},
            {QStringLiteral("fullMarket"), fullMarket},
            {QStringLiteral("rawTrades"), rawTrades},
            {QStringLiteral("polling"), polling},
            {QStringLiteral("nativeMinuteBars"), nativeMinuteBars},
            {QStringLiteral("minimumPeriodSeconds"), minimumPeriodSeconds},
            {QStringLiteral("pollingIntervalSeconds"), pollingIntervalSeconds},
            {QStringLiteral("watchdogTimeoutMilliseconds"), watchdogTimeoutMilliseconds},
            {QStringLiteral("authenticationRequired"), authenticationRequired},
            {QStringLiteral("configurationMessage"), configurationMessage}};
}

int realtimeProviderHealthScore(int health, int expectedCount, int receivedCount,
                                int failureStreak, qint64 silenceMilliseconds)
{
    const auto state = static_cast<ProviderHealthState>(health);
    const double completeness = expectedCount > 0
        ? std::clamp(static_cast<double>(receivedCount) / expectedCount, 0.0, 1.0)
        : 0.0;
    int score = 0;
    switch (state) {
    case ProviderHealthState::Healthy:
        score = 70 + static_cast<int>(std::lround(completeness * 30.0));
        break;
    case ProviderHealthState::Degraded:
        score = 20 + static_cast<int>(std::lround(completeness * 50.0));
        break;
    case ProviderHealthState::Starting:
        score = 35;
        break;
    case ProviderHealthState::Stopped:
    case ProviderHealthState::Error:
        score = 0;
        break;
    }
    score -= std::max(0, failureStreak) * 8;
    if (silenceMilliseconds > 10000) {
        score -= static_cast<int>(std::min<qint64>(30, (silenceMilliseconds - 10000) / 1000));
    }
    return std::clamp(score, 0, 100);
}

} // namespace pp
