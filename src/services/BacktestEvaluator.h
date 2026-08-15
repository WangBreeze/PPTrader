#pragma once

#include "domain/BacktestTypes.h"
#include "strategy/BacktestStrategy.h"

#include <QVector>

namespace pp {

// 同步回测评估器：对给定 K 线 + 策略一次跑完回测，返回摘要。
// 与 BacktestEngine 共享同一套开/平仓、滑点/手续费、权益曲线、摘要算法，
// 但不依赖事件循环与信号，供参数网格扫描等批量场景使用。
class BacktestEvaluator {
public:
    static PerformanceSummary run(const BacktestConfig &config,
                                  QVector<Candle> candles,
                                  BacktestStrategy &strategy);
};

} // namespace pp
