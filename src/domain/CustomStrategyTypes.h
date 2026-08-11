#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace pp {

struct CustomStrategyDefinition {
    int schemaVersion = 1;
    QString strategyId;
    int version = 1;
    QString name;
    QString description;
    QString author;
    QString market = QStringLiteral("A股");
    QString executionScope = QStringLiteral("backtest_only");
    QDateTime createdAt;
    QDateTime updatedAt;
    QJsonObject document;

    QString canonicalJson() const;
    QVariantMap toVariantMap() const;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::CustomStrategyDefinition)
