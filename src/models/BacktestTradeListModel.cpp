#include "models/BacktestTradeListModel.h"

namespace pp {

BacktestTradeListModel::BacktestTradeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BacktestTradeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_trades.size();
}

int BacktestTradeListModel::count() const
{
    return m_trades.size();
}

QVariant BacktestTradeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_trades.size()) {
        return {};
    }

    const BacktestTrade &trade = m_trades.at(index.row());
    switch (role) {
    case SymbolRole: return trade.instrument.symbol;
    case EntryTimeRole: return trade.entryTime;
    case ExitTimeRole: return trade.exitTime;
    case EntryPriceRole: return trade.entryPrice;
    case ExitPriceRole: return trade.exitPrice;
    case QuantityRole: return trade.quantity;
    case GrossPnlRole: return trade.grossPnl;
    case NetPnlRole: return trade.netPnl;
    case CommissionRole: return trade.commission;
    case ReturnPercentRole: return trade.returnPercent;
    case EntryReasonRole: return trade.entryReason;
    case ExitReasonRole: return trade.exitReason;
    case ProfitableRole: return trade.netPnl > 0.0;
    default: return {};
    }
}

QHash<int, QByteArray> BacktestTradeListModel::roleNames() const
{
    return {
        {SymbolRole, "symbol"},
        {EntryTimeRole, "entryTime"},
        {ExitTimeRole, "exitTime"},
        {EntryPriceRole, "entryPrice"},
        {ExitPriceRole, "exitPrice"},
        {QuantityRole, "quantity"},
        {GrossPnlRole, "grossPnl"},
        {NetPnlRole, "netPnl"},
        {CommissionRole, "commission"},
        {ReturnPercentRole, "returnPercent"},
        {EntryReasonRole, "entryReason"},
        {ExitReasonRole, "exitReason"},
        {ProfitableRole, "profitable"},
    };
}

QVariantMap BacktestTradeListModel::get(int row) const
{
    if (row < 0 || row >= m_trades.size()) return {};
    const BacktestTrade &trade = m_trades.at(row);
    return {{QStringLiteral("symbol"), trade.instrument.symbol},
            {QStringLiteral("entryTime"), trade.entryTime},
            {QStringLiteral("exitTime"), trade.exitTime},
            {QStringLiteral("entryPrice"), trade.entryPrice},
            {QStringLiteral("exitPrice"), trade.exitPrice},
            {QStringLiteral("quantity"), trade.quantity},
            {QStringLiteral("grossPnl"), trade.grossPnl},
            {QStringLiteral("netPnl"), trade.netPnl},
            {QStringLiteral("commission"), trade.commission},
            {QStringLiteral("returnPercent"), trade.returnPercent},
            {QStringLiteral("entryReason"), trade.entryReason},
            {QStringLiteral("exitReason"), trade.exitReason},
            {QStringLiteral("profitable"), trade.netPnl > 0.0}};
}

void BacktestTradeListModel::addTrade(const BacktestTrade &trade)
{
    const int row = m_trades.size();
    beginInsertRows({}, row, row);
    m_trades.append(trade);
    endInsertRows();
    emit countChanged();
}

void BacktestTradeListModel::loadTrades(const QVector<BacktestTrade> &trades)
{
    const bool countDidChange = m_trades.size() != trades.size();
    beginResetModel();
    m_trades = trades;
    endResetModel();
    if (countDidChange) emit countChanged();
}

void BacktestTradeListModel::clear()
{
    if (m_trades.isEmpty()) {
        return;
    }
    beginResetModel();
    m_trades.clear();
    endResetModel();
    emit countChanged();
}

} // namespace pp
