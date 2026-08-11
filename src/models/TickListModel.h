#pragma once

#include "domain/MarketTypes.h"

#include <QAbstractListModel>
#include <QVector>

namespace pp {

class TickListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        MarketRole,
        TimeRole,
        PriceRole,
        VolumeRole,
        BidRole,
        AskRole
    };

    explicit TickListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addTick(const Tick &tick);
    void loadTicks(const QVector<Tick> &ticks);
    Q_INVOKABLE void clear();

private:
    QVector<Tick> m_ticks;
    int m_maxRows = 500;
};

} // 命名空间 pp
