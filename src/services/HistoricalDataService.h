#pragma once

#include "services/HistoricalDataProvider.h"
#include "services/LocalDataStore.h"

namespace pp {

class HistoricalDataService final : public QObject {
    Q_OBJECT

public:
    HistoricalDataService(LocalDataStore *store, HistoricalDataProvider *provider, QObject *parent = nullptr);

    quint64 request(const Instrument &instrument, const QString &frequency, int count,
                    bool forceRefresh = false,
                    const QString &adjustment = QStringLiteral("none"));
    static int periodSecondsForFrequency(const QString &frequency);

signals:
    void candlesReady(quint64 requestId, const QVector<pp::Candle> &candles, bool fromCache);
    void datasetIdentityReady(quint64 requestId, const QString &source,
                              const QString &adjustment,
                              const QString &dataVersion,
                              const QDateTime &downloadedAt,
                              int requestedBars, int actualBars);
    void requestError(quint64 requestId, const QString &message);

private:
    void handleProviderCandles(quint64 requestId, const QVector<Candle> &candles);
    void handleProviderError(quint64 requestId, const QString &message);

    LocalDataStore *m_store = nullptr;
    HistoricalDataProvider *m_provider = nullptr;
    Instrument m_pendingInstrument;
    QString m_pendingFrequency;
    int m_pendingCount = 0;
    QString m_pendingAdjustment = QStringLiteral("none");
    quint64 m_nextRequestId = 0;
    quint64 m_activeRequestId = 0;
};

} // namespace pp
