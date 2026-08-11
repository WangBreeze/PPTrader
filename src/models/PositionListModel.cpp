#include "models/PositionListModel.h"

namespace pp {

PositionListModel::PositionListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PositionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_positions.size();
}

QVariant PositionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_positions.size()) return {};
    const Position &position = m_positions.at(index.row());
    switch (role) {
    case SymbolRole: return position.instrument.symbol;
    case NameRole: return position.instrument.name;
    case QuantityRole: return position.quantity;
    case AvailableQuantityRole: return position.availableQuantity;
    case AverageCostRole: return position.averageCost;
    case MarketPriceRole: return position.marketPrice;
    case MarketValueRole: return position.marketValue;
    case UnrealizedPnlRole: return position.unrealizedPnl;
    case RealizedPnlRole: return position.realizedPnl;
    case ReturnPercentRole:
        return position.averageCost > 0.0
            ? (position.marketPrice / position.averageCost - 1.0) * 100.0 : 0.0;
    default: return {};
    }
}

QHash<int, QByteArray> PositionListModel::roleNames() const
{
    return {{SymbolRole, "symbol"}, {NameRole, "name"}, {QuantityRole, "quantity"},
            {AvailableQuantityRole, "availableQuantity"}, {AverageCostRole, "averageCost"},
            {MarketPriceRole, "marketPrice"}, {MarketValueRole, "marketValue"},
            {UnrealizedPnlRole, "unrealizedPnl"}, {RealizedPnlRole, "realizedPnl"},
            {ReturnPercentRole, "returnPercent"}};
}

void PositionListModel::upsertPosition(const Position &position)
{
    for (int index = 0; index < m_positions.size(); ++index) {
        if (m_positions.at(index).instrument.symbol != position.instrument.symbol) continue;
        if (position.quantity <= 0.000001) {
            beginRemoveRows({}, index, index);
            m_positions.removeAt(index);
            endRemoveRows();
        } else {
            m_positions[index] = position;
            emit dataChanged(this->index(index), this->index(index));
        }
        return;
    }
    if (position.quantity <= 0.000001) return;
    const int row = m_positions.size();
    beginInsertRows({}, row, row);
    m_positions.append(position);
    endInsertRows();
}

void PositionListModel::loadPositions(const QVector<Position> &positions)
{
    beginResetModel();
    m_positions = positions;
    endResetModel();
}

void PositionListModel::clear()
{
    if (m_positions.isEmpty()) return;
    beginResetModel();
    m_positions.clear();
    endResetModel();
}

} // namespace pp
