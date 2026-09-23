#include "services/AShareTradingCalendar.h"

#include <array>
#include <set>
#include <algorithm>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMap>
#include <QSet>

namespace pp {

namespace {

struct ClosedRange {
    QDate first;
    QDate last;
    const char *reason;
};

const std::array<ClosedRange, 7> &closedRanges2026()
{
    // 上海证券交易所公告〔2025〕45号；沪深 A 股在这些法定节假日同步休市。
    static const std::array<ClosedRange, 7> ranges{{
        {QDate(2026, 1, 1), QDate(2026, 1, 3), "元旦休市"},
        {QDate(2026, 2, 15), QDate(2026, 2, 23), "春节休市"},
        {QDate(2026, 4, 4), QDate(2026, 4, 6), "清明节休市"},
        {QDate(2026, 5, 1), QDate(2026, 5, 5), "劳动节休市"},
        {QDate(2026, 6, 19), QDate(2026, 6, 21), "端午节休市"},
        {QDate(2026, 9, 25), QDate(2026, 9, 27), "中秋节休市"},
        {QDate(2026, 10, 1), QDate(2026, 10, 7), "国庆节休市"}
    }};
    return ranges;
}

const std::array<ClosedRange, 7> &closedRanges2024()
{
    // 上证公告〔2023〕47号。
    static const std::array<ClosedRange, 7> ranges{{
        {QDate(2024, 1, 1), QDate(2024, 1, 1), "元旦休市"},
        {QDate(2024, 2, 9), QDate(2024, 2, 17), "春节休市"},
        {QDate(2024, 4, 4), QDate(2024, 4, 6), "清明节休市"},
        {QDate(2024, 5, 1), QDate(2024, 5, 5), "劳动节休市"},
        {QDate(2024, 6, 10), QDate(2024, 6, 10), "端午节休市"},
        {QDate(2024, 9, 15), QDate(2024, 9, 17), "中秋节休市"},
        {QDate(2024, 10, 1), QDate(2024, 10, 7), "国庆节休市"}
    }};
    return ranges;
}

const std::array<ClosedRange, 6> &closedRanges2025()
{
    // 上证公告〔2024〕38号。
    static const std::array<ClosedRange, 6> ranges{{
        {QDate(2025, 1, 1), QDate(2025, 1, 1), "元旦休市"},
        {QDate(2025, 1, 28), QDate(2025, 2, 4), "春节休市"},
        {QDate(2025, 4, 4), QDate(2025, 4, 6), "清明节休市"},
        {QDate(2025, 5, 1), QDate(2025, 5, 5), "劳动节休市"},
        {QDate(2025, 5, 31), QDate(2025, 6, 2), "端午节休市"},
        {QDate(2025, 10, 1), QDate(2025, 10, 8), "国庆节、中秋节休市"}
    }};
    return ranges;
}

template<std::size_t Size>
bool applyHoliday(const QDate &date,
                  const std::array<ClosedRange, Size> &ranges,
                  AShareTradingDayStatus *result)
{
    for (const ClosedRange &range : ranges) {
        if (date >= range.first && date <= range.last) {
            result->holiday = true;
            result->reason = QString::fromUtf8(range.reason);
            return true;
        }
    }
    return false;
}

} // namespace

AShareTradingDayStatus AShareTradingCalendar::status(const QDate &date)
{
    AShareTradingDayStatus result;
    if (!date.isValid()) {
        result.reason = QStringLiteral("日期无效");
        return result;
    }
    result.coverageKnown = hasAuthoritativeCoverage(date.year());
    result.source = sourceForYear(date.year());
    result.weekend = date.dayOfWeek() == Qt::Saturday
        || date.dayOfWeek() == Qt::Sunday;
    if (result.weekend) {
        result.reason = QStringLiteral("周末休市");
        return result;
    }
    if (date.year() == 2024
        && applyHoliday(date, closedRanges2024(), &result)) return result;
    if (date.year() == 2025
        && applyHoliday(date, closedRanges2025(), &result)) return result;
    if (date.year() == 2026
        && applyHoliday(date, closedRanges2026(), &result)) return result;
    result.tradingDay = true;
    result.reason = result.coverageKnown
        ? QStringLiteral("交易日")
        : QStringLiteral("日历未覆盖，按工作日推定");
    return result;
}

bool AShareTradingCalendar::hasAuthoritativeCoverage(int year)
{
    return year >= 2024 && year <= 2026;
}

QString AShareTradingCalendar::sourceForYear(int year)
{
    switch (year) {
    case 2024: return QStringLiteral("SSE-2023-47");
    case 2025: return QStringLiteral("SSE-2024-38");
    case 2026: return QStringLiteral("SSE-2025-45");
    default: return QStringLiteral("weekday-fallback");
    }
}

QVector<QDate> AShareTradingCalendar::tradingDays(
    const QDate &firstDate, const QDate &lastDate)
{
    QVector<QDate> result;
    if (!firstDate.isValid() || !lastDate.isValid() || firstDate > lastDate)
        return result;
    for (QDate date = firstDate; date <= lastDate; date = date.addDays(1)) {
        if (status(date).tradingDay) result.append(date);
    }
    return result;
}

AShareCalendarSnapshot AShareTradingCalendar::snapshot(
    const QDate &firstDate, const QDate &lastDate,
    const QStringList &symbols,
    const QVector<AShareInstrumentDayRecord> &instrumentDays)
{
    AShareCalendarSnapshot result;
    result.firstDate = firstDate;
    result.lastDate = lastDate;
    if (!firstDate.isValid() || !lastDate.isValid() || firstDate > lastDate)
        return result;

    std::set<QString> marketSources;
    result.marketCoverageComplete = true;
    for (QDate date = firstDate; date <= lastDate; date = date.addDays(1)) {
        const AShareTradingDayStatus day = status(date);
        if (!day.coverageKnown) result.marketCoverageComplete = false;
        if (!day.source.isEmpty()) marketSources.insert(day.source);
        if (day.tradingDay) result.tradingDays.append(date);
    }
    for (const QString &source : marketSources) {
        if (!result.marketSource.isEmpty()) result.marketSource += QStringLiteral("/");
        result.marketSource += source;
    }

    QStringList normalizedSymbols;
    for (const QString &symbol : symbols) {
        const QString normalized = symbol.trimmed();
        if (!normalized.isEmpty() && !normalizedSymbols.contains(normalized))
            normalizedSymbols.append(normalized);
    }
    std::sort(normalizedSymbols.begin(), normalizedSymbols.end());

    QVector<AShareInstrumentDayRecord> sortedRecords = instrumentDays;
    std::sort(sortedRecords.begin(), sortedRecords.end(),
              [](const AShareInstrumentDayRecord &left,
                 const AShareInstrumentDayRecord &right) {
        if (left.instrument.symbol != right.instrument.symbol)
            return left.instrument.symbol < right.instrument.symbol;
        if (left.tradingDate != right.tradingDate)
            return left.tradingDate < right.tradingDate;
        if (left.source != right.source) return left.source < right.source;
        return left.sourceVersion < right.sourceVersion;
    });
    QMap<QString, AShareInstrumentDayRecord> recordsByKey;
    const QSet<QDate> tradingDaySet(result.tradingDays.cbegin(),
                                    result.tradingDays.cend());
    for (const AShareInstrumentDayRecord &record : sortedRecords) {
        if (!record.isValid()
            || !normalizedSymbols.contains(record.instrument.symbol)
            || !tradingDaySet.contains(record.tradingDate)) continue;
        recordsByKey.insert(record.instrument.symbol + QLatin1Char('|')
                                + record.tradingDate.toString(Qt::ISODate),
                            record);
    }

    const int expectedStatusCount = normalizedSymbols.size()
        * result.tradingDays.size();
    for (const QString &symbol : normalizedSymbols) {
        for (const QDate &date : result.tradingDays) {
            const QString key = symbol + QLatin1Char('|')
                + date.toString(Qt::ISODate);
            const auto found = recordsByKey.constFind(key);
            if (found == recordsByKey.constEnd()) {
                ++result.unknownInstrumentDays;
                continue;
            }
            result.instrumentDays.append(found.value());
            if (found->state == AShareInstrumentDayState::Suspended)
                ++result.suspendedInstrumentDays;
        }
    }
    result.instrumentCoverageComplete = expectedStatusCount > 0
        && result.instrumentDays.size() == expectedStatusCount;

    const QByteArray canonical = QJsonDocument::fromVariant(
        result.toVariantMap()).toJson(QJsonDocument::Compact);
    result.snapshotVersion = QStringLiteral("sha256:")
        + QString::fromLatin1(QCryptographicHash::hash(
            canonical, QCryptographicHash::Sha256).toHex());
    return result;
}

void AShareTradingCalendar::applyDatasetCoverage(BacktestDatasetIdentity &identity)
{
    identity.expectedTradingDays = 0;
    identity.gapDayCount = 0;
    identity.calendarCoverageKnown = true;
    identity.calendarSource.clear();
    identity.calendarNote.clear();

    if (!identity.firstTradingDate.isValid() || !identity.lastTradingDate.isValid()
        || identity.firstTradingDate > identity.lastTradingDate) {
        identity.calendarCoverageKnown = false;
        identity.calendarNote = QStringLiteral("无有效交易日范围");
        return;
    }

    QSet<QDate> present;
    for (const RawTickDayFingerprint &day : identity.days) {
        present.insert(day.tradingDate);
    }

    std::set<QString> sources;
    QDate cursor = identity.firstTradingDate;
    while (cursor <= identity.lastTradingDate) {
        const AShareTradingDayStatus status = AShareTradingCalendar::status(cursor);
        if (!status.coverageKnown) identity.calendarCoverageKnown = false;
        if (!status.source.isEmpty()) sources.insert(status.source);
        if (status.tradingDay) {
            ++identity.expectedTradingDays;
            if (!present.contains(cursor)) ++identity.gapDayCount;
        }
        cursor = cursor.addDays(1);
    }
    for (const QString &source : sources) {
        if (!identity.calendarSource.isEmpty()) identity.calendarSource += QStringLiteral("/");
        identity.calendarSource += source;
    }
    if (!identity.calendarCoverageKnown) {
        identity.calendarNote = QStringLiteral("日历未覆盖年份，缺口按工作日推定");
    }
}

} // namespace pp
