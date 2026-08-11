#pragma once

#include "domain/MarketTypes.h"

#include <QObject>
#include <QStringList>

namespace pp {

class Strategy : public QObject {
    Q_OBJECT

public:
    explicit Strategy(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~Strategy() override = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList subscribedSymbols() const { return {}; }
    virtual double thresholdPercent() const { return 0.0; }
    virtual int cooldownSeconds() const { return 0; }
    virtual void setRuntimeParameters(double thresholdPercent, int cooldownSeconds)
    {
        Q_UNUSED(thresholdPercent)
        Q_UNUSED(cooldownSeconds)
    }

public slots:
    virtual void onTick(const Tick &tick) = 0;
    virtual void onCandle(const Candle &candle) = 0;

signals:
    void signalGenerated(const pp::TradeSignal &signal);
};

} // 命名空间 pp
