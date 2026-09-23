#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/AShareCalendarTypes.h"

#include <QDate>
#include <QString>
#include <QStringList>

namespace pp {

struct AShareTradingDayStatus {
    bool tradingDay = false;
    bool coverageKnown = false;
    bool weekend = false;
    bool holiday = false;
    QString reason;
    QString source;
};

class AShareTradingCalendar final {
public:
    static AShareTradingDayStatus status(const QDate &date);
    static bool hasAuthoritativeCoverage(int year);
    static QString sourceForYear(int year);
    static QVector<QDate> tradingDays(const QDate &firstDate,
                                      const QDate &lastDate);
    static AShareCalendarSnapshot snapshot(
        const QDate &firstDate, const QDate &lastDate,
        const QStringList &symbols,
        const QVector<AShareInstrumentDayRecord> &instrumentDays = {});

    // 按 identity 的事实日期范围统计期望交易日与缺口，并标注日历来源。
    // 缺口以交易日历为准：停牌等尚未有事实来源的日也会计为缺口。
    static void applyDatasetCoverage(BacktestDatasetIdentity &identity);
};

} // namespace pp
