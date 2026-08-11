#include "services/RawTickCandleAggregator.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace pp {

RawTickCandleAggregator::RawTickCandleAggregator(
    const Instrument &instrument, int periodSeconds)
    : m_instrument(instrument), m_periodSeconds(periodSeconds)
{
}

bool RawTickCandleAggregator::validPeriod(int periodSeconds)
{
    return periodSeconds >= 1 && periodSeconds <= 86400;
}

std::optional<QDateTime> RawTickCandleAggregator::bucketStart(
    const QDateTime &timestamp, int periodSeconds)
{
    if (!timestamp.isValid() || !validPeriod(periodSeconds)) return std::nullopt;
    static const QTimeZone shanghai("Asia/Shanghai");
    const QDateTime local = timestamp.toUTC().toTimeZone(shanghai);
    const QDate date = local.date();
    const QTime time = local.time();
    if (periodSeconds >= 14400) {
        if (time < QTime(9, 25) || time > QTime(15, 0)) return std::nullopt;
        return QDateTime(date, QTime(9, 25), shanghai).toUTC();
    }
    if (time >= QTime(9, 25) && time < QTime(9, 30)) {
        return QDateTime(date, QTime(9, 25), shanghai).toUTC();
    }
    struct Session { QTime start; QTime close; };
    const Session sessions[] {{QTime(9, 30), QTime(11, 30)},
                              {QTime(13, 0), QTime(15, 0)}};
    for (const Session &session : sessions) {
        if (time < session.start || time > session.close) continue;
        const int duration = session.start.secsTo(session.close);
        const int elapsed = time == session.close
            ? std::max(0, duration - 1) : session.start.secsTo(time);
        const QTime bucket = session.start.addSecs((elapsed / periodSeconds) * periodSeconds);
        return QDateTime(date, bucket, shanghai).toUTC();
    }
    return std::nullopt;
}

bool RawTickCandleAggregator::append(const RawTradeTick &tick)
{
    const auto bucket = bucketStart(tick.timestamp, m_periodSeconds);
    if (!bucket.has_value() || !std::isfinite(tick.price) || tick.price <= 0.0
        || !std::isfinite(tick.volumeShares) || tick.volumeShares < 0.0) {
        ++m_rejectedCount;
        return false;
    }
    if (m_current && m_current->startTime != *bucket) flush();
    if (!m_current) {
        Candle candle;
        candle.instrument = m_instrument;
        candle.startTime = *bucket;
        candle.periodSeconds = m_periodSeconds;
        candle.open = candle.high = candle.low = candle.close = tick.price;
        candle.directionalVolumeKnown = true;
        candle.source = QStringLiteral("raw_tick_cache_v1");
        candle.adjustment = QStringLiteral("none");
        candle.downloadedAt = QDateTime::currentDateTimeUtc();
        m_current = candle;
    } else {
        m_current->high = std::max(m_current->high, tick.price);
        m_current->low = std::min(m_current->low, tick.price);
        m_current->close = tick.price;
    }
    m_current->volume += tick.volumeShares;
    switch (tick.direction) {
    case RawTradeDirection::Buy: m_current->buyVolume += tick.volumeShares; break;
    case RawTradeDirection::Sell: m_current->sellVolume += tick.volumeShares; break;
    case RawTradeDirection::Neutral: m_current->neutralVolume += tick.volumeShares; break;
    }
    return true;
}

QVector<Candle> RawTickCandleAggregator::finish()
{
    flush();
    return std::move(m_candles);
}

int RawTickCandleAggregator::rejectedCount() const { return m_rejectedCount; }

void RawTickCandleAggregator::flush()
{
    if (!m_current) return;
    m_candles.append(*m_current);
    m_current.reset();
}

} // namespace pp
