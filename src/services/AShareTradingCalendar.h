#pragma once

#include <QDate>
#include <QString>

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
};

} // namespace pp
