#include "strategy/FixedAmountPositionSizer.h"

#include <cmath>

#include <algorithm>

namespace pp {

double FixedAmountPositionSizer::calculateQuantity(const StrategyExecutionConfig &config,
                                                    const TradeSignal &signal,
                                                    const AccountSnapshot &account,
                                                    const std::optional<Position> &position) const
{
    if (signal.price <= 0.0) return 0.0;
    const int lotSize = config.lotSize > 0
        ? config.lotSize : (signal.instrument.market == Market::Crypto ? 1 : 100);

    if (signal.side == SignalSide::Sell) {
        if (!position || position->availableQuantity <= 0.0) return 0.0;
        if (config.sellAll) return position->availableQuantity;
        const double rawQuantity = config.fixedOrderValue / signal.price;
        return std::min(position->availableQuantity,
                        std::floor(rawQuantity / lotSize) * lotSize);
    }

    double budget = std::min(config.fixedOrderValue, account.cash - account.frozenCash);
    if (config.maxPositionPercent > 0.0) {
        const double maximumPositionValue = account.totalEquity * config.maxPositionPercent / 100.0;
        const double currentPositionValue = position ? position->marketValue : 0.0;
        budget = std::min(budget, std::max(0.0, maximumPositionValue - currentPositionValue));
    }
    if (budget <= 0.0) return 0.0;
    return std::floor((budget / signal.price) / lotSize) * lotSize;
}

} // namespace pp
