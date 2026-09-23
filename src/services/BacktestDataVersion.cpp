#include "services/BacktestDataVersion.h"

#include <QCryptographicHash>

#include <algorithm>

namespace pp {

RawTickBacktestDataVersionBuilder::RawTickBacktestDataVersionBuilder(
    const QString &symbol)
    : m_symbol(symbol), m_hash(QCryptographicHash::Sha256)
{
    m_hash.addData(m_symbol.toUtf8());
    m_hash.addData("\nraw_ticks_v1\n");
}

bool RawTickBacktestDataVersionBuilder::append(const RawTradeTick &tick)
{
    const bool ordered = !m_previousTimestamp.isValid()
        || tick.timestamp > m_previousTimestamp
        || (tick.timestamp == m_previousTimestamp
            && tick.sourceLine >= m_previousSourceLine);
    if (m_finished || tick.instrument.symbol != m_symbol
        || !tick.timestamp.isValid() || tick.price <= 0.0
        || tick.volumeShares < 0.0 || !ordered) {
        m_valid = false;
        return false;
    }
    m_hash.addData(QByteArray::number(
        tick.timestamp.toUTC().toMSecsSinceEpoch()));
    m_hash.addData(",");
    m_hash.addData(QByteArray::number(tick.sourceLine));
    m_hash.addData(",");
    m_hash.addData(QByteArray::number(tick.price, 'g', 17));
    m_hash.addData(",");
    m_hash.addData(QByteArray::number(tick.volumeShares, 'g', 17));
    m_hash.addData(",");
    m_hash.addData(QByteArray::number(tick.amount, 'g', 17));
    m_hash.addData(",");
    m_hash.addData(QByteArray::number(static_cast<int>(tick.direction)));
    m_hash.addData("\n");
    m_previousTimestamp = tick.timestamp;
    m_previousSourceLine = tick.sourceLine;
    ++m_count;
    return true;
}

QString RawTickBacktestDataVersionBuilder::finish()
{
    if (!m_valid || m_finished) return {};
    m_finished = true;
    return QStringLiteral("sha256:%1").arg(
        QString::fromLatin1(m_hash.result().toHex()));
}

int RawTickBacktestDataVersionBuilder::count() const { return m_count; }

QString computeBacktestDataVersion(const QVector<Candle> &candles,
                                   const QString &symbol,
                                   int periodSeconds)
{
    QVector<Candle> canonical = candles;
    canonical.erase(std::remove_if(canonical.begin(), canonical.end(), [&](const Candle &candle) {
        return candle.instrument.symbol != symbol || !candle.startTime.isValid()
            || candle.close <= 0.0;
    }), canonical.end());
    std::sort(canonical.begin(), canonical.end(), [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    });

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(symbol.toUtf8());
    hash.addData("\n");
    hash.addData(QByteArray::number(periodSeconds));
    hash.addData("\n");
    for (const Candle &candle : canonical) {
        hash.addData(QByteArray::number(candle.startTime.toUTC().toMSecsSinceEpoch()));
        hash.addData(",");
        hash.addData(QByteArray::number(candle.open, 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(candle.high, 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(candle.low, 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(candle.close, 'g', 17));
        hash.addData(",");
        hash.addData(QByteArray::number(candle.volume, 'g', 17));
        hash.addData("\n");
    }
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(hash.result().toHex()));
}

QString computeBacktestDataVersion(const QVector<RawTradeTick> &ticks,
                                   const QString &symbol)
{
    QVector<RawTradeTick> canonical = ticks;
    canonical.erase(std::remove_if(canonical.begin(), canonical.end(),
        [&](const RawTradeTick &tick) {
            return tick.instrument.symbol != symbol || !tick.timestamp.isValid()
                || tick.price <= 0.0 || tick.volumeShares < 0.0;
        }), canonical.end());
    std::sort(canonical.begin(), canonical.end(),
              [](const RawTradeTick &left, const RawTradeTick &right) {
        if (left.timestamp != right.timestamp) return left.timestamp < right.timestamp;
        return left.sourceLine < right.sourceLine;
    });

    RawTickBacktestDataVersionBuilder builder(symbol);
    for (const RawTradeTick &tick : canonical) {
        if (!builder.append(tick)) return {};
    }
    return builder.finish();
}

QString computeBacktestDatasetVersion(const BacktestDatasetIdentity &identity)
{
    const bool hasContentDigests = !identity.days.isEmpty()
        && std::all_of(identity.days.cbegin(), identity.days.cend(),
                       [](const RawTickDayFingerprint &day) {
            return day.rawContentSha256.startsWith(QStringLiteral("sha256:"))
                && day.rawContentSha256.size() == 71;
        });
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(identity.instrument.symbol.toUtf8());
    hash.addData("\n");
    hash.addData(QByteArray::number(identity.periodSeconds));
    hash.addData("\n");
    hash.addData(identity.source.toUtf8());
    hash.addData("\n");
    hash.addData(QByteArray::number(identity.aggregatorVersion));
    hash.addData("\n");
    if (hasContentDigests) hash.addData("raw-content-sha256-v2\n");

    QVector<RawTickDayFingerprint> days = identity.days;
    std::sort(days.begin(), days.end(), [](const RawTickDayFingerprint &left,
                                            const RawTickDayFingerprint &right) {
        return left.tradingDate < right.tradingDate;
    });
    for (const RawTickDayFingerprint &day : days) {
        hash.addData(day.tradingDate.toString(Qt::ISODate).toUtf8());
        hash.addData(",");
        hash.addData(QByteArray::number(day.rawCount));
        if (hasContentDigests) {
            hash.addData(",");
            hash.addData(day.rawContentSha256.toUtf8());
        } else {
            hash.addData(",");
            hash.addData(QByteArray::number(day.rawMaximumId));
        }
        hash.addData("\n");
    }
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(hash.result().toHex()));
}

} // namespace pp
