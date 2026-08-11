#pragma once

#include "domain/TradingTypes.h"

#include <QAbstractListModel>

namespace pp {

class OrderListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        OrderIdRole = Qt::UserRole + 1,
        SymbolRole,
        SideRole,
        TypeRole,
        TimeInForceRole,
        StatusRole,
        QuantityRole,
        FilledQuantityRole,
        RemainingQuantityRole,
        LimitPriceRole,
        AverageFillPriceRole,
        CreatedAtRole,
        ExpiresAtRole,
        UpdatedAtRole,
        MessageRole,
        CancellableRole
    };

    explicit OrderListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsertOrder(const Order &order);
    void loadOrders(const QVector<Order> &orders);
    Q_INVOKABLE void clear();

private:
    QVector<Order> m_orders;
};

} // namespace pp
