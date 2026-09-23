#include "models/FactorResearchComparisonListModel.h"

#include "services/MultipleTestingCorrection.h"

#include <QCryptographicHash>
#include <QJsonDocument>

#include <cmath>

namespace pp {

namespace {

constexpr int MaximumComparisonCount = 4;

QVariantList datasetIdentityMaps(const PortfolioResult &result)
{
    QVariantList maps;
    maps.reserve(result.datasetIdentities.size());
    for (const BacktestDatasetIdentity &identity : result.datasetIdentities)
        maps.append(identity.toVariantMap());
    return maps;
}

QVariantList alignedFrameIdentity(const FactorResearchSummary &summary)
{
    QVariantList frames;
    frames.reserve(summary.timeSeries.size());
    for (const FactorResearchPoint &point : summary.timeSeries) {
        frames.append(QVariantMap{
            {QStringLiteral("factorTimestamp"), point.factorTimestamp},
            {QStringLiteral("returnTimestamp"), point.returnTimestamp}});
    }
    return frames;
}

} // namespace

FactorResearchComparisonListModel::FactorResearchComparisonListModel(
    QObject *parent)
    : QAbstractListModel(parent)
{
}

int FactorResearchComparisonListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int FactorResearchComparisonListModel::count() const
{
    return m_entries.size();
}

int FactorResearchComparisonListModel::maximumCount() const
{
    return MaximumComparisonCount;
}

int FactorResearchComparisonListModel::multipleTestingCandidateCount() const
{
    return m_multipleTestingCandidateCount;
}

QString FactorResearchComparisonListModel::
multipleTestingCandidateSetSha256() const
{
    return m_multipleTestingCandidateSetSha256;
}

QString FactorResearchComparisonListModel::multipleTestingStatusText() const
{
    if (m_multipleTestingCandidateCount < 2) {
        return QStringLiteral(
            "至少选择 2 个受控可比且具备样本外 HAC p 值的归档");
    }
    return QStringLiteral("Holm–Bonferroni / Benjamini–Hochberg · α=0.05 · %1 项")
        .arg(m_multipleTestingCandidateCount);
}

QVariant FactorResearchComparisonListModel::data(const QModelIndex &index,
                                                  int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const QVariantMap map = entryMap(m_entries.at(index.row()));
    const QByteArray roleName = roleNames().value(role);
    return roleName.isEmpty() ? QVariant{} : map.value(QString::fromLatin1(roleName));
}

QHash<int, QByteArray> FactorResearchComparisonListModel::roleNames() const
{
    return {{ResultIdRole, "resultId"}, {NameRole, "name"},
            {CompletedAtRole, "completedAt"}, {FactorIdRole, "factorId"},
            {FactorVersionRole, "factorVersion"},
            {FactorAliasRole, "factorAlias"},
            {StrategyIdRole, "strategyId"},
            {StrategyVersionRole, "strategyVersion"},
            {PeriodSecondsRole, "periodSeconds"}, {LegCountRole, "legCount"},
            {EvaluatedFrameCountRole, "evaluatedFrameCount"},
            {CoverageRatioRole, "coverageRatio"}, {MeanIcRole, "meanIc"},
            {MeanRankIcRole, "meanRankIc"},
            {IcInformationRatioRole, "icInformationRatio"},
            {RankIcInformationRatioRole, "rankIcInformationRatio"},
            {IcirAvailableRole, "icirAvailable"},
            {RankIcirAvailableRole, "rankIcirAvailable"},
            {IcInferenceAvailableRole, "icInferenceAvailable"},
            {RankIcInferenceAvailableRole, "rankIcInferenceAvailable"},
            {InferenceFrameCountRole, "inferenceFrameCount"},
            {HacLagRole, "hacLag"},
            {IcConfidenceLowerRole, "icConfidenceLower"},
            {IcConfidenceUpperRole, "icConfidenceUpper"},
            {RankIcConfidenceLowerRole, "rankIcConfidenceLower"},
            {RankIcConfidenceUpperRole, "rankIcConfidenceUpper"},
            {IcTwoSidedPValueRole, "icTwoSidedPValue"},
            {RankIcTwoSidedPValueRole, "rankIcTwoSidedPValue"},
            {TemporalStabilityAvailableRole, "temporalStabilityAvailable"},
            {IcSameSignAcrossSegmentsRole, "icSameSignAcrossSegments"},
            {RankIcSameSignAcrossSegmentsRole,
             "rankIcSameSignAcrossSegments"},
            {TemporalSegmentsRole, "temporalSegments"},
            {TopMinusBottomMeanReturnRole, "topMinusBottomMeanReturn"},
            {TopMinusBottomMeanNetReturnRole, "topMinusBottomMeanNetReturn"},
            {MeanOneWayTurnoverRole, "meanOneWayTurnover"},
            {OneWayCostBpsRole, "oneWayCostBps"},
            {ResearchMethodVersionRole, "researchMethodVersion"},
            {NeutralizationMethodRole, "neutralizationMethod"},
            {ControlledComparableRole, "controlledComparable"},
            {ComparabilityLabelRole, "comparabilityLabel"},
            {ComparabilityReasonRole, "comparabilityReason"},
            {ComparisonBasisSha256Role, "comparisonBasisSha256"},
            {OutOfSampleAvailableRole, "outOfSampleAvailable"},
            {OutOfSampleEvaluationFrameCountRole,
             "outOfSampleEvaluationFrameCount"},
            {OutOfSampleWindowCountRole, "outOfSampleWindowCount"},
            {OutOfSampleMeanIcRole, "outOfSampleMeanIc"},
            {OutOfSampleIcInferenceAvailableRole,
             "outOfSampleIcInferenceAvailable"},
            {OutOfSampleIcConfidenceLowerRole,
             "outOfSampleIcConfidenceLower"},
            {OutOfSampleIcConfidenceUpperRole,
             "outOfSampleIcConfidenceUpper"},
            {OutOfSampleIcTwoSidedPValueRole,
             "outOfSampleIcTwoSidedPValue"},
            {MultipleTestingEligibleRole, "multipleTestingEligible"},
            {HolmAdjustedPValueRole, "holmAdjustedPValue"},
            {BhAdjustedPValueRole, "bhAdjustedPValue"},
            {HolmRejectedRole, "holmRejected"},
            {BhRejectedRole, "bhRejected"},
            {MultipleTestingCandidateSetSha256Role,
             "multipleTestingCandidateSetSha256"}};
}

QVariantMap FactorResearchComparisonListModel::get(int row) const
{
    return row >= 0 && row < m_entries.size()
        ? entryMap(m_entries.at(row)) : QVariantMap{};
}

bool FactorResearchComparisonListModel::contains(const QString &resultId) const
{
    for (const Entry &entry : m_entries) {
        if (entry.archive.resultId == resultId) return true;
    }
    return false;
}

bool FactorResearchComparisonListModel::addArchive(
    const PortfolioArchiveRecord &record, QString *error)
{
    if (record.resultId.trimmed().isEmpty()
        || record.result.resultKind != QStringLiteral("cross_sectional")
        || record.result.factorResearchSummary.factorId.trimmed().isEmpty()
        || record.result.immutableIdentity
               .value(QStringLiteral("factorResearch")).toMap().isEmpty()) {
        if (error) *error = QStringLiteral("归档没有可比较的不可变因子研究身份");
        return false;
    }
    if (contains(record.resultId)) {
        if (error) *error = QStringLiteral("该因子研究归档已选择");
        return false;
    }
    if (m_entries.size() >= MaximumComparisonCount) {
        if (error) *error = QStringLiteral("最多同时选择 4 个因子研究归档");
        return false;
    }

    Entry entry;
    entry.archive = record;
    entry.basis = comparisonBasis(record);
    entry.basisSha256 = basisSha256(entry.basis);
    if (m_entries.isEmpty()) {
        entry.controlledComparable = true;
        entry.comparabilityLabel = QStringLiteral("比较基准");
        entry.comparabilityReason = QStringLiteral("首个选择的归档作为比较基准");
    } else {
        entry.comparabilityReason = comparisonDifference(
            m_entries.first().basis, entry.basis);
        entry.controlledComparable = entry.comparabilityReason.isEmpty();
        entry.comparabilityLabel = entry.controlledComparable
            ? QStringLiteral("受控可比") : QStringLiteral("描述性对照");
        if (entry.controlledComparable)
            entry.comparabilityReason = QStringLiteral("研究基准身份一致");
    }
    const int row = m_entries.size();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.append(entry);
    endInsertRows();
    refreshMultipleTesting();
    emit dataChanged(index(0, 0), index(row, 0),
                     {MultipleTestingEligibleRole,
                      HolmAdjustedPValueRole, BhAdjustedPValueRole,
                      HolmRejectedRole, BhRejectedRole,
                      MultipleTestingCandidateSetSha256Role});
    emit countChanged();
    emit multipleTestingChanged();
    if (error) error->clear();
    return true;
}

bool FactorResearchComparisonListModel::removeArchive(const QString &resultId)
{
    int row = -1;
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).archive.resultId == resultId) {
            row = index;
            break;
        }
    }
    if (row < 0) return false;
    beginResetModel();
    m_entries.removeAt(row);
    refreshComparability();
    endResetModel();
    emit countChanged();
    emit multipleTestingChanged();
    return true;
}

void FactorResearchComparisonListModel::clear()
{
    if (m_entries.isEmpty()) return;
    beginResetModel();
    m_entries.clear();
    refreshMultipleTesting();
    endResetModel();
    emit countChanged();
    emit multipleTestingChanged();
}

QVariantMap FactorResearchComparisonListModel::comparisonBasis(
    const PortfolioArchiveRecord &record)
{
    const PortfolioResult &result = record.result;
    const QVariantMap identity = result.immutableIdentity;
    const QVariantMap factor = identity.value(QStringLiteral("factorResearch")).toMap();
    const QVariantMap calendar = identity.value(QStringLiteral("calendarSnapshot")).toMap();
    return {
        {QStringLiteral("periodSeconds"), result.definition.periodSeconds},
        {QStringLiteral("requestedBars"), identity.value(QStringLiteral("requestedBars"))},
        {QStringLiteral("actualBars"), identity.value(QStringLiteral("actualBars"))},
        {QStringLiteral("universeSymbols"), identity.value(QStringLiteral("universeSymbols"))},
        {QStringLiteral("datasetIdentities"), datasetIdentityMaps(result)},
        {QStringLiteral("alignedFrames"), alignedFrameIdentity(result.factorResearchSummary)},
        {QStringLiteral("calendarSnapshotVersion"),
         calendar.value(QStringLiteral("snapshotVersion"))},
        {QStringLiteral("preprocessing"), factor.value(QStringLiteral("preprocessing"))},
        {QStringLiteral("returnHorizon"), factor.value(QStringLiteral("returnHorizon"))},
        {QStringLiteral("returnCalculation"), factor.value(QStringLiteral("returnCalculation"))},
        {QStringLiteral("frameAggregation"), factor.value(QStringLiteral("frameAggregation"))},
        {QStringLiteral("researchMethodVersion"),
         factor.value(QStringLiteral("researchMethodVersion"))},
        {QStringLiteral("quantileCount"), factor.value(QStringLiteral("quantileCount"))},
        {QStringLiteral("quantileMethod"), factor.value(QStringLiteral("quantileMethod"))},
        {QStringLiteral("quantileAggregation"),
         factor.value(QStringLiteral("quantileAggregation"))},
        {QStringLiteral("spreadDefinition"), factor.value(QStringLiteral("spreadDefinition"))},
        {QStringLiteral("costPolicy"), factor.value(QStringLiteral("costPolicy"))},
        {QStringLiteral("neutralizationPolicy"),
         factor.value(QStringLiteral("neutralizationPolicy"))},
        {QStringLiteral("neutralizationDatasetId"),
         factor.value(QStringLiteral("neutralizationDatasetId"))},
        {QStringLiteral("neutralizationSnapshotVersion"),
         factor.value(QStringLiteral("neutralizationSnapshotVersion"))},
        {QStringLiteral("neutralizationAsOfDate"),
         factor.value(QStringLiteral("neutralizationAsOfDate"))},
        {QStringLiteral("inferenceMethodVersion"),
         factor.value(QStringLiteral("inferenceMethodVersion"))},
        {QStringLiteral("inferenceMinimumFrames"),
         factor.value(QStringLiteral("inferenceMinimumFrames"))},
        {QStringLiteral("confidenceLevel"),
         factor.value(QStringLiteral("confidenceLevel"))},
        {QStringLiteral("hacLagRule"),
         factor.value(QStringLiteral("hacLagRule"))},
        {QStringLiteral("temporalSegmentCount"),
         factor.value(QStringLiteral("temporalSegmentCount"))},
        {QStringLiteral("temporalSegmentMethod"),
         factor.value(QStringLiteral("temporalSegmentMethod"))},
        {QStringLiteral("outOfSampleMethodVersion"),
         factor.value(QStringLiteral("outOfSampleMethodVersion"))},
        {QStringLiteral("outOfSamplePolicy"),
         factor.value(QStringLiteral("outOfSamplePolicy"))},
        {QStringLiteral("outOfSampleInferenceMethodVersion"),
         factor.value(QStringLiteral("outOfSampleInferenceMethodVersion"))}};
}

QString FactorResearchComparisonListModel::basisSha256(const QVariantMap &basis)
{
    const QByteArray json = QJsonDocument::fromVariant(basis)
                                .toJson(QJsonDocument::Compact);
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex()));
}

QString FactorResearchComparisonListModel::comparisonDifference(
    const QVariantMap &baseline, const QVariantMap &candidate)
{
    const struct Difference { const char *key; const char *reason; } differences[] = {
        {"periodSeconds", "周期不同"}, {"requestedBars", "请求样本范围不同"},
        {"actualBars", "实际样本范围不同"}, {"universeSymbols", "股票池不同"},
        {"datasetIdentities", "逐腿数据身份不同"}, {"alignedFrames", "对齐帧不同"},
        {"calendarSnapshotVersion", "交易日历快照不同"},
        {"preprocessing", "预处理口径不同"}, {"returnHorizon", "收益期限不同"},
        {"returnCalculation", "收益计算口径不同"},
        {"frameAggregation", "跨帧汇总口径不同"},
        {"researchMethodVersion", "研究方法版本不同"},
        {"quantileCount", "分层数量不同"}, {"quantileMethod", "分层方法不同"},
        {"quantileAggregation", "分层汇总口径不同"},
        {"spreadDefinition", "多空差定义不同"}, {"costPolicy", "成本口径不同"},
        {"neutralizationPolicy", "中性化策略不同"},
        {"neutralizationDatasetId", "中性化数据集不同"},
        {"neutralizationSnapshotVersion", "中性化快照不同"},
        {"neutralizationAsOfDate", "中性化生效日不同"},
        {"inferenceMethodVersion", "统计推断方法不同"},
        {"inferenceMinimumFrames", "统计推断最小样本数不同"},
        {"confidenceLevel", "置信水平不同"},
        {"hacLagRule", "HAC 滞后规则不同"},
        {"temporalSegmentCount", "稳定性分段数量不同"},
        {"temporalSegmentMethod", "稳定性分段方法不同"},
        {"outOfSampleMethodVersion", "样本外方法版本不同"},
        {"outOfSamplePolicy", "样本外窗口策略不同"},
        {"outOfSampleInferenceMethodVersion", "样本外推断方法不同"}};
    for (const Difference &difference : differences) {
        const QString key = QString::fromLatin1(difference.key);
        if (baseline.value(key) != candidate.value(key))
            return QString::fromUtf8(difference.reason);
    }
    return {};
}

QVariantMap FactorResearchComparisonListModel::entryMap(const Entry &entry)
{
    const PortfolioResult &result = entry.archive.result;
    const FactorResearchSummary &summary = result.factorResearchSummary;
    const QVariantMap identity = result.immutableIdentity;
    const QVariantMap factor = identity.value(QStringLiteral("factorResearch")).toMap();
    return {{QStringLiteral("resultId"), entry.archive.resultId},
            {QStringLiteral("name"), result.definition.name},
            {QStringLiteral("completedAt"), entry.archive.completedAt},
            {QStringLiteral("factorId"), summary.factorId},
            {QStringLiteral("factorVersion"), summary.factorVersion},
            {QStringLiteral("factorAlias"), summary.factorAlias},
            {QStringLiteral("strategyId"), identity.value(QStringLiteral("strategyId"))},
            {QStringLiteral("strategyVersion"), identity.value(QStringLiteral("strategyVersion"))},
            {QStringLiteral("periodSeconds"), result.definition.periodSeconds},
            {QStringLiteral("legCount"), result.datasetIdentities.size()},
            {QStringLiteral("evaluatedFrameCount"), summary.evaluatedFrameCount},
            {QStringLiteral("coverageRatio"), summary.coverageRatio},
            {QStringLiteral("meanIc"), summary.meanIc},
            {QStringLiteral("meanRankIc"), summary.meanRankIc},
            {QStringLiteral("icInformationRatio"), summary.icInformationRatio},
            {QStringLiteral("rankIcInformationRatio"), summary.rankIcInformationRatio},
            {QStringLiteral("icirAvailable"), summary.icirAvailable},
            {QStringLiteral("rankIcirAvailable"), summary.rankIcirAvailable},
            {QStringLiteral("icInferenceAvailable"),
             summary.icInferenceAvailable},
            {QStringLiteral("rankIcInferenceAvailable"),
             summary.rankIcInferenceAvailable},
            {QStringLiteral("inferenceFrameCount"), summary.inferenceFrameCount},
            {QStringLiteral("hacLag"), summary.hacLag},
            {QStringLiteral("icConfidenceLower"), summary.icConfidenceLower},
            {QStringLiteral("icConfidenceUpper"), summary.icConfidenceUpper},
            {QStringLiteral("rankIcConfidenceLower"),
             summary.rankIcConfidenceLower},
            {QStringLiteral("rankIcConfidenceUpper"),
             summary.rankIcConfidenceUpper},
            {QStringLiteral("icTwoSidedPValue"), summary.icTwoSidedPValue},
            {QStringLiteral("rankIcTwoSidedPValue"),
             summary.rankIcTwoSidedPValue},
            {QStringLiteral("temporalStabilityAvailable"),
             summary.temporalStabilityAvailable},
            {QStringLiteral("icSameSignAcrossSegments"),
             summary.icSameSignAcrossSegments},
            {QStringLiteral("rankIcSameSignAcrossSegments"),
             summary.rankIcSameSignAcrossSegments},
            {QStringLiteral("temporalSegments"),
             summary.toVariantMap().value(QStringLiteral("temporalSegments"))},
            {QStringLiteral("topMinusBottomMeanReturn"),
             summary.topMinusBottomMeanReturn},
            {QStringLiteral("topMinusBottomMeanNetReturn"),
             summary.topMinusBottomMeanNetReturn},
            {QStringLiteral("meanOneWayTurnover"),
             summary.topMinusBottomMeanOneWayTurnover},
            {QStringLiteral("oneWayCostBps"), summary.costPolicy.oneWayCostBps},
            {QStringLiteral("researchMethodVersion"),
             factor.value(QStringLiteral("researchMethodVersion"))},
            {QStringLiteral("neutralizationMethod"),
             summary.neutralizationPolicy.method},
            {QStringLiteral("controlledComparable"), entry.controlledComparable},
            {QStringLiteral("comparabilityLabel"), entry.comparabilityLabel},
            {QStringLiteral("comparabilityReason"), entry.comparabilityReason},
            {QStringLiteral("comparisonBasisSha256"), entry.basisSha256},
            {QStringLiteral("outOfSampleAvailable"),
             summary.outOfSample.available},
            {QStringLiteral("outOfSampleEvaluationFrameCount"),
             summary.outOfSample.evaluationFrameCount},
            {QStringLiteral("outOfSampleWindowCount"),
             summary.outOfSample.windows.size()},
            {QStringLiteral("outOfSampleMeanIc"),
             summary.outOfSample.meanIc},
            {QStringLiteral("outOfSampleIcInferenceAvailable"),
             summary.outOfSample.icInferenceAvailable},
            {QStringLiteral("outOfSampleIcConfidenceLower"),
             summary.outOfSample.icConfidenceLower},
            {QStringLiteral("outOfSampleIcConfidenceUpper"),
             summary.outOfSample.icConfidenceUpper},
            {QStringLiteral("outOfSampleIcTwoSidedPValue"),
             summary.outOfSample.icTwoSidedPValue},
            {QStringLiteral("multipleTestingEligible"),
             entry.multipleTestingEligible},
            {QStringLiteral("holmAdjustedPValue"),
             entry.holmAdjustedPValue},
            {QStringLiteral("bhAdjustedPValue"),
             entry.bhAdjustedPValue},
            {QStringLiteral("holmRejected"), entry.holmRejected},
            {QStringLiteral("bhRejected"), entry.bhRejected},
            {QStringLiteral("multipleTestingCandidateSetSha256"),
             entry.multipleTestingCandidateSetSha256}};
}

void FactorResearchComparisonListModel::refreshComparability()
{
    if (m_entries.isEmpty()) {
        refreshMultipleTesting();
        return;
    }
    for (int index = 0; index < m_entries.size(); ++index) {
        Entry &entry = m_entries[index];
        if (index == 0) {
            entry.controlledComparable = true;
            entry.comparabilityLabel = QStringLiteral("比较基准");
            entry.comparabilityReason = QStringLiteral("首个选择的归档作为比较基准");
            continue;
        }
        entry.comparabilityReason = comparisonDifference(
            m_entries.first().basis, entry.basis);
        entry.controlledComparable = entry.comparabilityReason.isEmpty();
        entry.comparabilityLabel = entry.controlledComparable
            ? QStringLiteral("受控可比") : QStringLiteral("描述性对照");
        if (entry.controlledComparable)
            entry.comparabilityReason = QStringLiteral("研究基准身份一致");
    }
    refreshMultipleTesting();
}

void FactorResearchComparisonListModel::refreshMultipleTesting()
{
    QVector<MultipleTestingHypothesis> hypotheses;
    for (Entry &entry : m_entries) {
        entry.multipleTestingEligible = false;
        entry.holmAdjustedPValue = 0.0;
        entry.bhAdjustedPValue = 0.0;
        entry.holmRejected = false;
        entry.bhRejected = false;
        entry.multipleTestingCandidateSetSha256.clear();
        const FactorOutOfSampleSummary &summary =
            entry.archive.result.factorResearchSummary.outOfSample;
        if (!entry.controlledComparable || !summary.available
            || !summary.icInferenceAvailable
            || !std::isfinite(summary.icTwoSidedPValue)
            || summary.icTwoSidedPValue < 0.0
            || summary.icTwoSidedPValue > 1.0) {
            continue;
        }
        entry.multipleTestingEligible = true;
        hypotheses.append({entry.archive.resultId,
                           summary.icTwoSidedPValue});
    }
    m_multipleTestingCandidateCount = hypotheses.size();
    m_multipleTestingCandidateSetSha256.clear();
    if (hypotheses.size() < 2) return;

    QString error;
    const MultipleTestingSummary correction =
        MultipleTestingCorrection::adjust(hypotheses, 0.05, &error);
    if (!correction.available) return;
    m_multipleTestingCandidateSetSha256 = correction.candidateSetSha256;
    for (Entry &entry : m_entries) {
        if (!entry.multipleTestingEligible) continue;
        for (const MultipleTestingHypothesis &hypothesis
             : correction.hypotheses) {
            if (hypothesis.hypothesisId != entry.archive.resultId) continue;
            entry.holmAdjustedPValue = hypothesis.holmAdjustedPValue;
            entry.bhAdjustedPValue = hypothesis.bhAdjustedPValue;
            entry.holmRejected = hypothesis.holmRejected;
            entry.bhRejected = hypothesis.bhRejected;
            entry.multipleTestingCandidateSetSha256 =
                correction.candidateSetSha256;
            break;
        }
    }
}

} // namespace pp
