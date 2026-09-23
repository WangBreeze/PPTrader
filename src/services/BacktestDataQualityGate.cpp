#include "services/BacktestDataQualityGate.h"

#include "services/AShareTradingCalendar.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace pp {

BacktestDataQualityAssessment BacktestDataQualityGate::assess(
    const BacktestDatasetIdentity &identity,
    const QDate &requestedStartDate,
    const QDate &requestedEndDate,
    const QVector<AShareInstrumentDayRecord> &instrumentDays,
    const QVector<RawTickImportDayAudit> &importAudits,
    bool corporateActionCoverageKnown,
    const QVector<QDate> &corporateActionDates,
    const BacktestDataQualityPolicy &policy)
{
    BacktestDataQualityAssessment result;
    result.corporateActionCoverageKnown = corporateActionCoverageKnown;
    result.corporateActionDates = corporateActionDates;

    if (identity.instrument.market != Market::AShare
        || identity.instrument.symbol.trimmed().isEmpty()
        || requestedStartDate > requestedEndDate
        || !requestedStartDate.isValid() || !requestedEndDate.isValid()) {
        result.messages.append(QStringLiteral("数据质量准入请求无效"));
        return result;
    }

    QSet<QDate> presentDates;
    QHash<QDate, qint64> rawCountsByDate;
    QHash<QDate, QString> rawContentSha256ByDate;
    for (const RawTickDayFingerprint &day : identity.days) {
        if (day.tradingDate >= requestedStartDate
            && day.tradingDate <= requestedEndDate && day.rawCount > 0) {
            presentDates.insert(day.tradingDate);
            rawCountsByDate.insert(day.tradingDate, day.rawCount);
            rawContentSha256ByDate.insert(day.tradingDate,
                                         day.rawContentSha256);
        }
    }
    result.observedRawTickDates = presentDates.values();
    std::sort(result.observedRawTickDates.begin(),
              result.observedRawTickDates.end());
    QHash<QDate, AShareInstrumentDayState> states;
    for (const AShareInstrumentDayRecord &record : instrumentDays) {
        if (record.instrument.symbol == identity.instrument.symbol
            && record.tradingDate >= requestedStartDate
            && record.tradingDate <= requestedEndDate && record.isValid()) {
            states.insert(record.tradingDate, record.state);
        }
    }

    result.calendarCoverageKnown = true;
    for (QDate date = requestedStartDate; date <= requestedEndDate;
         date = date.addDays(1)) {
        const AShareTradingDayStatus marketDay = AShareTradingCalendar::status(date);
        if (!marketDay.coverageKnown) result.calendarCoverageKnown = false;
        const bool present = presentDates.contains(date);
        if (!marketDay.tradingDay) {
            if (marketDay.coverageKnown && present) {
                result.unexpectedNonTradingDates.append(date);
            }
            continue;
        }
        const AShareInstrumentDayState state = states.value(
            date, AShareInstrumentDayState::Unknown);
        if (present && state == AShareInstrumentDayState::Suspended) {
            result.contradictorySuspensionDates.append(date);
        } else if (!present && state == AShareInstrumentDayState::Suspended) {
            result.explainedNoTradeDates.append(date);
        } else if (!present) {
            result.missingTradingDates.append(date);
        }
    }

    QHash<QDate, RawTickImportDayAudit> auditsByDate;
    for (const RawTickImportDayAudit &audit : importAudits) {
        if (audit.tradingDate >= requestedStartDate
            && audit.tradingDate <= requestedEndDate && audit.isValid()
            && audit.rawContentSha256
                == rawContentSha256ByDate.value(audit.tradingDate)
            && !auditsByDate.contains(audit.tradingDate)) {
            auditsByDate.insert(audit.tradingDate, audit);
        }
    }
    for (const QDate &date : presentDates) {
        const auto found = auditsByDate.constFind(date);
        if (found == auditsByDate.constEnd()) {
            result.unknownImportAuditDates.append(date);
            continue;
        }
        ++result.auditedDayCount;
        result.validRecords += found->validRecords;
        result.skippedRecords += found->skippedRecords;
        result.outOfOrderRecords += found->outOfOrderRecords;
        if (found->outOfOrderRecords > 0) {
            result.outOfOrderAuditDates.append(date);
        }
        const QString auditStatus = found->qualityStatus.trimmed().toLower();
        if (auditStatus != QStringLiteral("healthy")
            && auditStatus != QStringLiteral("warning")) {
            result.failedAuditDates.append(date);
        }
        const qint64 total = found->validRecords + found->skippedRecords;
        const double ratio = total > 0
            ? static_cast<double>(found->skippedRecords) / total : 0.0;
        if (ratio > policy.maximumSkippedRecordRatio) {
            result.excessiveSkippedDates.append(date);
        }
        if (found->validRecords != rawCountsByDate.value(date)) {
            result.auditCountMismatchDates.append(date);
        }
    }
    std::sort(result.unknownImportAuditDates.begin(),
              result.unknownImportAuditDates.end());
    std::sort(result.outOfOrderAuditDates.begin(),
              result.outOfOrderAuditDates.end());
    std::sort(result.excessiveSkippedDates.begin(),
              result.excessiveSkippedDates.end());
    std::sort(result.failedAuditDates.begin(), result.failedAuditDates.end());
    std::sort(result.auditCountMismatchDates.begin(),
              result.auditCountMismatchDates.end());
    const qint64 auditedTotal = result.validRecords + result.skippedRecords;
    result.skippedRecordRatio = auditedTotal > 0
        ? static_cast<double>(result.skippedRecords) / auditedTotal : 0.0;

    QVector<QDate> relevantCorporateActionDates;
    if (corporateActionCoverageKnown) {
        for (const QDate &date : corporateActionDates) {
            if (date >= requestedStartDate && date <= requestedEndDate
                && !relevantCorporateActionDates.contains(date)) {
                relevantCorporateActionDates.append(date);
            }
        }
        std::sort(relevantCorporateActionDates.begin(),
                  relevantCorporateActionDates.end());
    }
    result.corporateActionDates = relevantCorporateActionDates;

    if (!result.missingTradingDates.isEmpty()) {
        result.messages.append(QStringLiteral("存在 %1 个未解释的原始 Tick 事实缺口")
                                   .arg(result.missingTradingDates.size()));
    }
    if (!result.contradictorySuspensionDates.isEmpty()) {
        QStringList dates;
        for (const QDate &date : result.contradictorySuspensionDates) {
            dates.append(date.toString(Qt::ISODate));
        }
        result.messages.append(QStringLiteral("停牌事实与原始成交同时存在：%1")
                                   .arg(dates.join(QStringLiteral(", "))));
    }
    if (!result.explainedNoTradeDates.isEmpty()) {
        result.messages.append(QStringLiteral("%1 个交易日由明确停牌事实解释为无成交")
                                   .arg(result.explainedNoTradeDates.size()));
    }
    if (!result.unexpectedNonTradingDates.isEmpty()) {
        QStringList dates;
        for (const QDate &date : result.unexpectedNonTradingDates) {
            dates.append(date.toString(Qt::ISODate));
        }
        result.messages.append(QStringLiteral("休市日存在原始成交：%1")
                                   .arg(dates.join(QStringLiteral(", "))));
    }
    if (!result.calendarCoverageKnown) {
        result.messages.append(QStringLiteral("请求范围缺少完整权威交易日历覆盖"));
    }
    if (!result.unknownImportAuditDates.isEmpty()) {
        result.messages.append(QStringLiteral("%1 个原始 Tick 事实日缺少可关联的导入质量审计")
                                   .arg(result.unknownImportAuditDates.size()));
    }
    if (!result.auditCountMismatchDates.isEmpty()) {
        result.messages.append(QStringLiteral("%1 个交易日的导入审计记录数与当前原始事实不一致")
                                   .arg(result.auditCountMismatchDates.size()));
    }
    if (!result.outOfOrderAuditDates.isEmpty()) {
        result.messages.append(QStringLiteral(
            "%1 个交易日存在 %2 条原始文件逆序记录；普通回测仅警告，自动完整区间不推荐")
                                   .arg(result.outOfOrderAuditDates.size())
                                   .arg(result.outOfOrderRecords));
    }
    if (result.skippedRecords > 0) {
        result.messages.append(QStringLiteral("导入审计记录 %1 条坏行，占已审计记录 %2%")
                                   .arg(result.skippedRecords)
                                   .arg(result.skippedRecordRatio * 100.0, 0, 'f', 3));
    }
    if (!result.excessiveSkippedDates.isEmpty()) {
        result.messages.append(QStringLiteral("%1 个交易日坏行率超过 %2% 准入上限")
                                   .arg(result.excessiveSkippedDates.size())
                                   .arg(policy.maximumSkippedRecordRatio * 100.0,
                                        0, 'f', 2));
    }
    if (!result.failedAuditDates.isEmpty()) {
        result.messages.append(QStringLiteral("%1 个交易日的导入审计状态为 error")
                                   .arg(result.failedAuditDates.size()));
    }
    if (!result.corporateActionCoverageKnown) {
        result.messages.append(QStringLiteral("公司行动覆盖未知，未完成复权一致性核验"));
    } else if (!result.corporateActionDates.isEmpty()) {
        result.messages.append(QStringLiteral("请求范围存在 %1 个公司行动日；未复权原始 Tick 不准入")
                                   .arg(result.corporateActionDates.size()));
    }

    const bool rejected = !result.missingTradingDates.isEmpty()
        || !result.contradictorySuspensionDates.isEmpty()
        || !result.excessiveSkippedDates.isEmpty()
        || !result.failedAuditDates.isEmpty()
        || !result.auditCountMismatchDates.isEmpty()
        || !result.unexpectedNonTradingDates.isEmpty()
        || !result.corporateActionDates.isEmpty();
    result.eligible = !rejected;
    result.status = rejected ? BacktestDataQualityStatus::Reject
        : result.messages.isEmpty() ? BacktestDataQualityStatus::Pass
                                    : BacktestDataQualityStatus::Warning;
    return result;
}

} // namespace pp
