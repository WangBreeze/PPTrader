#pragma once

namespace pp {

enum class RawTradeDirection {
    Buy,
    Sell,
    Neutral
};

inline bool isKnownRawTradeDirection(RawTradeDirection direction)
{
    return direction == RawTradeDirection::Buy
        || direction == RawTradeDirection::Sell
        || direction == RawTradeDirection::Neutral;
}

} // namespace pp
