#include "models/SignalListModel.h"

#include <QLocale>

#include <algorithm>

namespace pp {

SignalListModel::SignalListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SignalListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_signals.size();
}

QVariant SignalListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_signals.size()) {
        return {};
    }

    const TradeSignal &signal = m_signals.at(index.row());
    switch (role) {
    case SymbolRole:
        return signal.instrument.symbol;
    case TimeRole:
        return signal.timestamp.toString(QStringLiteral("HH:mm:ss"));
    case SideRole:
        return signalSideName(signal.side);
    case PriceRole:
        return QLocale::c().toString(signal.price, 'f', 2);
    case VolumeRole:
        return QLocale::c().toString(signal.volume, 'f', 0);
    case ReasonRole:
        return signal.reason;
    default:
        return {};
    }
}

QHash<int, QByteArray> SignalListModel::roleNames() const
{
    return {
        {SymbolRole, "symbol"},
        {TimeRole, "time"},
        {SideRole, "side"},
        {PriceRole, "price"},
        {VolumeRole, "volume"},
        {ReasonRole, "reason"},
    };
}

void SignalListModel::addSignal(const TradeSignal &signal)
{
    beginInsertRows({}, 0, 0);
    m_signals.prepend(signal);
    endInsertRows();

    if (m_signals.size() > m_maxRows) {
        beginRemoveRows({}, m_maxRows, m_signals.size() - 1);
        m_signals.erase(m_signals.begin() + m_maxRows, m_signals.end());
        endRemoveRows();
    }
}

void SignalListModel::loadSignals(const QVector<TradeSignal> &values)
{
    beginResetModel();
    m_signals.clear();
    const int signalCount = static_cast<int>(values.size());
    const int firstIndex = std::max(0, signalCount - m_maxRows);
    for (int index = signalCount - 1; index >= firstIndex; --index)
        m_signals.append(values.at(index));
    endResetModel();
}

void SignalListModel::clear()
{
    if (m_signals.isEmpty()) {
        return;
    }

    beginResetModel();
    m_signals.clear();
    endResetModel();
}

} // 命名空间 pp
