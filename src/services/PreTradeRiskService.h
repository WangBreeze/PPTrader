#pragma once

#include "domain/TradingTypes.h"

#include <optional>

namespace pp {

class PreTradeRiskService final {
public:
    explicit PreTradeRiskService(RiskLimits limits = {});

    RiskLimits limits() const;
    void setLimits(const RiskLimits &limits);
    RiskCheckResult check(const OrderRequest &request,
                          const AccountSnapshot &account,
                          const std::optional<Position> &position,
                          int activeOrderCount) const;

private:
    RiskLimits m_limits;
};

} // namespace pp
