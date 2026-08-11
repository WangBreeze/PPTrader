#include "models/MarketSnapshotListModel.h"

#include <algorithm>

namespace pp {

MarketSnapshotListModel::MarketSnapshotListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MarketSnapshotListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_snapshot.quotes.size();
}

int MarketSnapshotListModel::count() const { return m_snapshot.quotes.size(); }

QVariant MarketSnapshotListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_snapshot.quotes.size()) {
        return {};
    }
    const CrossSectionalQuote &quote = m_snapshot.quotes.at(index.row());
    const bool valid = quote.price > 0.0 && !quote.suspended;
    const bool stale = m_stale || quote.stale;
    const QDateTime snapshotTime = quote.timestamp.isValid()
        ? quote.timestamp : m_snapshot.timestamp;
    QString invalidReason;
    if (quote.suspended) invalidReason = QStringLiteral("停牌");
    else if (quote.price <= 0.0) invalidReason = QStringLiteral("价格无效");
    else if (stale) invalidReason = QStringLiteral("行情已陈旧");
    const QString health = stale ? QStringLiteral("stale")
        : quote.suspended ? QStringLiteral("suspended")
        : !valid ? QStringLiteral("invalid") : QStringLiteral("healthy");

    switch (role) {
    case DisplayIndexRole: return index.row() + 1;
    case SymbolRole: return quote.instrument.symbol;
    case NameRole: return quote.instrument.name.isEmpty()
            ? quote.instrument.symbol : quote.instrument.name;
    case MarketRole: return marketName(quote.instrument.market);
    case VenueRole: return quote.instrument.venue;
    case BoardRole:
    case MarketBucketRole: return boardForInstrument(quote.instrument);
    case PriceRole: return quote.price;
    case PreviousCloseRole: return quote.previousClose;
    case ChangePercentRole: return quote.previousClose > 0.0
            ? (quote.price - quote.previousClose) / quote.previousClose * 100.0 : 0.0;
    case OpenRole: return quote.open;
    case HighRole: return quote.high;
    case LowRole: return quote.low;
    case VolumeRole: return quote.volume;
    case AmountRole:
    case TurnoverRole: return quote.amount;
    case BidRole: return quote.bid;
    case AskRole: return quote.ask;
    case SnapshotTimeRole: return snapshotTime;
    case SuspendedRole: return quote.suspended;
    case StaleRole: return stale;
    case ValidRole: return valid;
    case HealthRole: return health;
    case InvalidReasonRole: return invalidReason;
    default: return {};
    }
}

QHash<int, QByteArray> MarketSnapshotListModel::roleNames() const
{
    return {{DisplayIndexRole, "displayIndex"}, {SymbolRole, "symbol"},
            {NameRole, "name"}, {MarketRole, "market"}, {VenueRole, "venue"},
            {BoardRole, "board"}, {MarketBucketRole, "marketBucket"},
            {PriceRole, "price"}, {PreviousCloseRole, "previousClose"},
            {ChangePercentRole, "changePercent"}, {OpenRole, "open"},
            {HighRole, "high"}, {LowRole, "low"}, {VolumeRole, "volume"},
            {AmountRole, "amount"}, {TurnoverRole, "turnover"},
            {BidRole, "bid"}, {AskRole, "ask"},
            {SnapshotTimeRole, "snapshotTime"}, {SuspendedRole, "suspended"},
            {StaleRole, "stale"}, {ValidRole, "valid"}, {HealthRole, "health"},
            {InvalidReasonRole, "invalidReason"}};
}

void MarketSnapshotListModel::loadSnapshot(const MarketSnapshot &snapshot, bool stale)
{
    const int previousCount = count();
    MarketSnapshot normalized = snapshot;
    std::sort(normalized.quotes.begin(), normalized.quotes.end(),
              [](const CrossSectionalQuote &left, const CrossSectionalQuote &right) {
                  return left.instrument.symbol < right.instrument.symbol;
              });
    beginResetModel();
    m_snapshot = std::move(normalized);
    m_stale = stale;
    endResetModel();
    if (previousCount != count()) emit countChanged();
}

const MarketSnapshot &MarketSnapshotListModel::snapshot() const { return m_snapshot; }
bool MarketSnapshotListModel::snapshotIsStale() const { return m_stale; }

QVariantMap MarketSnapshotListModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= count()) return result;
    const QModelIndex modelIndex = index(row);
    const auto names = roleNames();
    for (auto iterator = names.cbegin(); iterator != names.cend(); ++iterator) {
        result.insert(QString::fromLatin1(iterator.value()), data(modelIndex, iterator.key()));
    }
    return result;
}

int MarketSnapshotListModel::indexOfSymbol(const QString &symbol) const
{
    const auto iterator = std::lower_bound(
        m_snapshot.quotes.cbegin(), m_snapshot.quotes.cend(), symbol,
        [](const CrossSectionalQuote &quote, const QString &value) {
            return quote.instrument.symbol < value;
        });
    if (iterator == m_snapshot.quotes.cend() || iterator->instrument.symbol != symbol) return -1;
    return static_cast<int>(iterator - m_snapshot.quotes.cbegin());
}

void MarketSnapshotListModel::clear()
{
    if (m_snapshot.quotes.isEmpty()) return;
    beginResetModel();
    m_snapshot = {};
    m_stale = false;
    endResetModel();
    emit countChanged();
}

QString MarketSnapshotListModel::boardForInstrument(const Instrument &instrument)
{
    const QString symbol = instrument.symbol.trimmed();
    const QString venue = instrument.venue.trimmed().toLower();
    if (symbol.startsWith(QLatin1Char('4')) || symbol.startsWith(QLatin1Char('8'))
        || venue.contains(QStringLiteral("北交")) || venue.contains(QStringLiteral("bse"))) {
        return QStringLiteral("北交所");
    }
    if (symbol.startsWith(QStringLiteral("688"))) return QStringLiteral("科创板");
    if (symbol.startsWith(QStringLiteral("300"))
        || symbol.startsWith(QStringLiteral("301"))) {
        return QStringLiteral("创业板");
    }
    if (symbol.startsWith(QStringLiteral("000"))
        || symbol.startsWith(QStringLiteral("001"))
        || symbol.startsWith(QStringLiteral("002"))
        || symbol.startsWith(QStringLiteral("003"))
        || venue.contains(QStringLiteral("深圳")) || venue.contains(QStringLiteral("szse"))) {
        return QStringLiteral("深市主板");
    }
    if (symbol.startsWith(QStringLiteral("600"))
        || symbol.startsWith(QStringLiteral("601"))
        || symbol.startsWith(QStringLiteral("603"))
        || symbol.startsWith(QStringLiteral("605"))
        || venue.contains(QStringLiteral("上海")) || venue.contains(QStringLiteral("sse"))) {
        return QStringLiteral("主板");
    }
    return QStringLiteral("未知");
}

} // namespace pp
