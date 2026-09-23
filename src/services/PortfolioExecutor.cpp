#include "services/PortfolioExecutor.h"

#include <algorithm>
#include <cmath>

namespace pp {

double PortfolioExecutor::equity(const ExecutionState &state)
{
    double total = state.cash;
    for (const LegState &leg : state.legs) {
        total += leg.quantity * leg.lastPrice;
    }
    return total;
}

QVector<PortfolioPosition> PortfolioExecutor::positions(const ExecutionState &state,
                                                        const QDate &day,
                                                        double equity)
{
    QVector<PortfolioPosition> result;
    result.reserve(state.legs.size());
    for (const LegState &leg : state.legs) {
        PortfolioPosition position;
        position.instrument = leg.instrument;
        position.quantity = leg.quantity;
        position.lastPrice = leg.lastPrice;
        position.marketValue = leg.quantity * leg.lastPrice;
        position.weight = equity > 0.0
            ? position.marketValue / equity : 0.0;
        position.priceStale = leg.quantity > 0.0
            && (!leg.quoteAvailable
                || (leg.lastPriceDay.isValid() && leg.lastPriceDay < day));
        position.marketStatus = leg.marketStatus;
        result.append(position);
    }
    return result;
}

void PortfolioExecutor::rebalance(ExecutionState &state,
                                  double equity,
                                  double slippageRate,
                                  double commissionRate,
                                  int lotSize,
                                  bool tPlusOne,
                                  const QDateTime &barTime,
                                  QVector<PortfolioTrade> &tradesOut)
{
    // 先卖：超配腿按可卖数量卖至目标市值。
    for (LegState &leg : state.legs) {
        if (leg.quantity <= 0.0 || !leg.quoteAvailable) continue;
        if (tPlusOne && leg.lastBuyDay.isValid()
            && leg.lastBuyDay == barTime.date()) continue;
        const double target = equity * leg.weight;
        const double current = leg.quantity * leg.lastPrice;
        if (current <= target) continue;
        const double fillPrice = leg.lastPrice * (1.0 - slippageRate);
        const double sellable = leg.quantity;
        const double desired = (current - target) / fillPrice;
        const int lot = std::max(1, lotSize);
        const double quantity = std::floor(desired / lot) * lot;
        if (quantity <= 0.0 || quantity > sellable) continue;
        const double commission = fillPrice * quantity * commissionRate;
        state.cash += fillPrice * quantity - commission;
        state.totalCommission += commission;
        leg.quantity -= quantity;
        PortfolioTrade trade;
        trade.instrument = leg.instrument;
        trade.timestamp = barTime;
        trade.side = QStringLiteral("sell");
        trade.quantity = quantity;
        trade.price = fillPrice;
        trade.commission = commission;
        trade.amount = fillPrice * quantity;
        trade.reason = QStringLiteral("组合再平衡卖出");
        tradesOut.append(trade);
    }
    // 后买：低配腿用可用现金买至目标市值。
    for (LegState &leg : state.legs) {
        if (leg.weight <= 0.0 || !leg.quoteAvailable) continue;
        const double target = equity * leg.weight;
        const double current = leg.quantity * leg.lastPrice;
        if (current >= target) continue;
        const double fillPrice = leg.lastPrice * (1.0 + slippageRate);
        const int lot = std::max(1, lotSize);
        const double spend = std::min(target - current, state.cash);
        const double affordable =
            spend / (fillPrice * (1.0 + commissionRate));
        const double quantity = std::floor(affordable / lot) * lot;
        if (quantity <= 0.0) continue;
        const double cost = fillPrice * quantity;
        const double commission = cost * commissionRate;
        if (cost + commission > state.cash + 1e-9) continue;
        state.cash -= cost + commission;
        state.totalCommission += commission;
        leg.quantity += quantity;
        if (tPlusOne) {
            leg.boughtToday = true;
            leg.lastBuyDay = barTime.date();
        }
        PortfolioTrade trade;
        trade.instrument = leg.instrument;
        trade.timestamp = barTime;
        trade.side = QStringLiteral("buy");
        trade.quantity = quantity;
        trade.price = fillPrice;
        trade.commission = commission;
        trade.amount = cost;
        trade.reason = QStringLiteral("组合再平衡买入");
        tradesOut.append(trade);
    }
}

} // namespace pp
