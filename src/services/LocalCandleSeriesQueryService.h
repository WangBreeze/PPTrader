#pragma once
#include <atomic>

#include "domain/LocalCandleSeriesTypes.h"

namespace pp {

class SqliteLocalDataStore;

class LocalCandleSeriesQueryService final {
public:
    explicit LocalCandleSeriesQueryService(SqliteLocalDataStore *store);

    LocalCandleSeriesResult query(const LocalCandleSeriesQuery &request,
                                 const std::atomic_bool *cancelled = nullptr) const;
    bool refreshRawTickCandleCacheDay(const Instrument &instrument,
                                      const QDate &tradingDate, int periodSeconds,
                                      bool *regenerated = nullptr,
                                      QString *error = nullptr) const;
    static QVector<LocalCandleSeriesPoint> aggregate(
        const QVector<Candle> &candles, int maximumPoints, int *bucketSize = nullptr);

private:
    SqliteLocalDataStore *m_store = nullptr;
};

} // namespace pp
