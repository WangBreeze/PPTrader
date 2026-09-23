#pragma once

#include "domain/BacktestDataQualityTypes.h"

#include <QDate>
#include <QSet>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pp {

struct RawTickAvailabilitySegment {
    QDate startDate;
    QDate endDate;
    int tradingDayCount = 0;

    bool isValid() const;
    QVariantMap toVariantMap() const;
};

struct RawTickAvailabilityPlan {
    QDate requestedStartDate;
    QDate requestedEndDate;
    bool calendarCoverageKnown = false;
    QVector<QDate> blockedDates;
    QVector<RawTickAvailabilitySegment> segments;

    std::optional<RawTickAvailabilitySegment> recommendedSegment() const;
    QVariantMap toVariantMap() const;
};

// 只规划可审计的连续交易日区间。缺口、坏日、身份冲突、休市日成交和
// 公司行动日都作为边界；明确停牌日不视为缺口。该类不修改事实，也不把
// 分钟 K 线或相邻日数据反推为 Tick。
class BacktestDataAvailabilityPlanner final {
public:
    static QSet<QDate> blockingDates(
        const BacktestDataQualityAssessment &assessment);
    static RawTickAvailabilityPlan plan(
        const QDate &startDate, const QDate &endDate,
        const QVector<BacktestDataQualityAssessment> &assessments);
    static BacktestDatasetIdentity trimIdentity(
        BacktestDatasetIdentity identity, const QDate &startDate,
        const QDate &endDate);
};

} // namespace pp

Q_DECLARE_METATYPE(pp::RawTickAvailabilitySegment)
Q_DECLARE_METATYPE(pp::RawTickAvailabilityPlan)
