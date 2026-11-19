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
const QVector<CandlePoint> &CandleListModel::points() const { return m_points; }

void CandleListModel::setMaximumRows(int maximumRows)
{
    m_maxRows = std::max(1, maximumRows);
    if (m_points.size() <= m_maxRows) return;
    const int removed = m_points.size() - m_maxRows;
    beginRemoveRows({}, 0, removed - 1);
    m_points.remove(0, removed);
    endRemoveRows();
    emit countChanged();
}

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

void CandleListModel::loadPoints(const QVector<CandlePoint> &points)
{
    const int previousCount = m_points.size();
    beginResetModel();
    const int first = std::max(0, int(points.size()) - m_maxRows);
    m_points = points.mid(first);
    endResetModel();
    if (previousCount != m_points.size()) emit countChanged();
}

QVector<CandlePoint> CandleListModel::backtestPoints(const BacktestResult &result)
{
    QHash<qint64, TradeSignal> fillsByTimestamp;
    const auto addFill = [&fillsByTimestamp, &result](
        const QDateTime &timestamp, SignalSide side, double price,
        double quantity, double commission, const QString &reason) {
        if (!timestamp.isValid() || price <= 0.0 || quantity <= 0.0) return;
        TradeSignal fill;
        fill.instrument = result.config.instrument;
        fill.timestamp = timestamp;
        fill.side = side;
        fill.price = price;
        fill.volume = quantity;
        fill.reason = QStringLiteral("实际%1 · %2 股 · 手续费 %3 · %4")
            .arg(side == SignalSide::Buy ? QStringLiteral("入场")
                                         : QStringLiteral("出场"))
            .arg(quantity, 0, 'f', 0).arg(commission, 0, 'f', 2).arg(reason);
        fillsByTimestamp.insert(timestamp.toMSecsSinceEpoch(), fill);
    };
    for (const BacktestTrade &trade : result.trades) {
        const double entryCommission = trade.entryPrice * trade.quantity
            * std::max(0.0, result.config.commissionRate);
        addFill(trade.entryTime, SignalSide::Buy, trade.entryPrice,
                trade.quantity, entryCommission, trade.entryReason);
        addFill(trade.exitTime, SignalSide::Sell, trade.exitPrice,
                trade.quantity, trade.commission - entryCommission,
                trade.exitReason);
    }
    if (result.finalPosition.quantity > 0.0) {
        addFill(result.finalPosition.entryTime, SignalSide::Buy,
                result.finalPosition.averagePrice, result.finalPosition.quantity,
                result.finalPosition.entryCommission,
                result.finalPosition.entryReason + QStringLiteral(" · 持仓中"));
    }

    QVector<CandlePoint> points;
    points.reserve(result.candles.size());
    for (const Candle &candle : result.candles) {
        CandlePoint point;
        point.candle = candle;
        const auto signal = fillsByTimestamp.constFind(
            point.candle.startTime.toMSecsSinceEpoch());
        if (signal != fillsByTimestamp.constEnd()) {
            point.hasSignal = true;
            point.signal = *signal;
        }
        points.append(point);
    }
    return points;
}

void CandleListModel::loadBacktestResult(const BacktestResult &result)
{
    loadPoints(backtestPoints(result));
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
