#pragma once

#include "services/MarketDataProvider.h"
#include "services/MarketSnapshotAssembler.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace pp {

class TencentMarketDataProvider final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit TencentMarketDataProvider(QObject *parent = nullptr);
    ~TencentMarketDataProvider() override;

    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

private:
    void beginSnapshot();
    void requestNextChunk();
    void handleReply(QNetworkReply *reply, int requestedCount);
    void completeSnapshot();
    void publishState(ProviderHealthState health, const QString &message,
                      const QString &error = {}, int receivedCount = -1,
                      const QString &batchId = {});

    QNetworkAccessManager m_network;
    QTimer m_pollTimer;
    QPointer<QNetworkReply> m_activeReply;
    QVector<Instrument> m_instruments;
    QHash<QString, Instrument> m_instrumentBySymbol;
    MarketSnapshotAssembler m_snapshotAssembler {Market::AShare};
    QString m_batchId;
    QStringList m_errors;
    QDateTime m_startedAt;
    QDateTime m_latestQuoteTime;
    QDateTime m_lastEmittedQuoteTime;
    int m_offset = 0;
    int m_batchSize = 80;
    int m_pollIntervalMilliseconds = 60000;
    int m_completedBatches = 0;
    int m_maximumBatches = 0;
    bool m_running = false;
};

} // namespace pp
