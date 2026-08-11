#pragma once

#include "domain/TradingTypes.h"

#include <QAbstractListModel>

namespace pp {

class PositionListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        NameRole,
        QuantityRole,
        AvailableQuantityRole,
        AverageCostRole,
        MarketPriceRole,
        MarketValueRole,
        UnrealizedPnlRole,
        RealizedPnlRole,
        ReturnPercentRole
    };

    explicit PositionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsertPosition(const Position &position);
    void loadPositions(const QVector<Position> &positions);
    Q_INVOKABLE void clear();

private:
    QVector<Position> m_positions;
};

} // namespace pp
