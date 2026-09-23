#include "services/BacktestDataQualityGate.h"
#include "services/BacktestDataAvailabilityPlanner.h"

#include <QtTest>

using namespace pp;

namespace {

Instrument instrument()
{
    return {QStringLiteral("600000"), QStringLiteral("测试"),
            Market::AShare, QStringLiteral("上交所")};
}

BacktestDatasetIdentity identityFor(const QVector<QDate> &dates)
{
    BacktestDatasetIdentity identity;
    identity.instrument = instrument();
    identity.periodSeconds = 0;
    identity.source = QStringLiteral("raw_trade_ticks_v1");
    for (const QDate &date : dates) {
        RawTickDayFingerprint day;
        day.tradingDate = date;
        day.rawCount = 100;
        day.rawMaximumId = 100;
        day.rawContentSha256 = QStringLiteral("sha256:") + QString(64, QLatin1Char('a'));
        day.cacheFresh = true;
        identity.days.append(day);
    }
    return identity;
}

AShareInstrumentDayRecord suspended(const QDate &date)
{
    AShareInstrumentDayRecord record;
    record.instrument = instrument();
    record.tradingDate = date;
    record.state = AShareInstrumentDayState::Suspended;
    record.reason = QStringLiteral("公告停牌");
    record.source = QStringLiteral("fixture");
    record.sourceVersion = QStringLiteral("v1");
    return record;
}

RawTickImportDayAudit auditFor(const QDate &date, qint64 validRecords,
                               qint64 skippedRecords = 0,
                               const QString &qualityStatus = QStringLiteral("healthy"),
                               QChar digestCharacter = QLatin1Char('a'))
{
    RawTickImportDayAudit audit;
    audit.tradingDate = date;
    audit.rawContentSha256 = QStringLiteral("sha256:")
        + QString(64, digestCharacter);
    audit.auditVersion = 1;
    audit.linesRead = validRecords + skippedRecords;
    audit.validRecords = validRecords;
    audit.skippedRecords = skippedRecords;
    audit.qualityStatus = qualityStatus;
    audit.source = QStringLiteral("fixture");
    return audit;
}

} // namespace

class BacktestDataQualityGateTest final : public QObject {
    Q_OBJECT

private slots:
    void missingRequestedTradingDayIsRejected();
    void explicitSuspensionExplainsNoTradeDay();
    void marketHolidayIsNotADataGap();
    void marketHolidayWithTradesIsRejected();
    void suspensionFactCannotCoexistWithTrades();
    void excessiveSkippedRowsAreRejected();
    void staleContentAuditIsNotTrusted();
    void smallSkippedRatioIsWarningOnly();
    void repairDatesAreStructuredAndSorted();
    void auditCountMismatchIsRejected();
    void knownCorporateActionRejectsUnadjustedRawTicks();
    void completeEvidencePasses();
    void availabilityPlannerRecommendsLongestNewestSegment();
    void explicitSuspensionDoesNotSplitAvailability();
    void unknownAuditDateIsNotRecommendedAsAuditable();
    void outOfOrderAuditWarnsAndBlocksCleanRecommendation();
    void suspensionOnlyRangeIsNotRecommendedAsExecutable();
};

void BacktestDataQualityGateTest::missingRequestedTradingDayIsRejected()
{
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 6)}),
        QDate(2026, 1, 5), QDate(2026, 1, 6));
    QVERIFY(!result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Reject);
    QCOMPARE(result.missingTradingDates, QVector<QDate>{QDate(2026, 1, 5)});
}

void BacktestDataQualityGateTest::explicitSuspensionExplainsNoTradeDay()
{
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 6)}),
        QDate(2026, 1, 5), QDate(2026, 1, 6),
        {suspended(QDate(2026, 1, 5))});
    QVERIFY(result.eligible);
    QVERIFY(result.status != BacktestDataQualityStatus::Reject);
    QVERIFY(result.missingTradingDates.isEmpty());
    QCOMPARE(result.explainedNoTradeDates,
             QVector<QDate>{QDate(2026, 1, 5)});
}

void BacktestDataQualityGateTest::marketHolidayIsNotADataGap()
{
    const RawTickImportDayAudit audit = auditFor(QDate(2026, 6, 22), 100);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 6, 22)}), QDate(2026, 6, 19),
        QDate(2026, 6, 22), {}, {audit}, true);
    QVERIFY(result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Pass);
    QVERIFY(result.missingTradingDates.isEmpty());
}

void BacktestDataQualityGateTest::marketHolidayWithTradesIsRejected()
{
    const RawTickImportDayAudit audit = auditFor(QDate(2026, 6, 19), 100);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 6, 19)}), QDate(2026, 6, 19),
        QDate(2026, 6, 19), {}, {audit}, true);
    QVERIFY(!result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Reject);
    QCOMPARE(result.unexpectedNonTradingDates,
             QVector<QDate>{QDate(2026, 6, 19)});
    QCOMPARE(result.toVariantMap().value(
                 QStringLiteral("unexpectedNonTradingDates")).toList(),
             QVariantList{QDate(2026, 6, 19)});
    QVERIFY(result.messages.join(QStringLiteral("；"))
                .contains(QStringLiteral("休市日存在原始成交")));
}

void BacktestDataQualityGateTest::suspensionFactCannotCoexistWithTrades()
{
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 5)}), QDate(2026, 1, 5),
        QDate(2026, 1, 5), {suspended(QDate(2026, 1, 5))});
    QVERIFY(!result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Reject);
    QVERIFY(result.messages.join(QStringLiteral("；"))
                .contains(QStringLiteral("同时存在")));
}

void BacktestDataQualityGateTest::excessiveSkippedRowsAreRejected()
{
    BacktestDatasetIdentity identity = identityFor({QDate(2026, 1, 5)});
    identity.days[0].rawCount = 980;
    const RawTickImportDayAudit audit = auditFor(
        QDate(2026, 1, 5), 980, 20, QStringLiteral("warning"));
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identity, QDate(2026, 1, 5),
        QDate(2026, 1, 5), {}, {audit}, true);
    QVERIFY(!result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Reject);
    QCOMPARE(result.auditedDayCount, 1);
    QCOMPARE(result.skippedRecords, 20);
    QVERIFY(result.skippedRecordRatio > 0.01);
}

void BacktestDataQualityGateTest::staleContentAuditIsNotTrusted()
{
    const RawTickImportDayAudit audit = auditFor(
        QDate(2026, 1, 5), 980, 20, QStringLiteral("warning"),
        QLatin1Char('b'));
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 5)}), QDate(2026, 1, 5),
        QDate(2026, 1, 5), {}, {audit}, true);
    QVERIFY(result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Warning);
    QCOMPARE(result.auditedDayCount, 0);
    QCOMPARE(result.skippedRecords, 0);
    QCOMPARE(result.unknownImportAuditDates,
             QVector<QDate>{QDate(2026, 1, 5)});
}

void BacktestDataQualityGateTest::smallSkippedRatioIsWarningOnly()
{
    BacktestDatasetIdentity identity = identityFor({QDate(2026, 1, 5)});
    identity.days[0].rawCount = 999;
    const RawTickImportDayAudit audit = auditFor(
        QDate(2026, 1, 5), 999, 1, QStringLiteral("warning"));
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identity, QDate(2026, 1, 5),
        QDate(2026, 1, 5), {}, {audit}, true);
    QVERIFY(result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Warning);
    QCOMPARE(result.skippedRecords, 1);
}

void BacktestDataQualityGateTest::repairDatesAreStructuredAndSorted()
{
    const QDate firstDay(2026, 1, 5);
    const QDate secondDay(2026, 1, 6);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({secondDay, firstDay}), firstDay, secondDay,
        {suspended(firstDay)},
        {auditFor(secondDay, 99),
         auditFor(firstDay, 100, 20, QStringLiteral("error"))},
        true);

    QVERIFY(!result.eligible);
    QCOMPARE(result.contradictorySuspensionDates,
             QVector<QDate>{firstDay});
    QCOMPARE(result.excessiveSkippedDates,
             QVector<QDate>{firstDay});
    QCOMPARE(result.failedAuditDates,
             QVector<QDate>{firstDay});
    QCOMPARE(result.auditCountMismatchDates,
             QVector<QDate>{secondDay});

    const QVariantMap map = result.toVariantMap();
    QCOMPARE(map.value(QStringLiteral("contradictorySuspensionDates")).toList(),
             QVariantList{firstDay});
    QCOMPARE(map.value(QStringLiteral("excessiveSkippedDates")).toList(),
             QVariantList{firstDay});
    QCOMPARE(map.value(QStringLiteral("failedAuditDates")).toList(),
             QVariantList{firstDay});
    QCOMPARE(map.value(QStringLiteral("auditCountMismatchDates")).toList(),
             QVariantList{secondDay});
}

void BacktestDataQualityGateTest::auditCountMismatchIsRejected()
{
    const QDate day(2026, 1, 5);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({day}), day, day, {}, {auditFor(day, 99)}, true);

    QVERIFY(!result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Reject);
    QCOMPARE(result.auditCountMismatchDates, QVector<QDate>{day});
    QVERIFY(result.messages.join(QStringLiteral("；"))
                .contains(QStringLiteral("审计记录数与当前原始事实不一致")));
}

void BacktestDataQualityGateTest::knownCorporateActionRejectsUnadjustedRawTicks()
{
    const RawTickImportDayAudit audit = auditFor(QDate(2026, 1, 5), 100);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 5)}), QDate(2026, 1, 5),
        QDate(2026, 1, 5), {}, {audit}, true, {QDate(2026, 1, 5)});
    QVERIFY(!result.eligible);
    QCOMPARE(result.corporateActionDates,
             QVector<QDate>{QDate(2026, 1, 5)});
}

void BacktestDataQualityGateTest::completeEvidencePasses()
{
    const RawTickImportDayAudit audit = auditFor(QDate(2026, 1, 5), 100);
    const BacktestDataQualityAssessment result = BacktestDataQualityGate::assess(
        identityFor({QDate(2026, 1, 5)}), QDate(2026, 1, 5),
        QDate(2026, 1, 5), {}, {audit}, true);
    QVERIFY(result.eligible);
    QCOMPARE(result.status, BacktestDataQualityStatus::Pass);
    QVERIFY(result.messages.isEmpty());
    QVERIFY(result.unknownImportAuditDates.isEmpty());
}

void BacktestDataQualityGateTest::availabilityPlannerRecommendsLongestNewestSegment()
{
    BacktestDataQualityAssessment assessment;
    assessment.calendarCoverageKnown = true;
    assessment.observedRawTickDates = {
        QDate(2026, 7, 13), QDate(2026, 7, 20),
        QDate(2026, 7, 23), QDate(2026, 7, 24)};
    assessment.missingTradingDates = {
        QDate(2026, 7, 14), QDate(2026, 7, 15),
        QDate(2026, 7, 16), QDate(2026, 7, 17),
        QDate(2026, 7, 21), QDate(2026, 7, 22)};

    const RawTickAvailabilityPlan plan = BacktestDataAvailabilityPlanner::plan(
        QDate(2026, 7, 13), QDate(2026, 7, 24), {assessment});

    QVERIFY(plan.calendarCoverageKnown);
    QCOMPARE(plan.segments.size(), 3);
    QCOMPARE(plan.segments.at(0).startDate, QDate(2026, 7, 13));
    QCOMPARE(plan.segments.at(1).startDate, QDate(2026, 7, 20));
    QCOMPARE(plan.segments.at(2).startDate, QDate(2026, 7, 23));
    QCOMPARE(plan.segments.at(2).endDate, QDate(2026, 7, 24));
    QCOMPARE(plan.segments.at(2).tradingDayCount, 2);
    QVERIFY(plan.recommendedSegment().has_value());
    QCOMPARE(plan.recommendedSegment()->startDate, QDate(2026, 7, 23));
    QCOMPARE(plan.recommendedSegment()->endDate, QDate(2026, 7, 24));
}

void BacktestDataQualityGateTest::explicitSuspensionDoesNotSplitAvailability()
{
    const BacktestDataQualityAssessment assessment =
        BacktestDataQualityGate::assess(
            identityFor({QDate(2026, 1, 5), QDate(2026, 1, 7)}),
            QDate(2026, 1, 5), QDate(2026, 1, 7),
            {suspended(QDate(2026, 1, 6))},
            {auditFor(QDate(2026, 1, 5), 100),
             auditFor(QDate(2026, 1, 7), 100)}, true);
    const RawTickAvailabilityPlan plan = BacktestDataAvailabilityPlanner::plan(
        QDate(2026, 1, 5), QDate(2026, 1, 7), {assessment});

    QVERIFY(assessment.eligible);
    QCOMPARE(plan.segments.size(), 1);
    QCOMPARE(plan.segments.first().startDate, QDate(2026, 1, 5));
    QCOMPARE(plan.segments.first().endDate, QDate(2026, 1, 7));
    QCOMPARE(plan.segments.first().tradingDayCount, 3);
}

void BacktestDataQualityGateTest::unknownAuditDateIsNotRecommendedAsAuditable()
{
    const BacktestDataQualityAssessment assessment =
        BacktestDataQualityGate::assess(
            identityFor({QDate(2026, 1, 5)}),
            QDate(2026, 1, 5), QDate(2026, 1, 5));
    const RawTickAvailabilityPlan plan = BacktestDataAvailabilityPlanner::plan(
        QDate(2026, 1, 5), QDate(2026, 1, 5), {assessment});

    QVERIFY(assessment.eligible);
    QCOMPARE(assessment.status, BacktestDataQualityStatus::Warning);
    QCOMPARE(plan.blockedDates, QVector<QDate>{QDate(2026, 1, 5)});
    QVERIFY(plan.segments.isEmpty());
    QVERIFY(!plan.recommendedSegment().has_value());
}

void BacktestDataQualityGateTest::outOfOrderAuditWarnsAndBlocksCleanRecommendation()
{
    RawTickImportDayAudit audit = auditFor(QDate(2026, 1, 5), 100);
    audit.outOfOrderRecords = 1;
    audit.qualityStatus = QStringLiteral("warning");
    const BacktestDataQualityAssessment assessment =
        BacktestDataQualityGate::assess(
            identityFor({QDate(2026, 1, 5)}),
            QDate(2026, 1, 5), QDate(2026, 1, 5), {}, {audit}, true);
    const RawTickAvailabilityPlan plan = BacktestDataAvailabilityPlanner::plan(
        QDate(2026, 1, 5), QDate(2026, 1, 5), {assessment});

    QVERIFY(assessment.eligible);
    QCOMPARE(assessment.status, BacktestDataQualityStatus::Warning);
    QCOMPARE(assessment.outOfOrderAuditDates,
             QVector<QDate>{QDate(2026, 1, 5)});
    QCOMPARE(assessment.outOfOrderRecords, 1LL);
    QVERIFY(plan.segments.isEmpty());
}

void BacktestDataQualityGateTest::suspensionOnlyRangeIsNotRecommendedAsExecutable()
{
    const BacktestDataQualityAssessment assessment =
        BacktestDataQualityGate::assess(
            identityFor({}), QDate(2026, 1, 5), QDate(2026, 1, 6),
            {suspended(QDate(2026, 1, 5)),
             suspended(QDate(2026, 1, 6))}, {}, true);
    const RawTickAvailabilityPlan plan = BacktestDataAvailabilityPlanner::plan(
        QDate(2026, 1, 5), QDate(2026, 1, 6), {assessment});

    QVERIFY(assessment.eligible);
    QVERIFY(assessment.observedRawTickDates.isEmpty());
    QVERIFY(plan.segments.isEmpty());
}

QTEST_GUILESS_MAIN(BacktestDataQualityGateTest)
#include "tst_backtest_data_quality_gate.moc"
