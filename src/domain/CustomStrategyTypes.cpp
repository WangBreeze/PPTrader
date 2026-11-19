#include "domain/CustomStrategyTypes.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace pp {

QString CustomStrategyDefinition::canonicalJson() const
{
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

QString CustomStrategyDefinition::newVersionDraftJson(
    int nextVersion, const QDateTime &createdAt) const
{
    if (nextVersion <= version || nextVersion > 1000000 || !createdAt.isValid()) return {};
    QJsonObject draft = document;
    const QString timestamp = createdAt.toUTC().toString(Qt::ISODateWithMs);
    draft.insert(QStringLiteral("version"), nextVersion);
    draft.insert(QStringLiteral("createdAt"), timestamp);
    draft.insert(QStringLiteral("updatedAt"), timestamp);
    return QString::fromUtf8(QJsonDocument(draft).toJson(QJsonDocument::Indented));
}

QVariantMap CustomStrategyDefinition::toVariantMap() const
{
    return {{QStringLiteral("schemaVersion"), schemaVersion},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("version"), version},
            {QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("author"), author},
            {QStringLiteral("market"), market},
            {QStringLiteral("executionScope"), executionScope},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("updatedAt"), updatedAt},
            {QStringLiteral("parameters"), document.value(QStringLiteral("parameters")).toArray().toVariantList()},
            {QStringLiteral("entry"), document.value(QStringLiteral("entry")).toObject().toVariantMap()},
            {QStringLiteral("exit"), document.value(QStringLiteral("exit")).toObject().toVariantMap()},
            {QStringLiteral("definitionJson"), canonicalJson()},
            {QStringLiteral("stage"), QStringLiteral("backtest_ready")},
            {QStringLiteral("executable"), true}};
}

} // namespace pp
