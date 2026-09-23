#pragma once

#include "domain/AShareCalendarTypes.h"
#include "domain/PortfolioTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace pp {

// 组合资金回测引擎：共享资金池，在观测交易日并集逐日估值，
// 仅在共同交易日按目标权重收盘定期再平衡。
// 与单标的 BacktestEngine 独立，复用 A 股整手、费用、滑点与 T+1 语义。
class PortfolioBacktestEngine final {
public:
    static PortfolioResult run(const PortfolioDefinition &definition,
                               const QHash<QString, QVector<Candle>> &series,
                               QString *error = nullptr,
                               const AShareCalendarSnapshot &calendar = {},
                               const AShareIndustrySnapshot &industry = {});
};

} // namespace pp
