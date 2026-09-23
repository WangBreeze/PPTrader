#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <cmath>

namespace pp {

struct FactorPreprocessingPolicy {
    QString missingValuePolicy = QStringLiteral("pairwise_drop");
    QString winsorizationPolicy = QStringLiteral("none");
    double lowerQuantile = 0.01;
    double upperQuantile = 0.99;
    int minimumPairs = 3;

    bool isValid(QString *error = nullptr) const
    {
        if (missingValuePolicy != QStringLiteral("pairwise_drop")) {
            if (error) *error = QStringLiteral("当前仅支持 pairwise_drop 缺失值策略");
            return false;
        }
        if (winsorizationPolicy != QStringLiteral("none")
            && winsorizationPolicy != QStringLiteral("percentile_clip")) {
            if (error) *error = QStringLiteral("去极值策略必须为 none 或 percentile_clip");
            return false;
        }
        if (!std::isfinite(lowerQuantile) || !std::isfinite(upperQuantile)
            || lowerQuantile < 0.0 || lowerQuantile >= upperQuantile
            || upperQuantile > 1.0 || minimumPairs < 2) {
            if (error) *error = QStringLiteral("因子分位数或最小样本数无效");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("missingValuePolicy"), missingValuePolicy},
                {QStringLiteral("winsorizationPolicy"), winsorizationPolicy},
                {QStringLiteral("lowerQuantile"), lowerQuantile},
                {QStringLiteral("upperQuantile"), upperQuantile},
                {QStringLiteral("minimumPairs"), minimumPairs}};
    }

    static FactorPreprocessingPolicy fromVariantMap(const QVariantMap &map)
    {
        FactorPreprocessingPolicy result;
        result.missingValuePolicy = map.value(
            QStringLiteral("missingValuePolicy"), result.missingValuePolicy).toString();
        result.winsorizationPolicy = map.value(
            QStringLiteral("winsorizationPolicy"), result.winsorizationPolicy).toString();
        result.lowerQuantile = map.value(
            QStringLiteral("lowerQuantile"), result.lowerQuantile).toDouble();
        result.upperQuantile = map.value(
            QStringLiteral("upperQuantile"), result.upperQuantile).toDouble();
        result.minimumPairs = map.value(
            QStringLiteral("minimumPairs"), result.minimumPairs).toInt();
        return result;
    }
};

struct FactorDefinitionVersion {
    QString factorId;
    int version = 0;
    QString name;
    QString alias;
    QString sourceStrategyId;
    int sourceStrategyVersion = 0;
    QVariantMap definition;
    FactorPreprocessingPolicy preprocessing;
    QString contentSha256;
    QDateTime createdAt;

    QString computedContentSha256() const
    {
        const QVariantMap identity {
            {QStringLiteral("definition"), definition},
            {QStringLiteral("preprocessing"), preprocessing.toVariantMap()}};
        const QByteArray bytes = QJsonDocument::fromVariant(identity)
                                     .toJson(QJsonDocument::Compact);
        return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }

    bool isValid(QString *error = nullptr) const
    {
        QString policyError;
        if (factorId.trimmed().isEmpty() || name.trimmed().isEmpty()
            || alias.trimmed().isEmpty() || sourceStrategyId.trimmed().isEmpty()
            || sourceStrategyVersion < 1 || version < 0 || definition.isEmpty()) {
            if (error) *error = QStringLiteral("因子定义身份、来源或内容不完整");
            return false;
        }
        if (!preprocessing.isValid(&policyError)) {
            if (error) *error = policyError;
            return false;
        }
        if (!contentSha256.isEmpty() && contentSha256 != computedContentSha256()) {
            if (error) *error = QStringLiteral("因子定义内容摘要不匹配");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("factorId"), factorId},
                {QStringLiteral("version"), version},
                {QStringLiteral("name"), name},
                {QStringLiteral("alias"), alias},
                {QStringLiteral("sourceStrategyId"), sourceStrategyId},
                {QStringLiteral("sourceStrategyVersion"), sourceStrategyVersion},
                {QStringLiteral("definition"), definition},
                {QStringLiteral("preprocessing"), preprocessing.toVariantMap()},
                {QStringLiteral("contentSha256"), contentSha256},
                {QStringLiteral("createdAt"), createdAt}};
    }
};

struct FactorResearchFrame {
    QDateTime factorTimestamp;
    QDateTime returnTimestamp;
    QHash<QString, double> factorValues;
    QHash<QString, double> forwardReturns;
    QHash<QString, QString> neutralizationGroups;
};

struct FactorResearchCostPolicy {
    double oneWayCostBps = 0.0;

    bool isValid(QString *error = nullptr) const
    {
        if (!std::isfinite(oneWayCostBps)
            || oneWayCostBps < 0.0 || oneWayCostBps > 1000.0) {
            if (error) *error = QStringLiteral("因子研究单边成本必须在 0–1000 bps 之间");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("oneWayCostBps"), oneWayCostBps},
                {QStringLiteral("turnoverConvention"),
                 QStringLiteral("half_sum_absolute_weight_change")},
                {QStringLiteral("initialFormation"),
                 QStringLiteral("excluded")}};
    }

    static FactorResearchCostPolicy fromVariantMap(const QVariantMap &map)
    {
        FactorResearchCostPolicy result;
        result.oneWayCostBps = map.value(
            QStringLiteral("oneWayCostBps"), result.oneWayCostBps).toDouble();
        return result;
    }
};

struct FactorNeutralizationPolicy {
    QString method = QStringLiteral("none");
    QString groupType = QStringLiteral("industry");
    int minimumGroupSize = 2;
    bool requireCompleteCoverage = true;

    bool isValid(QString *error = nullptr) const
    {
        if (method != QStringLiteral("none")
            && method != QStringLiteral("within_group_demean")) {
            if (error) {
                *error = QStringLiteral(
                    "因子中性化方法必须为 none 或 within_group_demean");
            }
            return false;
        }
        if (groupType != QStringLiteral("industry")
            || minimumGroupSize < 2 || minimumGroupSize > 10000
            || !requireCompleteCoverage) {
            if (error) *error = QStringLiteral("行业中性化分组策略无效");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    bool enabled() const
    {
        return method == QStringLiteral("within_group_demean");
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("method"), method},
                {QStringLiteral("groupType"), groupType},
                {QStringLiteral("minimumGroupSize"), minimumGroupSize},
                {QStringLiteral("requireCompleteCoverage"),
                 requireCompleteCoverage},
                {QStringLiteral("factorTransform"),
                 enabled() ? QStringLiteral("demean_within_group")
                           : QStringLiteral("none")},
                {QStringLiteral("returnTransform"),
                 QStringLiteral("none")}};
    }

    static FactorNeutralizationPolicy fromVariantMap(const QVariantMap &map)
    {
        FactorNeutralizationPolicy result;
        result.method = map.value(
            QStringLiteral("method"), result.method).toString();
        result.groupType = map.value(
            QStringLiteral("groupType"), result.groupType).toString();
        result.minimumGroupSize = map.value(
            QStringLiteral("minimumGroupSize"),
            result.minimumGroupSize).toInt();
        result.requireCompleteCoverage = map.value(
            QStringLiteral("requireCompleteCoverage"),
            result.requireCompleteCoverage).toBool();
        return result;
    }
};

struct FactorResearchPoint {
    QDateTime factorTimestamp;
    QDateTime returnTimestamp;
    int candidatePairCount = 0;
    int validPairCount = 0;
    double ic = 0.0;
    double rankIc = 0.0;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("factorTimestamp"), factorTimestamp},
                {QStringLiteral("returnTimestamp"), returnTimestamp},
                {QStringLiteral("candidatePairCount"), candidatePairCount},
                {QStringLiteral("validPairCount"), validPairCount},
                {QStringLiteral("ic"), ic},
                {QStringLiteral("rankIc"), rankIc}};
    }

    static FactorResearchPoint fromVariantMap(const QVariantMap &map)
    {
        FactorResearchPoint result;
        result.factorTimestamp = map.value(QStringLiteral("factorTimestamp")).toDateTime();
        result.returnTimestamp = map.value(QStringLiteral("returnTimestamp")).toDateTime();
        result.candidatePairCount = map.value(
            QStringLiteral("candidatePairCount")).toInt();
        result.validPairCount = map.value(QStringLiteral("validPairCount")).toInt();
        result.ic = map.value(QStringLiteral("ic")).toDouble();
        result.rankIc = map.value(QStringLiteral("rankIc")).toDouble();
        return result;
    }
};

struct FactorQuantileReturn {
    int quantile = 0;
    int evaluatedFrameCount = 0;
    double meanForwardReturn = 0.0;
    double meanNetForwardReturn = 0.0;
    double meanOneWayTurnover = 0.0;
    double meanTransactionCost = 0.0;
    int turnoverObservationCount = 0;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("quantile"), quantile},
                {QStringLiteral("evaluatedFrameCount"), evaluatedFrameCount},
                {QStringLiteral("meanForwardReturn"), meanForwardReturn},
                {QStringLiteral("meanNetForwardReturn"), meanNetForwardReturn},
                {QStringLiteral("meanOneWayTurnover"), meanOneWayTurnover},
                {QStringLiteral("meanTransactionCost"), meanTransactionCost},
                {QStringLiteral("turnoverObservationCount"),
                 turnoverObservationCount}};
    }

    static FactorQuantileReturn fromVariantMap(const QVariantMap &map)
    {
        FactorQuantileReturn result;
        result.quantile = map.value(QStringLiteral("quantile")).toInt();
        result.evaluatedFrameCount = map.value(
            QStringLiteral("evaluatedFrameCount")).toInt();
        result.meanForwardReturn = map.value(
            QStringLiteral("meanForwardReturn")).toDouble();
        result.meanNetForwardReturn = map.value(
            QStringLiteral("meanNetForwardReturn"),
            result.meanForwardReturn).toDouble();
        result.meanOneWayTurnover = map.value(
            QStringLiteral("meanOneWayTurnover")).toDouble();
        result.meanTransactionCost = map.value(
            QStringLiteral("meanTransactionCost")).toDouble();
        result.turnoverObservationCount = map.value(
            QStringLiteral("turnoverObservationCount")).toInt();
        return result;
    }
};

struct FactorTemporalSegment {
    int segment = 0;
    QDateTime fromFactorTimestamp;
    QDateTime toFactorTimestamp;
    int frameCount = 0;
    double meanIc = 0.0;
    double meanRankIc = 0.0;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("segment"), segment},
                {QStringLiteral("fromFactorTimestamp"), fromFactorTimestamp},
                {QStringLiteral("toFactorTimestamp"), toFactorTimestamp},
                {QStringLiteral("frameCount"), frameCount},
                {QStringLiteral("meanIc"), meanIc},
                {QStringLiteral("meanRankIc"), meanRankIc}};
    }

    static FactorTemporalSegment fromVariantMap(const QVariantMap &map)
    {
        FactorTemporalSegment result;
        result.segment = map.value(QStringLiteral("segment")).toInt();
        result.fromFactorTimestamp = map.value(
            QStringLiteral("fromFactorTimestamp")).toDateTime();
        result.toFactorTimestamp = map.value(
            QStringLiteral("toFactorTimestamp")).toDateTime();
        result.frameCount = map.value(QStringLiteral("frameCount")).toInt();
        result.meanIc = map.value(QStringLiteral("meanIc")).toDouble();
        result.meanRankIc = map.value(QStringLiteral("meanRankIc")).toDouble();
        return result;
    }
};

struct FactorOutOfSamplePolicy {
    QString method = QStringLiteral("expanding_non_overlapping");
    int initialTrainingFrames = 20;
    int evaluationFrames = 10;
    int stepFrames = 10;

    bool isValid(QString *error = nullptr) const
    {
        if (method != QStringLiteral("expanding_non_overlapping")
            || initialTrainingFrames < 20 || evaluationFrames < 1
            || stepFrames != evaluationFrames) {
            if (error) {
                *error = QStringLiteral(
                    "样本外策略必须为至少 20 帧扩展训练窗和不重叠固定测试窗");
            }
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("method"), method},
                {QStringLiteral("initialTrainingFrames"), initialTrainingFrames},
                {QStringLiteral("evaluationFrames"), evaluationFrames},
                {QStringLiteral("stepFrames"), stepFrames},
                {QStringLiteral("partialFinalWindow"), QStringLiteral("excluded")},
                {QStringLiteral("parameterRefit"), QStringLiteral("none")}};
    }

    static FactorOutOfSamplePolicy fromVariantMap(const QVariantMap &map)
    {
        FactorOutOfSamplePolicy result;
        result.method = map.value(QStringLiteral("method"), result.method).toString();
        result.initialTrainingFrames = map.value(
            QStringLiteral("initialTrainingFrames"),
            result.initialTrainingFrames).toInt();
        result.evaluationFrames = map.value(
            QStringLiteral("evaluationFrames"), result.evaluationFrames).toInt();
        result.stepFrames = map.value(
            QStringLiteral("stepFrames"), result.stepFrames).toInt();
        return result;
    }
};

struct FactorOutOfSampleWindow {
    int window = 0;
    int trainingFrameCount = 0;
    int evaluationFrameCount = 0;
    QDateTime trainingFromFactorTimestamp;
    QDateTime trainingToFactorTimestamp;
    QDateTime evaluationFromFactorTimestamp;
    QDateTime evaluationToFactorTimestamp;
    double meanIc = 0.0;
    double meanRankIc = 0.0;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("window"), window},
                {QStringLiteral("trainingFrameCount"), trainingFrameCount},
                {QStringLiteral("evaluationFrameCount"), evaluationFrameCount},
                {QStringLiteral("trainingFromFactorTimestamp"),
                 trainingFromFactorTimestamp},
                {QStringLiteral("trainingToFactorTimestamp"),
                 trainingToFactorTimestamp},
                {QStringLiteral("evaluationFromFactorTimestamp"),
                 evaluationFromFactorTimestamp},
                {QStringLiteral("evaluationToFactorTimestamp"),
                 evaluationToFactorTimestamp},
                {QStringLiteral("meanIc"), meanIc},
                {QStringLiteral("meanRankIc"), meanRankIc}};
    }

    static FactorOutOfSampleWindow fromVariantMap(const QVariantMap &map)
    {
        FactorOutOfSampleWindow result;
        result.window = map.value(QStringLiteral("window")).toInt();
        result.trainingFrameCount = map.value(
            QStringLiteral("trainingFrameCount")).toInt();
        result.evaluationFrameCount = map.value(
            QStringLiteral("evaluationFrameCount")).toInt();
        result.trainingFromFactorTimestamp = map.value(
            QStringLiteral("trainingFromFactorTimestamp")).toDateTime();
        result.trainingToFactorTimestamp = map.value(
            QStringLiteral("trainingToFactorTimestamp")).toDateTime();
        result.evaluationFromFactorTimestamp = map.value(
            QStringLiteral("evaluationFromFactorTimestamp")).toDateTime();
        result.evaluationToFactorTimestamp = map.value(
            QStringLiteral("evaluationToFactorTimestamp")).toDateTime();
        result.meanIc = map.value(QStringLiteral("meanIc")).toDouble();
        result.meanRankIc = map.value(QStringLiteral("meanRankIc")).toDouble();
        return result;
    }
};

struct FactorOutOfSampleSummary {
    bool available = false;
    QString availabilityReason;
    FactorOutOfSamplePolicy policy;
    int totalFrameCount = 0;
    int evaluationFrameCount = 0;
    QDateTime evaluationFromFactorTimestamp;
    QDateTime evaluationToFactorTimestamp;
    double meanIc = 0.0;
    double meanRankIc = 0.0;
    bool icInferenceAvailable = false;
    bool rankIcInferenceAvailable = false;
    int inferenceFrameCount = 0;
    int hacLag = 0;
    double confidenceLevel = 0.95;
    double icMeanStandardError = 0.0;
    double rankIcMeanStandardError = 0.0;
    double icConfidenceLower = 0.0;
    double icConfidenceUpper = 0.0;
    double rankIcConfidenceLower = 0.0;
    double rankIcConfidenceUpper = 0.0;
    double icTwoSidedPValue = 0.0;
    double rankIcTwoSidedPValue = 0.0;
    QVector<FactorOutOfSampleWindow> windows;

    QVariantMap toVariantMap() const
    {
        QVariantList windowMaps;
        windowMaps.reserve(windows.size());
        for (const FactorOutOfSampleWindow &item : windows)
            windowMaps.append(item.toVariantMap());
        return {{QStringLiteral("available"), available},
                {QStringLiteral("availabilityReason"), availabilityReason},
                {QStringLiteral("policy"), policy.toVariantMap()},
                {QStringLiteral("totalFrameCount"), totalFrameCount},
                {QStringLiteral("evaluationFrameCount"), evaluationFrameCount},
                {QStringLiteral("evaluationFromFactorTimestamp"),
                 evaluationFromFactorTimestamp},
                {QStringLiteral("evaluationToFactorTimestamp"),
                 evaluationToFactorTimestamp},
                {QStringLiteral("meanIc"), meanIc},
                {QStringLiteral("meanRankIc"), meanRankIc},
                {QStringLiteral("icInferenceAvailable"), icInferenceAvailable},
                {QStringLiteral("rankIcInferenceAvailable"),
                 rankIcInferenceAvailable},
                {QStringLiteral("inferenceFrameCount"), inferenceFrameCount},
                {QStringLiteral("hacLag"), hacLag},
                {QStringLiteral("confidenceLevel"), confidenceLevel},
                {QStringLiteral("icMeanStandardError"), icMeanStandardError},
                {QStringLiteral("rankIcMeanStandardError"),
                 rankIcMeanStandardError},
                {QStringLiteral("icConfidenceLower"), icConfidenceLower},
                {QStringLiteral("icConfidenceUpper"), icConfidenceUpper},
                {QStringLiteral("rankIcConfidenceLower"),
                 rankIcConfidenceLower},
                {QStringLiteral("rankIcConfidenceUpper"),
                 rankIcConfidenceUpper},
                {QStringLiteral("icTwoSidedPValue"), icTwoSidedPValue},
                {QStringLiteral("rankIcTwoSidedPValue"),
                 rankIcTwoSidedPValue},
                {QStringLiteral("windows"), windowMaps}};
    }

    static FactorOutOfSampleSummary fromVariantMap(const QVariantMap &map)
    {
        FactorOutOfSampleSummary result;
        result.available = map.value(QStringLiteral("available")).toBool();
        result.availabilityReason = map.value(
            QStringLiteral("availabilityReason")).toString();
        result.policy = FactorOutOfSamplePolicy::fromVariantMap(
            map.value(QStringLiteral("policy")).toMap());
        result.totalFrameCount = map.value(
            QStringLiteral("totalFrameCount")).toInt();
        result.evaluationFrameCount = map.value(
            QStringLiteral("evaluationFrameCount")).toInt();
        result.evaluationFromFactorTimestamp = map.value(
            QStringLiteral("evaluationFromFactorTimestamp")).toDateTime();
        result.evaluationToFactorTimestamp = map.value(
            QStringLiteral("evaluationToFactorTimestamp")).toDateTime();
        result.meanIc = map.value(QStringLiteral("meanIc")).toDouble();
        result.meanRankIc = map.value(QStringLiteral("meanRankIc")).toDouble();
        result.icInferenceAvailable = map.value(
            QStringLiteral("icInferenceAvailable")).toBool();
        result.rankIcInferenceAvailable = map.value(
            QStringLiteral("rankIcInferenceAvailable")).toBool();
        result.inferenceFrameCount = map.value(
            QStringLiteral("inferenceFrameCount")).toInt();
        result.hacLag = map.value(QStringLiteral("hacLag")).toInt();
        result.confidenceLevel = map.value(
            QStringLiteral("confidenceLevel"), result.confidenceLevel).toDouble();
        result.icMeanStandardError = map.value(
            QStringLiteral("icMeanStandardError")).toDouble();
        result.rankIcMeanStandardError = map.value(
            QStringLiteral("rankIcMeanStandardError")).toDouble();
        result.icConfidenceLower = map.value(
            QStringLiteral("icConfidenceLower")).toDouble();
        result.icConfidenceUpper = map.value(
            QStringLiteral("icConfidenceUpper")).toDouble();
        result.rankIcConfidenceLower = map.value(
            QStringLiteral("rankIcConfidenceLower")).toDouble();
        result.rankIcConfidenceUpper = map.value(
            QStringLiteral("rankIcConfidenceUpper")).toDouble();
        result.icTwoSidedPValue = map.value(
            QStringLiteral("icTwoSidedPValue")).toDouble();
        result.rankIcTwoSidedPValue = map.value(
            QStringLiteral("rankIcTwoSidedPValue")).toDouble();
        const QVariantList windowMaps = map.value(QStringLiteral("windows")).toList();
        result.windows.reserve(windowMaps.size());
        for (const QVariant &value : windowMaps) {
            result.windows.append(
                FactorOutOfSampleWindow::fromVariantMap(value.toMap()));
        }
        return result;
    }
};

struct FactorResearchSummary {
    QString factorId;
    int factorVersion = 0;
    QString factorAlias;
    FactorPreprocessingPolicy preprocessing;
    int candidatePairCount = 0;
    int validPairCount = 0;
    int evaluatedFrameCount = 0;
    int skippedFrameCount = 0;
    double coverageRatio = 0.0;
    double meanIc = 0.0;
    double meanRankIc = 0.0;
    double positiveIcFrameRatio = 0.0;
    double icStandardDeviation = 0.0;
    double rankIcStandardDeviation = 0.0;
    double icInformationRatio = 0.0;
    double rankIcInformationRatio = 0.0;
    bool icirAvailable = false;
    bool rankIcirAvailable = false;
    bool icInferenceAvailable = false;
    bool rankIcInferenceAvailable = false;
    int inferenceFrameCount = 0;
    int hacLag = 0;
    double confidenceLevel = 0.95;
    double icMeanStandardError = 0.0;
    double rankIcMeanStandardError = 0.0;
    double icConfidenceLower = 0.0;
    double icConfidenceUpper = 0.0;
    double rankIcConfidenceLower = 0.0;
    double rankIcConfidenceUpper = 0.0;
    double icTwoSidedPValue = 0.0;
    double rankIcTwoSidedPValue = 0.0;
    bool temporalStabilityAvailable = false;
    bool icSameSignAcrossSegments = false;
    bool rankIcSameSignAcrossSegments = false;
    QVector<FactorTemporalSegment> temporalSegments;
    FactorOutOfSampleSummary outOfSample;
    QVector<FactorResearchPoint> timeSeries;
    FactorResearchCostPolicy costPolicy;
    FactorNeutralizationPolicy neutralizationPolicy;
    int neutralizationAppliedFrameCount = 0;
    int neutralizationSkippedFrameCount = 0;
    int neutralizedPairCount = 0;
    int smallGroupDroppedPairCount = 0;
    int quantileCount = 5;
    QVector<FactorQuantileReturn> quantileReturns;
    double topMinusBottomMeanReturn = 0.0;
    double topMinusBottomMeanNetReturn = 0.0;
    double topMinusBottomMeanTransactionCost = 0.0;
    double topMinusBottomMeanOneWayTurnover = 0.0;
    int topMinusBottomFrameCount = 0;
    int turnoverObservationCount = 0;

    QVariantMap toVariantMap() const
    {
        QVariantList series;
        series.reserve(timeSeries.size());
        for (const FactorResearchPoint &point : timeSeries)
            series.append(point.toVariantMap());
        QVariantList quantiles;
        quantiles.reserve(quantileReturns.size());
        for (const FactorQuantileReturn &quantile : quantileReturns)
            quantiles.append(quantile.toVariantMap());
        QVariantList segments;
        segments.reserve(temporalSegments.size());
        for (const FactorTemporalSegment &segment : temporalSegments)
            segments.append(segment.toVariantMap());
        return {{QStringLiteral("factorId"), factorId},
                {QStringLiteral("factorVersion"), factorVersion},
                {QStringLiteral("factorAlias"), factorAlias},
                {QStringLiteral("preprocessing"), preprocessing.toVariantMap()},
                {QStringLiteral("candidatePairCount"), candidatePairCount},
                {QStringLiteral("validPairCount"), validPairCount},
                {QStringLiteral("evaluatedFrameCount"), evaluatedFrameCount},
                {QStringLiteral("skippedFrameCount"), skippedFrameCount},
                {QStringLiteral("coverageRatio"), coverageRatio},
                {QStringLiteral("meanIc"), meanIc},
                {QStringLiteral("meanRankIc"), meanRankIc},
                {QStringLiteral("positiveIcFrameRatio"), positiveIcFrameRatio},
                {QStringLiteral("icStandardDeviation"), icStandardDeviation},
                {QStringLiteral("rankIcStandardDeviation"), rankIcStandardDeviation},
                {QStringLiteral("icInformationRatio"), icInformationRatio},
                {QStringLiteral("rankIcInformationRatio"), rankIcInformationRatio},
                {QStringLiteral("icirAvailable"), icirAvailable},
                {QStringLiteral("rankIcirAvailable"), rankIcirAvailable},
                {QStringLiteral("icInferenceAvailable"), icInferenceAvailable},
                {QStringLiteral("rankIcInferenceAvailable"),
                 rankIcInferenceAvailable},
                {QStringLiteral("inferenceFrameCount"), inferenceFrameCount},
                {QStringLiteral("hacLag"), hacLag},
                {QStringLiteral("confidenceLevel"), confidenceLevel},
                {QStringLiteral("icMeanStandardError"), icMeanStandardError},
                {QStringLiteral("rankIcMeanStandardError"),
                 rankIcMeanStandardError},
                {QStringLiteral("icConfidenceLower"), icConfidenceLower},
                {QStringLiteral("icConfidenceUpper"), icConfidenceUpper},
                {QStringLiteral("rankIcConfidenceLower"),
                 rankIcConfidenceLower},
                {QStringLiteral("rankIcConfidenceUpper"),
                 rankIcConfidenceUpper},
                {QStringLiteral("icTwoSidedPValue"), icTwoSidedPValue},
                {QStringLiteral("rankIcTwoSidedPValue"),
                 rankIcTwoSidedPValue},
                {QStringLiteral("temporalStabilityAvailable"),
                 temporalStabilityAvailable},
                {QStringLiteral("icSameSignAcrossSegments"),
                 icSameSignAcrossSegments},
                {QStringLiteral("rankIcSameSignAcrossSegments"),
                 rankIcSameSignAcrossSegments},
                {QStringLiteral("temporalSegments"), segments},
                {QStringLiteral("outOfSample"), outOfSample.toVariantMap()},
                {QStringLiteral("timeSeries"), series},
                {QStringLiteral("costPolicy"), costPolicy.toVariantMap()},
                {QStringLiteral("neutralizationPolicy"),
                 neutralizationPolicy.toVariantMap()},
                {QStringLiteral("neutralizationAppliedFrameCount"),
                 neutralizationAppliedFrameCount},
                {QStringLiteral("neutralizationSkippedFrameCount"),
                 neutralizationSkippedFrameCount},
                {QStringLiteral("neutralizedPairCount"), neutralizedPairCount},
                {QStringLiteral("smallGroupDroppedPairCount"),
                 smallGroupDroppedPairCount},
                {QStringLiteral("quantileCount"), quantileCount},
                {QStringLiteral("quantileReturns"), quantiles},
                {QStringLiteral("topMinusBottomMeanReturn"),
                 topMinusBottomMeanReturn},
                {QStringLiteral("topMinusBottomMeanNetReturn"),
                 topMinusBottomMeanNetReturn},
                {QStringLiteral("topMinusBottomMeanTransactionCost"),
                 topMinusBottomMeanTransactionCost},
                {QStringLiteral("topMinusBottomMeanOneWayTurnover"),
                 topMinusBottomMeanOneWayTurnover},
                {QStringLiteral("topMinusBottomFrameCount"),
                 topMinusBottomFrameCount},
                {QStringLiteral("turnoverObservationCount"),
                 turnoverObservationCount}};
    }

    static FactorResearchSummary fromVariantMap(const QVariantMap &map)
    {
        FactorResearchSummary result;
        result.factorId = map.value(QStringLiteral("factorId")).toString();
        result.factorVersion = map.value(QStringLiteral("factorVersion")).toInt();
        result.factorAlias = map.value(QStringLiteral("factorAlias")).toString();
        result.preprocessing = FactorPreprocessingPolicy::fromVariantMap(
            map.value(QStringLiteral("preprocessing")).toMap());
        result.candidatePairCount = map.value(QStringLiteral("candidatePairCount")).toInt();
        result.validPairCount = map.value(QStringLiteral("validPairCount")).toInt();
        result.evaluatedFrameCount = map.value(QStringLiteral("evaluatedFrameCount")).toInt();
        result.skippedFrameCount = map.value(QStringLiteral("skippedFrameCount")).toInt();
        result.coverageRatio = map.value(QStringLiteral("coverageRatio")).toDouble();
        result.meanIc = map.value(QStringLiteral("meanIc")).toDouble();
        result.meanRankIc = map.value(QStringLiteral("meanRankIc")).toDouble();
        result.positiveIcFrameRatio = map.value(
            QStringLiteral("positiveIcFrameRatio")).toDouble();
        result.icStandardDeviation = map.value(
            QStringLiteral("icStandardDeviation")).toDouble();
        result.rankIcStandardDeviation = map.value(
            QStringLiteral("rankIcStandardDeviation")).toDouble();
        result.icInformationRatio = map.value(
            QStringLiteral("icInformationRatio")).toDouble();
        result.rankIcInformationRatio = map.value(
            QStringLiteral("rankIcInformationRatio")).toDouble();
        result.icirAvailable = map.value(QStringLiteral("icirAvailable")).toBool();
        result.rankIcirAvailable = map.value(
            QStringLiteral("rankIcirAvailable")).toBool();
        result.icInferenceAvailable = map.value(
            QStringLiteral("icInferenceAvailable")).toBool();
        result.rankIcInferenceAvailable = map.value(
            QStringLiteral("rankIcInferenceAvailable")).toBool();
        result.inferenceFrameCount = map.value(
            QStringLiteral("inferenceFrameCount")).toInt();
        result.hacLag = map.value(QStringLiteral("hacLag")).toInt();
        result.confidenceLevel = map.value(
            QStringLiteral("confidenceLevel"), result.confidenceLevel).toDouble();
        result.icMeanStandardError = map.value(
            QStringLiteral("icMeanStandardError")).toDouble();
        result.rankIcMeanStandardError = map.value(
            QStringLiteral("rankIcMeanStandardError")).toDouble();
        result.icConfidenceLower = map.value(
            QStringLiteral("icConfidenceLower")).toDouble();
        result.icConfidenceUpper = map.value(
            QStringLiteral("icConfidenceUpper")).toDouble();
        result.rankIcConfidenceLower = map.value(
            QStringLiteral("rankIcConfidenceLower")).toDouble();
        result.rankIcConfidenceUpper = map.value(
            QStringLiteral("rankIcConfidenceUpper")).toDouble();
        result.icTwoSidedPValue = map.value(
            QStringLiteral("icTwoSidedPValue")).toDouble();
        result.rankIcTwoSidedPValue = map.value(
            QStringLiteral("rankIcTwoSidedPValue")).toDouble();
        result.temporalStabilityAvailable = map.value(
            QStringLiteral("temporalStabilityAvailable")).toBool();
        result.icSameSignAcrossSegments = map.value(
            QStringLiteral("icSameSignAcrossSegments")).toBool();
        result.rankIcSameSignAcrossSegments = map.value(
            QStringLiteral("rankIcSameSignAcrossSegments")).toBool();
        const QVariantList segments = map.value(
            QStringLiteral("temporalSegments")).toList();
        result.temporalSegments.reserve(segments.size());
        for (const QVariant &value : segments) {
            result.temporalSegments.append(
                FactorTemporalSegment::fromVariantMap(value.toMap()));
        }
        result.outOfSample = FactorOutOfSampleSummary::fromVariantMap(
            map.value(QStringLiteral("outOfSample")).toMap());
        const QVariantList series = map.value(QStringLiteral("timeSeries")).toList();
        result.timeSeries.reserve(series.size());
        for (const QVariant &value : series)
            result.timeSeries.append(FactorResearchPoint::fromVariantMap(value.toMap()));
        result.costPolicy = FactorResearchCostPolicy::fromVariantMap(
            map.value(QStringLiteral("costPolicy")).toMap());
        result.neutralizationPolicy =
            FactorNeutralizationPolicy::fromVariantMap(
                map.value(QStringLiteral("neutralizationPolicy")).toMap());
        result.neutralizationAppliedFrameCount = map.value(
            QStringLiteral("neutralizationAppliedFrameCount")).toInt();
        result.neutralizationSkippedFrameCount = map.value(
            QStringLiteral("neutralizationSkippedFrameCount")).toInt();
        result.neutralizedPairCount = map.value(
            QStringLiteral("neutralizedPairCount")).toInt();
        result.smallGroupDroppedPairCount = map.value(
            QStringLiteral("smallGroupDroppedPairCount")).toInt();
        result.quantileCount = map.value(
            QStringLiteral("quantileCount"), result.quantileCount).toInt();
        const QVariantList quantiles = map.value(
            QStringLiteral("quantileReturns")).toList();
        result.quantileReturns.reserve(quantiles.size());
        for (const QVariant &value : quantiles) {
            result.quantileReturns.append(
                FactorQuantileReturn::fromVariantMap(value.toMap()));
        }
        result.topMinusBottomMeanReturn = map.value(
            QStringLiteral("topMinusBottomMeanReturn")).toDouble();
        result.topMinusBottomMeanNetReturn = map.value(
            QStringLiteral("topMinusBottomMeanNetReturn"),
            result.topMinusBottomMeanReturn).toDouble();
        result.topMinusBottomMeanTransactionCost = map.value(
            QStringLiteral("topMinusBottomMeanTransactionCost")).toDouble();
        result.topMinusBottomMeanOneWayTurnover = map.value(
            QStringLiteral("topMinusBottomMeanOneWayTurnover")).toDouble();
        result.topMinusBottomFrameCount = map.value(
            QStringLiteral("topMinusBottomFrameCount")).toInt();
        result.turnoverObservationCount = map.value(
            QStringLiteral("turnoverObservationCount")).toInt();
        return result;
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::FactorDefinitionVersion)
Q_DECLARE_METATYPE(pp::FactorResearchSummary)
