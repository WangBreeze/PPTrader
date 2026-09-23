#pragma once

#include "domain/LocalTickSeriesTypes.h"

#include <QAbstractListModel>
#include <QVariantMap>

namespace pp {

class LocalTickSeriesListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role {
        TimestampRole = Qt::UserRole + 1, EventIndexRole, PriceRole,
        VolumeSharesRole, AmountRole, DirectionRole, DirectionTextRole,
        SourceLineRole, SourceRole, RepresentedCountRole
    };
    explicit LocalTickSeriesListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    const QVector<LocalTickSeriesPoint> &points() const;
    void setPoints(const QVector<LocalTickSeriesPoint> &points);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE void clear();
signals:
    void countChanged();
private:
    QVariantMap pointMap(const LocalTickSeriesPoint &point) const;
    QVector<LocalTickSeriesPoint> m_points;
};

} // namespace pp
