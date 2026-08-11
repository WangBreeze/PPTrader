#include "services/RealtimeProviderRouter.h"

#include <algorithm>

namespace pp {

RealtimeProviderRouter::RealtimeProviderRouter(QObject *parent)
    : MarketDataProvider(parent)
    , m_watchdog(new QTimer(this))
{
    m_watchdog->setInterval(1000);
    connect(m_watchdog, &QTimer::timeout, this,
            &RealtimeProviderRouter::checkWatchdog);
}

void RealtimeProviderRouter::addProvider(
    MarketDataProvider *provider, const RealtimeProviderCapability &capability)
{
    if (!provider) return;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.provider == provider || entry.capability.providerId == capability.providerId)
            return;
    }
    const int index = m_entries.size();
    Entry entry;
    entry.provider = provider;
    entry.capability = capability;
    m_entries.append(entry);
    connect(provider, &MarketDataProvider::tickReceived, this,
            [this, index](const Tick &tick) {
        if (index == m_activeIndex) {
            m_entries[index].lastSignal.restart();
            emit tickReceived(tick);
        }
    });
    connect(provider, &MarketDataProvider::snapshotReceived, this,
            [this, index](const MarketSnapshot &snapshot) {
        if (index == m_activeIndex) {
            m_entries[index].lastSignal.restart();
            emit snapshotReceived(snapshot);
        }
    });
    connect(provider, &MarketDataProvider::providerError, this,
            [this, index](const QString &message) {
        if (index == m_activeIndex) emit providerError(message);
    });
    connect(provider, &MarketDataProvider::providerStateChanged, this,
            [this, index](int health, const QString &message, const QString &error,
                          int expectedCount, int receivedCount,
                          const QString &batchId, const QDateTime &updatedAt) {
        handleState(index, health, message, error, expectedCount, receivedCount,
                    batchId, updatedAt);
    });
}

void RealtimeProviderRouter::start(const QVector<Instrument> &instruments)
{
    stop();
    m_instruments = instruments;
    m_failoverCount = 0;
    m_running = true;
    for (Entry &entry : m_entries) {
        entry.failureStreak = 0;
        entry.lastDegradedBatchId.clear();
        entry.circuitOpenUntil = {};
        entry.score = 0;
    }
    m_watchdog->start();
    activateBest(QStringLiteral("选择优先级最高的已配置实时行情源"));
}

void RealtimeProviderRouter::stop()
{
    m_running = false;
    m_watchdog->stop();
    const int previous = m_activeIndex;
    m_activeIndex = -1;
    if (previous >= 0 && previous < m_entries.size()) {
        m_entries[previous].provider->stop();
    }
}

QVariantList RealtimeProviderRouter::providerCapabilities() const
{
    QVector<RealtimeProviderCapability> capabilities;
    for (const Entry &entry : m_entries) capabilities.append(entry.capability);
    std::sort(capabilities.begin(), capabilities.end(), [](const auto &left, const auto &right) {
        return left.priority < right.priority;
    });
    QVariantList result;
    for (const auto &capability : capabilities) result.append(capability.toVariantMap());
    return result;
}

QString RealtimeProviderRouter::activeProviderId() const
{
    return m_activeIndex >= 0 ? m_entries.at(m_activeIndex).capability.providerId : QString{};
}

int RealtimeProviderRouter::activeProviderScore() const
{
    return m_activeIndex >= 0 ? m_entries.at(m_activeIndex).score : 0;
}

bool RealtimeProviderRouter::fallbackUsed() const
{
    return m_activeIndex >= 0
        && m_entries.at(m_activeIndex).capability.priority > primaryPriority();
}

int RealtimeProviderRouter::failoverCount() const { return m_failoverCount; }

void RealtimeProviderRouter::setFailurePolicy(
    int degradedBatchThreshold, int watchdogTimeoutMilliseconds,
    int circuitCooldownMilliseconds)
{
    m_degradedBatchThreshold = std::max(1, degradedBatchThreshold);
    m_watchdogTimeoutMilliseconds = std::max(10, watchdogTimeoutMilliseconds);
    m_circuitCooldownMilliseconds = std::max(10, circuitCooldownMilliseconds);
    m_watchdog->setInterval(std::clamp(m_watchdogTimeoutMilliseconds / 4, 5, 1000));
}

void RealtimeProviderRouter::activateBest(const QString &reason, int excludedIndex)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int selected = -1;
    for (int index = 0; index < m_entries.size(); ++index) {
        const Entry &entry = m_entries.at(index);
        if (index == excludedIndex || !entry.capability.configured) continue;
        if (entry.circuitOpenUntil.isValid() && entry.circuitOpenUntil > now) continue;
        if (selected < 0 || entry.capability.priority
                < m_entries.at(selected).capability.priority) {
            selected = index;
        }
    }
    if (selected < 0) {
        m_activeIndex = -1;
        emit providerStateChanged(static_cast<int>(ProviderHealthState::Error),
                                  QStringLiteral("没有可用的实时行情候选源"), reason,
                                  m_instruments.size(), 0, {}, now);
        publishRoute(QStringLiteral("所有候选源均未配置或处于熔断冷却"));
        return;
    }
    m_activeIndex = selected;
    Entry &entry = m_entries[selected];
    entry.health = static_cast<int>(ProviderHealthState::Starting);
    entry.expectedCount = m_instruments.size();
    entry.receivedCount = 0;
    entry.score = realtimeProviderHealthScore(entry.health, entry.expectedCount, 0,
                                               entry.failureStreak, 0);
    entry.lastSignal.restart();
    publishRoute(reason);
    entry.provider->start(m_instruments);
}

void RealtimeProviderRouter::failover(const QString &reason)
{
    if (!m_running || m_activeIndex < 0) return;
    const int failed = m_activeIndex;
    Entry &entry = m_entries[failed];
    entry.circuitOpenUntil = QDateTime::currentDateTimeUtc().addMSecs(
        m_circuitCooldownMilliseconds);
    m_activeIndex = -1;
    entry.provider->stop();
    ++m_failoverCount;
    activateBest(reason, failed);
}

void RealtimeProviderRouter::handleState(
    int index, int health, const QString &message, const QString &error,
    int expectedCount, int receivedCount, const QString &batchId,
    const QDateTime &updatedAt)
{
    if (index < 0 || index >= m_entries.size()) return;
    Entry &entry = m_entries[index];
    entry.health = health;
    entry.expectedCount = std::max(0, expectedCount);
    entry.receivedCount = std::max(0, receivedCount);
    entry.lastSignal.restart();
    if (health == static_cast<int>(ProviderHealthState::Healthy)) {
        entry.failureStreak = 0;
        entry.lastDegradedBatchId.clear();
    } else if (health == static_cast<int>(ProviderHealthState::Degraded)
               && !batchId.isEmpty() && batchId != entry.lastDegradedBatchId) {
        const double completeness = expectedCount > 0
            ? static_cast<double>(receivedCount) / expectedCount : 0.0;
        if (completeness < 0.90) ++entry.failureStreak;
        entry.lastDegradedBatchId = batchId;
    } else if (health == static_cast<int>(ProviderHealthState::Error)) {
        ++entry.failureStreak;
    }
    entry.score = realtimeProviderHealthScore(
        health, entry.expectedCount, entry.receivedCount, entry.failureStreak, 0);
    if (index != m_activeIndex) return;

    const QString routedMessage = QStringLiteral("%1 · %2")
        .arg(entry.capability.displayName, message);
    emit providerStateChanged(health, routedMessage, error, expectedCount,
                              receivedCount, batchId, updatedAt);
    publishRoute(routedMessage);
    if (health == static_cast<int>(ProviderHealthState::Error)) {
        failover(QStringLiteral("%1 进入错误状态，切换备用源")
                     .arg(entry.capability.displayName));
    } else if (entry.failureStreak >= m_degradedBatchThreshold) {
        failover(QStringLiteral("%1 连续 %2 个批次降级，切换备用源")
                     .arg(entry.capability.displayName)
                     .arg(entry.failureStreak));
    }
}

void RealtimeProviderRouter::publishRoute(const QString &message)
{
    if (m_activeIndex < 0) {
        emit routeStateChanged({}, {}, 0, false, m_failoverCount,
                               message, providerCapabilities());
        return;
    }
    const Entry &entry = m_entries.at(m_activeIndex);
    emit routeStateChanged(entry.capability.providerId,
                           entry.capability.displayName, entry.score,
                           fallbackUsed(), m_failoverCount, message,
                           providerCapabilities());
}

void RealtimeProviderRouter::checkWatchdog()
{
    if (!m_running) return;
    if (m_activeIndex < 0) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const bool candidateReady = std::any_of(
            m_entries.cbegin(), m_entries.cend(), [&now](const Entry &entry) {
            return entry.capability.configured
                && (!entry.circuitOpenUntil.isValid() || entry.circuitOpenUntil <= now);
        });
        if (candidateReady) {
            activateBest(QStringLiteral("熔断冷却结束，重试实时行情候选源"));
        }
        return;
    }
    Entry &entry = m_entries[m_activeIndex];
    if (!entry.lastSignal.isValid()) return;
    const qint64 silence = entry.lastSignal.elapsed();
    entry.score = realtimeProviderHealthScore(
        entry.health, entry.expectedCount, entry.receivedCount,
        entry.failureStreak, silence);
    const int watchdogTimeout = entry.capability.watchdogTimeoutMilliseconds > 0
        ? entry.capability.watchdogTimeoutMilliseconds
        : m_watchdogTimeoutMilliseconds;
    if (silence >= watchdogTimeout) {
        failover(QStringLiteral("%1 超过 %2 ms 未更新，切换备用源")
                     .arg(entry.capability.displayName)
                     .arg(watchdogTimeout));
    }
}

int RealtimeProviderRouter::primaryPriority() const
{
    int priority = 1000000;
    for (const Entry &entry : m_entries) {
        if (entry.capability.configured) priority = std::min(priority, entry.capability.priority);
    }
    return priority;
}

} // namespace pp
