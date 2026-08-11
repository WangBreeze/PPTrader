#pragma once

#include "domain/MarketTypes.h"

#include <QAbstractListModel>
#include <QVector>

namespace pp {

class InstrumentListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        NameRole,
        MarketRole,
        VenueRole
    };

    explicit InstrumentListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addInstrument(const Instrument &inst);
    void setInstruments(const QVector<Instrument> &instruments);
    QVector<Instrument> toVector() const;
    Instrument at(int index) const;
    Q_INVOKABLE QString symbolAt(int index) const;

signals:
    void instrumentAdded(const QString &symbol);

private:
    QVector<Instrument> m_instruments;
};

} // namespace pp
