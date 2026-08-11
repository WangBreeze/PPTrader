#pragma once

#include "domain/MarketTypes.h"

#include <QAbstractListModel>
#include <QVector>

namespace pp {

class SignalListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        TimeRole,
        SideRole,
        PriceRole,
        VolumeRole,
        ReasonRole
    };

    explicit SignalListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addSignal(const TradeSignal &signal);
    Q_INVOKABLE void clear();

private:
    QVector<TradeSignal> m_signals;
    int m_maxRows = 200;
};

} // 命名空间 pp
