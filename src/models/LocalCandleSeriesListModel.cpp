#include "models/LocalCandleSeriesListModel.h"

namespace pp {

LocalCandleSeriesListModel::LocalCandleSeriesListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LocalCandleSeriesListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_points.size();
}

QVariant LocalCandleSeriesListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) return {};
    const LocalCandleSeriesPoint &point = m_points.at(index.row());
    switch (role) {
    case TimestampRole: return point.candle.startTime;
    case EndTimeRole: return point.endTime;
    case OpenRole: return point.candle.open;
    case HighRole: return point.candle.high;
    case LowRole: return point.candle.low;
    case CloseRole: return point.candle.close;
    case VolumeRole: return point.candle.volume;
    case BuyVolumeRole: return point.candle.buyVolume;
    case SellVolumeRole: return point.candle.sellVolume;
    case NeutralVolumeRole: return point.candle.neutralVolume;
    case DirectionalVolumeKnownRole: return point.candle.directionalVolumeKnown;
    case SourceRole: return point.candle.source;
    case SourceCountRole: return point.sourceCount;
    default: return {};
    }
}

QHash<int, QByteArray> LocalCandleSeriesListModel::roleNames() const
{
    return {{TimestampRole, "timestamp"}, {EndTimeRole, "endTime"},
            {OpenRole, "open"}, {HighRole, "high"}, {LowRole, "low"},
            {CloseRole, "close"}, {VolumeRole, "volume"},
            {BuyVolumeRole, "buyVolume"}, {SellVolumeRole, "sellVolume"},
            {NeutralVolumeRole, "neutralVolume"},
            {DirectionalVolumeKnownRole, "directionalVolumeKnown"},
            {SourceRole, "source"}, {SourceCountRole, "sourceCount"}};
}

int LocalCandleSeriesListModel::count() const { return m_points.size(); }

const QVector<LocalCandleSeriesPoint> &LocalCandleSeriesListModel::points() const
{
    return m_points;
}

void LocalCandleSeriesListModel::setPoints(const QVector<LocalCandleSeriesPoint> &points)
{
    beginResetModel();
    m_points = points;
    endResetModel();
    emit countChanged();
}

QVariantMap LocalCandleSeriesListModel::get(int row) const
{
    return row >= 0 && row < m_points.size() ? pointMap(m_points.at(row)) : QVariantMap{};
}

void LocalCandleSeriesListModel::clear()
{
    if (m_points.isEmpty()) return;
    beginResetModel();
    m_points.clear();
    endResetModel();
    emit countChanged();
}

QVariantMap LocalCandleSeriesListModel::pointMap(const LocalCandleSeriesPoint &point) const
{
    return {{QStringLiteral("timestamp"), point.candle.startTime},
            {QStringLiteral("endTime"), point.endTime},
            {QStringLiteral("open"), point.candle.open},
            {QStringLiteral("high"), point.candle.high},
            {QStringLiteral("low"), point.candle.low},
            {QStringLiteral("close"), point.candle.close},
            {QStringLiteral("volume"), point.candle.volume},
            {QStringLiteral("buyVolume"), point.candle.buyVolume},
            {QStringLiteral("sellVolume"), point.candle.sellVolume},
            {QStringLiteral("neutralVolume"), point.candle.neutralVolume},
            {QStringLiteral("directionalVolumeKnown"), point.candle.directionalVolumeKnown},
            {QStringLiteral("source"), point.candle.source},
            {QStringLiteral("sourceCount"), point.sourceCount}};
}

} // namespace pp
