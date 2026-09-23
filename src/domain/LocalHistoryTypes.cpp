#include "domain/LocalHistoryTypes.h"

#include <QRegularExpression>

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

int localHistoryPeriodSeconds(const QString &frequency)
{
    const QString normalized = frequency.trimmed().toLower();
    if (normalized == QStringLiteral("day")) return 86400;
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)([smhd])$"));
    const QRegularExpressionMatch match = pattern.match(normalized);
    if (!match.hasMatch()) return 0;
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    if (!ok || value <= 0) return 0;
    qint64 multiplier = 1;
    const QChar unit = match.captured(2).at(0);
    if (unit == QLatin1Char('m')) multiplier = 60;
    else if (unit == QLatin1Char('h')) multiplier = 3600;
    else if (unit == QLatin1Char('d')) multiplier = 86400;
    const qint64 seconds = qint64(value) * multiplier;
    return seconds >= 1 && seconds <= 86400 ? int(seconds) : 0;
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
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("datasetFirstDate"), datasetFirstDate},
            {QStringLiteral("datasetLastDate"), datasetLastDate},
            {QStringLiteral("datasetDayCount"), datasetDayCount},
            {QStringLiteral("datasetRawTickCount"), datasetRawTickCount},
            {QStringLiteral("datasetGapDayCount"), datasetGapDayCount},
            {QStringLiteral("datasetStaleDayCount"), datasetStaleDayCount},
            {QStringLiteral("rawTickDerived"), isRawTickDerived()},
            {QStringLiteral("backtestEligible"), isBacktestEligible()},
            {QStringLiteral("available"), candleCount > 0 && lastTime.isValid()}};
}

} // namespace pp
