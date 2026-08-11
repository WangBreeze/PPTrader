#pragma once

#include "domain/BacktestTypes.h"

#include <QAbstractListModel>

namespace pp {

class EquityCurveListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        EquityRole,
        CashRole,
        PositionValueRole,
        DrawdownPercentRole
    };

    explicit EquityCurveListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

    void addPoint(const EquityPoint &point);
    void loadPoints(const QVector<EquityPoint> &points);
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    QVector<EquityPoint> m_points;
};

} // namespace pp
