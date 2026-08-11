#include "strategy/MacdTrendStrategy.h"

#include <algorithm>

namespace pp {

MacdTrendStrategy::MacdTrendStrategy(int fastPeriod, int slowPeriod,
                                     int signalPeriod, QObject *parent)
    : BacktestStrategy(parent)
    , m_fastPeriod(std::max(2, fastPeriod))
    , m_slowPeriod(std::max(m_fastPeriod + 1, slowPeriod))
    , m_signalPeriod(std::max(2, signalPeriod))
{
}

QString MacdTrendStrategy::id() const { return QStringLiteral("macd_trend"); }
QString MacdTrendStrategy::displayName() const { return QStringLiteral("MACD 趋势"); }
int MacdTrendStrategy::minimumWarmupBars() const { return m_slowPeriod + m_signalPeriod; }
void MacdTrendStrategy::onTick(const Tick &tick) { Q_UNUSED(tick) }

void MacdTrendStrategy::onCandle(const Candle &candle)
{
    if (candle.close <= 0.0) return;
    ++m_count;
    if (m_count == 1) {
        m_fastEma = candle.close;
        m_slowEma = candle.close;
        m_signalEma = 0.0;
        return;
    }

    const double fastAlpha = 2.0 / (m_fastPeriod + 1.0);
    const double slowAlpha = 2.0 / (m_slowPeriod + 1.0);
    const double signalAlpha = 2.0 / (m_signalPeriod + 1.0);
    m_fastEma += fastAlpha * (candle.close - m_fastEma);
    m_slowEma += slowAlpha * (candle.close - m_slowEma);
    const double macd = m_fastEma - m_slowEma;
    m_signalEma += signalAlpha * (macd - m_signalEma);
    const double difference = macd - m_signalEma;

    if (m_count < minimumWarmupBars()) {
        m_previousDifference = difference;
        m_hasPreviousDifference = true;
        return;
    }

    SignalSide side{};
    bool emitSignal = false;
    if ((!m_hasPreviousDifference || m_previousDifference <= 0.0) && difference > 0.0) {
        side = SignalSide::Buy;
        emitSignal = true;
    } else if (m_hasPreviousDifference && m_previousDifference >= 0.0 && difference < 0.0) {
        side = SignalSide::Sell;
        emitSignal = true;
    }
    m_previousDifference = difference;
    m_hasPreviousDifference = true;
    if (!emitSignal) return;

    TradeSignal signal;
    signal.instrument = candle.instrument;
    signal.timestamp = candle.startTime;
    signal.side = side;
    signal.price = candle.close;
    signal.volume = candle.volume;
    signal.reason = side == SignalSide::Buy
        ? QStringLiteral("MACD 上穿信号线") : QStringLiteral("MACD 下穿信号线");
    emit signalGenerated(signal);
}

void MacdTrendStrategy::reset()
{
    m_count = 0;
    m_fastEma = 0.0;
    m_slowEma = 0.0;
    m_signalEma = 0.0;
    m_previousDifference = 0.0;
    m_hasPreviousDifference = false;
}

} // namespace pp
