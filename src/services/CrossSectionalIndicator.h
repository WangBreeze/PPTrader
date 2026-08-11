#pragma once

#include "domain/CrossSectionalTypes.h"

namespace pp {

class CrossSectionalIndicator {
public:
    virtual ~CrossSectionalIndicator() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual bool supportsMarket(Market market) const = 0;
    virtual FactorFrame calculate(const MarketSnapshot &snapshot) const = 0;
};

} // namespace pp
