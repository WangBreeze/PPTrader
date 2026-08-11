#include "domain/CustomStrategyTypes.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace pp {

QString CustomStrategyDefinition::canonicalJson() const
{
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
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
