#pragma once

#include "strategy/Strategy.h"

#include <QDateTime>
#include <QMutex>

namespace pp {

class TickMomentumStrategy final : public Strategy {
    Q_OBJECT

public:
    explicit TickMomentumStrategy(const Instrument &instrument, double thresholdRatio, int cooldownSeconds, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList subscribedSymbols() const override;
    double thresholdPercent() const override;
    int cooldownSeconds() const override;
    void setRuntimeParameters(double thresholdPercent, int cooldownSeconds) override;

public slots:
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;

private:
    Instrument m_instrument;
    double m_thresholdRatio = 0.003;
    int m_cooldownSeconds = 10;
    double m_lastPrice = 0.0;
    QDateTime m_lastSignalTime;
    mutable QMutex m_mutex;
};

} // 命名空间 pp
