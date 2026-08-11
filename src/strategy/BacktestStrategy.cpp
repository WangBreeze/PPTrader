#include "strategy/BacktestStrategy.h"

namespace pp {

QVariantMap BacktestParameterSpec::toVariantMap() const
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("displayName"), displayName},
            {QStringLiteral("type"), type},
            {QStringLiteral("defaultValue"), defaultValue},
            {QStringLiteral("minimumValue"), minimumValue},
            {QStringLiteral("maximumValue"), maximumValue},
            {QStringLiteral("step"), step}};
}

QVariantMap BacktestStrategyDescriptor::toVariantMap() const
{
    QVariantList parameterList;
    parameterList.reserve(parameters.size());
    for (const BacktestParameterSpec &parameter : parameters) {
        parameterList.append(parameter.toVariantMap());
    }
    return {{QStringLiteral("strategyId"), id},
            {QStringLiteral("name"), displayName},
            {QStringLiteral("description"), description},
            {QStringLiteral("supportedMarket"), supportedMarket},
            {QStringLiteral("minimumWarmupBars"), minimumWarmupBars},
            {QStringLiteral("parameters"), parameterList}};
}

} // namespace pp
