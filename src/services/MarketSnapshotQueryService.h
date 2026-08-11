#pragma once

#include "domain/CrossSectionalTypes.h"

#include <QString>

#include <optional>

namespace pp {

class LocalDataStore;

enum class MarketSnapshotHealth {
    Empty,
    Healthy,
    Incomplete,
    Stale,
    Error
};

struct MarketSnapshotQueryResult {
    MarketSnapshot snapshot;
    QDateTime requestedTime;
    QDateTime resolvedTime;
    qint64 stalenessMilliseconds = 0;
    MarketSnapshotHealth health = MarketSnapshotHealth::Empty;
    QString error;
    bool found = false;
};

class MarketSnapshotQueryService final {
public:
    explicit MarketSnapshotQueryService(LocalDataStore *store);

    static std::optional<QDateTime> parseMarketTime(const QString &value,
                                                    QString *error = nullptr);
    static MarketSnapshotQueryResult evaluate(const MarketSnapshot &snapshot,
                                              const QDateTime &requestedTime,
                                              qint64 maximumStalenessMilliseconds = 10000);
    MarketSnapshotQueryResult query(const QDateTime &requestedTime,
                                    qint64 maximumStalenessMilliseconds = 10000) const;

private:
    LocalDataStore *m_store = nullptr;
};

QString marketSnapshotHealthName(MarketSnapshotHealth health);

} // namespace pp

Q_DECLARE_METATYPE(pp::MarketSnapshotQueryResult)
