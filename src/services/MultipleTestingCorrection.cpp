#include "services/MultipleTestingCorrection.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

MultipleTestingSummary MultipleTestingCorrection::adjust(
    const QVector<MultipleTestingHypothesis> &hypotheses,
    double alpha, QString *error)
{
    MultipleTestingSummary result;
    result.alpha = alpha;
    if (hypotheses.isEmpty() || !std::isfinite(alpha)
        || alpha <= 0.0 || alpha >= 1.0) {
        if (error) *error = QStringLiteral("候选假设为空或显著性水平无效");
        return result;
    }
    QSet<QString> identifiers;
    for (const MultipleTestingHypothesis &hypothesis : hypotheses) {
        const QString id = hypothesis.hypothesisId.trimmed();
        if (id.isEmpty() || identifiers.contains(id)
            || !std::isfinite(hypothesis.rawPValue)
            || hypothesis.rawPValue < 0.0 || hypothesis.rawPValue > 1.0) {
            if (error) *error = QStringLiteral("候选假设身份重复、为空或 p 值无效");
            return result;
        }
        identifiers.insert(id);
    }

    result.hypotheses = hypotheses;
    QVector<int> order(hypotheses.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&hypotheses](int left, int right) {
        if (hypotheses.at(left).rawPValue != hypotheses.at(right).rawPValue)
            return hypotheses.at(left).rawPValue
                < hypotheses.at(right).rawPValue;
        return hypotheses.at(left).hypothesisId
            < hypotheses.at(right).hypothesisId;
    });

    const int count = hypotheses.size();
    double runningHolm = 0.0;
    for (int rank = 0; rank < count; ++rank) {
        const int index = order.at(rank);
        runningHolm = std::max(
            runningHolm,
            static_cast<double>(count - rank)
                * hypotheses.at(index).rawPValue);
        MultipleTestingHypothesis &adjusted = result.hypotheses[index];
        adjusted.holmAdjustedPValue = std::min(1.0, runningHolm);
        adjusted.holmRejected = adjusted.holmAdjustedPValue <= alpha;
    }

    double runningBh = 1.0;
    for (int rank = count - 1; rank >= 0; --rank) {
        const int index = order.at(rank);
        runningBh = std::min(
            runningBh,
            hypotheses.at(index).rawPValue
                * static_cast<double>(count)
                / static_cast<double>(rank + 1));
        MultipleTestingHypothesis &adjusted = result.hypotheses[index];
        adjusted.bhAdjustedPValue = std::clamp(runningBh, 0.0, 1.0);
        adjusted.bhRejected = adjusted.bhAdjustedPValue <= alpha;
    }

    QVector<MultipleTestingHypothesis> identityOrder = hypotheses;
    std::sort(identityOrder.begin(), identityOrder.end(),
              [](const MultipleTestingHypothesis &left,
                 const MultipleTestingHypothesis &right) {
        return left.hypothesisId < right.hypothesisId;
    });
    QJsonArray identity;
    for (const MultipleTestingHypothesis &hypothesis : identityOrder) {
        identity.append(QJsonObject{
            {QStringLiteral("hypothesisId"), hypothesis.hypothesisId},
            {QStringLiteral("rawPValue"), hypothesis.rawPValue}});
    }
    const QByteArray bytes = QJsonDocument(identity).toJson(QJsonDocument::Compact);
    result.candidateSetSha256 = QStringLiteral("sha256:%1").arg(
        QString::fromLatin1(QCryptographicHash::hash(
            bytes, QCryptographicHash::Sha256).toHex()));
    result.available = true;
    if (error) error->clear();
    return result;
}

} // namespace pp
