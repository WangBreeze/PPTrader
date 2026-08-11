#pragma once

#include "domain/CrossSectionalTypes.h"

#include <QObject>
#include <QVariantList>
#include <QVector>

namespace pp {

enum class ProviderHealthState {
    Stopped = 0,
    Starting,
    Healthy,
    Degraded,
    Error
};

inline ProviderHealthState providerHealthForBatch(int expectedCount, int receivedCount,
                                                   bool hadFailure)
{
    if (expectedCount <= 0 || (receivedCount <= 0 && hadFailure)) {
        return ProviderHealthState::Error;
    }
    if (hadFailure || receivedCount < expectedCount) {
        return ProviderHealthState::Degraded;
    }
    return ProviderHealthState::Healthy;
}

inline QString providerHealthName(ProviderHealthState health)
{
    switch (health) {
    case ProviderHealthState::Stopped: return QStringLiteral("stopped");
    case ProviderHealthState::Starting: return QStringLiteral("starting");
    case ProviderHealthState::Healthy: return QStringLiteral("healthy");
    case ProviderHealthState::Degraded: return QStringLiteral("degraded");
    case ProviderHealthState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

class MarketDataProvider : public QObject {
    Q_OBJECT

public:
    explicit MarketDataProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
        qRegisterMetaType<MarketSnapshot>();
    }

    ~MarketDataProvider() override = default;

    virtual void start(const QVector<Instrument> &instruments) = 0;
    virtual void stop() = 0;

signals:
    void tickReceived(const pp::Tick &tick);
    void snapshotReceived(const pp::MarketSnapshot &snapshot);
    void providerError(const QString &message);
    void providerStateChanged(int health, const QString &message, const QString &error,
                              int expectedCount, int receivedCount,
                              const QString &batchId, const QDateTime &updatedAt);
    void providerRouteChanged(const QString &providerId, const QString &displayName,
                              int score, bool fallbackUsed, int failoverCount,
                              const QString &message,
                              const QVariantList &capabilities);
};

} // 命名空间 pp
