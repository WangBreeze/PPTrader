#pragma once

#include "domain/PortfolioTypes.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace pp {

// 组合资金回测引擎：共享资金池，按目标权重在共同交易日收盘定期再平衡。
// 与单标的 BacktestEngine 独立，复用 A 股整手、费用、滑点与 T+1 语义。
class PortfolioBacktestEngine final {
public:
    static PortfolioResult run(const PortfolioDefinition &definition,
                               const QHash<QString, QVector<Candle>> &series,
                               QString *error = nullptr);
};

} // namespace pp
