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
    m_quotes.clear();
    m_quoteGenerations.clear();
    m_universe.reserve(instruments.size());
    m_quotes.reserve(instruments.size());
    m_quoteGenerations.reserve(instruments.size());
    for (const Instrument &instrument : instruments) {
        if (!instrument.symbol.isEmpty() && instrument.market == m_market) {
            m_universe.insert(instrument.symbol, instrument);
            CrossSectionalQuote quote;
            quote.instrument = instrument;
            m_quotes.insert(instrument.symbol, quote);
            m_quoteGenerations.insert(instrument.symbol, 0);
        }
    }
    m_generation = 0;
    m_receivedCount = 0;
}

void MarketSnapshotAssembler::beginBatch(const QString &batchId, const QDateTime &timestamp)
{
    m_batchId = batchId;
    m_timestamp = timestamp;
    ++m_generation;
    if (m_generation == 0) {
        m_generation = 1;
        for (auto iterator = m_quoteGenerations.begin();
             iterator != m_quoteGenerations.end(); ++iterator) {
            iterator.value() = 0;
        }
    }
    m_receivedCount = 0;
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
    auto quoteIterator = m_quotes.find(normalized.instrument.symbol);
    auto generationIterator = m_quoteGenerations.find(normalized.instrument.symbol);
    if (quoteIterator == m_quotes.end()
        || generationIterator == m_quoteGenerations.end()) return false;
    if (generationIterator.value() != m_generation) ++m_receivedCount;
    quoteIterator.value() = std::move(normalized);
    generationIterator.value() = m_generation;
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
    snapshot.quotes.reserve(m_receivedCount);
    for (auto iterator = m_quotes.cbegin(); iterator != m_quotes.cend(); ++iterator) {
        if (m_quoteGenerations.value(iterator.key()) == m_generation) {
            snapshot.quotes.append(iterator.value());
        }
    }
    std::sort(snapshot.quotes.begin(), snapshot.quotes.end(),
              [](const CrossSectionalQuote &left, const CrossSectionalQuote &right) {
                  return left.instrument.symbol < right.instrument.symbol;
              });
    m_receivedCount = 0;
    return snapshot;
}

int MarketSnapshotAssembler::expectedCount() const { return m_universe.size(); }
int MarketSnapshotAssembler::receivedCount() const { return m_receivedCount; }

} // namespace pp
