#include "services/NasTickSyncQueue.h"

#include <QSet>

#include <algorithm>

namespace pp {

bool NasTickSyncQueue::prepare(const QVector<NasCatalogEntry> &entries,
                               const QString &symbol)
{
    return prepareManifest(entries, QStringList{symbol});
}

bool NasTickSyncQueue::prepareManifest(const QVector<NasCatalogEntry> &entries,
                                       const QStringList &symbols)
{
    if (m_running) return false;
    const QSet<QString> acceptedSymbols(symbols.cbegin(), symbols.cend());
    if (acceptedSymbols.isEmpty()) return false;
    QVector<NasCatalogEntry> filtered;
    for (const NasCatalogEntry &entry : entries) {
        if (acceptedSymbols.contains(entry.symbol) && entry.tradingDate.isValid())
            filtered.append(entry);
    }
    std::sort(filtered.begin(), filtered.end(),
              [](const NasCatalogEntry &left, const NasCatalogEntry &right) {
                  if (left.tradingDate != right.tradingDate)
                      return left.tradingDate < right.tradingDate;
                  return left.symbol < right.symbol;
              });
    filtered.erase(std::unique(filtered.begin(), filtered.end(),
                               [](const NasCatalogEntry &left, const NasCatalogEntry &right) {
                                   return left.tradingDate == right.tradingDate
                                       && left.symbol == right.symbol;
                               }), filtered.end());
    m_items.clear();
    m_items.reserve(filtered.size());
    for (const NasCatalogEntry &entry : filtered) m_items.append({entry});
    m_activeIndices.clear();
    return !m_items.isEmpty();
}

bool NasTickSyncQueue::restore(const QVector<NasSyncPlanItem> &items)
{
    if (m_running || items.isEmpty()) return false;
    m_items = items;
    std::sort(m_items.begin(), m_items.end(),
              [](const NasSyncPlanItem &left, const NasSyncPlanItem &right) {
                  if (left.entry.tradingDate != right.entry.tradingDate)
                      return left.entry.tradingDate < right.entry.tradingDate;
                  return left.entry.symbol < right.entry.symbol;
              });
    m_items.erase(std::unique(m_items.begin(), m_items.end(),
                              [](const NasSyncPlanItem &left,
                                 const NasSyncPlanItem &right) {
                                  return left.entry.tradingDate == right.entry.tradingDate
                                      && left.entry.symbol == right.entry.symbol;
                              }), m_items.end());
    for (NasSyncPlanItem &item : m_items) {
        if (item.status == NasSyncItemStatus::Downloading
            || item.status == NasSyncItemStatus::Importing) {
            item.status = NasSyncItemStatus::Cancelled;
            item.error = QStringLiteral("应用退出时尚未完成，可重试");
        }
    }
    m_running = false;
    m_activeIndices.clear();
    return !m_items.isEmpty();
}

bool NasTickSyncQueue::start()
{
    if (m_running || m_items.isEmpty()) return false;
    for (NasSyncPlanItem &item : m_items) {
        item.status = NasSyncItemStatus::Ready;
        item.rawTicksWritten = 0;
        item.candleBarsWritten = 0;
        item.fromCache = false;
        item.transferElapsedMilliseconds = 0;
        item.parseElapsedMilliseconds = 0;
        item.rawDatabaseElapsedMilliseconds = 0;
        item.candleDatabaseElapsedMilliseconds = 0;
        item.importElapsedMilliseconds = 0;
        item.error.clear();
    }
    m_running = true;
    m_activeIndices.clear();
    return true;
}

bool NasTickSyncQueue::resume()
{
    if (m_running || m_items.isEmpty() || !canResume()) return false;
    for (NasSyncPlanItem &item : m_items) {
        if (item.status == NasSyncItemStatus::Failed
            || item.status == NasSyncItemStatus::Cancelled
            || item.status == NasSyncItemStatus::Downloading
            || item.status == NasSyncItemStatus::Importing) {
            item.status = NasSyncItemStatus::Ready;
            item.error.clear();
        }
    }
    m_running = true;
    m_activeIndices.clear();
    return true;
}

int NasTickSyncQueue::takeNextReady()
{
    const QVector<int> batch = takeReadyBatch(1);
    return batch.isEmpty() ? -1 : batch.first();
}

QVector<int> NasTickSyncQueue::takeReadyBatch(int maximum)
{
    QVector<int> result;
    if (!m_running || maximum <= 0) return result;
    for (int index = 0; index < m_items.size(); ++index) {
        if (result.size() >= maximum) break;
        if (m_items.at(index).status == NasSyncItemStatus::Ready) {
            m_items[index].status = NasSyncItemStatus::Downloading;
            m_activeIndices.append(index);
            result.append(index);
        }
    }
    if (result.isEmpty()) m_running = false;
    return result;
}

bool NasTickSyncQueue::markImporting(int index, qint64 transferElapsedMilliseconds,
                                     bool fromCache)
{
    if (!validIndexAndStatus(index, NasSyncItemStatus::Downloading)) return false;
    m_items[index].status = NasSyncItemStatus::Importing;
    m_items[index].transferElapsedMilliseconds = transferElapsedMilliseconds;
    m_items[index].fromCache = fromCache;
    return true;
}

bool NasTickSyncQueue::markSucceeded(int index, qint64 rawTicksWritten,
                                     int candleBarsWritten,
                                     qint64 parseElapsedMilliseconds,
                                     qint64 rawDatabaseElapsedMilliseconds,
                                     qint64 candleDatabaseElapsedMilliseconds,
                                     qint64 importElapsedMilliseconds)
{
    if (!validIndexAndStatus(index, NasSyncItemStatus::Importing)) return false;
    NasSyncPlanItem &item = m_items[index];
    item.status = NasSyncItemStatus::Succeeded;
    item.rawTicksWritten = rawTicksWritten;
    item.candleBarsWritten = candleBarsWritten;
    item.parseElapsedMilliseconds = parseElapsedMilliseconds;
    item.rawDatabaseElapsedMilliseconds = rawDatabaseElapsedMilliseconds;
    item.candleDatabaseElapsedMilliseconds = candleDatabaseElapsedMilliseconds;
    item.importElapsedMilliseconds = importElapsedMilliseconds;
    item.error.clear();
    m_activeIndices.removeAll(index);
    return true;
}

bool NasTickSyncQueue::markFailed(int index, const QString &error)
{
    if (index < 0 || index >= m_items.size()
        || !m_activeIndices.contains(index)) return false;
    NasSyncPlanItem &item = m_items[index];
    if (item.status != NasSyncItemStatus::Downloading
        && item.status != NasSyncItemStatus::Importing) return false;
    item.status = NasSyncItemStatus::Failed;
    item.error = error.isEmpty() ? QStringLiteral("未知同步错误") : error;
    m_activeIndices.removeAll(index);
    return true;
}

void NasTickSyncQueue::cancelActiveAndPending()
{
    for (int index : m_activeIndices) {
        if (index < 0 || index >= m_items.size()) continue;
        m_items[index].status = NasSyncItemStatus::Cancelled;
        m_items[index].error = QStringLiteral("用户取消");
    }
    for (NasSyncPlanItem &item : m_items) {
        if (item.status == NasSyncItemStatus::Ready)
            item.status = NasSyncItemStatus::Cancelled;
    }
    m_running = false;
    m_activeIndices.clear();
}

const QVector<NasSyncPlanItem> &NasTickSyncQueue::items() const { return m_items; }

NasSyncSummary NasTickSyncQueue::summary() const
{
    NasSyncSummary result;
    result.total = m_items.size();
    QSet<QString> symbols;
    for (const NasSyncPlanItem &item : m_items) {
        symbols.insert(item.entry.symbol);
        result.totalBytes += item.entry.size;
        if (item.status == NasSyncItemStatus::Succeeded) {
            ++result.completed;
            ++result.succeeded;
            result.succeededBytes += item.entry.size;
            result.rawTicksWritten += item.rawTicksWritten;
            result.candleBarsWritten += item.candleBarsWritten;
            result.transferElapsedMilliseconds += item.transferElapsedMilliseconds;
            result.parseElapsedMilliseconds += item.parseElapsedMilliseconds;
            result.rawDatabaseElapsedMilliseconds += item.rawDatabaseElapsedMilliseconds;
            result.candleDatabaseElapsedMilliseconds += item.candleDatabaseElapsedMilliseconds;
            result.importElapsedMilliseconds += item.importElapsedMilliseconds;
            if (item.fromCache) ++result.cacheHits;
        } else if (item.status == NasSyncItemStatus::Failed) {
            ++result.completed;
            ++result.failed;
        } else {
            ++result.pending;
        }
    }
    result.symbols = symbols.size();
    return result;
}

bool NasTickSyncQueue::running() const { return m_running; }

bool NasTickSyncQueue::canResume() const
{
    return std::any_of(m_items.cbegin(), m_items.cend(), [](const NasSyncPlanItem &item) {
        return item.status == NasSyncItemStatus::Failed
            || item.status == NasSyncItemStatus::Cancelled
            || item.status == NasSyncItemStatus::Downloading
            || item.status == NasSyncItemStatus::Importing;
    });
}

int NasTickSyncQueue::activeIndex() const
{
    return m_activeIndices.isEmpty() ? -1 : m_activeIndices.first();
}

int NasTickSyncQueue::activeCount() const { return m_activeIndices.size(); }

int NasTickSyncQueue::downloadingCount() const
{
    int count = 0;
    for (int index : m_activeIndices) {
        if (index >= 0 && index < m_items.size()
            && m_items.at(index).status == NasSyncItemStatus::Downloading) {
            ++count;
        }
    }
    return count;
}

bool NasTickSyncQueue::validIndexAndStatus(int index, NasSyncItemStatus status) const
{
    return index >= 0 && index < m_items.size()
        && m_activeIndices.contains(index)
        && m_items.at(index).status == status;
}

} // namespace pp
