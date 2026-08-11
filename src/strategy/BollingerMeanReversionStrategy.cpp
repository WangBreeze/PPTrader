#include "strategy/BollingerMeanReversionStrategy.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

BollingerMeanReversionStrategy::BollingerMeanReversionStrategy(
    int period, double standardDeviations, QObject *parent)
    : BacktestStrategy(parent)
    , m_period(std::max(2, period))
    , m_standardDeviations(std::clamp(standardDeviations, 0.1, 10.0))
{
}

QString BollingerMeanReversionStrategy::id() const
{
    return QStringLiteral("bollinger_mean_reversion");
}

QString BollingerMeanReversionStrategy::displayName() const
{
    return QStringLiteral("布林带均值回归");
}

int BollingerMeanReversionStrategy::minimumWarmupBars() const { return m_period; }
void BollingerMeanReversionStrategy::onTick(const Tick &tick) { Q_UNUSED(tick) }

void BollingerMeanReversionStrategy::onCandle(const Candle &candle)
{
    if (candle.close <= 0.0) return;
    m_closes.append(candle.close);
    if (m_closes.size() < m_period) return;
    if (m_closes.size() > m_period) m_closes.removeFirst();

    const double mean = std::accumulate(m_closes.cbegin(), m_closes.cend(), 0.0) / m_closes.size();
    double variance = 0.0;
    for (double close : m_closes) variance += (close - mean) * (close - mean);
    const double deviation = std::sqrt(variance / m_closes.size());
    const double lowerBand = mean - m_standardDeviations * deviation;

    SignalSide side{};
    bool emitSignal = false;
    if (!m_hasPosition && candle.close < lowerBand) {
        side = SignalSide::Buy;
        m_hasPosition = true;
        emitSignal = true;
    } else if (m_hasPosition && candle.close >= mean) {
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
    signal.reason = side == SignalSide::Buy
        ? QStringLiteral("收盘价跌破布林带下轨") : QStringLiteral("收盘价回归布林带中轨");
    emit signalGenerated(signal);
}

void BollingerMeanReversionStrategy::reset()
{
    m_closes.clear();
    m_hasPosition = false;
}

} // namespace pp
