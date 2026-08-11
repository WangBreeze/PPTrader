#include "models/CandleListModel.h"

#include <QLocale>

namespace pp {

CandleListModel::CandleListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CandleListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_points.size();
}

QVariant CandleListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) {
        return {};
    }

    const CandlePoint &point = m_points.at(index.row());
    const Candle &candle = point.candle;
    switch (role) {
    case SymbolRole:
        return candle.instrument.symbol;
    case TimeRole:
        if (candle.periodSeconds >= 86400)
            return candle.startTime.toString(QStringLiteral("MM-dd"));
        if (candle.periodSeconds >= 3600)
            return candle.startTime.toString(QStringLiteral("HH:mm"));
        return candle.startTime.toString(QStringLiteral("HH:mm:ss"));
    case OpenRole:
        return candle.open;
    case HighRole:
        return candle.high;
    case LowRole:
        return candle.low;
    case CloseRole:
        return candle.close;
    case VolumeRole:
        return QLocale::c().toString(candle.volume, 'f', 0);
    case HasSignalRole:
        return point.hasSignal;
    case SignalSideRole:
        return point.hasSignal ? signalSideName(point.signal.side) : QString();
    case SignalReasonRole:
        return point.hasSignal ? point.signal.reason : QString();
    case SignalPriceRole:
        return point.hasSignal ? QLocale::c().toString(point.signal.price, 'f', 2) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> CandleListModel::roleNames() const
{
    return {
        {SymbolRole, "symbol"},
        {TimeRole, "time"},
        {OpenRole, "open"},
        {HighRole, "high"},
        {LowRole, "low"},
        {CloseRole, "close"},
        {VolumeRole, "volume"},
        {HasSignalRole, "hasSignal"},
        {SignalSideRole, "signalSide"},
        {SignalReasonRole, "signalReason"},
        {SignalPriceRole, "signalPrice"},
    };
}

void CandleListModel::addPoint(const CandlePoint &point)
{
    const int row = m_points.size();
    beginInsertRows({}, row, row);
    m_points.push_back(point);
    endInsertRows();

    if (m_points.size() > m_maxRows) {
        beginRemoveRows({}, 0, 0);
        m_points.removeFirst();
        endRemoveRows();
    }
}

void CandleListModel::loadCandles(const QVector<Candle> &candles)
{
    beginResetModel();
    m_points.clear();
    for (const auto &c : candles) {
        CandlePoint pt;
        pt.candle = c;
        pt.hasSignal = false;
        m_points.append(pt);
    }
    endResetModel();
}

void CandleListModel::clear()
{
    if (m_points.isEmpty()) {
        return;
    }

    beginResetModel();
    m_points.clear();
    endResetModel();
}

} // 命名空间 pp
