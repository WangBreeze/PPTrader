#include "strategy/PairSpreadStrategy.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace pp {

PairSpreadStrategy::PairSpreadStrategy(const Instrument &primary, const Instrument &secondary, double thresholdPercent, int cooldownSeconds, QObject *parent)
    : Strategy(parent)
    , m_primary(primary)
    , m_secondary(secondary)
    , m_thresholdPercent(thresholdPercent)
    , m_cooldownSeconds(cooldownSeconds)
{
}

QString PairSpreadStrategy::id() const
{
    return QStringLiteral("pair_spread_%1_%2").arg(m_primary.symbol, m_secondary.symbol);
}

QString PairSpreadStrategy::displayName() const
{
    return QStringLiteral("%1/%2 联合价差").arg(m_primary.symbol, m_secondary.symbol);
}

QStringList PairSpreadStrategy::subscribedSymbols() const
{
    return {m_primary.symbol, m_secondary.symbol};
}

double PairSpreadStrategy::thresholdPercent() const
{
    QMutexLocker locker(&m_mutex);
    return m_thresholdPercent;
}

int PairSpreadStrategy::cooldownSeconds() const
{
    QMutexLocker locker(&m_mutex);
    return m_cooldownSeconds;
}

void PairSpreadStrategy::setRuntimeParameters(double thresholdPercent, int cooldownSeconds)
{
    QMutexLocker locker(&m_mutex);
    m_thresholdPercent = std::max(0.01, thresholdPercent);
    m_cooldownSeconds = std::max(0, cooldownSeconds);
}

void PairSpreadStrategy::onTick(const Tick &tick)
{
    if (tick.price <= 0.0) {
        return;
    }

    QMutexLocker locker(&m_mutex);
    if (tick.instrument.symbol == m_primary.symbol) {
        m_primaryPrice = tick.price;
    } else if (tick.instrument.symbol == m_secondary.symbol) {
        m_secondaryPrice = tick.price;
    } else {
        return;
    }

    evaluateLocked(tick);
}

void PairSpreadStrategy::onCandle(const Candle &candle)
{
    Q_UNUSED(candle)
}

void PairSpreadStrategy::evaluateLocked(const Tick &tick)
{
    if (m_primaryPrice <= 0.0 || m_secondaryPrice <= 0.0) {
        return;
    }

    const double ratio = m_primaryPrice / m_secondaryPrice;
    if (m_baselineRatio <= 0.0) {
        m_baselineRatio = ratio;
        return;
    }

    const double deviationPercent = (ratio - m_baselineRatio) / m_baselineRatio * 100.0;
    if (std::abs(deviationPercent) < m_thresholdPercent) {
        return;
    }

    if (m_lastSignalTime.isValid() && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
        return;
    }

    m_lastSignalTime = tick.timestamp;
    m_baselineRatio = ratio;

    TradeSignal signal;
    signal.instrument = m_primary;
    signal.timestamp = tick.timestamp;
    signal.side = deviationPercent > 0.0 ? SignalSide::Sell : SignalSide::Buy;
    signal.price = m_primaryPrice;
    signal.volume = tick.volume;
    signal.reason = QStringLiteral("%1/%2 价差偏离 %3%")
        .arg(m_primary.symbol, m_secondary.symbol)
        .arg(deviationPercent, 0, 'f', 2);

    emit signalGenerated(signal);
}

} // 命名空间 pp
