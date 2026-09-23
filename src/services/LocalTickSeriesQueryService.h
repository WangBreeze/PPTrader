#pragma once
#include <atomic>

#include "domain/LocalTickSeriesTypes.h"

namespace pp {

class SqliteLocalDataStore;

class LocalTickSeriesQueryService final {
public:
    explicit LocalTickSeriesQueryService(SqliteLocalDataStore *store);
    LocalTickSeriesResult query(const LocalTickSeriesQuery &request,
                                 const std::atomic_bool *cancelled = nullptr) const;
    static QVector<LocalTickSeriesPoint> sampleByEventIndex(
        const QVector<RawTradeTick> &ticks, int maximumPoints);

private:
    SqliteLocalDataStore *m_store = nullptr;
};

} // namespace pp
