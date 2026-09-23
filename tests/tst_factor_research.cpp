#include "services/FactorResearchAnalyzer.h"
#include "services/FactorOutOfSampleAnalyzer.h"
#include "services/FactorCandidateGovernance.h"
#include "services/MultipleTestingCorrection.h"
#include "services/FactorResearchReportBuilder.h"
#include "services/AsyncFactorResearchReportService.h"
#include "services/ExternalFactorResearchEngine.h"
#include "models/FactorResearchComparisonListModel.h"

#include <QtTest>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <limits>

using namespace pp;

class FactorResearchTest final : public QObject {
    Q_OBJECT

private slots:
    void nextFrameReturnProducesPerfectIc();
    void perFrameSeriesPreservesTimestampsAndSampleIcir();
    void quintileReturnsAreEqualWeightedWithinFrame();
    void quantileTurnoverAndCostsUseAdjacentMemberships();
    void industryDemeaningRemovesBetweenIndustryLevel();
    void missingValuesAreDroppedPairwise();
    void percentileWinsorizationIsDeterministic();
    void invalidPolicyIsRejected();
    void hacInferenceUsesAutocorrelationRobustStandardError();
    void hacInferenceRequiresTwentyFrames();
    void temporalStabilityUsesThreeContiguousInSampleSegments();
    void expandingWindowsKeepEvaluationStrictlyAfterTraining();
    void outOfSampleInferenceUsesOnlyEvaluationWindows();
    void multipleTestingCorrectionUsesHolmAndBenjaminiHochberg();
    void reportUsesImmutableIdentityAndKnownStatistics();
    void reportIncludesImmutableOutOfSampleValidation();
    void reportEscapesUserVisibleArchiveText();
    void reportWritesUtf8FileAtomically();
    void invalidReportDoesNotOverwriteExistingFile();
    void asyncReportExportReturnsRequestIdentity();
    void comparisonAcceptsDifferentFactorsOnSameBasis();
    void comparisonLabelsDifferentDataIdentityAsDescriptive();
    void comparisonEnforcesSelectionBoundariesAndRebases();
    void comparisonCorrectsOnlyControlledComparableOutOfSamplePValues();
    void prospectiveCandidateEvaluationRequiresPriorFreeze();
    void retrospectiveCandidateEvaluationNeverClaimsPreregistration();
    void externalFactorUsesOnlyCompletedBarsForNextObservationReturn();
};

namespace {

FactorResearchFrame perfectCorrelationFrame(int index, bool positive)
{
    FactorResearchFrame frame;
    frame.factorTimestamp = QDateTime(
        QDate(2026, 1, 1), QTime(9, 30), QTimeZone::utc()).addSecs(index * 300);
    frame.returnTimestamp = frame.factorTimestamp.addSecs(300);
    frame.factorValues = {{QStringLiteral("600001"), 1.0},
                          {QStringLiteral("600002"), 2.0},
                          {QStringLiteral("600003"), 3.0}};
    frame.forwardReturns = positive
        ? QHash<QString, double>{{QStringLiteral("600001"), 0.01},
                                 {QStringLiteral("600002"), 0.02},
                                 {QStringLiteral("600003"), 0.03}}
        : QHash<QString, double>{{QStringLiteral("600001"), 0.03},
                                 {QStringLiteral("600002"), 0.02},
                                 {QStringLiteral("600003"), 0.01}};
    return frame;
}

PortfolioArchiveRecord comparisonArchive(const QString &resultId,
                                          const QString &factorId,
                                          const QChar datasetFill)
{
    PortfolioArchiveRecord record;
    record.resultId = resultId;
    record.completedAt = QDateTime(QDate(2026, 8, 22), QTime(16, 0),
                                   QTimeZone::utc());
    record.result.resultKind = QStringLiteral("cross_sectional");
    record.result.definition.name = QStringLiteral("因子研究归档");
    record.result.definition.periodSeconds = 300;
    record.result.definition.factorResearchAlias = factorId;
    record.result.factorResearchSummary.factorId = factorId;
    record.result.factorResearchSummary.factorVersion = 1;
    record.result.factorResearchSummary.factorAlias = factorId;
    record.result.factorResearchSummary.evaluatedFrameCount = 12;
    record.result.factorResearchSummary.coverageRatio = 0.95;
    record.result.factorResearchSummary.meanIc = 0.12;
    record.result.factorResearchSummary.meanRankIc = 0.15;
    record.result.factorResearchSummary.icInformationRatio = 0.8;
    record.result.factorResearchSummary.rankIcInformationRatio = 0.9;
    record.result.factorResearchSummary.icirAvailable = true;
    record.result.factorResearchSummary.rankIcirAvailable = true;
    record.result.factorResearchSummary.icInferenceAvailable = true;
    record.result.factorResearchSummary.rankIcInferenceAvailable = true;
    record.result.factorResearchSummary.inferenceFrameCount = 24;
    record.result.factorResearchSummary.hacLag = 2;
    record.result.factorResearchSummary.confidenceLevel = 0.95;
    record.result.factorResearchSummary.icConfidenceLower = 0.03;
    record.result.factorResearchSummary.icConfidenceUpper = 0.21;
    record.result.factorResearchSummary.icTwoSidedPValue = 0.01;
    record.result.factorResearchSummary.temporalStabilityAvailable = true;
    record.result.factorResearchSummary.topMinusBottomMeanReturn = 0.04;
    record.result.factorResearchSummary.topMinusBottomMeanNetReturn = 0.035;
    record.result.factorResearchSummary.topMinusBottomMeanOneWayTurnover = 0.2;
    record.result.factorResearchSummary.costPolicy.oneWayCostBps = 5.0;
    record.result.factorResearchSummary.timeSeries = {
        {QDateTime(QDate(2026, 8, 20), QTime(9, 35), QTimeZone::utc()),
         QDateTime(QDate(2026, 8, 20), QTime(9, 40), QTimeZone::utc()),
         10, 10, 0.1, 0.2}};

    BacktestDatasetIdentity dataset;
    dataset.instrument.symbol = QStringLiteral("600001");
    dataset.periodSeconds = 300;
    dataset.datasetVersion = QStringLiteral("sha256:%1").arg(
        QString(64, datasetFill));
    record.result.datasetIdentities = {dataset};
    record.result.datasetVersions = {dataset.datasetVersion};
    const QVariantMap factorIdentity {
        {QStringLiteral("factorId"), factorId},
        {QStringLiteral("factorVersion"), 1},
        {QStringLiteral("alias"), factorId},
        {QStringLiteral("preprocessing"),
         FactorPreprocessingPolicy{}.toVariantMap()},
        {QStringLiteral("returnHorizon"), QStringLiteral("next_aligned_frame")},
        {QStringLiteral("returnCalculation"), QStringLiteral("close_to_close")},
        {QStringLiteral("frameAggregation"), QStringLiteral("equal_weighted_mean")},
        {QStringLiteral("researchMethodVersion"), QStringLiteral("factor_research_v3")},
        {QStringLiteral("quantileCount"), 5},
        {QStringLiteral("quantileMethod"),
         QStringLiteral("stable_factor_then_symbol_equal_count")},
        {QStringLiteral("quantileAggregation"),
         QStringLiteral("equal_weighted_within_frame_then_across_frames")},
        {QStringLiteral("spreadDefinition"), QStringLiteral("q5_minus_q1")},
        {QStringLiteral("costPolicy"),
         record.result.factorResearchSummary.costPolicy.toVariantMap()},
        {QStringLiteral("neutralizationPolicy"),
         FactorNeutralizationPolicy{}.toVariantMap()}};
    record.result.immutableIdentity = {
        {QStringLiteral("strategyId"), QStringLiteral("strategy_fixture")},
        {QStringLiteral("strategyVersion"), 1},
        {QStringLiteral("periodSeconds"), 300},
        {QStringLiteral("requestedBars"), 500},
        {QStringLiteral("actualBars"), 480},
        {QStringLiteral("universeSymbols"), QStringList{QStringLiteral("600001")}},
        {QStringLiteral("calendarSnapshot"),
         QVariantMap{{QStringLiteral("snapshotVersion"),
                      QStringLiteral("calendar-v1")}}},
        {QStringLiteral("factorResearch"), factorIdentity}};
    return record;
}

PortfolioArchiveRecord reportArchive()
{
    PortfolioArchiveRecord record = comparisonArchive(
        QStringLiteral("report-result-v5"), QStringLiteral("momentum_rank"), 'd');
    FactorResearchSummary &summary = record.result.factorResearchSummary;
    summary.evaluatedFrameCount = 24;
    summary.inferenceFrameCount = 24;
    summary.hacLag = 2;
    summary.icInferenceAvailable = true;
    summary.rankIcInferenceAvailable = true;
    summary.icConfidenceLower = 0.03;
    summary.icConfidenceUpper = 0.21;
    summary.rankIcConfidenceLower = 0.05;
    summary.rankIcConfidenceUpper = 0.25;
    summary.icTwoSidedPValue = 0.01;
    summary.rankIcTwoSidedPValue = 0.004;
    summary.temporalStabilityAvailable = true;
    summary.icSameSignAcrossSegments = false;
    summary.rankIcSameSignAcrossSegments = true;
    summary.temporalSegments = {
        {1, QDateTime(QDate(2026, 8, 1), QTime(9, 35), QTimeZone::utc()),
         QDateTime(QDate(2026, 8, 1), QTime(10, 10), QTimeZone::utc()),
         8, 0.12, 0.15},
        {2, QDateTime(QDate(2026, 8, 1), QTime(10, 15), QTimeZone::utc()),
         QDateTime(QDate(2026, 8, 1), QTime(10, 50), QTimeZone::utc()),
         8, -0.02, 0.08},
        {3, QDateTime(QDate(2026, 8, 1), QTime(10, 55), QTimeZone::utc()),
         QDateTime(QDate(2026, 8, 1), QTime(11, 30), QTimeZone::utc()),
         8, 0.09, 0.11}};
    summary.quantileReturns = {
        {1, 24, -0.01, -0.011, 0.20, 0.001, 23},
        {2, 24, 0.00, -0.001, 0.18, 0.001, 23},
        {3, 24, 0.01, 0.009, 0.15, 0.001, 23},
        {4, 24, 0.02, 0.019, 0.17, 0.001, 23},
        {5, 24, 0.03, 0.029, 0.22, 0.001, 23}};
    QVariantMap factorIdentity = record.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    factorIdentity.insert(QStringLiteral("researchMethodVersion"),
                          QStringLiteral("factor_research_v5"));
    factorIdentity.insert(QStringLiteral("definitionSha256"),
                          QStringLiteral("sha256:") + QString(64, 'f'));
    factorIdentity.insert(QStringLiteral("inferenceMethodVersion"),
                          QStringLiteral("newey_west_bartlett_v1"));
    factorIdentity.insert(QStringLiteral("inferenceMinimumFrames"), 20);
    factorIdentity.insert(QStringLiteral("confidenceLevel"), 0.95);
    factorIdentity.insert(QStringLiteral("hacLagRule"),
                          QStringLiteral("floor_4_times_n_over_100_power_2_over_9"));
    factorIdentity.insert(QStringLiteral("temporalSegmentCount"), 3);
    factorIdentity.insert(QStringLiteral("temporalSegmentMethod"),
                          QStringLiteral("contiguous_equal_count"));
    factorIdentity.insert(QStringLiteral("summary"), summary.toVariantMap());
    record.result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                           factorIdentity);
    return record;
}

PortfolioArchiveRecord outOfSampleComparisonArchive(
    const QString &resultId, const QString &factorId,
    const QChar datasetFill, double pValue)
{
    PortfolioArchiveRecord record = comparisonArchive(
        resultId, factorId, datasetFill);
    FactorOutOfSampleSummary &summary =
        record.result.factorResearchSummary.outOfSample;
    summary.available = true;
    summary.totalFrameCount = 40;
    summary.evaluationFrameCount = 20;
    summary.meanIc = 0.08;
    summary.meanRankIc = 0.10;
    summary.icInferenceAvailable = true;
    summary.rankIcInferenceAvailable = true;
    summary.inferenceFrameCount = 20;
    summary.hacLag = 2;
    summary.icMeanStandardError = 0.03;
    summary.rankIcMeanStandardError = 0.04;
    summary.icConfidenceLower = 0.02;
    summary.icConfidenceUpper = 0.14;
    summary.rankIcConfidenceLower = 0.02;
    summary.rankIcConfidenceUpper = 0.18;
    summary.icTwoSidedPValue = pValue;
    summary.rankIcTwoSidedPValue = pValue;
    summary.windows = {
        {1, 20, 10, {}, {}, {}, {}, 0.09, 0.11},
        {2, 30, 10, {}, {}, {}, {}, 0.07, 0.09}};
    QVariantMap factorIdentity = record.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    factorIdentity.insert(QStringLiteral("researchMethodVersion"),
                          QStringLiteral("factor_research_v6"));
    factorIdentity.insert(QStringLiteral("outOfSampleMethodVersion"),
                          QStringLiteral("expanding_oos_v1"));
    factorIdentity.insert(QStringLiteral("outOfSamplePolicy"),
                          summary.policy.toVariantMap());
    factorIdentity.insert(
        QStringLiteral("outOfSampleInferenceMethodVersion"),
        QStringLiteral("newey_west_bartlett_v1"));
    record.result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                           factorIdentity);
    return record;
}

} // namespace

void FactorResearchTest::externalFactorUsesOnlyCompletedBarsForNextObservationReturn()
{
    const QDateTime firstFactorTime(
        QDate(2026, 8, 20), QTime(10, 0), QTimeZone::utc());
    const QDateTime secondFactorTime = firstFactorTime.addSecs(300);
    const QStringList symbols{QStringLiteral("600001"),
                              QStringLiteral("600002"),
                              QStringLiteral("600003")};

    QVector<ExternalFactorObservation> observations;
    for (int index = 0; index < symbols.size(); ++index) {
        observations.append({firstFactorTime, symbols.at(index),
                             double(index + 1)});
        observations.append({secondFactorTime, symbols.at(index),
                             double(index + 4)});
    }

    const QVector<double> firstCloses{10.0, 20.0, 30.0};
    const QVector<double> secondCloses{11.0, 20.0, 27.0};
    QHash<QString, QVector<Candle>> candlesBySymbol;
    for (int index = 0; index < symbols.size(); ++index) {
        Candle first;
        first.instrument.symbol = symbols.at(index);
        first.startTime = firstFactorTime.addSecs(-60);
        first.periodSeconds = 60;
        first.close = firstCloses.at(index);

        Candle second = first;
        second.startTime = secondFactorTime.addSecs(-60);
        second.close = secondCloses.at(index);

        Candle future = second;
        future.startTime = secondFactorTime;
        future.close = 1000.0;
        candlesBySymbol.insert(symbols.at(index), {first, second, future});
    }

    ExternalFactorResearchRequest request;
    request.periodSeconds = 60;
    request.preprocessing.minimumPairs = 3;

    const ExternalFactorResearchResult result =
        ExternalFactorResearchEngine::analyze(request, observations,
                                              candlesBySymbol);

    QVERIFY2(result.succeeded, qPrintable(result.error));
    QCOMPARE(result.summary.evaluatedFrameCount, 1);
    QCOMPARE(result.summary.timeSeries.size(), 1);
    QCOMPARE(result.summary.timeSeries.first().factorTimestamp,
             firstFactorTime);
    QCOMPARE(result.summary.timeSeries.first().returnTimestamp,
             secondFactorTime);
    QCOMPARE(result.summary.timeSeries.first().ic, -1.0);
    QCOMPARE(result.summary.timeSeries.first().rankIc, -1.0);
}

void FactorResearchTest::nextFrameReturnProducesPerfectIc()
{
    FactorResearchFrame frame;
    frame.factorValues = {{QStringLiteral("600001"), 1.0},
                          {QStringLiteral("600002"), 2.0},
                          {QStringLiteral("600003"), 3.0}};
    frame.forwardReturns = {{QStringLiteral("600001"), 0.01},
                            {QStringLiteral("600002"), 0.02},
                            {QStringLiteral("600003"), 0.03}};
    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        {frame}, FactorPreprocessingPolicy{});
    QCOMPARE(summary.evaluatedFrameCount, 1);
    QCOMPARE(summary.validPairCount, 3);
    QCOMPARE(summary.meanIc, 1.0);
    QCOMPARE(summary.meanRankIc, 1.0);
    QCOMPARE(summary.positiveIcFrameRatio, 1.0);
}

void FactorResearchTest::perFrameSeriesPreservesTimestampsAndSampleIcir()
{
    const QDateTime firstFactorTime(
        QDate(2026, 8, 20), QTime(9, 35), QTimeZone::utc());
    const QDateTime firstReturnTime(
        QDate(2026, 8, 20), QTime(9, 40), QTimeZone::utc());
    const QDateTime secondReturnTime(
        QDate(2026, 8, 20), QTime(9, 45), QTimeZone::utc());
    FactorResearchFrame positive;
    positive.factorTimestamp = firstFactorTime;
    positive.returnTimestamp = firstReturnTime;
    positive.factorValues = {{QStringLiteral("600001"), 1.0},
                             {QStringLiteral("600002"), 2.0},
                             {QStringLiteral("600003"), 3.0}};
    positive.forwardReturns = {{QStringLiteral("600001"), 0.01},
                               {QStringLiteral("600002"), 0.02},
                               {QStringLiteral("600003"), 0.03}};
    FactorResearchFrame negative = positive;
    negative.factorTimestamp = firstReturnTime;
    negative.returnTimestamp = secondReturnTime;
    negative.forwardReturns = {{QStringLiteral("600001"), 0.03},
                               {QStringLiteral("600002"), 0.02},
                               {QStringLiteral("600003"), 0.01}};

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        {positive, negative}, FactorPreprocessingPolicy{});

    QCOMPARE(summary.timeSeries.size(), 2);
    QCOMPARE(summary.timeSeries.at(0).factorTimestamp, firstFactorTime);
    QCOMPARE(summary.timeSeries.at(0).returnTimestamp, firstReturnTime);
    QCOMPARE(summary.timeSeries.at(0).ic, 1.0);
    QCOMPARE(summary.timeSeries.at(1).ic, -1.0);
    QVERIFY(summary.icirAvailable);
    QVERIFY(summary.rankIcirAvailable);
    QCOMPARE(summary.icStandardDeviation, std::sqrt(2.0));
    QCOMPARE(summary.rankIcStandardDeviation, std::sqrt(2.0));
    QCOMPARE(summary.icInformationRatio, 0.0);
    QCOMPARE(summary.rankIcInformationRatio, 0.0);
}

void FactorResearchTest::quintileReturnsAreEqualWeightedWithinFrame()
{
    FactorResearchFrame frame;
    for (int index = 1; index <= 10; ++index) {
        const QString symbol = QStringLiteral("600%1").arg(index, 3, 10, QLatin1Char('0'));
        frame.factorValues.insert(symbol, static_cast<double>(index));
        frame.forwardReturns.insert(symbol, static_cast<double>(index) / 100.0);
    }

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        {frame}, FactorPreprocessingPolicy{});

    QCOMPARE(summary.quantileCount, 5);
    QCOMPARE(summary.quantileReturns.size(), 5);
    QCOMPARE(summary.quantileReturns.at(0).quantile, 1);
    QCOMPARE(summary.quantileReturns.at(0).meanForwardReturn, 0.015);
    QCOMPARE(summary.quantileReturns.at(2).meanForwardReturn, 0.055);
    QCOMPARE(summary.quantileReturns.at(4).meanForwardReturn, 0.095);
    QCOMPARE(summary.quantileReturns.at(4).evaluatedFrameCount, 1);
    QCOMPARE(summary.topMinusBottomMeanReturn, 0.08);
    QCOMPARE(summary.topMinusBottomFrameCount, 1);
}

void FactorResearchTest::quantileTurnoverAndCostsUseAdjacentMemberships()
{
    FactorResearchFrame first;
    FactorResearchFrame second;
    for (int index = 1; index <= 10; ++index) {
        const QString symbol = QStringLiteral("600%1").arg(
            index, 3, 10, QLatin1Char('0'));
        first.factorValues.insert(symbol, static_cast<double>(index));
        first.forwardReturns.insert(symbol, static_cast<double>(index) / 100.0);
        second.factorValues.insert(symbol,
            index <= 2 ? static_cast<double>(index + 2)
                       : index <= 4 ? static_cast<double>(index - 2)
                                    : static_cast<double>(index));
        second.forwardReturns.insert(symbol, static_cast<double>(index) / 100.0);
    }
    FactorResearchCostPolicy costPolicy;
    costPolicy.oneWayCostBps = 10.0;

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        {first, second}, FactorPreprocessingPolicy{}, costPolicy);

    QCOMPARE(summary.turnoverObservationCount, 1);
    QCOMPARE(summary.quantileReturns.at(0).meanOneWayTurnover, 1.0);
    QCOMPARE(summary.quantileReturns.at(0).turnoverObservationCount, 1);
    QCOMPARE(summary.quantileReturns.at(0).meanTransactionCost, 0.001);
    QCOMPARE(summary.quantileReturns.at(0).meanNetForwardReturn, 0.024);
    QCOMPARE(summary.quantileReturns.at(4).meanOneWayTurnover, 0.0);
    QCOMPARE(summary.topMinusBottomMeanTransactionCost, 0.001);
    QCOMPARE(summary.topMinusBottomMeanNetReturn, 0.069);
}

void FactorResearchTest::industryDemeaningRemovesBetweenIndustryLevel()
{
    FactorResearchFrame frame;
    for (int index = 1; index <= 6; ++index) {
        const QString symbol = QStringLiteral("60000%1").arg(index);
        const bool firstIndustry = index <= 3;
        const int withinIndustry = firstIndustry ? index : index - 3;
        frame.factorValues.insert(
            symbol, (firstIndustry ? 0.0 : 100.0) + withinIndustry);
        frame.forwardReturns.insert(
            symbol, static_cast<double>(withinIndustry) / 100.0);
        frame.neutralizationGroups.insert(
            symbol, firstIndustry ? QStringLiteral("TECH")
                                  : QStringLiteral("FIN"));
    }
    FactorNeutralizationPolicy neutralization;
    neutralization.method = QStringLiteral("within_group_demean");
    neutralization.minimumGroupSize = 2;

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        {frame}, FactorPreprocessingPolicy{}, FactorResearchCostPolicy{},
        neutralization);

    QCOMPARE(summary.evaluatedFrameCount, 1);
    QCOMPARE(summary.neutralizationAppliedFrameCount, 1);
    QCOMPARE(summary.neutralizedPairCount, 6);
    QCOMPARE(summary.smallGroupDroppedPairCount, 0);
    QCOMPARE(summary.meanIc, 1.0);
    QCOMPARE(summary.meanRankIc, 1.0);
}

void FactorResearchTest::missingValuesAreDroppedPairwise()
{
    FactorResearchFrame frame;
    frame.factorValues = {{QStringLiteral("600001"), 1.0},
                          {QStringLiteral("600002"), 2.0},
                          {QStringLiteral("600003"), std::numeric_limits<double>::quiet_NaN()},
                          {QStringLiteral("600004"), 4.0}};
    frame.forwardReturns = {{QStringLiteral("600001"), 0.01},
                            {QStringLiteral("600002"), 0.02},
                            {QStringLiteral("600003"), 0.03}};
    FactorPreprocessingPolicy policy;
    policy.minimumPairs = 2;
    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze({frame}, policy);
    QCOMPARE(summary.candidatePairCount, 4);
    QCOMPARE(summary.validPairCount, 2);
    QCOMPARE(summary.evaluatedFrameCount, 1);
    QCOMPARE(summary.coverageRatio, 0.5);
}

void FactorResearchTest::percentileWinsorizationIsDeterministic()
{
    FactorResearchFrame frame;
    frame.factorValues = {{QStringLiteral("600001"), 0.0},
                          {QStringLiteral("600002"), 1.0},
                          {QStringLiteral("600003"), 2.0},
                          {QStringLiteral("600004"), 100.0}};
    frame.forwardReturns = {{QStringLiteral("600001"), 0.0},
                            {QStringLiteral("600002"), 1.0},
                            {QStringLiteral("600003"), 2.0},
                            {QStringLiteral("600004"), 3.0}};
    FactorPreprocessingPolicy policy;
    policy.winsorizationPolicy = QStringLiteral("percentile_clip");
    policy.lowerQuantile = 0.0;
    policy.upperQuantile = 0.75;
    const FactorResearchSummary raw = FactorResearchAnalyzer::analyze(
        {frame}, FactorPreprocessingPolicy{});
    const FactorResearchSummary clipped = FactorResearchAnalyzer::analyze({frame}, policy);
    QVERIFY(clipped.meanIc > raw.meanIc);
    QVERIFY(clipped.meanIc < 1.0);
    QCOMPARE(clipped.meanRankIc, 1.0);
}

void FactorResearchTest::invalidPolicyIsRejected()
{
    FactorPreprocessingPolicy policy;
    policy.missingValuePolicy = QStringLiteral("forward_fill");
    QString error;
    QVERIFY(!policy.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("pairwise_drop")));
}

void FactorResearchTest::hacInferenceUsesAutocorrelationRobustStandardError()
{
    QVector<FactorResearchFrame> frames;
    for (int index = 0; index < 20; ++index)
        frames.append(perfectCorrelationFrame(index, index < 15));

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        frames, FactorPreprocessingPolicy{});

    QVERIFY(summary.icInferenceAvailable);
    QVERIFY(summary.rankIcInferenceAvailable);
    QCOMPARE(summary.inferenceFrameCount, 20);
    QCOMPARE(summary.hacLag, 2);
    QCOMPARE(summary.confidenceLevel, 0.95);
    QCOMPARE(summary.meanIc, 0.5);
    QVERIFY(std::abs(summary.icMeanStandardError - 0.301385688667085) < 1e-12);
    QVERIFY(std::abs(summary.icConfidenceLower - (-0.090705095243289)) < 1e-12);
    QVERIFY(std::abs(summary.icConfidenceUpper - 1.09070509524329) < 1e-12);
    QVERIFY(summary.icTwoSidedPValue > 0.09);
    QVERIFY(summary.icTwoSidedPValue < 0.10);
}

void FactorResearchTest::hacInferenceRequiresTwentyFrames()
{
    QVector<FactorResearchFrame> frames;
    for (int index = 0; index < 19; ++index)
        frames.append(perfectCorrelationFrame(index, index < 14));

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        frames, FactorPreprocessingPolicy{});

    QCOMPARE(summary.inferenceFrameCount, 19);
    QVERIFY(!summary.icInferenceAvailable);
    QVERIFY(!summary.rankIcInferenceAvailable);
}

void FactorResearchTest::temporalStabilityUsesThreeContiguousInSampleSegments()
{
    QVector<FactorResearchFrame> frames;
    for (int index = 0; index < 9; ++index)
        frames.append(perfectCorrelationFrame(index, index < 3 || index >= 6));

    const FactorResearchSummary summary = FactorResearchAnalyzer::analyze(
        frames, FactorPreprocessingPolicy{});

    QVERIFY(summary.temporalStabilityAvailable);
    QCOMPARE(summary.temporalSegments.size(), 3);
    QCOMPARE(summary.temporalSegments.at(0).frameCount, 3);
    QCOMPARE(summary.temporalSegments.at(0).meanIc, 1.0);
    QCOMPARE(summary.temporalSegments.at(1).meanIc, -1.0);
    QCOMPARE(summary.temporalSegments.at(2).meanIc, 1.0);
    QVERIFY(!summary.icSameSignAcrossSegments);
    QVERIFY(!summary.rankIcSameSignAcrossSegments);
    QCOMPARE(summary.temporalSegments.at(0).fromFactorTimestamp,
             frames.at(0).factorTimestamp);
    QCOMPARE(summary.temporalSegments.at(2).toFactorTimestamp,
             frames.at(8).factorTimestamp);
}

void FactorResearchTest::reportUsesImmutableIdentityAndKnownStatistics()
{
    QString error;
    const QString report = FactorResearchReportBuilder::buildMarkdown(
        reportArchive(), &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(report.startsWith(QStringLiteral("# 因子研究报告\n")));
    QVERIFY(report.contains(QStringLiteral("report-result-v5")));
    QVERIFY(report.contains(QStringLiteral("momentum_rank · v1")));
    QVERIFY(report.contains(QStringLiteral("strategy_fixture · v1")));
    QVERIFY(report.contains(QStringLiteral("factor_research_v5")));
    QVERIFY(report.contains(QStringLiteral("[0.0300, 0.2100]")));
    QVERIFY(report.contains(QStringLiteral("0.0100")));
    QVERIFY(report.contains(QStringLiteral("S2")));
    QVERIFY(report.contains(QStringLiteral("存在变号")));
    QVERIFY(report.contains(QStringLiteral("sha256:") + QString(64, 'd')));
    QVERIFY(report.contains(QStringLiteral("<svg")));
    QVERIFY(report.contains(QStringLiteral("不构成投资建议")));
}

void FactorResearchTest::reportIncludesImmutableOutOfSampleValidation()
{
    PortfolioArchiveRecord record = reportArchive();
    record.resultId = QStringLiteral("report-result-v6");
    FactorOutOfSampleSummary &oos =
        record.result.factorResearchSummary.outOfSample;
    oos.available = true;
    oos.totalFrameCount = 40;
    oos.evaluationFrameCount = 20;
    oos.meanIc = 0.08;
    oos.meanRankIc = 0.10;
    oos.icInferenceAvailable = true;
    oos.inferenceFrameCount = 20;
    oos.hacLag = 2;
    oos.icConfidenceLower = 0.02;
    oos.icConfidenceUpper = 0.14;
    oos.icTwoSidedPValue = 0.03;
    oos.windows = {
        {1, 20, 10, {}, {}, {}, {}, 0.09, 0.11},
        {2, 30, 10, {}, {}, {}, {}, 0.07, 0.09}};
    QVariantMap factorIdentity = record.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    factorIdentity.insert(QStringLiteral("researchMethodVersion"),
                          QStringLiteral("factor_research_v6"));
    factorIdentity.insert(QStringLiteral("outOfSampleMethodVersion"),
                          QStringLiteral("expanding_oos_v1"));
    factorIdentity.insert(QStringLiteral("outOfSamplePolicy"),
                          oos.policy.toVariantMap());
    factorIdentity.insert(
        QStringLiteral("outOfSampleInferenceMethodVersion"),
        QStringLiteral("newey_west_bartlett_v1"));
    record.result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                           factorIdentity);

    QString error;
    const QString report = FactorResearchReportBuilder::buildMarkdown(
        record, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(report.contains(QStringLiteral("## 时间有序样本外验证")));
    QVERIFY(report.contains(QStringLiteral("expanding_non_overlapping")));
    QVERIFY(report.contains(QStringLiteral("测试帧：20")));
    QVERIFY(report.contains(QStringLiteral("[0.0200, 0.1400]")));
    QVERIFY(report.contains(QStringLiteral("多重检验校正依赖当前显式比较候选集")));
}

void FactorResearchTest::reportEscapesUserVisibleArchiveText()
{
    PortfolioArchiveRecord record = reportArchive();
    record.result.factorResearchSummary.factorAlias =
        QStringLiteral("mom<script>alert(1)</script>|`rank`");
    QVariantMap factorIdentity = record.result.immutableIdentity
        .value(QStringLiteral("factorResearch")).toMap();
    factorIdentity.insert(QStringLiteral("alias"),
                          record.result.factorResearchSummary.factorAlias);
    record.result.immutableIdentity.insert(QStringLiteral("factorResearch"),
                                           factorIdentity);
    QString error;
    const QString report = FactorResearchReportBuilder::buildMarkdown(
        record, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!report.contains(QStringLiteral("<script>")));
    QVERIFY(report.contains(QStringLiteral("&lt;script&gt;")));
    QVERIFY(report.contains(QStringLiteral("\\|")));
    QVERIFY(report.contains(QStringLiteral("&#96;rank&#96;")));
}

void FactorResearchTest::reportWritesUtf8FileAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("factor-report.md"));
    QString error;
    QVERIFY2(FactorResearchReportBuilder::writeMarkdownAtomically(
                 reportArchive(), path, &error),
             qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(bytes.startsWith(QStringLiteral("# 因子研究报告").toUtf8()));
    QVERIFY(bytes.contains(QStringLiteral("不构成投资建议").toUtf8()));
}

void FactorResearchTest::invalidReportDoesNotOverwriteExistingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("existing.md"));
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    QCOMPARE(seed.write("keep-existing"), qint64(13));
    seed.close();
    PortfolioArchiveRecord invalid = reportArchive();
    invalid.result.resultKind = QStringLiteral("fixed_portfolio");
    QString error;
    QVERIFY(!FactorResearchReportBuilder::writeMarkdownAtomically(
        invalid, path, &error));
    QVERIFY(!error.isEmpty());
    QFile preserved(path);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), QByteArray("keep-existing"));
}

void FactorResearchTest::asyncReportExportReturnsRequestIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("async-report.md"));
    AsyncFactorResearchReportService service;
    QSignalSpy finished(&service,
                        &AsyncFactorResearchReportService::exportFinished);

    QVERIFY(service.requestExport(42, reportArchive(), path));
    QVERIFY(finished.wait(5000));
    const QList<QVariant> arguments = finished.takeFirst();
    QCOMPARE(arguments.at(0).toULongLong(), qulonglong(42));
    const QVariantMap result = arguments.at(1).toMap();
    QVERIFY2(result.value(QStringLiteral("success")).toBool(),
             qPrintable(result.value(QStringLiteral("error")).toString()));
    QCOMPARE(result.value(QStringLiteral("filePath")).toString(), path);
    QVERIFY(QFileInfo::exists(path));
}

void FactorResearchTest::expandingWindowsKeepEvaluationStrictlyAfterTraining()
{
    QVector<FactorResearchPoint> series;
    const QDateTime origin(QDate(2026, 1, 1), QTime(9, 30),
                           QTimeZone::utc());
    for (int index = 0; index < 40; ++index) {
        series.append({origin.addSecs(index * 300),
                       origin.addSecs((index + 1) * 300),
                       20, 20, index % 2 == 0 ? 0.2 : 0.1,
                       index % 2 == 0 ? 0.3 : 0.2});
    }

    const FactorOutOfSampleSummary summary =
        FactorOutOfSampleAnalyzer::analyze(series, {});

    QVERIFY(summary.available);
    QCOMPARE(summary.windows.size(), 2);
    QCOMPARE(summary.evaluationFrameCount, 20);
    QCOMPARE(summary.windows.at(0).trainingFrameCount, 20);
    QCOMPARE(summary.windows.at(0).evaluationFrameCount, 10);
    QCOMPARE(summary.windows.at(1).trainingFrameCount, 30);
    QCOMPARE(summary.windows.at(1).evaluationFrameCount, 10);
    QVERIFY(summary.windows.at(0).trainingToFactorTimestamp
            < summary.windows.at(0).evaluationFromFactorTimestamp);
    QVERIFY(summary.windows.at(1).trainingToFactorTimestamp
            < summary.windows.at(1).evaluationFromFactorTimestamp);
    QCOMPARE(summary.evaluationFromFactorTimestamp,
             series.at(20).factorTimestamp);
    QCOMPARE(summary.evaluationToFactorTimestamp,
             series.at(39).factorTimestamp);
}

void FactorResearchTest::outOfSampleInferenceUsesOnlyEvaluationWindows()
{
    QVector<FactorResearchPoint> series;
    const QDateTime origin(QDate(2026, 2, 1), QTime(9, 30),
                           QTimeZone::utc());
    for (int index = 0; index < 40; ++index) {
        const double value = index < 20 ? -1.0
            : index < 35 ? 1.0 : -1.0;
        series.append({origin.addSecs(index * 300),
                       origin.addSecs((index + 1) * 300),
                       20, 20, value, value});
    }

    const FactorOutOfSampleSummary summary =
        FactorOutOfSampleAnalyzer::analyze(series, {});

    QCOMPARE(summary.meanIc, 0.5);
    QCOMPARE(summary.inferenceFrameCount, 20);
    QCOMPARE(summary.hacLag, 2);
    QVERIFY(summary.icInferenceAvailable);
    QVERIFY(std::abs(summary.icMeanStandardError
                     - 0.301385688667085) < 1e-12);
    QVERIFY(std::abs(summary.icConfidenceLower
                     - (-0.090705095243289)) < 1e-12);
    QVERIFY(std::abs(summary.icConfidenceUpper
                     - 1.09070509524329) < 1e-12);
    QVERIFY(summary.icTwoSidedPValue > 0.09);
    QVERIFY(summary.icTwoSidedPValue < 0.10);
}

void FactorResearchTest::multipleTestingCorrectionUsesHolmAndBenjaminiHochberg()
{
    QString error;
    const MultipleTestingSummary summary = MultipleTestingCorrection::adjust(
        {{QStringLiteral("factor-a"), 0.01},
         {QStringLiteral("factor-b"), 0.03},
         {QStringLiteral("factor-c"), 0.04}},
        0.05, &error);

    QVERIFY2(summary.available, qPrintable(error));
    QCOMPARE(summary.hypotheses.size(), 3);
    QVERIFY(std::abs(summary.hypotheses.at(0).holmAdjustedPValue - 0.03)
            < 1e-12);
    QVERIFY(std::abs(summary.hypotheses.at(1).holmAdjustedPValue - 0.06)
            < 1e-12);
    QVERIFY(std::abs(summary.hypotheses.at(2).holmAdjustedPValue - 0.06)
            < 1e-12);
    QVERIFY(std::abs(summary.hypotheses.at(0).bhAdjustedPValue - 0.03)
            < 1e-12);
    QVERIFY(std::abs(summary.hypotheses.at(1).bhAdjustedPValue - 0.04)
            < 1e-12);
    QVERIFY(std::abs(summary.hypotheses.at(2).bhAdjustedPValue - 0.04)
            < 1e-12);
    QVERIFY(summary.hypotheses.at(0).holmRejected);
    QVERIFY(!summary.hypotheses.at(1).holmRejected);
    QVERIFY(summary.hypotheses.at(2).bhRejected);

    const MultipleTestingSummary reordered = MultipleTestingCorrection::adjust(
        {{QStringLiteral("factor-c"), 0.04},
         {QStringLiteral("factor-a"), 0.01},
         {QStringLiteral("factor-b"), 0.03}},
        0.05, &error);
    QCOMPARE(reordered.candidateSetSha256, summary.candidateSetSha256);
}

void FactorResearchTest::comparisonAcceptsDifferentFactorsOnSameBasis()
{
    FactorResearchComparisonListModel model;
    QString error;
    QVERIFY(model.addArchive(comparisonArchive(
        QStringLiteral("result-a"), QStringLiteral("momentum"), 'a'), &error));
    QVERIFY(model.addArchive(comparisonArchive(
        QStringLiteral("result-b"), QStringLiteral("value"), 'a'), &error));
    QCOMPARE(model.count(), 2);
    const QVariantMap baseline = model.get(0);
    const QVariantMap candidate = model.get(1);
    QCOMPARE(baseline.value(QStringLiteral("comparabilityLabel")).toString(),
             QStringLiteral("比较基准"));
    QVERIFY(candidate.value(QStringLiteral("controlledComparable")).toBool());
    QCOMPARE(candidate.value(QStringLiteral("comparabilityLabel")).toString(),
             QStringLiteral("受控可比"));
    QCOMPARE(candidate.value(QStringLiteral("factorId")).toString(),
             QStringLiteral("value"));
    QCOMPARE(candidate.value(QStringLiteral("meanRankIc")).toDouble(), 0.15);
    QCOMPARE(candidate.value(QStringLiteral("icConfidenceLower")).toDouble(),
             0.03);
    QCOMPARE(candidate.value(QStringLiteral("icTwoSidedPValue")).toDouble(),
             0.01);
    QVERIFY(candidate.value(QStringLiteral("temporalStabilityAvailable")).toBool());
    QVERIFY(!candidate.value(QStringLiteral("comparisonBasisSha256"))
                 .toString().isEmpty());
}

void FactorResearchTest::comparisonLabelsDifferentDataIdentityAsDescriptive()
{
    FactorResearchComparisonListModel model;
    QString error;
    QVERIFY(model.addArchive(comparisonArchive(
        QStringLiteral("result-a"), QStringLiteral("momentum"), 'a'), &error));
    QVERIFY(model.addArchive(comparisonArchive(
        QStringLiteral("result-b"), QStringLiteral("value"), 'b'), &error));
    const QVariantMap candidate = model.get(1);
    QVERIFY(!candidate.value(QStringLiteral("controlledComparable")).toBool());
    QCOMPARE(candidate.value(QStringLiteral("comparabilityLabel")).toString(),
             QStringLiteral("描述性对照"));
    QVERIFY(candidate.value(QStringLiteral("comparabilityReason")).toString()
                .contains(QStringLiteral("数据身份")));
}

void FactorResearchTest::comparisonEnforcesSelectionBoundariesAndRebases()
{
    FactorResearchComparisonListModel model;
    QString error;
    for (int index = 0; index < 4; ++index) {
        QVERIFY(model.addArchive(comparisonArchive(
            QStringLiteral("result-%1").arg(index),
            QStringLiteral("factor-%1").arg(index), 'a'), &error));
    }
    QVERIFY(!model.addArchive(comparisonArchive(
        QStringLiteral("result-4"), QStringLiteral("factor-4"), 'a'), &error));
    QVERIFY(error.contains(QStringLiteral("4")));
    QVERIFY(!model.addArchive(comparisonArchive(
        QStringLiteral("result-1"), QStringLiteral("duplicate"), 'a'), &error));
    QVERIFY(error.contains(QStringLiteral("已选择")));
    QVERIFY(model.removeArchive(QStringLiteral("result-0")));
    QCOMPARE(model.get(0).value(QStringLiteral("comparabilityLabel")).toString(),
             QStringLiteral("比较基准"));
    model.clear();
    QCOMPARE(model.count(), 0);
}

void FactorResearchTest::
comparisonCorrectsOnlyControlledComparableOutOfSamplePValues()
{
    FactorResearchComparisonListModel model;
    QString error;
    QVERIFY(model.addArchive(outOfSampleComparisonArchive(
        QStringLiteral("result-a"), QStringLiteral("factor-a"), 'a', 0.01),
        &error));
    QVERIFY(model.addArchive(outOfSampleComparisonArchive(
        QStringLiteral("result-b"), QStringLiteral("factor-b"), 'a', 0.03),
        &error));
    QVERIFY(model.addArchive(outOfSampleComparisonArchive(
        QStringLiteral("result-c"), QStringLiteral("factor-c"), 'a', 0.04),
        &error));

    QCOMPARE(model.multipleTestingCandidateCount(), 3);
    QVERIFY(!model.multipleTestingCandidateSetSha256().isEmpty());
    const QVariantMap first = model.get(0);
    const QVariantMap second = model.get(1);
    const QVariantMap third = model.get(2);
    QVERIFY(first.value(QStringLiteral("multipleTestingEligible")).toBool());
    QVERIFY(std::abs(first.value(QStringLiteral("holmAdjustedPValue")).toDouble()
                     - 0.03) < 1e-12);
    QVERIFY(std::abs(second.value(QStringLiteral("holmAdjustedPValue")).toDouble()
                     - 0.06) < 1e-12);
    QVERIFY(std::abs(third.value(QStringLiteral("bhAdjustedPValue")).toDouble()
                     - 0.04) < 1e-12);
    const QString threeCandidateSha =
        model.multipleTestingCandidateSetSha256();
    QCOMPARE(first.value(
                 QStringLiteral("multipleTestingCandidateSetSha256")).toString(),
             model.multipleTestingCandidateSetSha256());

    QVERIFY(model.addArchive(outOfSampleComparisonArchive(
        QStringLiteral("result-descriptive"),
        QStringLiteral("factor-descriptive"), 'b', 0.001), &error));
    QCOMPARE(model.multipleTestingCandidateCount(), 3);
    QCOMPARE(model.multipleTestingCandidateSetSha256(), threeCandidateSha);
    QVERIFY(!model.get(3).value(
                 QStringLiteral("multipleTestingEligible")).toBool());
    QCOMPARE(model.get(3).value(
                 QStringLiteral("holmAdjustedPValue")).toDouble(), 0.0);
    QVERIFY(model.removeArchive(QStringLiteral("result-c")));
    QCOMPARE(model.multipleTestingCandidateCount(), 2);
    QVERIFY(model.multipleTestingCandidateSetSha256() != threeCandidateSha);
}

void FactorResearchTest::prospectiveCandidateEvaluationRequiresPriorFreeze()
{
    FactorCandidateVersion candidate;
    candidate.candidateId = QStringLiteral("candidate.momentum");
    candidate.version = 1;
    candidate.name = QStringLiteral("动量候选");
    candidate.hypothesis = QStringLiteral("过去收益较高的股票后续收益更高");
    candidate.sourceKind = QStringLiteral("manual");
    candidate.factorDefinition = {
        {QStringLiteral("indicator"), QStringLiteral("ROC")},
        {QStringLiteral("period"), 20}};
    candidate.proposedAt = QDateTime(QDate(2026, 8, 23), QTime(9, 0),
                                     QTimeZone::utc());

    PortfolioArchiveRecord archive = outOfSampleComparisonArchive(
        QStringLiteral("future-v6"), QStringLiteral("momentum"), 'a', 0.03);
    archive.completedAt = candidate.proposedAt.addDays(1);

    FactorCandidateEvaluationDecision decision =
        FactorCandidateGovernance::evaluate(
            candidate, archive,
            FactorCandidateEvaluationRole::ProspectiveHoldout);
    QVERIFY(!decision.accepted);
    QVERIFY(decision.reason.contains(QStringLiteral("冻结")));

    candidate.frozenAt = candidate.proposedAt.addSecs(60);
    archive.completedAt = candidate.frozenAt;
    decision = FactorCandidateGovernance::evaluate(
        candidate, archive,
        FactorCandidateEvaluationRole::ProspectiveHoldout);
    QVERIFY(!decision.accepted);
    QVERIFY(decision.reason.contains(QStringLiteral("晚于")));

    archive.completedAt = candidate.frozenAt.addSecs(1);
    decision = FactorCandidateGovernance::evaluate(
        candidate, archive,
        FactorCandidateEvaluationRole::ProspectiveHoldout);
    QVERIFY2(decision.accepted, qPrintable(decision.reason));
    QVERIFY(decision.preregistered);
    QCOMPARE(decision.researchMethodVersion,
             QStringLiteral("factor_research_v6"));
}

void FactorResearchTest::
retrospectiveCandidateEvaluationNeverClaimsPreregistration()
{
    FactorCandidateVersion candidate;
    candidate.candidateId = QStringLiteral("candidate.value");
    candidate.version = 1;
    candidate.name = QStringLiteral("价值候选");
    candidate.hypothesis = QStringLiteral("估值较低的股票后续收益更高");
    candidate.sourceKind = QStringLiteral("authorized_import");
    candidate.sourceReference = QStringLiteral("研究备忘录 v1");
    candidate.factorDefinition = {
        {QStringLiteral("indicator"), QStringLiteral("RANK")}};
    candidate.proposedAt = QDateTime(QDate(2026, 8, 23), QTime(9, 0),
                                     QTimeZone::utc());
    candidate.frozenAt = candidate.proposedAt.addSecs(60);

    PortfolioArchiveRecord oldArchive = comparisonArchive(
        QStringLiteral("old-v3"), QStringLiteral("value"), 'b');
    oldArchive.completedAt = candidate.proposedAt.addDays(-1);
    const FactorCandidateEvaluationDecision decision =
        FactorCandidateGovernance::evaluate(
            candidate, oldArchive,
            FactorCandidateEvaluationRole::RetrospectiveReference);

    QVERIFY2(decision.accepted, qPrintable(decision.reason));
    QVERIFY(!decision.preregistered);
    QCOMPARE(decision.roleLabel, QStringLiteral("回溯参考"));
    QVERIFY(decision.reason.contains(QStringLiteral("不计入")));
}

QTEST_GUILESS_MAIN(FactorResearchTest)
#include "tst_factor_research.moc"
