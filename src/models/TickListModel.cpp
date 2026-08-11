#include "models/TickListModel.h"

#include <QLocale>

#include <algorithm>

namespace pp {

TickListModel::TickListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TickListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_ticks.size();
}

QVariant TickListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ticks.size()) {
        return {};
    }

    const Tick &tick = m_ticks.at(index.row());
    switch (role) {
    case SymbolRole:
        return tick.instrument.symbol;
    case MarketRole:
        return marketName(tick.instrument.market);
    case TimeRole:
        return tick.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    case PriceRole:
        return QLocale::c().toString(tick.price, 'f', 2);
    case VolumeRole:
        return QLocale::c().toString(tick.volume, 'f', 0);
    case BidRole:
        return QLocale::c().toString(tick.bid, 'f', 2);
    case AskRole:
        return QLocale::c().toString(tick.ask, 'f', 2);
    default:
        return {};
    }
}

QHash<int, QByteArray> TickListModel::roleNames() const
{
    return {
        {SymbolRole, "symbol"},
        {MarketRole, "market"},
        {TimeRole, "time"},
        {PriceRole, "price"},
        {VolumeRole, "volume"},
        {BidRole, "bid"},
        {AskRole, "ask"},
    };
}

void TickListModel::addTick(const Tick &tick)
{
    beginInsertRows({}, 0, 0);
    m_ticks.prepend(tick);
    endInsertRows();

    if (m_ticks.size() > m_maxRows) {
        beginRemoveRows({}, m_maxRows, m_ticks.size() - 1);
        m_ticks.erase(m_ticks.begin() + m_maxRows, m_ticks.end());
        endRemoveRows();
    }
}

void TickListModel::loadTicks(const QVector<Tick> &ticks)
{
    beginResetModel();
    const int count = std::min(m_maxRows, static_cast<int>(ticks.size()));
    m_ticks = ticks.mid(0, count);
    endResetModel();
}

void TickListModel::clear()
{
    if (m_ticks.isEmpty()) {
        return;
    }

    beginResetModel();
    m_ticks.clear();
    endResetModel();
}

} // 命名空间 pp
