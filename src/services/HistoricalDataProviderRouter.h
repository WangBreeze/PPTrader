#pragma once

#include "services/HistoricalDataProvider.h"

#include <QHash>
#include <QVariantList>

namespace pp {

class HistoricalDataProviderRouter final : public HistoricalDataProvider {
    Q_OBJECT

public:
    explicit HistoricalDataProviderRouter(QObject *parent = nullptr);

    void addProvider(HistoricalDataProvider *provider);
    HistoricalProviderCapability capability() const override;
    QVariantList providerCapabilities() const;
    void fetch(quint64 requestId, const Instrument &instrument,
               const QString &frequency, int count) override;
    void fetchAdjusted(quint64 requestId, const Instrument &instrument,
                       const QString &frequency, int count,
                       const QString &adjustment) override;

signals:
    void routeAttempted(quint64 requestId, const QString &providerId,
                        int attemptNumber);
    void routeResolved(quint64 requestId, const QString &providerId,
                       bool fallbackUsed);

private:
    struct RouteState {
        Instrument instrument;
        QString frequency;
        QString adjustment;
        int count = 0;
        QVector<HistoricalDataProvider *> candidates;
        int candidateIndex = -1;
        QStringList errors;
    };

    void startNext(quint64 requestId);
    void handleCandles(HistoricalDataProvider *provider, quint64 requestId,
                       const QVector<Candle> &candles);
    void handleError(HistoricalDataProvider *provider, quint64 requestId,
                     const QString &message);

    QVector<HistoricalDataProvider *> m_providers;
    QHash<quint64, RouteState> m_routes;
};

} // namespace pp

