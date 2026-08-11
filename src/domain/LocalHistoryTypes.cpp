#include "domain/LocalHistoryTypes.h"

namespace pp {

QString localHistoryFrequency(int periodSeconds)
{
    switch (periodSeconds) {
    case 60: return QStringLiteral("1m");
    case 300: return QStringLiteral("5m");
    case 900: return QStringLiteral("15m");
    case 1800: return QStringLiteral("30m");
    case 3600: return QStringLiteral("60m");
    case 86400: return QStringLiteral("day");
    default: return QStringLiteral("%1s").arg(periodSeconds);
    }
}

QString LocalHistoryDataset::frequency() const
{
    return localHistoryFrequency(periodSeconds);
}

bool LocalHistoryDataset::isRawTickDerived() const
{
    return source == QStringLiteral("raw_tick_cache_v1");
}

bool LocalHistoryDataset::isBacktestEligible() const
{
    return instrument.market == Market::AShare && candleCount > 0
        && firstTime.isValid() && lastTime.isValid() && isRawTickDerived();
}

QVariantMap LocalHistoryDataset::toVariantMap() const
{
    return {{QStringLiteral("symbol"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("title"), instrument.symbol.isEmpty()
                ? QString{} : QStringLiteral("%1  %2").arg(instrument.symbol, instrument.name)},
            {QStringLiteral("market"), marketName(instrument.market)},
            {QStringLiteral("venue"), instrument.venue},
            {QStringLiteral("frequency"), frequency()},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("candleCount"), candleCount},
            {QStringLiteral("firstTime"), firstTime},
            {QStringLiteral("lastTime"), lastTime},
            {QStringLiteral("source"), source},
            {QStringLiteral("adjustment"), adjustment},
            {QStringLiteral("downloadedAt"), downloadedAt},
            {QStringLiteral("dataVersion"), dataVersion},
            {QStringLiteral("rawTickDerived"), isRawTickDerived()},
            {QStringLiteral("backtestEligible"), isBacktestEligible()},
            {QStringLiteral("available"), candleCount > 0 && lastTime.isValid()}};
}

} // namespace pp
