#include "domain/PortfolioTypes.h"

#include <cmath>

namespace pp {

bool PortfolioDefinition::isValid(QString *error) const
{
    if (portfolioId.trimmed().isEmpty() || legs.isEmpty()) {
        if (error) *error = QStringLiteral("组合 ID 与腿列表不能为空");
        return false;
    }
    if (initialCapital <= 0.0 || commissionRate < 0.0 || commissionRate > 0.1
        || slippageRate < 0.0 || slippageRate > 0.1 || lotSize <= 0
        || rebalanceEveryTradingDays < 1 || maximumLegWeight <= 0.0
        || !std::isfinite(initialCapital) || !std::isfinite(commissionRate)
        || !std::isfinite(slippageRate)) {
        if (error) *error = QStringLiteral("组合资金、费率或周期参数无效");
        return false;
    }
    for (const PortfolioLeg &leg : legs) {
        if (leg.instrument.symbol.trimmed().isEmpty()) {
            if (error) *error = QStringLiteral("组合腿包含空标的");
            return false;
        }
        if (leg.targetWeight < 0.0 || !std::isfinite(leg.targetWeight)
            || leg.targetWeight > 1.0) {
            if (error) *error = QStringLiteral("组合腿权重必须在 0–1 之间");
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

QVariantMap PortfolioDefinition::toVariantMap() const
{
    QVariantList legList;
    for (const PortfolioLeg &leg : legs) {
        legList.append(QVariantMap{
            {QStringLiteral("symbol"), leg.instrument.symbol},
            {QStringLiteral("name"), leg.instrument.name},
            {QStringLiteral("targetWeight"), leg.targetWeight}});
    }
    return {{QStringLiteral("portfolioId"), portfolioId},
            {QStringLiteral("name"), name},
            {QStringLiteral("legs"), legList},
            {QStringLiteral("rebalanceEveryTradingDays"),
             rebalanceEveryTradingDays},
            {QStringLiteral("initialCapital"), initialCapital},
            {QStringLiteral("commissionRate"), commissionRate},
            {QStringLiteral("slippageRate"), slippageRate},
            {QStringLiteral("lotSize"), lotSize},
            {QStringLiteral("tPlusOne"), tPlusOne},
            {QStringLiteral("maximumLegWeight"), maximumLegWeight},
            {QStringLiteral("maximumActiveLegs"), maximumActiveLegs}};
}

QVector<Instrument> PortfolioDefinition::instruments() const
{
    QVector<Instrument> result;
    result.reserve(legs.size());
    for (const PortfolioLeg &leg : legs) result.append(leg.instrument);
    return result;
}

} // namespace pp
