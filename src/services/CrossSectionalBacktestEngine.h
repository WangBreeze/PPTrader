#pragma once

#include "domain/PortfolioTypes.h"
#include "services/AlignedBarSeries.h"
#include "strategy/CrossSectionalCustomStrategy.h"

#include <QString>

namespace pp {

// 多股横截面回测引擎：逐帧推进策略，按目标持仓调仓，产出组合回测结果。
// 复用 PortfolioExecutor 的先卖后买/整手/费用/滑点/T+1/前向填充 stale 语义。
class CrossSectionalBacktestEngine {
public:
    static PortfolioResult run(CrossSectionalCustomStrategy &strategy,
                               const AlignedBarSeries &alignedSeries,
                               const PortfolioDefinition &definition,
                               QString *error = nullptr);
};

} // namespace pp
