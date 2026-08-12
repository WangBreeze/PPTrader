#pragma once

#include "domain/PortfolioTypes.h"

#include <QAbstractListModel>

namespace pp {

class PortfolioTradeListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        NameRole,
        SideRole,
        QuantityRole,
        PriceRole,
        CommissionRole,
        AmountRole,
        TimestampRole,
        ReasonRole
    };

    explicit PortfolioTradeListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    void loadTrades(const QVector<PortfolioTrade> &trades);
    void clear();

signals:
    void countChanged();

private:
    QVector<PortfolioTrade> m_trades;
};

} // namespace pp
