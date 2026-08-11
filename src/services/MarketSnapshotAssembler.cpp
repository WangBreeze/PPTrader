#include "services/MarketSnapshotAssembler.h"

#include <algorithm>
#include <utility>

namespace pp {

MarketSnapshotAssembler::MarketSnapshotAssembler(Market market)
    : m_market(market)
{
}

void MarketSnapshotAssembler::setUniverse(const QVector<Instrument> &instruments)
{
    m_universe.clear();
    for (const Instrument &instrument : instruments) {
        if (!instrument.symbol.isEmpty() && instrument.market == m_market) {
            m_universe.insert(instrument.symbol, instrument);
        }
    }
    m_quotes.clear();
}

void MarketSnapshotAssembler::beginBatch(const QString &batchId, const QDateTime &timestamp)
{
    m_batchId = batchId;
    m_timestamp = timestamp;
    m_quotes.clear();
}

bool MarketSnapshotAssembler::addTick(const Tick &tick)
{
    CrossSectionalQuote quote;
    quote.instrument = tick.instrument;
    quote.timestamp = tick.timestamp;
    quote.price = tick.price;
    quote.previousClose = tick.previousClose;
    quote.open = tick.open;
    quote.high = tick.high;
    quote.low = tick.low;
    quote.volume = tick.volume;
    quote.amount = tick.amount;
    quote.bid = tick.bid;
    quote.ask = tick.ask;
    return addQuote(quote);
}

bool MarketSnapshotAssembler::addQuote(const CrossSectionalQuote &quote)
{
    const auto universeIterator = m_universe.constFind(quote.instrument.symbol);
    if (universeIterator == m_universe.constEnd() || quote.instrument.market != m_market) {
        return false;
    }
    CrossSectionalQuote normalized = quote;
    normalized.instrument = *universeIterator;
    if (!normalized.timestamp.isValid()) normalized.timestamp = m_timestamp;
    m_quotes.insert(normalized.instrument.symbol, normalized);
    return true;
}

MarketSnapshot MarketSnapshotAssembler::completeBatch(const QDateTime &completedAt)
{
    MarketSnapshot snapshot;
    snapshot.batchId = m_batchId;
    snapshot.market = m_market;
    snapshot.timestamp = m_timestamp;
    snapshot.completedAt = completedAt.isValid() ? completedAt : m_timestamp;
    snapshot.expectedCount = m_universe.size();
    snapshot.quotes.reserve(m_quotes.size());
    for (const CrossSectionalQuote &quote : std::as_const(m_quotes)) {
        snapshot.quotes.append(quote);
    }
    std::sort(snapshot.quotes.begin(), snapshot.quotes.end(),
              [](const CrossSectionalQuote &left, const CrossSectionalQuote &right) {
                  return left.instrument.symbol < right.instrument.symbol;
              });
    m_quotes.clear();
    return snapshot;
}

int MarketSnapshotAssembler::expectedCount() const { return m_universe.size(); }
int MarketSnapshotAssembler::receivedCount() const { return m_quotes.size(); }

} // namespace pp
