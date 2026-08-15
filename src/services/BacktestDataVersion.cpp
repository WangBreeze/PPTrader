#include "services/BacktestDataVersion.h"

#include <QCryptographicHash>

#include <algorithm>

namespace pp {

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
