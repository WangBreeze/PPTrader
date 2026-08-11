#include "models/ExecutionListModel.h"

namespace pp {

ExecutionListModel::ExecutionListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ExecutionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_executions.size();
}

QVariant ExecutionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_executions.size()) return {};
    const Execution &execution = m_executions.at(index.row());
    switch (role) {
    case ExecutionIdRole: return execution.executionId;
    case OrderIdRole: return execution.orderId;
    case SymbolRole: return execution.instrument.symbol;
    case SideRole: return orderSideName(execution.side);
    case QuantityRole: return execution.quantity;
    case PriceRole: return execution.price;
    case CommissionRole: return execution.commission;
    case TimestampRole: return execution.timestamp;
    case AmountRole: return execution.price * execution.quantity;
    default: return {};
    }
}

QHash<int, QByteArray> ExecutionListModel::roleNames() const
{
    return {{ExecutionIdRole, "executionId"}, {OrderIdRole, "orderId"},
            {SymbolRole, "symbol"}, {SideRole, "side"}, {QuantityRole, "quantity"},
            {PriceRole, "price"}, {CommissionRole, "commission"},
            {TimestampRole, "timestamp"}, {AmountRole, "amount"}};
}

void ExecutionListModel::addExecution(const Execution &execution)
{
    const int row = m_executions.size();
    beginInsertRows({}, row, row);
    m_executions.append(execution);
    endInsertRows();
}

void ExecutionListModel::loadExecutions(const QVector<Execution> &executions)
{
    beginResetModel();
    m_executions = executions;
    endResetModel();
}

void ExecutionListModel::clear()
{
    if (m_executions.isEmpty()) return;
    beginResetModel();
    m_executions.clear();
    endResetModel();
}

} // namespace pp
