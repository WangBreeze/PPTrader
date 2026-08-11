#include "strategy/SmaCrossoverStrategy.h"

#include "services/IndicatorEngine.h"

#include <algorithm>

namespace pp {

SmaCrossoverStrategy::SmaCrossoverStrategy(int fastPeriod, int slowPeriod, QObject *parent)
    : BacktestStrategy(parent)
    , m_fastPeriod(std::max(1, fastPeriod))
    , m_slowPeriod(std::max(m_fastPeriod + 1, slowPeriod))
{
}

QString SmaCrossoverStrategy::id() const
{
    return QStringLiteral("sma_crossover");
}

QString SmaCrossoverStrategy::displayName() const
{
    return QStringLiteral("SMA(%1/%2) 交叉策略").arg(m_fastPeriod).arg(m_slowPeriod);
}

int SmaCrossoverStrategy::minimumWarmupBars() const
{
    return m_slowPeriod;
}

void SmaCrossoverStrategy::onTick(const Tick &tick)
{
    Q_UNUSED(tick)
}

void SmaCrossoverStrategy::onCandle(const Candle &candle)
{
    if (candle.close <= 0.0) {
        return;
    }

    m_closes.append(candle.close);
    const auto fast = IndicatorEngine::simpleMovingAverage(m_closes, m_fastPeriod);
    const auto slow = IndicatorEngine::simpleMovingAverage(m_closes, m_slowPeriod);
    if (!fast || !slow) {
        return;
    }

    const double spread = *fast - *slow;
    SignalSide side;
    bool shouldSignal = false;
    if ((!m_hasPreviousSpread || m_previousSpread <= 0.0) && spread > 0.0) {
        side = SignalSide::Buy;
        shouldSignal = true;
    } else if (m_hasPreviousSpread && m_previousSpread >= 0.0 && spread < 0.0) {
        side = SignalSide::Sell;
        shouldSignal = true;
    }

    m_previousSpread = spread;
    m_hasPreviousSpread = true;
    if (!shouldSignal) {
        return;
    }

    TradeSignal signal;
    signal.instrument = candle.instrument;
    signal.timestamp = candle.startTime;
    signal.side = side;
    signal.price = candle.close;
    signal.volume = candle.volume;
    signal.reason = side == SignalSide::Buy
        ? QStringLiteral("SMA(%1) 上穿 SMA(%2)").arg(m_fastPeriod).arg(m_slowPeriod)
        : QStringLiteral("SMA(%1) 下穿 SMA(%2)").arg(m_fastPeriod).arg(m_slowPeriod);
    emit signalGenerated(signal);
}

void SmaCrossoverStrategy::reset()
{
    m_closes.clear();
    m_previousSpread = 0.0;
    m_hasPreviousSpread = false;
}

} // namespace pp
