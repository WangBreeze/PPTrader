#pragma once

#include "domain/CrossSectionalTypes.h"

#include <QHash>

namespace pp {

class MarketSnapshotAssembler final {
public:
    explicit MarketSnapshotAssembler(Market market = Market::AShare);

    void setUniverse(const QVector<Instrument> &instruments);
    void beginBatch(const QString &batchId, const QDateTime &timestamp);
    bool addTick(const Tick &tick);
    bool addQuote(const CrossSectionalQuote &quote);
    MarketSnapshot completeBatch(const QDateTime &completedAt = {});

    int expectedCount() const;
    int receivedCount() const;

private:
    Market m_market = Market::AShare;
    QString m_batchId;
    QDateTime m_timestamp;
    QHash<QString, Instrument> m_universe;
    QHash<QString, CrossSectionalQuote> m_quotes;
    QHash<QString, quint64> m_quoteGenerations;
    quint64 m_generation = 0;
    int m_receivedCount = 0;
};

} // namespace pp
