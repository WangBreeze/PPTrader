#include "services/DefaultExchangeRuleService.h"

#include "services/MarketSession.h"

#include <cmath>

namespace pp {

namespace {

RiskCheckResult rejectedRule(const QString &code, const QString &message)
{
    return {false, code, message};
}

bool isTickAligned(double price, double tickSize)
{
    if (tickSize <= 0.0) return true;
    const double units = price / tickSize;
    return std::abs(units - std::round(units)) < 0.000001;
}

} // namespace

RiskCheckResult DefaultExchangeRuleService::validate(
    const OrderRequest &request, const ExchangeRuleContext &context) const
{
    if (request.instrument.symbol.isEmpty() || request.quantity <= 0.0) {
        return rejectedRule(QStringLiteral("INVALID_ORDER"), QStringLiteral("交易所规则：订单参数无效"));
    }
    if (context.suspended) {
        return rejectedRule(QStringLiteral("INSTRUMENT_SUSPENDED"), QStringLiteral("交易所规则：标的当前停牌"));
    }
    if (context.enforceTradingSession
        && (!context.evaluationTime.isValid()
            || !MarketSession::isOpen(request.instrument, context.evaluationTime.toUTC()))) {
        return rejectedRule(QStringLiteral("MARKET_CLOSED"), QStringLiteral("交易所规则：当前不在交易时段"));
    }

    const int lot = defaultLotSize(request.instrument, request.side);
    if (lot > 1 && std::fmod(request.quantity, static_cast<double>(lot)) > 0.000001) {
        return rejectedRule(QStringLiteral("INVALID_LOT_SIZE"),
                            QStringLiteral("交易所规则：委托数量必须是 %1 的整数倍").arg(lot));
    }
    if (request.instrument.market != Market::Crypto
        && std::abs(request.quantity - std::round(request.quantity)) > 0.000001) {
        return rejectedRule(QStringLiteral("FRACTIONAL_QUANTITY"),
                            QStringLiteral("交易所规则：当前市场不支持小数数量"));
    }

    if (request.type == OrderType::Limit) {
        if (request.limitPrice <= 0.0) {
            return rejectedRule(QStringLiteral("INVALID_LIMIT_PRICE"),
                                QStringLiteral("交易所规则：限价必须大于 0"));
        }
        const double minimumTick = tickSize(request.instrument);
        if (!isTickAligned(request.limitPrice, minimumTick)) {
            return rejectedRule(QStringLiteral("INVALID_PRICE_TICK"),
                                QStringLiteral("交易所规则：价格不符合最小变动单位 %1")
                                    .arg(minimumTick, 0, 'f', 4));
        }
        if (request.instrument.market == Market::AShare && context.previousClose > 0.0) {
            const double limitPercent = aSharePriceLimitPercent(request.instrument);
            const double lowerLimit = context.previousClose * (1.0 - limitPercent);
            const double upperLimit = context.previousClose * (1.0 + limitPercent);
            if (request.limitPrice < lowerLimit - 0.000001
                || request.limitPrice > upperLimit + 0.000001) {
                return rejectedRule(QStringLiteral("PRICE_LIMIT"),
                                    QStringLiteral("交易所规则：限价超出涨跌停范围"));
            }
        }
    }
    return {true, QStringLiteral("OK"), QStringLiteral("交易所规则检查通过")};
}

int DefaultExchangeRuleService::defaultLotSize(const Instrument &instrument, OrderSide side) const
{
    Q_UNUSED(side)
    return instrument.market == Market::AShare ? 100 : 1;
}

double DefaultExchangeRuleService::tickSize(const Instrument &instrument) const
{
    return instrument.market == Market::Crypto ? 0.00000001 : 0.01;
}

double DefaultExchangeRuleService::aSharePriceLimitPercent(const Instrument &instrument) const
{
    if (instrument.symbol.startsWith(QStringLiteral("300"))
        || instrument.symbol.startsWith(QStringLiteral("301"))
        || instrument.symbol.startsWith(QStringLiteral("688"))) {
        return 0.20;
    }
    if (instrument.symbol.startsWith(QLatin1Char('4'))
        || instrument.symbol.startsWith(QLatin1Char('8'))) {
        return 0.30;
    }
    return 0.10;
}

} // namespace pp
