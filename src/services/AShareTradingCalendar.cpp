#include "services/AShareTradingCalendar.h"

#include <array>

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
    if (date.year() == 2026) {
        for (const ClosedRange &range : closedRanges2026()) {
            if (date >= range.first && date <= range.last) {
                result.holiday = true;
                result.reason = QString::fromUtf8(range.reason);
                return result;
            }
        }
    }
    result.tradingDay = true;
    result.reason = result.coverageKnown
        ? QStringLiteral("交易日")
        : QStringLiteral("日历未覆盖，按工作日推定");
    return result;
}

bool AShareTradingCalendar::hasAuthoritativeCoverage(int year)
{
    return year == 2026;
}

QString AShareTradingCalendar::sourceForYear(int year)
{
    return year == 2026 ? QStringLiteral("SSE-2025-45")
                        : QStringLiteral("weekday-fallback");
}

} // namespace pp
