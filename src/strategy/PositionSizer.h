#pragma once

#include "domain/StrategyExecutionTypes.h"

#include <optional>

namespace pp {

class PositionSizer {
public:
    virtual ~PositionSizer() = default;

    virtual double calculateQuantity(const StrategyExecutionConfig &config,
                                     const TradeSignal &signal,
                                     const AccountSnapshot &account,
                                     const std::optional<Position> &position) const = 0;
};

} // namespace pp
