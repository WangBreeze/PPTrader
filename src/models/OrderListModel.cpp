#include "models/OrderListModel.h"

#include <algorithm>

namespace pp {

OrderListModel::OrderListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int OrderListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_orders.size();
}

QVariant OrderListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_orders.size()) return {};
    const Order &order = m_orders.at(index.row());
    switch (role) {
    case OrderIdRole: return order.orderId;
    case SymbolRole: return order.request.instrument.symbol;
    case SideRole: return orderSideName(order.request.side);
    case TypeRole: return orderTypeName(order.request.type);
    case TimeInForceRole: return timeInForceName(order.request.timeInForce);
    case StatusRole: return orderStatusName(order.status);
    case QuantityRole: return order.request.quantity;
    case FilledQuantityRole: return order.filledQuantity;
    case RemainingQuantityRole: return std::max(0.0, order.request.quantity - order.filledQuantity);
    case LimitPriceRole: return order.request.limitPrice;
    case AverageFillPriceRole: return order.averageFillPrice;
    case CreatedAtRole: return order.request.createdAt;
    case ExpiresAtRole: return order.request.expiresAt;
    case UpdatedAtRole: return order.updatedAt;
    case MessageRole: return order.message;
    case CancellableRole: return isActiveOrder(order.status);
    default: return {};
    }
}

QHash<int, QByteArray> OrderListModel::roleNames() const
{
    return {{OrderIdRole, "orderId"}, {SymbolRole, "symbol"}, {SideRole, "side"},
            {TypeRole, "orderType"}, {TimeInForceRole, "timeInForce"},
            {StatusRole, "status"}, {QuantityRole, "quantity"},
            {FilledQuantityRole, "filledQuantity"}, {RemainingQuantityRole, "remainingQuantity"},
            {LimitPriceRole, "limitPrice"}, {AverageFillPriceRole, "averageFillPrice"},
            {CreatedAtRole, "createdAt"}, {ExpiresAtRole, "expiresAt"},
            {UpdatedAtRole, "updatedAt"},
            {MessageRole, "message"}, {CancellableRole, "cancellable"}};
}

void OrderListModel::upsertOrder(const Order &order)
{
    for (int index = 0; index < m_orders.size(); ++index) {
        if (m_orders.at(index).orderId == order.orderId) {
            m_orders[index] = order;
            emit dataChanged(this->index(index), this->index(index));
            return;
        }
    }
    const int row = m_orders.size();
    beginInsertRows({}, row, row);
    m_orders.append(order);
    endInsertRows();
}

void OrderListModel::loadOrders(const QVector<Order> &orders)
{
    beginResetModel();
    m_orders = orders;
    endResetModel();
}

void OrderListModel::clear()
{
    if (m_orders.isEmpty()) return;
    beginResetModel();
    m_orders.clear();
    endResetModel();
}

} // namespace pp
