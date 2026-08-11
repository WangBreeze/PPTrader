#pragma once

#include "domain/BacktestTypes.h"

#include <QAbstractListModel>

namespace pp {

class BacktestTradeListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        EntryTimeRole,
        ExitTimeRole,
        EntryPriceRole,
        ExitPriceRole,
        QuantityRole,
        GrossPnlRole,
        NetPnlRole,
        CommissionRole,
        ReturnPercentRole,
        EntryReasonRole,
        ExitReasonRole,
        ProfitableRole
    };

    explicit BacktestTradeListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

    void addTrade(const BacktestTrade &trade);
    void loadTrades(const QVector<BacktestTrade> &trades);
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    QVector<BacktestTrade> m_trades;
};

} // namespace pp
