#pragma once

#include "domain/CustomStrategyTypes.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>

namespace pp {

class CustomStrategyValidator {
public:
    bool parseAndValidate(const QString &json, CustomStrategyDefinition *definition,
                          QString *error) const;
    bool validate(const QJsonObject &document, CustomStrategyDefinition *definition,
                  QString *error) const;
    QVariantMap schemaDescriptor() const;

private:
    struct ParameterInfo {
        QString type;
        double defaultValue = 0.0;
        double minimumValue = 0.0;
        double maximumValue = 0.0;
    };

    using ParameterMap = QHash<QString, ParameterInfo>;

    bool validateParameters(const QJsonArray &parameters, ParameterMap *parameterMap,
                            QString *error) const;
    bool validateCondition(const QJsonObject &condition, const ParameterMap &parameters,
                           const QString &path, int depth, bool v2,
                           const QSet<QString> &aliases,
                           const QSet<QString> &crossAliases, int *nodeCount,
                           QString *error) const;
    bool validateOperand(const QJsonObject &operand, const ParameterMap &parameters,
                         const QString &path, int depth, bool v2,
                         const QSet<QString> &aliases,
                         const QSet<QString> &crossAliases, int *nodeCount,
                         QString *error) const;
};

} // namespace pp
