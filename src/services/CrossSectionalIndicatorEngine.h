#pragma once

#include "services/CrossSectionalIndicator.h"

#include <memory>
#include <vector>

namespace pp {

class CrossSectionalIndicatorEngine final {
public:
    bool addIndicator(std::unique_ptr<CrossSectionalIndicator> indicator);
    int indicatorCount() const;
    QVector<FactorFrame> calculate(const MarketSnapshot &snapshot) const;

private:
    std::vector<std::unique_ptr<CrossSectionalIndicator>> m_indicators;
};

} // namespace pp
