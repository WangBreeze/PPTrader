#include "models/PortfolioTradeListModel.h"

namespace pp {

PortfolioTradeListModel::PortfolioTradeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PortfolioTradeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_trades.size();
}

int PortfolioTradeListModel::count() const { return m_trades.size(); }

QVariant PortfolioTradeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_trades.size()) return {};
    const PortfolioTrade &trade = m_trades.at(index.row());
    switch (role) {
    case SymbolRole: return trade.instrument.symbol;
    case NameRole: return trade.instrument.name;
    case SideRole: return trade.side;
    case QuantityRole: return trade.quantity;
    case PriceRole: return trade.price;
    case CommissionRole: return trade.commission;
    case AmountRole: return trade.amount;
    case TimestampRole: return trade.timestamp;
    case ReasonRole: return trade.reason;
    default: return {};
    }
}

QHash<int, QByteArray> PortfolioTradeListModel::roleNames() const
{
    return {{SymbolRole, "symbol"}, {NameRole, "name"}, {SideRole, "side"},
            {QuantityRole, "quantity"}, {PriceRole, "price"},
            {CommissionRole, "commission"}, {AmountRole, "amount"},
            {TimestampRole, "timestamp"}, {ReasonRole, "reason"}};
}

QVariantMap PortfolioTradeListModel::get(int row) const
{
    if (row < 0 || row >= m_trades.size()) return {};
    const PortfolioTrade &trade = m_trades.at(row);
    return {{QStringLiteral("symbol"), trade.instrument.symbol},
            {QStringLiteral("name"), trade.instrument.name},
            {QStringLiteral("side"), trade.side},
            {QStringLiteral("quantity"), trade.quantity},
            {QStringLiteral("price"), trade.price},
            {QStringLiteral("commission"), trade.commission},
            {QStringLiteral("amount"), trade.amount},
            {QStringLiteral("timestamp"), trade.timestamp},
            {QStringLiteral("reason"), trade.reason}};
}

void PortfolioTradeListModel::loadTrades(const QVector<PortfolioTrade> &trades)
{
    beginResetModel();
    m_trades = trades;
    endResetModel();
    emit countChanged();
}

void PortfolioTradeListModel::clear()
{
    if (m_trades.isEmpty()) return;
    beginResetModel();
    m_trades.clear();
    endResetModel();
    emit countChanged();
}

} // namespace pp
