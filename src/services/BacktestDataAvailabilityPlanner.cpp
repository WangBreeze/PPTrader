#include "services/BacktestDataAvailabilityPlanner.h"

#include "services/AShareTradingCalendar.h"
#include "services/BacktestDataVersion.h"

#include <algorithm>
#include <utility>

namespace pp {

bool RawTickAvailabilitySegment::isValid() const
{
    return startDate.isValid() && endDate.isValid() && startDate <= endDate
        && tradingDayCount > 0;
}

QVariantMap RawTickAvailabilitySegment::toVariantMap() const
{
    return {{QStringLiteral("startDate"), startDate.toString(Qt::ISODate)},
            {QStringLiteral("endDate"), endDate.toString(Qt::ISODate)},
            {QStringLiteral("tradingDayCount"), tradingDayCount}};
}

std::optional<RawTickAvailabilitySegment>
RawTickAvailabilityPlan::recommendedSegment() const
{
    if (!calendarCoverageKnown || segments.isEmpty()) return std::nullopt;
    return *std::max_element(
        segments.cbegin(), segments.cend(),
        [](const RawTickAvailabilitySegment &left,
           const RawTickAvailabilitySegment &right) {
            if (left.tradingDayCount != right.tradingDayCount) {
                return left.tradingDayCount < right.tradingDayCount;
            }
            // 同长度优先较新的完整区间，避免旧损坏段成为默认建议。
            return left.startDate < right.startDate;
        });
}

QVariantMap RawTickAvailabilityPlan::toVariantMap() const
{
    QVariantList blocked;
    blocked.reserve(blockedDates.size());
    for (const QDate &date : blockedDates) {
        blocked.append(date.toString(Qt::ISODate));
    }
    QVariantList available;
    available.reserve(segments.size());
    for (const RawTickAvailabilitySegment &segment : segments) {
        available.append(segment.toVariantMap());
    }
    const auto recommended = recommendedSegment();
    return {{QStringLiteral("requestedStartDate"),
             requestedStartDate.toString(Qt::ISODate)},
            {QStringLiteral("requestedEndDate"),
             requestedEndDate.toString(Qt::ISODate)},
            {QStringLiteral("calendarCoverageKnown"), calendarCoverageKnown},
            {QStringLiteral("blockedDates"), blocked},
            {QStringLiteral("segments"), available},
            {QStringLiteral("segmentCount"), available.size()},
            {QStringLiteral("hasRecommendation"), recommended.has_value()},
            {QStringLiteral("recommendedStartDate"),
             recommended ? recommended->startDate.toString(Qt::ISODate)
                         : QString{}},
            {QStringLiteral("recommendedEndDate"),
             recommended ? recommended->endDate.toString(Qt::ISODate)
                         : QString{}},
            {QStringLiteral("recommendedTradingDayCount"),
             recommended ? recommended->tradingDayCount : 0}};
}

QSet<QDate> BacktestDataAvailabilityPlanner::blockingDates(
    const BacktestDataQualityAssessment &assessment)
{
    QSet<QDate> result;
    const auto append = [&result](const QVector<QDate> &dates) {
        for (const QDate &date : dates) {
            if (date.isValid()) result.insert(date);
        }
    };
    append(assessment.missingTradingDates);
    append(assessment.unexpectedNonTradingDates);
    append(assessment.contradictorySuspensionDates);
    // 普通严格回测仍允许“审计未知”以 warning 执行；但自动推荐和分段研究
    // 只选择可审计区间，不能把未知误写成“完整干净”。
    append(assessment.unknownImportAuditDates);
    append(assessment.outOfOrderAuditDates);
    append(assessment.excessiveSkippedDates);
    append(assessment.failedAuditDates);
    append(assessment.auditCountMismatchDates);
    append(assessment.corporateActionDates);
    return result;
}

RawTickAvailabilityPlan BacktestDataAvailabilityPlanner::plan(
    const QDate &startDate, const QDate &endDate,
    const QVector<BacktestDataQualityAssessment> &assessments)
{
    RawTickAvailabilityPlan result;
    result.requestedStartDate = startDate;
    result.requestedEndDate = endDate;
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate
        || assessments.isEmpty()) {
        return result;
    }

    result.calendarCoverageKnown = true;
    QSet<QDate> blocked;
    for (const BacktestDataQualityAssessment &assessment : assessments) {
        result.calendarCoverageKnown = result.calendarCoverageKnown
            && assessment.calendarCoverageKnown;
        blocked.unite(blockingDates(assessment));
    }
    result.blockedDates = blocked.values();
    std::sort(result.blockedDates.begin(), result.blockedDates.end());

    RawTickAvailabilitySegment current;
    const auto flush = [&result, &current]() {
        if (current.isValid()) result.segments.append(current);
        current = {};
    };
    for (QDate date = startDate; date <= endDate; date = date.addDays(1)) {
        if (blocked.contains(date)) {
            flush();
            continue;
        }
        const AShareTradingDayStatus status = AShareTradingCalendar::status(date);
        result.calendarCoverageKnown = result.calendarCoverageKnown
            && status.coverageKnown;
        if (!status.tradingDay) continue;
        if (!current.startDate.isValid()) current.startDate = date;
        current.endDate = date;
        ++current.tradingDayCount;
    }
    flush();
    result.segments.erase(
        std::remove_if(
            result.segments.begin(), result.segments.end(),
            [&assessments](const RawTickAvailabilitySegment &segment) {
                for (const BacktestDataQualityAssessment &assessment
                     : assessments) {
                    const bool hasExecutableTick = std::any_of(
                        assessment.observedRawTickDates.cbegin(),
                        assessment.observedRawTickDates.cend(),
                        [&segment](const QDate &date) {
                            return date >= segment.startDate
                                && date <= segment.endDate;
                        });
                    if (!hasExecutableTick) return true;
                }
                return false;
            }),
        result.segments.end());
    return result;
}

BacktestDatasetIdentity BacktestDataAvailabilityPlanner::trimIdentity(
    BacktestDatasetIdentity identity, const QDate &startDate,
    const QDate &endDate)
{
    std::sort(identity.days.begin(), identity.days.end(),
              [](const RawTickDayFingerprint &left,
                 const RawTickDayFingerprint &right) {
                  return left.tradingDate < right.tradingDate;
              });
    identity.days.erase(std::remove_if(
        identity.days.begin(), identity.days.end(),
        [&](const RawTickDayFingerprint &day) {
            return day.tradingDate < startDate || day.tradingDate > endDate;
        }), identity.days.end());
    identity.staleDayCount = 0;
    if (identity.periodSeconds > 0) {
        for (const RawTickDayFingerprint &day : identity.days) {
            if (!day.cacheFresh) ++identity.staleDayCount;
        }
    }
    identity.firstTradingDate = identity.days.isEmpty()
        ? QDate{} : identity.days.first().tradingDate;
    identity.lastTradingDate = identity.days.isEmpty()
        ? QDate{} : identity.days.last().tradingDate;
    identity.datasetVersion = identity.days.isEmpty()
        ? QString{} : computeBacktestDatasetVersion(identity);
    AShareTradingCalendar::applyDatasetCoverage(identity);
    return identity;
}

} // namespace pp
