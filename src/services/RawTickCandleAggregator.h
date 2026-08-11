#pragma once

#include "domain/RawTradeTypes.h"

#include <optional>

namespace pp {

class RawTickCandleAggregator final {
public:
    RawTickCandleAggregator(const Instrument &instrument, int periodSeconds);

    bool append(const RawTradeTick &tick);
    QVector<Candle> finish();
    int rejectedCount() const;

    static bool validPeriod(int periodSeconds);
    static std::optional<QDateTime> bucketStart(const QDateTime &timestamp,
                                                int periodSeconds);

private:
    void flush();

    Instrument m_instrument;
    int m_periodSeconds = 60;
    std::optional<Candle> m_current;
    QVector<Candle> m_candles;
    int m_rejectedCount = 0;
};

} // namespace pp
