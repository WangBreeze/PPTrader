#include "strategy/TickMomentumStrategy.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace pp {

TickMomentumStrategy::TickMomentumStrategy(const Instrument &instrument, double thresholdRatio, int cooldownSeconds, QObject *parent)
    : Strategy(parent)
    , m_instrument(instrument)
    , m_thresholdRatio(thresholdRatio)
    , m_cooldownSeconds(cooldownSeconds)
{
}

QString TickMomentumStrategy::id() const
{
    return QStringLiteral("tick_momentum_%1").arg(m_instrument.symbol);
}

QString TickMomentumStrategy::displayName() const
{
    return QStringLiteral("%1 tick 动量监测").arg(m_instrument.symbol);
}

QStringList TickMomentumStrategy::subscribedSymbols() const
{
    return {m_instrument.symbol};
}

double TickMomentumStrategy::thresholdPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_thresholdRatio * 100.0;
}

int TickMomentumStrategy::cooldownSeconds() const
{
    QMutexLocker locker(&m_mutex);
    return m_cooldownSeconds;
}

void TickMomentumStrategy::setRuntimeParameters(double thresholdPercent, int cooldownSeconds)
{
    QMutexLocker locker(&m_mutex);
    m_thresholdRatio = std::max(0.0001, thresholdPercent / 100.0);
    m_cooldownSeconds = std::max(0, cooldownSeconds);
}

void TickMomentumStrategy::onTick(const Tick &tick)
{
    if (tick.instrument.symbol != m_instrument.symbol || tick.price <= 0.0) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (m_lastPrice <= 0.0) {
        m_lastPrice = tick.price;
        return;
    }

    const double changeRatio = (tick.price - m_lastPrice) / m_lastPrice;
    m_lastPrice = tick.price;

    if (std::abs(changeRatio) < m_thresholdRatio) {
        return;
    }

    if (m_lastSignalTime.isValid() && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
        return;
    }

    m_lastSignalTime = tick.timestamp;

    TradeSignal signal;
    signal.instrument = tick.instrument;
    signal.timestamp = tick.timestamp;
    signal.side = changeRatio > 0.0 ? SignalSide::Buy : SignalSide::Sell;
    signal.price = tick.price;
    signal.volume = tick.volume;
    signal.reason = QStringLiteral("tick 动量变化 %1%")
        .arg(changeRatio * 100.0, 0, 'f', 2);

    emit signalGenerated(signal);
}

void TickMomentumStrategy::onCandle(const Candle &candle)
{
    Q_UNUSED(candle)
}

} // 命名空间 pp
