#pragma once

#include "domain/TradingTypes.h"

#include <QAbstractListModel>

namespace pp {

class ExecutionListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        ExecutionIdRole = Qt::UserRole + 1,
        OrderIdRole,
        SymbolRole,
        SideRole,
        QuantityRole,
        PriceRole,
        CommissionRole,
        TimestampRole,
        AmountRole
    };

    explicit ExecutionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addExecution(const Execution &execution);
    void loadExecutions(const QVector<Execution> &executions);
    Q_INVOKABLE void clear();

private:
    QVector<Execution> m_executions;
};

} // namespace pp
