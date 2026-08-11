#pragma once

#include "services/CrossSectionalIndicator.h"

namespace pp {

class AShareRelativeStrengthIndicator final : public CrossSectionalIndicator {
public:
    QString id() const override;
    QString displayName() const override;
    bool supportsMarket(Market market) const override;
    FactorFrame calculate(const MarketSnapshot &snapshot) const override;
};

} // namespace pp
