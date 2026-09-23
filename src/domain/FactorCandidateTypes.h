#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QString>
#include <QVariantMap>

namespace pp {

enum class FactorCandidateEvaluationRole {
    ProspectiveHoldout,
    RetrospectiveReference
};

inline QString factorCandidateEvaluationRoleCode(
    FactorCandidateEvaluationRole role)
{
    return role == FactorCandidateEvaluationRole::ProspectiveHoldout
        ? QStringLiteral("prospective_holdout")
        : QStringLiteral("retrospective_reference");
}

inline QString factorCandidateEvaluationRoleLabel(
    FactorCandidateEvaluationRole role)
{
    return role == FactorCandidateEvaluationRole::ProspectiveHoldout
        ? QStringLiteral("前瞻验证") : QStringLiteral("回溯参考");
}

struct FactorCandidateVersion {
    QString candidateId;
    int version = 0;
    QString name;
    QString hypothesis;
    QString sourceKind = QStringLiteral("manual");
    QString sourceReference;
    QVariantMap factorDefinition;
    QDateTime proposedAt;
    QDateTime frozenAt;
    QString contentSha256;

    QString computedContentSha256() const
    {
        const QVariantMap content {
            {QStringLiteral("name"), name.trimmed()},
            {QStringLiteral("hypothesis"), hypothesis.trimmed()},
            {QStringLiteral("sourceKind"), sourceKind.trimmed()},
            {QStringLiteral("sourceReference"), sourceReference.trimmed()},
            {QStringLiteral("factorDefinition"), factorDefinition}};
        const QByteArray bytes = QJsonDocument::fromVariant(content)
                                     .toJson(QJsonDocument::Compact);
        return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }

    QString computedFactorDefinitionSha256() const
    {
        const QByteArray bytes = QJsonDocument::fromVariant(factorDefinition)
                                     .toJson(QJsonDocument::Compact);
        return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }

    bool isFrozen() const { return frozenAt.isValid(); }

    bool isValid(QString *error = nullptr) const
    {
        if (candidateId.trimmed().isEmpty() || name.trimmed().isEmpty()
            || hypothesis.trimmed().isEmpty() || factorDefinition.isEmpty()
            || version < 0 || !proposedAt.isValid()) {
            if (error) *error = QStringLiteral("候选因子身份、假设、定义或提出时间不完整");
            return false;
        }
        if (sourceKind != QStringLiteral("manual")
            && sourceKind != QStringLiteral("authorized_import")) {
            if (error) *error = QStringLiteral("候选因子来源仅允许 manual 或 authorized_import");
            return false;
        }
        if (sourceKind == QStringLiteral("authorized_import")
            && sourceReference.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("授权导入候选必须记录来源引用");
            return false;
        }
        if (frozenAt.isValid() && frozenAt < proposedAt) {
            if (error) *error = QStringLiteral("候选因子冻结时间不能早于提出时间");
            return false;
        }
        if (!contentSha256.isEmpty()
            && contentSha256 != computedContentSha256()) {
            if (error) *error = QStringLiteral("候选因子内容摘要不匹配");
            return false;
        }
        if (error) error->clear();
        return true;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("candidateId"), candidateId},
                {QStringLiteral("version"), version},
                {QStringLiteral("name"), name},
                {QStringLiteral("hypothesis"), hypothesis},
                {QStringLiteral("sourceKind"), sourceKind},
                {QStringLiteral("sourceReference"), sourceReference},
                {QStringLiteral("factorDefinition"), factorDefinition},
                {QStringLiteral("proposedAt"), proposedAt},
                {QStringLiteral("frozenAt"), frozenAt},
                {QStringLiteral("frozen"), isFrozen()},
                {QStringLiteral("contentSha256"), contentSha256}};
    }
};

struct FactorCandidateEvaluationDecision {
    bool accepted = false;
    bool preregistered = false;
    QString roleLabel;
    QString reason;
    QString researchMethodVersion;
    QString archiveIdentitySha256;
};

struct FactorCandidateEvaluation {
    QString evaluationId;
    QString candidateId;
    int candidateVersion = 0;
    QString candidateContentSha256;
    QString archiveResultId;
    QDateTime archiveCompletedAt;
    QString archiveFactorId;
    int archiveFactorVersion = 0;
    QString researchMethodVersion;
    FactorCandidateEvaluationRole role =
        FactorCandidateEvaluationRole::RetrospectiveReference;
    bool preregistered = false;
    QString archiveIdentitySha256;
    QDateTime linkedAt;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("evaluationId"), evaluationId},
                {QStringLiteral("candidateId"), candidateId},
                {QStringLiteral("candidateVersion"), candidateVersion},
                {QStringLiteral("candidateContentSha256"), candidateContentSha256},
                {QStringLiteral("archiveResultId"), archiveResultId},
                {QStringLiteral("archiveCompletedAt"), archiveCompletedAt},
                {QStringLiteral("archiveFactorId"), archiveFactorId},
                {QStringLiteral("archiveFactorVersion"), archiveFactorVersion},
                {QStringLiteral("researchMethodVersion"), researchMethodVersion},
                {QStringLiteral("role"), factorCandidateEvaluationRoleCode(role)},
                {QStringLiteral("roleLabel"), factorCandidateEvaluationRoleLabel(role)},
                {QStringLiteral("preregistered"), preregistered},
                {QStringLiteral("archiveIdentitySha256"), archiveIdentitySha256},
                {QStringLiteral("linkedAt"), linkedAt}};
    }
};

} // namespace pp
