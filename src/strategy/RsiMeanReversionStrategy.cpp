#include "strategy/RsiMeanReversionStrategy.h"

#include <algorithm>

namespace pp {

RsiMeanReversionStrategy::RsiMeanReversionStrategy(int period, double oversold,
                                                   double overbought, QObject *parent)
    : BacktestStrategy(parent)
    , m_period(std::max(2, period))
    , m_oversold(std::clamp(oversold, 1.0, 98.0))
    , m_overbought(std::clamp(overbought, m_oversold + 1.0, 99.0))
{
}

QString RsiMeanReversionStrategy::id() const { return QStringLiteral("rsi_mean_reversion"); }

QString RsiMeanReversionStrategy::displayName() const
{
    return QStringLiteral("RSI 均值回归");
}

int RsiMeanReversionStrategy::minimumWarmupBars() const { return m_period + 1; }

void RsiMeanReversionStrategy::onTick(const Tick &tick) { Q_UNUSED(tick) }

void RsiMeanReversionStrategy::onCandle(const Candle &candle)
{
    if (candle.close <= 0.0) return;
    m_closes.append(candle.close);
    if (m_closes.size() < minimumWarmupBars()) return;
    if (m_closes.size() > minimumWarmupBars()) m_closes.removeFirst();

    double gains = 0.0;
    double losses = 0.0;
    for (int index = 1; index < m_closes.size(); ++index) {
        const double change = m_closes.at(index) - m_closes.at(index - 1);
        if (change > 0.0) gains += change;
        else losses -= change;
    }
    const double rsi = losses <= 0.0 ? 100.0
        : 100.0 - 100.0 / (1.0 + gains / losses);

    SignalSide side{};
    bool emitSignal = false;
    if (!m_hasPosition && rsi <= m_oversold) {
        side = SignalSide::Buy;
        m_hasPosition = true;
        emitSignal = true;
    } else if (m_hasPosition && rsi >= m_overbought) {
        side = SignalSide::Sell;
        m_hasPosition = false;
        emitSignal = true;
    }
    if (!emitSignal) return;

    TradeSignal signal;
    signal.instrument = candle.instrument;
    signal.timestamp = candle.startTime;
    signal.side = side;
    signal.price = candle.close;
    signal.volume = candle.volume;
    signal.reason = QStringLiteral("RSI(%1)=%2，%3阈值")
        .arg(m_period).arg(rsi, 0, 'f', 2)
        .arg(side == SignalSide::Buy ? QStringLiteral("低于超卖") : QStringLiteral("高于超买"));
    emit signalGenerated(signal);
}

void RsiMeanReversionStrategy::reset()
{
    m_closes.clear();
    m_hasPosition = false;
}

} // namespace pp
