#include "SampleThresholdStrategy.h"

#include <cmath>

namespace pp {

SampleThresholdStrategy::SampleThresholdStrategy(const Instrument &instrument,
                                                 double threshold,
                                                 const QString &strategyId,
                                                 QObject *parent)
    : Strategy(parent)
    , m_instrument(instrument)
    , m_threshold(threshold)
    , m_strategyId(strategyId)
{
}

QString SampleThresholdStrategy::id() const
{
    return m_strategyId;
}

QString SampleThresholdStrategy::displayName() const
{
    return QStringLiteral("示例阈值策略 %1 @ %2")
        .arg(m_instrument.symbol)
        .arg(m_threshold, 0, 'f', 2);
}

QStringList SampleThresholdStrategy::subscribedSymbols() const
{
    return m_instrument.symbol.isEmpty() ? QStringList{} : QStringList{m_instrument.symbol};
}

void SampleThresholdStrategy::onTick(const Tick &tick)
{
    Q_UNUSED(tick)
}

void SampleThresholdStrategy::onCandle(const Candle &candle)
{
    QMutexLocker locker(&m_mutex);
    if (!std::isfinite(candle.close) || candle.close <= 0.0) return;
    const bool above = candle.close > m_threshold;
    if (above == m_above) return;
    m_above = above;

    TradeSignal signal;
    signal.instrument = candle.instrument;
    signal.timestamp = candle.startTime;
    signal.side = above ? SignalSide::Buy : SignalSide::Sell;
    signal.price = candle.close;
    signal.volume = candle.volume;
    signal.reason = above ? QStringLiteral("close 上穿阈值 %1").arg(m_threshold)
                          : QStringLiteral("close 下穿阈值 %1").arg(m_threshold);
    emit signalGenerated(signal);
}

} // namespace pp
