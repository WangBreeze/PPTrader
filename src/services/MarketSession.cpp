#include "services/MarketSession.h"
#include "services/AShareTradingCalendar.h"

#include <QTimeZone>

namespace pp {

namespace {

bool isWeekday(const QDate &date)
{
    return date.dayOfWeek() >= 1 && date.dayOfWeek() <= 5;
}

bool inTimeRange(const QTime &time, const QTime &from, const QTime &to)
{
    return time >= from && time < to;
}

QString marketTimeText(const QDateTime &localNow)
{
    return localNow.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // 匿名命名空间

bool MarketSession::isOpen(const Instrument &instrument, const QDateTime &utcNow)
{
    if (instrument.market == Market::Crypto) {
        return true;
    }

    if (instrument.market == Market::AShare) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("Asia/Shanghai"));
        const QDate date = localNow.date();
        const QTime time = localNow.time();
        return AShareTradingCalendar::status(date).tradingDay
            && (inTimeRange(time, QTime(9, 30), QTime(11, 30))
                || inTimeRange(time, QTime(13, 0), QTime(15, 0)));
    }

    if (instrument.market == Market::HongKong) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("Asia/Hong_Kong"));
        const QDate date = localNow.date();
        const QTime time = localNow.time();
        return isWeekday(date)
            && (inTimeRange(time, QTime(9, 30), QTime(12, 0))
                || inTimeRange(time, QTime(13, 0), QTime(16, 0)));
    }

    if (instrument.market == Market::USStock) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("America/New_York"));
        const QDate date = localNow.date();
        const QTime time = localNow.time();
        return isWeekday(date) && inTimeRange(time, QTime(9, 30), QTime(16, 0));
    }

    return false;
}

QString MarketSession::statusText(const Instrument &instrument, const QDateTime &utcNow)
{
    if (instrument.symbol.isEmpty()) {
        return QStringLiteral("未选择标的");
    }

    if (instrument.market == Market::Crypto) {
        const QDateTime utcDisplay = utcNow.toUTC();
        return QStringLiteral("加密货币 7x24 交易，UTC：%1").arg(marketTimeText(utcDisplay));
    }

    if (instrument.market == Market::AShare) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("Asia/Shanghai"));
        const AShareTradingDayStatus calendar =
            AShareTradingCalendar::status(localNow.date());
        const QString calendarText = calendar.coverageKnown
            ? QStringLiteral("日历 %1").arg(calendar.source)
            : QStringLiteral("日历未覆盖，工作日推定");
        return QStringLiteral("A股%1，北京时间：%2，%3，%4，交易时段 09:30-11:30 / 13:00-15:00")
            .arg(isOpen(instrument, utcNow) ? QStringLiteral("开盘中") : QStringLiteral("休市中"),
                 marketTimeText(localNow), calendar.reason, calendarText);
    }

    if (instrument.market == Market::HongKong) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("Asia/Hong_Kong"));
        return QStringLiteral("港股%1，香港时间：%2，交易时段 09:30-12:00 / 13:00-16:00")
            .arg(isOpen(instrument, utcNow) ? QStringLiteral("开盘中") : QStringLiteral("休市中"),
                 marketTimeText(localNow));
    }

    if (instrument.market == Market::USStock) {
        const QDateTime localNow = utcNow.toTimeZone(QTimeZone("America/New_York"));
        return QStringLiteral("美股%1，纽约时间：%2，交易时段 09:30-16:00")
            .arg(isOpen(instrument, utcNow) ? QStringLiteral("开盘中") : QStringLiteral("休市中"),
                 marketTimeText(localNow));
    }

    return QStringLiteral("未知市场状态");
}

} // 命名空间 pp
