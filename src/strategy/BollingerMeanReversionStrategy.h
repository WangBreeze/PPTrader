#pragma once

#include "strategy/BacktestStrategy.h"

#include <QVector>

namespace pp {

class BollingerMeanReversionStrategy final : public BacktestStrategy {
public:
    BollingerMeanReversionStrategy(int period = 20, double standardDeviations = 2.0,
                                   QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    int minimumWarmupBars() const override;
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;
    void reset() override;

private:
    int m_period = 20;
    double m_standardDeviations = 2.0;
    QVector<double> m_closes;
    bool m_hasPosition = false;
};

} // namespace pp
