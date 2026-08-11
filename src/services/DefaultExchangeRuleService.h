#pragma once

#include "services/ExchangeRuleService.h"

namespace pp {

class DefaultExchangeRuleService final : public ExchangeRuleService {
public:
    RiskCheckResult validate(const OrderRequest &request,
                             const ExchangeRuleContext &context) const override;
    int defaultLotSize(const Instrument &instrument, OrderSide side) const override;
    double tickSize(const Instrument &instrument) const override;

private:
    double aSharePriceLimitPercent(const Instrument &instrument) const;
};

} // namespace pp
