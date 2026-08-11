#pragma once

#include "strategy/SignalExecutionPolicy.h"

namespace pp {

class DefaultSignalExecutionPolicy final : public SignalExecutionPolicy {
public:
    RiskCheckResult evaluate(const StrategyExecutionConfig &config,
                             const TradeSignal &signal,
                             const AccountSnapshot &account,
                             const std::optional<Position> &position,
                             const QDateTime &lastSubmissionTime,
                             const QDateTime &evaluationTime) const override;
};

} // namespace pp
