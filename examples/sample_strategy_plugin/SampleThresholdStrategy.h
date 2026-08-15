#pragma once

#include "strategy/Strategy.h"

#include <QMutex>

namespace pp {

// 示例插件自带的简单策略：close 上穿阈值买入、下穿阈值卖出。
class SampleThresholdStrategy final : public Strategy {
    Q_OBJECT

public:
    explicit SampleThresholdStrategy(const Instrument &instrument, double threshold,
                                     const QString &strategyId, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList subscribedSymbols() const override;

    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;

private:
    Instrument m_instrument;
    double m_threshold = 100.0;
    QString m_strategyId;
    bool m_above = false;
    mutable QMutex m_mutex;
};

} // namespace pp
