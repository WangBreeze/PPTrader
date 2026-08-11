#pragma once

#include "domain/CrossSectionalTypes.h"

#include <QAbstractListModel>
#include <QVariantMap>

namespace pp {

class MarketSnapshotListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        DisplayIndexRole = Qt::UserRole + 1,
        SymbolRole,
        NameRole,
        MarketRole,
        VenueRole,
        BoardRole,
        MarketBucketRole,
        PriceRole,
        PreviousCloseRole,
        ChangePercentRole,
        OpenRole,
        HighRole,
        LowRole,
        VolumeRole,
        AmountRole,
        TurnoverRole,
        BidRole,
        AskRole,
        SnapshotTimeRole,
        SuspendedRole,
        StaleRole,
        ValidRole,
        HealthRole,
        InvalidReasonRole
    };
    Q_ENUM(Role)

    explicit MarketSnapshotListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void loadSnapshot(const MarketSnapshot &snapshot, bool stale);
    const MarketSnapshot &snapshot() const;
    bool snapshotIsStale() const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfSymbol(const QString &symbol) const;
    Q_INVOKABLE void clear();

    static QString boardForInstrument(const Instrument &instrument);

signals:
    void countChanged();

private:
    MarketSnapshot m_snapshot;
    bool m_stale = false;
};

} // namespace pp
