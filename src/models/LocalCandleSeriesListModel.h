#pragma once

#include "domain/LocalCandleSeriesTypes.h"

#include <QAbstractListModel>
#include <QVariantMap>

namespace pp {

class LocalCandleSeriesListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        EndTimeRole,
        OpenRole,
        HighRole,
        LowRole,
        CloseRole,
        VolumeRole,
        BuyVolumeRole,
        SellVolumeRole,
        NeutralVolumeRole,
        DirectionalVolumeKnownRole,
        SourceRole,
        SourceCountRole
    };

    explicit LocalCandleSeriesListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    const QVector<LocalCandleSeriesPoint> &points() const;

    void setPoints(const QVector<LocalCandleSeriesPoint> &points);
    bool prependPoints(const QVector<LocalCandleSeriesPoint> &points);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    QVariantMap pointMap(const LocalCandleSeriesPoint &point) const;
    QVector<LocalCandleSeriesPoint> m_points;
};

} // namespace pp
