#include "services/HistoricalDataService.h"

#include "services/BacktestDataVersion.h"

#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace pp {

namespace {

bool isValidCandle(const Candle &candle, const Instrument &instrument,
                   int periodSeconds)
{
    if (candle.instrument.symbol != instrument.symbol
        || candle.periodSeconds != periodSeconds || !candle.startTime.isValid()) {
        return false;
    }
    const double values[] = {candle.open, candle.high, candle.low,
                             candle.close, candle.volume};
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    if (candle.open <= 0.0 || candle.high <= 0.0
        || candle.low <= 0.0 || candle.close <= 0.0 || candle.volume < 0.0) {
        return false;
    }
    const double maximum = std::max({candle.open, candle.close, candle.low});
    const double minimum = std::min({candle.open, candle.close, candle.high});
    return candle.high >= maximum && candle.low <= minimum;
}

} // namespace

HistoricalDataService::HistoricalDataService(LocalDataStore *store, HistoricalDataProvider *provider, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_provider(provider)
{
    Q_ASSERT(m_store);
    Q_ASSERT(m_provider);
    connect(m_provider, &HistoricalDataProvider::candlesReady,
            this, &HistoricalDataService::handleProviderCandles);
    connect(m_provider, &HistoricalDataProvider::fetchError,
            this, &HistoricalDataService::handleProviderError);
}

int HistoricalDataService::periodSecondsForFrequency(const QString &frequency)
{
    if (frequency == QStringLiteral("1m")) return 60;
    if (frequency == QStringLiteral("5m")) return 300;
    if (frequency == QStringLiteral("15m")) return 900;
    if (frequency == QStringLiteral("30m")) return 1800;
    if (frequency == QStringLiteral("60m")) return 3600;
    if (frequency == QStringLiteral("day")) return 86400;
    return 0;
}

quint64 HistoricalDataService::request(const Instrument &instrument, const QString &frequency,
                                       int count, bool forceRefresh,
                                       const QString &adjustment)
{
    const quint64 requestId = ++m_nextRequestId;
    m_activeRequestId = requestId;
    const int periodSeconds = periodSecondsForFrequency(frequency);
    if (instrument.symbol.isEmpty() || periodSeconds == 0 || count <= 0) {
        emit requestError(requestId, QStringLiteral("历史数据请求参数无效"));
        return requestId;
    }

    m_pendingInstrument = instrument;
    m_pendingFrequency = frequency;
    m_pendingCount = std::clamp(count, 1, 5000);
    m_pendingAdjustment = adjustment.isEmpty() ? QStringLiteral("none") : adjustment;
    if (!QStringList{QStringLiteral("none"), QStringLiteral("qfq"),
                     QStringLiteral("hfq")}.contains(m_pendingAdjustment)) {
        emit requestError(requestId, QStringLiteral("历史数据复权方式无效：%1")
                                         .arg(m_pendingAdjustment));
        return requestId;
    }

    if (!forceRefresh) {
        QVector<Candle> cached = m_store->loadRecentCandles(
            instrument, periodSeconds, m_pendingCount);
        cached.erase(std::remove_if(cached.begin(), cached.end(),
                                    [&](const Candle &candle) {
            return !isValidCandle(candle, instrument, periodSeconds)
                || candle.adjustment != m_pendingAdjustment;
        }), cached.end());
        if (cached.size() >= m_pendingCount) {
            QTimer::singleShot(0, this, [this, requestId, cached]() {
                if (requestId != m_activeRequestId) return;
                const QString source = cached.isEmpty() ? QStringLiteral("unknown")
                                                        : cached.last().source;
                const QDateTime downloadedAt = cached.isEmpty() ? QDateTime{}
                                                                 : cached.last().downloadedAt;
                emit datasetIdentityReady(
                    requestId, source, m_pendingAdjustment,
                    computeBacktestDataVersion(cached, m_pendingInstrument.symbol,
                                               cached.first().periodSeconds),
                    downloadedAt, m_pendingCount, cached.size());
                emit candlesReady(requestId, cached, true);
            });
            return requestId;
        }
    }

    const int requestedCount = m_pendingCount;
    QTimer::singleShot(0, this, [this, requestId, instrument, frequency, requestedCount]() {
        if (requestId == m_activeRequestId) {
            m_provider->fetchAdjusted(requestId, instrument, frequency,
                                      requestedCount, m_pendingAdjustment);
        }
    });
    return requestId;
}

void HistoricalDataService::handleProviderCandles(quint64 requestId,
                                                  const QVector<Candle> &candles)
{
    if (requestId != m_activeRequestId) return;
    if (candles.isEmpty()) {
        emit requestError(requestId, QStringLiteral("行情提供方未返回历史 K 线"));
        return;
    }
    const int periodSeconds = periodSecondsForFrequency(m_pendingFrequency);
    QVector<Candle> validCandles;
    validCandles.reserve(candles.size());
    std::copy_if(candles.cbegin(), candles.cend(), std::back_inserter(validCandles),
                 [this, periodSeconds](const Candle &candle) {
        return isValidCandle(candle, m_pendingInstrument, periodSeconds);
    });
    if (validCandles.isEmpty()) {
        emit requestError(requestId,
                          QStringLiteral("行情提供方返回的 OHLCV 全部无效，未写入缓存"));
        return;
    }

    const QString dataVersion = computeBacktestDataVersion(
        validCandles, m_pendingInstrument.symbol, periodSeconds);
    if (!m_store->upsertHistoricalDataset(validCandles, m_pendingCount,
                                          dataVersion)) {
        emit requestError(requestId, QStringLiteral("历史 K 线缓存写入失败"));
        return;
    }

    QVector<Candle> result = m_store->loadRecentCandles(m_pendingInstrument, periodSeconds, m_pendingCount);
    if (result.isEmpty()) {
        result = validCandles;
    }
    const Candle &identityCandle = validCandles.last();
    emit datasetIdentityReady(requestId, identityCandle.source,
                              identityCandle.adjustment, dataVersion,
                              identityCandle.downloadedAt,
                              m_pendingCount, validCandles.size());
    emit candlesReady(requestId, result, false);
}

void HistoricalDataService::handleProviderError(quint64 requestId, const QString &message)
{
    if (requestId == m_activeRequestId) emit requestError(requestId, message);
}

} // namespace pp
