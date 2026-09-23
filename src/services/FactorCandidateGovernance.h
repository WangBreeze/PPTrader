#pragma once

#include "domain/FactorCandidateTypes.h"
#include "domain/PortfolioTypes.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace pp {

class FactorCandidateGovernance final {
public:
    static FactorCandidateEvaluationDecision evaluate(
        const FactorCandidateVersion &candidate,
        const PortfolioArchiveRecord &archive,
        FactorCandidateEvaluationRole role)
    {
        FactorCandidateEvaluationDecision decision;
        decision.roleLabel = factorCandidateEvaluationRoleLabel(role);
        QString candidateError;
        if (!candidate.isValid(&candidateError) || candidate.version < 1) {
            decision.reason = candidateError.isEmpty()
                ? QStringLiteral("候选因子尚未形成不可变版本") : candidateError;
            return decision;
        }
        if (!candidate.isFrozen()) {
            decision.reason = QStringLiteral("候选因子必须先冻结，才能关联任何验证结果");
            return decision;
        }
        if (archive.resultId.trimmed().isEmpty() || !archive.completedAt.isValid()
            || archive.result.resultKind != QStringLiteral("cross_sectional")) {
            decision.reason = QStringLiteral("所选结果不是有效的横截面回测归档");
            return decision;
        }
        const QVariantMap factorIdentity = archive.result.immutableIdentity
            .value(QStringLiteral("factorResearch")).toMap();
        const QString factorId = factorIdentity.value(
            QStringLiteral("factorId")).toString();
        decision.researchMethodVersion = factorIdentity.value(
            QStringLiteral("researchMethodVersion")).toString();
        if (factorId.trimmed().isEmpty()
            || decision.researchMethodVersion.trimmed().isEmpty()) {
            decision.reason = QStringLiteral("归档没有可审计的不可变因子研究身份");
            return decision;
        }

        const QVariantMap archiveIdentity {
            {QStringLiteral("resultId"), archive.resultId},
            {QStringLiteral("completedAt"), archive.completedAt.toUTC()},
            {QStringLiteral("immutableIdentity"), archive.result.immutableIdentity},
            {QStringLiteral("factorResearchSummary"),
             archive.result.factorResearchSummary.toVariantMap()}};
        const QByteArray bytes = QJsonDocument::fromVariant(archiveIdentity)
                                     .toJson(QJsonDocument::Compact);
        decision.archiveIdentitySha256 = QStringLiteral("sha256:%1").arg(
            QString::fromLatin1(QCryptographicHash::hash(
                bytes, QCryptographicHash::Sha256).toHex()));

        if (role == FactorCandidateEvaluationRole::ProspectiveHoldout) {
            if (archive.completedAt <= candidate.frozenAt) {
                decision.reason = QStringLiteral(
                    "前瞻验证归档完成时间必须严格晚于候选冻结时间");
                return decision;
            }
            if (decision.researchMethodVersion != QStringLiteral("factor_research_v6")
                || factorIdentity.value(QStringLiteral("outOfSampleMethodVersion"))
                       .toString() != QStringLiteral("expanding_oos_v1")
                || !archive.result.factorResearchSummary.outOfSample.available) {
                decision.reason = QStringLiteral(
                    "前瞻验证只接受包含扩展窗样本外身份的 factor_research_v6 归档");
                return decision;
            }
            decision.accepted = true;
            decision.preregistered = true;
            decision.reason = QStringLiteral("冻结后完成的 v6 样本外归档，可计入预注册证据");
            return decision;
        }

        decision.accepted = true;
        decision.preregistered = false;
        decision.reason = QStringLiteral(
            "已关联为回溯参考；该结果不计入预注册或前瞻验证证据");
        return decision;
    }
};

} // namespace pp
