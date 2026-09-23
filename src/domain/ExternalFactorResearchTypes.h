#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/FactorResearchTypes.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pp {

struct ExternalFactorResearchRequest {
    QString candidateId;
    int candidateVersion = 0;
    QString datasetId;
    int periodSeconds = 0;
    FactorPreprocessingPolicy preprocessing;
    FactorResearchCostPolicy costPolicy;
    FactorNeutralizationPolicy neutralization;
    FactorOutOfSamplePolicy outOfSamplePolicy;
};

struct ExternalFactorResearchResult {
    bool succeeded = false;
    QString error;
    FactorResearchSummary summary;
    int observationCount = 0;
    int factorFrameCount = 0;
    int returnFrameCount = 0;
};

struct ExternalFactorResearchArchiveSummary {
    QString resultId;
    QDateTime completedAt;
    QString candidateId;
    int candidateVersion = 0;
    QString datasetId;
    QString factorKey;
    int periodSeconds = 0;
    int evaluatedFrameCount = 0;
    double meanIc = 0.0;
    double meanRankIc = 0.0;
    QString identitySha256;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("resultId"), resultId},
                {QStringLiteral("completedAt"), completedAt},
                {QStringLiteral("candidateId"), candidateId},
                {QStringLiteral("candidateVersion"), candidateVersion},
                {QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("factorKey"), factorKey},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("evaluatedFrameCount"), evaluatedFrameCount},
                {QStringLiteral("meanIc"), meanIc},
                {QStringLiteral("meanRankIc"), meanRankIc},
                {QStringLiteral("identitySha256"), identitySha256}};
    }
};

struct ExternalFactorResearchArchiveRecord {
    QString resultId;
    QDateTime completedAt;
    QString methodVersion = QStringLiteral("external_factor_research_v1");
    QString returnAlignmentMethod = QStringLiteral(
        "completed_close_at_or_before_next_observation_v1");
    QString candidateId;
    int candidateVersion = 0;
    QString candidateContentSha256;
    QString datasetId;
    QString datasetContentSha256;
    QString factorDefinitionSha256;
    QString factorKey;
    int periodSeconds = 0;
    FactorPreprocessingPolicy preprocessing;
    FactorResearchCostPolicy costPolicy;
    FactorNeutralizationPolicy neutralization;
    FactorOutOfSamplePolicy outOfSamplePolicy;
    QVector<BacktestDatasetIdentity> returnDatasetIdentities;
    FactorResearchSummary summary;
    QString identitySha256;

    QVariantMap identityMap() const
    {
        QVariantList identities;
        identities.reserve(returnDatasetIdentities.size());
        for (const BacktestDatasetIdentity &identity : returnDatasetIdentities)
            identities.append(identity.toJson());
        return {{QStringLiteral("methodVersion"), methodVersion},
                {QStringLiteral("returnAlignmentMethod"), returnAlignmentMethod},
                {QStringLiteral("candidateId"), candidateId},
                {QStringLiteral("candidateVersion"), candidateVersion},
                {QStringLiteral("candidateContentSha256"), candidateContentSha256},
                {QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("datasetContentSha256"), datasetContentSha256},
                {QStringLiteral("factorDefinitionSha256"), factorDefinitionSha256},
                {QStringLiteral("factorKey"), factorKey},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("preprocessing"), preprocessing.toVariantMap()},
                {QStringLiteral("costPolicy"), costPolicy.toVariantMap()},
                {QStringLiteral("neutralization"), neutralization.toVariantMap()},
                {QStringLiteral("outOfSamplePolicy"), outOfSamplePolicy.toVariantMap()},
                {QStringLiteral("returnDatasetIdentities"), identities},
                {QStringLiteral("summary"), summary.toVariantMap()}};
    }

    QString computedIdentitySha256() const
    {
        const QByteArray bytes = QJsonDocument::fromVariant(identityMap())
                                     .toJson(QJsonDocument::Compact);
        return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }

    bool isValid(QString *error = nullptr) const
    {
        QString policyError;
        if (resultId.trimmed().isEmpty() || !completedAt.isValid()
            || methodVersion != QStringLiteral("external_factor_research_v1")
            || returnAlignmentMethod != QStringLiteral(
                "completed_close_at_or_before_next_observation_v1")
            || candidateId.trimmed().isEmpty() || candidateVersion < 1
            || datasetId.trimmed().isEmpty() || factorKey.trimmed().isEmpty()
            || periodSeconds <= 0 || returnDatasetIdentities.isEmpty()
            || summary.evaluatedFrameCount < 1
            || summary.timeSeries.size() != summary.evaluatedFrameCount) {
            if (error) *error = QStringLiteral("外部因子研究归档身份或结果不完整");
            return false;
        }
        const QStringList hashes{candidateContentSha256, datasetContentSha256,
                                 factorDefinitionSha256};
        for (const QString &hash : hashes) {
            if (!hash.startsWith(QStringLiteral("sha256:"))) {
                if (error) *error = QStringLiteral("外部因子研究内容摘要不完整");
                return false;
            }
        }
        for (const BacktestDatasetIdentity &identity : returnDatasetIdentities) {
            if (identity.isEmpty() || identity.periodSeconds != periodSeconds
                || !identity.datasetVersion.startsWith(QStringLiteral("sha256:"))) {
                if (error) *error = QStringLiteral("本地收益数据身份不完整或周期不一致");
                return false;
            }
        }
        if (!preprocessing.isValid(&policyError)
            || !costPolicy.isValid(&policyError)
            || !neutralization.isValid(&policyError)
            || !outOfSamplePolicy.isValid(&policyError)) {
            if (error) *error = policyError;
            return false;
        }
        if (!identitySha256.isEmpty()
            && identitySha256 != computedIdentitySha256()) {
            if (error) *error = QStringLiteral("外部因子研究归档摘要不匹配");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        QVariantMap map = identityMap();
        map.insert(QStringLiteral("resultId"), resultId);
        map.insert(QStringLiteral("completedAt"),
                   completedAt.toUTC().toString(Qt::ISODateWithMs));
        map.insert(QStringLiteral("identitySha256"), identitySha256);
        return map;
    }

    static std::optional<ExternalFactorResearchArchiveRecord> fromVariantMap(
        const QVariantMap &map)
    {
        QVariantMap storedIdentity = map;
        storedIdentity.remove(QStringLiteral("resultId"));
        storedIdentity.remove(QStringLiteral("completedAt"));
        const QString storedSha256 = storedIdentity.take(
            QStringLiteral("identitySha256")).toString();
        const QByteArray storedBytes = QJsonDocument::fromVariant(storedIdentity)
                                           .toJson(QJsonDocument::Compact);
        const QString computedStoredSha256 = QStringLiteral("sha256:%1").arg(
            QString::fromLatin1(QCryptographicHash::hash(
                storedBytes, QCryptographicHash::Sha256).toHex()));
        if (storedSha256 != computedStoredSha256) return std::nullopt;

        ExternalFactorResearchArchiveRecord result;
        result.resultId = map.value(QStringLiteral("resultId")).toString();
        result.completedAt = QDateTime::fromString(
            map.value(QStringLiteral("completedAt")).toString(),
            Qt::ISODateWithMs);
        result.methodVersion = map.value(QStringLiteral("methodVersion")).toString();
        result.returnAlignmentMethod = map.value(
            QStringLiteral("returnAlignmentMethod")).toString();
        result.candidateId = map.value(QStringLiteral("candidateId")).toString();
        result.candidateVersion = map.value(
            QStringLiteral("candidateVersion")).toInt();
        result.candidateContentSha256 = map.value(
            QStringLiteral("candidateContentSha256")).toString();
        result.datasetId = map.value(QStringLiteral("datasetId")).toString();
        result.datasetContentSha256 = map.value(
            QStringLiteral("datasetContentSha256")).toString();
        result.factorDefinitionSha256 = map.value(
            QStringLiteral("factorDefinitionSha256")).toString();
        result.factorKey = map.value(QStringLiteral("factorKey")).toString();
        result.periodSeconds = map.value(QStringLiteral("periodSeconds")).toInt();
        result.preprocessing = FactorPreprocessingPolicy::fromVariantMap(
            map.value(QStringLiteral("preprocessing")).toMap());
        result.costPolicy = FactorResearchCostPolicy::fromVariantMap(
            map.value(QStringLiteral("costPolicy")).toMap());
        result.neutralization = FactorNeutralizationPolicy::fromVariantMap(
            map.value(QStringLiteral("neutralization")).toMap());
        result.outOfSamplePolicy = FactorOutOfSamplePolicy::fromVariantMap(
            map.value(QStringLiteral("outOfSamplePolicy")).toMap());
        for (const QVariant &value : map.value(
                 QStringLiteral("returnDatasetIdentities")).toList()) {
            BacktestDatasetIdentity identity;
            if (!identity.fromJson(value.toString())) return std::nullopt;
            result.returnDatasetIdentities.append(identity);
        }
        result.summary = FactorResearchSummary::fromVariantMap(
            map.value(QStringLiteral("summary")).toMap());
        result.identitySha256.clear();
        QString validationError;
        if (!result.isValid(&validationError)) return std::nullopt;
        result.identitySha256 = storedSha256;
        return result;
    }
};

struct ExternalFactorResearchExecutionResult {
    bool succeeded = false;
    bool cancelled = false;
    QString error;
    qint64 elapsedMilliseconds = 0;
    ExternalFactorResearchArchiveRecord archive;
};

struct ExternalFactorResearchPreflightResult {
    bool ready = false;
    bool cancelled = false;
    QString error;
    QString datasetId;
    int periodSeconds = 0;
    int observationCount = 0;
    int timestampCount = 0;
    int symbolCount = 0;
    int readySymbolCount = 0;
    int evaluatedFrameCount = 0;
    QStringList missingSymbols;
    QDateTime firstTimestamp;
    QDateTime lastTimestamp;
    qint64 elapsedMilliseconds = 0;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("ready"), ready},
                {QStringLiteral("cancelled"), cancelled},
                {QStringLiteral("error"), error},
                {QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("observationCount"), observationCount},
                {QStringLiteral("timestampCount"), timestampCount},
                {QStringLiteral("symbolCount"), symbolCount},
                {QStringLiteral("readySymbolCount"), readySymbolCount},
                {QStringLiteral("evaluatedFrameCount"), evaluatedFrameCount},
                {QStringLiteral("missingSymbols"), missingSymbols},
                {QStringLiteral("firstTimestamp"), firstTimestamp},
                {QStringLiteral("lastTimestamp"), lastTimestamp},
                {QStringLiteral("elapsedMilliseconds"), elapsedMilliseconds}};
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::ExternalFactorResearchRequest)
Q_DECLARE_METATYPE(pp::ExternalFactorResearchResult)
Q_DECLARE_METATYPE(pp::ExternalFactorResearchExecutionResult)
Q_DECLARE_METATYPE(pp::ExternalFactorResearchPreflightResult)
Q_DECLARE_METATYPE(pp::ExternalFactorResearchArchiveSummary)
Q_DECLARE_METATYPE(pp::ExternalFactorResearchArchiveRecord)
