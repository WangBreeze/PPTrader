#pragma once

#include <QString>
#include <QVariantMap>

namespace pp {

struct RealtimeProviderCapability {
    QString providerId;
    QString displayName;
    QString transport;
    int priority = 100;
    bool configured = false;
    bool fullMarket = false;
    bool rawTrades = false;
    bool polling = true;
    bool nativeMinuteBars = false;
    int minimumPeriodSeconds = 0;
    int pollingIntervalSeconds = 0;
    int watchdogTimeoutMilliseconds = 0;
    bool authenticationRequired = false;
    QString configurationMessage;

    QVariantMap toVariantMap() const;
};

int realtimeProviderHealthScore(int health, int expectedCount, int receivedCount,
                                int failureStreak, qint64 silenceMilliseconds);
int normalizedRealtimePollingIntervalSeconds(int requestedSeconds);

} // namespace pp

Q_DECLARE_METATYPE(pp::RealtimeProviderCapability)
