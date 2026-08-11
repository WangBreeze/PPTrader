#include "services/PreTradeRiskService.h"

#include <algorithm>

namespace pp {

namespace {

RiskCheckResult rejected(const QString &code, const QString &message)
{
    return {false, code, message};
}

} // namespace

PreTradeRiskService::PreTradeRiskService(RiskLimits limits)
{
    setLimits(limits);
}

RiskLimits PreTradeRiskService::limits() const
{
    return m_limits;
}

void PreTradeRiskService::setLimits(const RiskLimits &limits)
{
    m_limits.maxOrderValue = std::max(0.0, limits.maxOrderValue);
    m_limits.maxPositionValue = std::max(0.0, limits.maxPositionValue);
    m_limits.maxDailyLoss = std::max(0.0, limits.maxDailyLoss);
    m_limits.maxActiveOrders = std::max(0, limits.maxActiveOrders);
}

RiskCheckResult PreTradeRiskService::check(const OrderRequest &request,
                                           const AccountSnapshot &account,
                                           const std::optional<Position> &position,
                                           int activeOrderCount) const
{
    const double estimatedPrice = request.type == OrderType::Limit
        ? request.limitPrice : request.referencePrice;
    const double orderValue = estimatedPrice * request.quantity;
    if (m_limits.maxOrderValue > 0.0 && orderValue > m_limits.maxOrderValue + 0.000001) {
        return rejected(QStringLiteral("MAX_ORDER_VALUE"),
                        QStringLiteral("订单金额超过单笔上限 %1").arg(m_limits.maxOrderValue, 0, 'f', 2));
    }

    if (request.side == OrderSide::Buy && m_limits.maxPositionValue > 0.0) {
        const double currentPositionValue = position ? position->marketValue : 0.0;
        if (currentPositionValue + orderValue > m_limits.maxPositionValue + 0.000001) {
            return rejected(QStringLiteral("MAX_POSITION_VALUE"),
                            QStringLiteral("成交后持仓市值将超过上限 %1")
                                .arg(m_limits.maxPositionValue, 0, 'f', 2));
        }
    }

    const double currentLoss = account.dayStartEquity > 0.0
        ? std::max(0.0, -account.dailyPnl)
        : std::max(0.0, account.initialEquity - account.totalEquity);
    if (m_limits.maxDailyLoss > 0.0 && currentLoss >= m_limits.maxDailyLoss) {
        return rejected(QStringLiteral("MAX_DAILY_LOSS"),
                        QStringLiteral("账户亏损已达到交易熔断阈值"));
    }

    if (m_limits.maxActiveOrders > 0 && activeOrderCount >= m_limits.maxActiveOrders) {
        return rejected(QStringLiteral("MAX_ACTIVE_ORDERS"),
                        QStringLiteral("活动委托数量已达到上限 %1").arg(m_limits.maxActiveOrders));
    }
    return {true, QStringLiteral("OK"), QStringLiteral("风控检查通过")};
}

} // namespace pp
