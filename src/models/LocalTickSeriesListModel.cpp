#include "models/LocalTickSeriesListModel.h"

namespace pp {

namespace {
QString directionCode(RawTradeDirection value)
{
    if (value == RawTradeDirection::Buy) return QStringLiteral("B");
    if (value == RawTradeDirection::Sell) return QStringLiteral("S");
    return QStringLiteral("M");
}
QString directionText(RawTradeDirection value)
{
    if (value == RawTradeDirection::Buy) return QStringLiteral("买盘");
    if (value == RawTradeDirection::Sell) return QStringLiteral("卖盘");
    return QStringLiteral("中性盘");
}
}

LocalTickSeriesListModel::LocalTickSeriesListModel(QObject *parent)
    : QAbstractListModel(parent) {}
int LocalTickSeriesListModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_points.size(); }
QVariant LocalTickSeriesListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) return {};
    const auto &point = m_points.at(index.row());
    switch (role) {
    case TimestampRole: return point.tick.timestamp;
    case EventIndexRole: return point.eventIndex;
    case PriceRole: return point.tick.price;
    case VolumeSharesRole: return point.tick.volumeShares;
    case AmountRole: return point.tick.amount;
    case DirectionRole: return directionCode(point.tick.direction);
    case DirectionTextRole: return directionText(point.tick.direction);
    case SourceLineRole: return point.tick.sourceLine;
    case SourceRole: return point.tick.source;
    case RepresentedCountRole: return point.representedCount;
    default: return {};
    }
}
QHash<int, QByteArray> LocalTickSeriesListModel::roleNames() const
{
    return {{TimestampRole, "timestamp"}, {EventIndexRole, "eventIndex"},
            {PriceRole, "price"}, {VolumeSharesRole, "volumeShares"},
            {AmountRole, "amount"}, {DirectionRole, "direction"},
            {DirectionTextRole, "directionText"}, {SourceLineRole, "sourceLine"},
            {SourceRole, "source"}, {RepresentedCountRole, "representedCount"}};
}
int LocalTickSeriesListModel::count() const { return m_points.size(); }
const QVector<LocalTickSeriesPoint> &LocalTickSeriesListModel::points() const { return m_points; }
void LocalTickSeriesListModel::setPoints(const QVector<LocalTickSeriesPoint> &points)
{
    beginResetModel(); m_points = points; endResetModel(); emit countChanged();
}
QVariantMap LocalTickSeriesListModel::get(int row) const
{ return row >= 0 && row < m_points.size() ? pointMap(m_points.at(row)) : QVariantMap{}; }
void LocalTickSeriesListModel::clear()
{ if (m_points.isEmpty()) return; beginResetModel(); m_points.clear(); endResetModel(); emit countChanged(); }
QVariantMap LocalTickSeriesListModel::pointMap(const LocalTickSeriesPoint &point) const
{
    return {{QStringLiteral("timestamp"), point.tick.timestamp},
            {QStringLiteral("eventIndex"), point.eventIndex},
            {QStringLiteral("price"), point.tick.price},
            {QStringLiteral("volumeShares"), point.tick.volumeShares},
            {QStringLiteral("amount"), point.tick.amount},
            {QStringLiteral("direction"), directionCode(point.tick.direction)},
            {QStringLiteral("directionText"), directionText(point.tick.direction)},
            {QStringLiteral("sourceLine"), point.tick.sourceLine},
            {QStringLiteral("source"), point.tick.source},
            {QStringLiteral("representedCount"), point.representedCount}};
}

} // namespace pp
