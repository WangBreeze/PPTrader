#include "domain/BacktestDataQualityTypes.h"

namespace pp {

QVariantMap BacktestDataQualityAssessment::toVariantMap() const
{
    QVariantList observed;
    for (const QDate &date : observedRawTickDates) observed.append(date);
    QVariantList missing;
    for (const QDate &date : missingTradingDates) missing.append(date);
    QVariantList explained;
    for (const QDate &date : explainedNoTradeDates) explained.append(date);
    QVariantList unexpectedNonTrading;
    for (const QDate &date : unexpectedNonTradingDates) {
        unexpectedNonTrading.append(date);
    }
    QVariantList contradictorySuspensions;
    for (const QDate &date : contradictorySuspensionDates) {
        contradictorySuspensions.append(date);
    }
    QVariantList unknownAudits;
    for (const QDate &date : unknownImportAuditDates) unknownAudits.append(date);
    QVariantList outOfOrderAudits;
    for (const QDate &date : outOfOrderAuditDates) {
        outOfOrderAudits.append(date);
    }
    QVariantList excessiveSkipped;
    for (const QDate &date : excessiveSkippedDates) {
        excessiveSkipped.append(date);
    }
    QVariantList failedAudits;
    for (const QDate &date : failedAuditDates) failedAudits.append(date);
    QVariantList auditCountMismatches;
    for (const QDate &date : auditCountMismatchDates) {
        auditCountMismatches.append(date);
    }
    QVariantList corporateActions;
    for (const QDate &date : corporateActionDates) corporateActions.append(date);
    return {{QStringLiteral("status"), backtestDataQualityStatusCode(status)},
            {QStringLiteral("eligible"), eligible},
            {QStringLiteral("observedRawTickDates"), observed},
            {QStringLiteral("missingTradingDates"), missing},
            {QStringLiteral("explainedNoTradeDates"), explained},
            {QStringLiteral("unexpectedNonTradingDates"), unexpectedNonTrading},
            {QStringLiteral("contradictorySuspensionDates"),
             contradictorySuspensions},
            {QStringLiteral("unknownImportAuditDates"), unknownAudits},
            {QStringLiteral("outOfOrderAuditDates"), outOfOrderAudits},
            {QStringLiteral("excessiveSkippedDates"), excessiveSkipped},
            {QStringLiteral("failedAuditDates"), failedAudits},
            {QStringLiteral("auditCountMismatchDates"), auditCountMismatches},
            {QStringLiteral("auditedDayCount"), auditedDayCount},
            {QStringLiteral("validRecords"), validRecords},
            {QStringLiteral("skippedRecords"), skippedRecords},
            {QStringLiteral("outOfOrderRecords"), outOfOrderRecords},
            {QStringLiteral("skippedRecordRatio"), skippedRecordRatio},
            {QStringLiteral("calendarCoverageKnown"), calendarCoverageKnown},
            {QStringLiteral("corporateActionCoverageKnown"),
             corporateActionCoverageKnown},
            {QStringLiteral("corporateActionDates"), corporateActions},
            {QStringLiteral("messages"), messages}};
}

} // namespace pp
