#pragma once

#include "domain/RealtimeProviderTypes.h"
#include "services/MarketDataProvider.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>

namespace pp {

class RealtimeProviderRouter final : public MarketDataProvider {
    Q_OBJECT

public:
    explicit RealtimeProviderRouter(QObject *parent = nullptr);

    void addProvider(MarketDataProvider *provider,
                     const RealtimeProviderCapability &capability);
    void start(const QVector<Instrument> &instruments) override;
    void stop() override;

    QVariantList providerCapabilities() const;
    QString activeProviderId() const;
    int activeProviderScore() const;
    bool fallbackUsed() const;
    int failoverCount() const;

    void setFailurePolicy(int degradedBatchThreshold,
                          int watchdogTimeoutMilliseconds,
                          int circuitCooldownMilliseconds);

signals:
    void routeStateChanged(const QString &providerId, const QString &displayName,
                           int score, bool fallbackUsed, int failoverCount,
                           const QString &message,
                           const QVariantList &capabilities);

private:
    struct Entry {
        MarketDataProvider *provider = nullptr;
        RealtimeProviderCapability capability;
        int health = static_cast<int>(ProviderHealthState::Stopped);
        int expectedCount = 0;
        int receivedCount = 0;
        int failureStreak = 0;
        int score = 0;
        QString lastDegradedBatchId;
        QElapsedTimer lastSignal;
        QDateTime circuitOpenUntil;
    };

    void activateBest(const QString &reason, int excludedIndex = -1);
    void failover(const QString &reason);
    void handleState(int index, int health, const QString &message,
                     const QString &error, int expectedCount, int receivedCount,
                     const QString &batchId, const QDateTime &updatedAt);
    void publishRoute(const QString &message);
    void checkWatchdog();
    int primaryPriority() const;

    QVector<Entry> m_entries;
    QVector<Instrument> m_instruments;
    QTimer *m_watchdog = nullptr;
    int m_activeIndex = -1;
    int m_degradedBatchThreshold = 3;
    int m_watchdogTimeoutMilliseconds = 30000;
    int m_circuitCooldownMilliseconds = 60000;
    int m_failoverCount = 0;
    bool m_running = false;
};

} // namespace pp
