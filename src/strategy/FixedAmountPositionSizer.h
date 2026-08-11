#pragma once

#include "strategy/PositionSizer.h"

namespace pp {

class FixedAmountPositionSizer final : public PositionSizer {
public:
    double calculateQuantity(const StrategyExecutionConfig &config,
                             const TradeSignal &signal,
                             const AccountSnapshot &account,
                             const std::optional<Position> &position) const override;
};

} // namespace pp
