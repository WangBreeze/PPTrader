#pragma once

#include "domain/StrategyExecutionTypes.h"

#include <optional>

namespace pp {

class SignalExecutionPolicy {
public:
    virtual ~SignalExecutionPolicy() = default;

    virtual RiskCheckResult evaluate(const StrategyExecutionConfig &config,
                                     const TradeSignal &signal,
                                     const AccountSnapshot &account,
                                     const std::optional<Position> &position,
                                     const QDateTime &lastSubmissionTime,
                                     const QDateTime &evaluationTime) const = 0;
};

} // namespace pp
