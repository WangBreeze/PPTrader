#include "services/RawTradeIncrementalIngestService.h"

#include "services/LocalCandleSeriesQueryService.h"
#include "services/SqliteLocalDataStore.h"

namespace pp {

RawTradeIncrementalIngestService::RawTradeIncrementalIngestService(
    SqliteLocalDataStore *store)
    : m_store(store)
{
}

RawTradeIncrementalAppendResult RawTradeIncrementalIngestService::ingest(
    const QVector<RawTradeTick> &ticks) const
{
    if (!m_store) {
        RawTradeIncrementalAppendResult result;
        result.receivedCount = ticks.size();
        result.error = QStringLiteral("盘中逐笔增量数据库不可用");
        return result;
    }
    RawTradeIncrementalAppendResult result =
        m_store->appendRawTradeTicksIncremental(ticks);
    if (!result.factCommitted || result.insertedCount == 0) return result;

    LocalCandleSeriesQueryService queryService(m_store);
    for (const int periodSeconds : result.invalidatedPeriods) {
        bool regenerated = false;
        QString error;
        if (!queryService.refreshRawTickCandleCacheDay(
                result.instrument, result.tradingDate, periodSeconds,
                &regenerated, &error)) {
            result.cacheRefreshError = error;
            break;
        }
        if (regenerated) result.refreshedPeriods.append(periodSeconds);
    }
    return result;
}

} // namespace pp
