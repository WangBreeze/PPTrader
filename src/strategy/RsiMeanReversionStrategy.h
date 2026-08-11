#pragma once

#include "strategy/BacktestStrategy.h"

#include <QVector>

namespace pp {

class RsiMeanReversionStrategy final : public BacktestStrategy {
public:
    RsiMeanReversionStrategy(int period = 14, double oversold = 30.0,
                             double overbought = 70.0, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    int minimumWarmupBars() const override;
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;
    void reset() override;

private:
    int m_period = 14;
    double m_oversold = 30.0;
    double m_overbought = 70.0;
    QVector<double> m_closes;
    bool m_hasPosition = false;
};

} // namespace pp
