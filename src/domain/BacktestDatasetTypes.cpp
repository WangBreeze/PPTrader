#include "domain/BacktestDatasetTypes.h"
#include "domain/LocalHistoryTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace pp {

int BacktestDatasetIdentity::rawTickTotal() const
{
    qint64 total = 0;
    for (const RawTickDayFingerprint &day : days) total += day.rawCount;
    return static_cast<int>(total);
}

int BacktestDatasetIdentity::derivedBarTotal() const
{
    int total = 0;
    for (const RawTickDayFingerprint &day : days) total += day.barCount;
    return total;
}

bool BacktestDatasetIdentity::isEmpty() const
{
    return days.isEmpty() || instrument.symbol.isEmpty() || periodSeconds <= 0;
}

bool BacktestDatasetIdentity::hasStaleCache() const
{
    return staleDayCount > 0;
}

QVariantMap BacktestDatasetIdentity::toVariantMap() const
{
    return {{QStringLiteral("symbol"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("market"), marketName(instrument.market)},
            {QStringLiteral("venue"), instrument.venue},
            {QStringLiteral("frequency"), localHistoryFrequency(periodSeconds)},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("source"), source},
            {QStringLiteral("aggregatorVersion"), aggregatorVersion},
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("firstTradingDate"), firstTradingDate},
            {QStringLiteral("lastTradingDate"), lastTradingDate},
            {QStringLiteral("rawTickCount"), rawTickTotal()},
            {QStringLiteral("derivedBarCount"), derivedBarTotal()},
            {QStringLiteral("dayCount"), days.size()},
            {QStringLiteral("expectedTradingDays"), expectedTradingDays},
            {QStringLiteral("gapDayCount"), gapDayCount},
            {QStringLiteral("staleDayCount"), staleDayCount},
            {QStringLiteral("calendarCoverageKnown"), calendarCoverageKnown},
            {QStringLiteral("calendarSource"), calendarSource},
            {QStringLiteral("calendarNote"), calendarNote}};
}

QString BacktestDatasetIdentity::toJson() const
{
    QJsonArray dayArray;
    for (const RawTickDayFingerprint &day : days) {
        dayArray.append(QJsonObject{
            {QStringLiteral("tradingDate"), day.tradingDate.toString(Qt::ISODate)},
            {QStringLiteral("rawCount"), day.rawCount},
            {QStringLiteral("rawMaximumId"), day.rawMaximumId},
            {QStringLiteral("barCount"), day.barCount},
            {QStringLiteral("cacheVersion"), day.cacheVersion}});
    }
    const QJsonObject root{
        {QStringLiteral("symbol"), instrument.symbol},
        {QStringLiteral("name"), instrument.name},
        {QStringLiteral("market"), marketName(instrument.market)},
        {QStringLiteral("venue"), instrument.venue},
        {QStringLiteral("periodSeconds"), periodSeconds},
        {QStringLiteral("source"), source},
        {QStringLiteral("aggregatorVersion"), aggregatorVersion},
        {QStringLiteral("datasetVersion"), datasetVersion},
        {QStringLiteral("firstTradingDate"), firstTradingDate.toString(Qt::ISODate)},
        {QStringLiteral("lastTradingDate"), lastTradingDate.toString(Qt::ISODate)},
        {QStringLiteral("expectedTradingDays"), expectedTradingDays},
        {QStringLiteral("gapDayCount"), gapDayCount},
        {QStringLiteral("staleDayCount"), staleDayCount},
        {QStringLiteral("calendarCoverageKnown"), calendarCoverageKnown},
        {QStringLiteral("calendarSource"), calendarSource},
        {QStringLiteral("calendarNote"), calendarNote},
        {QStringLiteral("days"), dayArray}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace pp
