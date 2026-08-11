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

bool BacktestDatasetIdentity::fromJson(const QString &json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonArray dayArray = root.value(QStringLiteral("days")).toArray();
    if (dayArray.isEmpty()) return false;

    BacktestDatasetIdentity parsed;
    parsed.instrument.symbol = root.value(QStringLiteral("symbol")).toString();
    parsed.instrument.name = root.value(QStringLiteral("name")).toString();
    parsed.instrument.venue = root.value(QStringLiteral("venue")).toString();
    const QString market = root.value(QStringLiteral("market")).toString();
    parsed.instrument.market = market == QStringLiteral("A股") ? Market::AShare
        : market == QStringLiteral("美股") ? Market::USStock
        : market == QStringLiteral("加密货币") ? Market::Crypto
        : market == QStringLiteral("港股") ? Market::HongKong : Market::AShare;
    parsed.periodSeconds = root.value(QStringLiteral("periodSeconds")).toInt();
    parsed.source = root.value(QStringLiteral("source")).toString();
    parsed.aggregatorVersion = root.value(QStringLiteral("aggregatorVersion")).toInt();
    parsed.firstTradingDate = QDate::fromString(
        root.value(QStringLiteral("firstTradingDate")).toString(), Qt::ISODate);
    parsed.lastTradingDate = QDate::fromString(
        root.value(QStringLiteral("lastTradingDate")).toString(), Qt::ISODate);
    parsed.expectedTradingDays = root.value(QStringLiteral("expectedTradingDays")).toInt();
    parsed.gapDayCount = root.value(QStringLiteral("gapDayCount")).toInt();
    parsed.staleDayCount = root.value(QStringLiteral("staleDayCount")).toInt();
    parsed.calendarCoverageKnown =
        root.value(QStringLiteral("calendarCoverageKnown")).toBool();
    parsed.calendarSource = root.value(QStringLiteral("calendarSource")).toString();
    parsed.calendarNote = root.value(QStringLiteral("calendarNote")).toString();
    for (const QJsonValue &value : dayArray) {
        const QJsonObject dayObject = value.toObject();
        RawTickDayFingerprint day;
        day.tradingDate = QDate::fromString(
            dayObject.value(QStringLiteral("tradingDate")).toString(), Qt::ISODate);
        day.rawCount = dayObject.value(QStringLiteral("rawCount")).toVariant().toLongLong();
        day.rawMaximumId =
            dayObject.value(QStringLiteral("rawMaximumId")).toVariant().toLongLong();
        day.barCount = dayObject.value(QStringLiteral("barCount")).toInt();
        day.cacheVersion = dayObject.value(QStringLiteral("cacheVersion")).toInt();
        if (!day.tradingDate.isValid()) return false;
        parsed.days.append(day);
    }
    if (parsed.isEmpty()) return false;
    parsed.datasetVersion = root.value(QStringLiteral("datasetVersion")).toString();
    if (!parsed.datasetVersion.startsWith(QStringLiteral("sha256:"))) return false;
    *this = parsed;
    return true;
}

} // namespace pp
