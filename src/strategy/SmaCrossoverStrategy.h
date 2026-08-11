#pragma once

#include "strategy/BacktestStrategy.h"

#include <QVector>

namespace pp {

class SmaCrossoverStrategy final : public BacktestStrategy {
public:
    explicit SmaCrossoverStrategy(int fastPeriod = 5, int slowPeriod = 20, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    int minimumWarmupBars() const override;
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;
    void reset() override;

private:
    int m_fastPeriod = 5;
    int m_slowPeriod = 20;
    QVector<double> m_closes;
    double m_previousSpread = 0.0;
    bool m_hasPreviousSpread = false;
};

} // namespace pp
