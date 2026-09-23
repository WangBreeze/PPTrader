#pragma once

#include "strategy/BacktestStrategy.h"

#include <cmath>
#include <optional>
#include <QTimeZone>

namespace pp {

inline bool backtestPositionSellable(Market market, const QDateTime &entry,
                                    const QDateTime &exit)
{
    if (!entry.isValid() || !exit.isValid() || exit < entry) return false;
    if (market != Market::AShare) return true;
    const QTimeZone shanghai("Asia/Shanghai");
    return entry.toTimeZone(shanghai).date() < exit.toTimeZone(shanghai).date();
}

// 正式周期回测与扫描器共用的报价门禁；signal.price 只作策略参考，不能决定成交。
inline std::optional<double> candleExecutionPrice(
    const Candle &candle, const TradeSignal &signal,
    const CandleExecutionContext &context)
{
    if (signal.instrument.symbol != candle.instrument.symbol
        || signal.timestamp != candle.startTime
        || !std::isfinite(candle.close) || candle.close <= 0.0) return {};
    if (context.basis == CandleExecutionBasis::NextOpen) {
        if (!context.decisionTime.isValid() || context.decisionTime >= candle.startTime
            || !std::isfinite(candle.open) || candle.open <= 0.0
            || !std::isfinite(candle.high) || !std::isfinite(candle.low)
            || candle.low <= 0.0 || candle.high < candle.low
            || candle.open < candle.low || candle.open > candle.high) return {};
        return candle.open;
    }
    return candle.close;
}

} // namespace pp
