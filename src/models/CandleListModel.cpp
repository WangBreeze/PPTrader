#include "models/CandleListModel.h"

#include <QLocale>

#include <algorithm>

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
    case TimestampRole:
        return candle.startTime;
    case EndTimeRole:
        return candle.startTime.addSecs(std::max(1, candle.periodSeconds));
    case PeriodSecondsRole:
        return candle.periodSeconds;
    case SourceRole:
        return candle.source;
    case SourceCountRole:
        return 1;
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
        {TimestampRole, "timestamp"},
        {EndTimeRole, "endTime"},
        {PeriodSecondsRole, "periodSeconds"},
        {SourceRole, "source"},
        {SourceCountRole, "sourceCount"},
    };
}

int CandleListModel::count() const { return m_points.size(); }

QVariantMap CandleListModel::get(int row) const
{
    if (row < 0 || row >= m_points.size()) return {};
    const CandlePoint &point = m_points.at(row);
    const Candle &candle = point.candle;
    QString time;
    if (candle.periodSeconds >= 86400) time = candle.startTime.toString(QStringLiteral("MM-dd"));
    else if (candle.periodSeconds >= 3600) time = candle.startTime.toString(QStringLiteral("HH:mm"));
    else time = candle.startTime.toString(QStringLiteral("HH:mm:ss"));
    return {{QStringLiteral("symbol"), candle.instrument.symbol},
            {QStringLiteral("time"), time},
            {QStringLiteral("timestamp"), candle.startTime},
            {QStringLiteral("endTime"), candle.startTime.addSecs(std::max(1, candle.periodSeconds))},
            {QStringLiteral("periodSeconds"), candle.periodSeconds},
            {QStringLiteral("open"), candle.open},
            {QStringLiteral("high"), candle.high},
            {QStringLiteral("low"), candle.low},
            {QStringLiteral("close"), candle.close},
            {QStringLiteral("volume"), candle.volume},
            {QStringLiteral("source"), candle.source},
            {QStringLiteral("sourceCount"), 1},
            {QStringLiteral("hasSignal"), point.hasSignal},
            {QStringLiteral("signalSide"), point.hasSignal
                ? signalSideName(point.signal.side) : QString{}},
            {QStringLiteral("signalReason"), point.hasSignal
                ? point.signal.reason : QString{}},
            {QStringLiteral("signalPrice"), point.hasSignal
                ? point.signal.price : 0.0}};
}

void CandleListModel::addPoint(const CandlePoint &point)
{
    const int previousCount = m_points.size();
    const int row = m_points.size();
    beginInsertRows({}, row, row);
    m_points.push_back(point);
    endInsertRows();

    if (m_points.size() > m_maxRows) {
        beginRemoveRows({}, 0, 0);
        m_points.removeFirst();
        endRemoveRows();
    }
    if (previousCount != m_points.size()) emit countChanged();
}

void CandleListModel::loadCandles(const QVector<Candle> &candles)
{
    const int previousCount = m_points.size();
    beginResetModel();
    m_points.clear();
    for (const auto &c : candles) {
        CandlePoint pt;
        pt.candle = c;
        pt.hasSignal = false;
        m_points.append(pt);
    }
    endResetModel();
    if (previousCount != m_points.size()) emit countChanged();
}

void CandleListModel::loadBacktestResult(const BacktestResult &result)
{
    QHash<qint64, TradeSignal> signalsByTimestamp;
    for (const TradeSignal &signal : result.chartSignals) {
        if (signal.timestamp.isValid())
            signalsByTimestamp.insert(signal.timestamp.toMSecsSinceEpoch(), signal);
    }

    const int previousCount = m_points.size();
    const int candleCount = static_cast<int>(result.candles.size());
    const int firstIndex = std::max(0, candleCount - m_maxRows);
    beginResetModel();
    m_points.clear();
    m_points.reserve(candleCount - firstIndex);
    for (int index = firstIndex; index < candleCount; ++index) {
        CandlePoint point;
        point.candle = result.candles.at(index);
        const auto signal = signalsByTimestamp.constFind(
            point.candle.startTime.toMSecsSinceEpoch());
        if (signal != signalsByTimestamp.constEnd()) {
            point.hasSignal = true;
            point.signal = *signal;
        }
        m_points.append(point);
    }
    endResetModel();
    if (previousCount != m_points.size()) emit countChanged();
}

void CandleListModel::clear()
{
    if (m_points.isEmpty()) {
        return;
    }

    beginResetModel();
    m_points.clear();
    endResetModel();
    emit countChanged();
}

} // 命名空间 pp
