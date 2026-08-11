#pragma once

#include "strategy/Strategy.h"

#include <QDateTime>
#include <QMutex>

namespace pp {

class PairSpreadStrategy final : public Strategy {
    Q_OBJECT

public:
    PairSpreadStrategy(const Instrument &primary, const Instrument &secondary, double thresholdPercent, int cooldownSeconds, QObject *parent = nullptr);

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
    void evaluateLocked(const Tick &tick);

    Instrument m_primary;
    Instrument m_secondary;
    double m_thresholdPercent = 1.0;
    int m_cooldownSeconds = 30;
    double m_primaryPrice = 0.0;
    double m_secondaryPrice = 0.0;
    double m_baselineRatio = 0.0;
    QDateTime m_lastSignalTime;
    mutable QMutex m_mutex;
};

} // 命名空间 pp
