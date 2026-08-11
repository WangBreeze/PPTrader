#pragma once

#include "strategy/BacktestStrategy.h"

namespace pp {

class MacdTrendStrategy final : public BacktestStrategy {
public:
    MacdTrendStrategy(int fastPeriod = 12, int slowPeriod = 26,
                      int signalPeriod = 9, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    int minimumWarmupBars() const override;
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;
    void reset() override;

private:
    int m_fastPeriod = 12;
    int m_slowPeriod = 26;
    int m_signalPeriod = 9;
    int m_count = 0;
    double m_fastEma = 0.0;
    double m_slowEma = 0.0;
    double m_signalEma = 0.0;
    double m_previousDifference = 0.0;
    bool m_hasPreviousDifference = false;
};

} // namespace pp
