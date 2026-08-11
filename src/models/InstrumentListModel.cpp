#include "models/InstrumentListModel.h"

namespace pp {

InstrumentListModel::InstrumentListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int InstrumentListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_instruments.size();
}

QVariant InstrumentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_instruments.size())
        return {};

    const auto &inst = m_instruments.at(index.row());
    switch (role) {
    case SymbolRole:  return inst.symbol;
    case NameRole:    return inst.name;
    case MarketRole:  return marketName(inst.market);
    case VenueRole:   return inst.venue;
    default:          return {};
    }
}

QHash<int, QByteArray> InstrumentListModel::roleNames() const
{
    return {
        {SymbolRole, "symbol"},
        {NameRole,   "name"},
        {MarketRole, "market"},
        {VenueRole,  "venue"},
    };
}

void InstrumentListModel::addInstrument(const Instrument &inst)
{
    for (const auto &existing : m_instruments) {
        if (existing.symbol == inst.symbol)
            return;
    }

    const int row = m_instruments.size();
    beginInsertRows({}, row, row);
    m_instruments.append(inst);
    endInsertRows();
    emit instrumentAdded(inst.symbol);
}

void InstrumentListModel::setInstruments(const QVector<Instrument> &instruments)
{
    beginResetModel();
    m_instruments = instruments;
    endResetModel();
}

QVector<Instrument> InstrumentListModel::toVector() const
{
    return m_instruments;
}

Instrument InstrumentListModel::at(int index) const
{
    if (index < 0 || index >= m_instruments.size())
        return {};
    return m_instruments.at(index);
}

QString InstrumentListModel::symbolAt(int index) const
{
    if (index < 0 || index >= m_instruments.size())
        return {};
    return m_instruments.at(index).symbol;
}

} // namespace pp
