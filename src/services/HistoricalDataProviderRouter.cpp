#include "services/HistoricalDataProviderRouter.h"

#include <algorithm>

namespace pp {

HistoricalDataProviderRouter::HistoricalDataProviderRouter(QObject *parent)
    : HistoricalDataProvider(parent)
{
}

void HistoricalDataProviderRouter::addProvider(HistoricalDataProvider *provider)
{
    if (!provider || m_providers.contains(provider)) return;
    m_providers.append(provider);
    connect(provider, &HistoricalDataProvider::candlesReady, this,
            [this, provider](quint64 requestId, const QVector<Candle> &candles) {
        handleCandles(provider, requestId, candles);
    });
    connect(provider, &HistoricalDataProvider::fetchError, this,
            [this, provider](quint64 requestId, const QString &message) {
        handleError(provider, requestId, message);
    });
}

HistoricalProviderCapability HistoricalDataProviderRouter::capability() const
{
    HistoricalProviderCapability result;
    result.providerId = QStringLiteral("router");
    result.displayName = QStringLiteral("历史数据 Provider 路由");
    result.configured = std::any_of(m_providers.cbegin(), m_providers.cend(),
                                    [](const HistoricalDataProvider *provider) {
        return provider->capability().configured;
    });
    for (const HistoricalDataProvider *provider : m_providers) {
        const HistoricalProviderCapability item = provider->capability();
        for (const Market market : item.markets) {
            if (!result.markets.contains(market)) result.markets.append(market);
        }
        for (const QString &venue : item.venues) {
            if (!result.venues.contains(venue)) result.venues.append(venue);
        }
        for (const QString &frequency : item.frequencies) {
            if (!result.frequencies.contains(frequency)) result.frequencies.append(frequency);
        }
        for (const QString &adjustment : item.adjustments) {
            if (!result.adjustments.contains(adjustment)) result.adjustments.append(adjustment);
        }
    }
    return result;
}

QVariantList HistoricalDataProviderRouter::providerCapabilities() const
{
    QVector<HistoricalProviderCapability> capabilities;
    capabilities.reserve(m_providers.size());
    for (const HistoricalDataProvider *provider : m_providers) {
        capabilities.append(provider->capability());
    }
    std::sort(capabilities.begin(), capabilities.end(),
              [](const auto &left, const auto &right) {
        return left.priority < right.priority;
    });
    QVariantList result;
    for (const auto &item : capabilities) result.append(item.toVariantMap());
    return result;
}

void HistoricalDataProviderRouter::fetch(
    quint64 requestId, const Instrument &instrument,
    const QString &frequency, int count)
{
    fetchAdjusted(requestId, instrument, frequency, count, QStringLiteral("none"));
}

void HistoricalDataProviderRouter::fetchAdjusted(
    quint64 requestId, const Instrument &instrument, const QString &frequency,
    int count, const QString &adjustment)
{
    // The routed bridge instances are single-flight QProcess adapters. A newer
    // explicit request supersedes every older route so abandoned attempts do
    // not retain state or later publish a stale result.
    m_routes.clear();
    RouteState state;
    state.instrument = instrument;
    state.frequency = frequency;
    state.adjustment = adjustment.isEmpty() ? QStringLiteral("none") : adjustment;
    state.count = count;
    for (HistoricalDataProvider *provider : m_providers) {
        if (provider->capability().supports(instrument, frequency, state.adjustment)) {
            state.candidates.append(provider);
        }
    }
    std::sort(state.candidates.begin(), state.candidates.end(),
              [](const auto *left, const auto *right) {
        return left->capability().priority < right->capability().priority;
    });
    m_routes.insert(requestId, state);
    startNext(requestId);
}

void HistoricalDataProviderRouter::startNext(quint64 requestId)
{
    auto iterator = m_routes.find(requestId);
    if (iterator == m_routes.end()) return;
    RouteState &state = iterator.value();
    ++state.candidateIndex;
    if (state.candidateIndex >= state.candidates.size()) {
        const QString detail = state.errors.isEmpty()
            ? QStringLiteral("没有已配置且支持该市场/周期/复权方式的历史数据源")
            : state.errors.join(QStringLiteral("；"));
        m_routes.erase(iterator);
        emit fetchError(requestId, detail);
        return;
    }
    HistoricalDataProvider *provider = state.candidates.at(state.candidateIndex);
    emit routeAttempted(requestId, provider->capability().providerId,
                        state.candidateIndex + 1);
    provider->fetchAdjusted(requestId, state.instrument, state.frequency,
                            state.count, state.adjustment);
}

void HistoricalDataProviderRouter::handleCandles(
    HistoricalDataProvider *provider, quint64 requestId,
    const QVector<Candle> &candles)
{
    auto iterator = m_routes.find(requestId);
    if (iterator == m_routes.end()) return;
    const RouteState state = iterator.value();
    if (state.candidateIndex < 0
        || state.candidateIndex >= state.candidates.size()
        || state.candidates.at(state.candidateIndex) != provider) return;
    if (candles.isEmpty()) {
        handleError(provider, requestId, QStringLiteral("返回空 K 线"));
        return;
    }
    const QString providerId = provider->capability().providerId;
    const bool fallbackUsed = state.candidateIndex > 0;
    m_routes.erase(iterator);
    emit routeResolved(requestId, providerId, fallbackUsed);
    emit candlesReady(requestId, candles);
}

void HistoricalDataProviderRouter::handleError(
    HistoricalDataProvider *provider, quint64 requestId, const QString &message)
{
    auto iterator = m_routes.find(requestId);
    if (iterator == m_routes.end()) return;
    RouteState &state = iterator.value();
    if (state.candidateIndex < 0
        || state.candidateIndex >= state.candidates.size()
        || state.candidates.at(state.candidateIndex) != provider) return;
    state.errors.append(QStringLiteral("%1：%2")
                            .arg(provider->capability().displayName, message));
    startNext(requestId);
}

} // namespace pp
