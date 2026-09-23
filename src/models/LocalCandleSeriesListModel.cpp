#include "models/LocalCandleSeriesListModel.h"

namespace pp {

namespace {

bool sameSeriesIdentity(const Candle &left, const Candle &right)
{
    return left.instrument.symbol == right.instrument.symbol
        && left.instrument.market == right.instrument.market
        && left.instrument.venue == right.instrument.venue
        && left.periodSeconds == right.periodSeconds
        && left.source == right.source
        && left.adjustment == right.adjustment;
}

bool samePoint(const LocalCandleSeriesPoint &left,
               const LocalCandleSeriesPoint &right)
{
    const Candle &a = left.candle;
    const Candle &b = right.candle;
    return sameSeriesIdentity(a, b)
        && a.startTime == b.startTime && left.endTime == right.endTime
        && a.open == b.open && a.high == b.high && a.low == b.low
        && a.close == b.close && a.volume == b.volume
        && a.buyVolume == b.buyVolume && a.sellVolume == b.sellVolume
        && a.neutralVolume == b.neutralVolume
        && a.directionalVolumeKnown == b.directionalVolumeKnown
        && a.source == b.source && left.sourceCount == right.sourceCount;
}

} // namespace

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
    bool compatible = points.size() >= m_points.size() && !m_points.isEmpty()
        && sameSeriesIdentity(m_points.first().candle, points.first().candle);
    int changedFirst = -1;
    int changedLast = -1;
    if (compatible) {
        for (int index = 0; index < m_points.size(); ++index) {
            if (m_points.at(index).candle.startTime != points.at(index).candle.startTime) {
                compatible = false;
                break;
            }
            if (!samePoint(m_points.at(index), points.at(index))) {
                if (changedFirst < 0) changedFirst = index;
                changedLast = index;
                m_points[index] = points.at(index);
            }
        }
    }
    if (compatible) {
        const int oldCount = m_points.size();
        if (changedFirst >= 0)
            emit dataChanged(this->index(changedFirst), this->index(changedLast));
        if (points.size() > oldCount) {
            beginInsertRows({}, oldCount, points.size() - 1);
            m_points.reserve(points.size());
            for (int index = oldCount; index < points.size(); ++index)
                m_points.append(points.at(index));
            endInsertRows();
            emit countChanged();
        }
        return;
    }
    beginResetModel();
    m_points = points;
    endResetModel();
    emit countChanged();
}

bool LocalCandleSeriesListModel::prependPoints(
    const QVector<LocalCandleSeriesPoint> &points)
{
    if (points.isEmpty()) return true;
    if (m_points.isEmpty()) {
        setPoints(points);
        return true;
    }
    if (!sameSeriesIdentity(points.first().candle, m_points.first().candle)
        || points.last().endTime >= m_points.first().candle.startTime) {
        return false;
    }
    QVector<LocalCandleSeriesPoint> combined;
    combined.reserve(points.size() + m_points.size());
    combined.append(points);
    combined.append(m_points);
    beginInsertRows({}, 0, points.size() - 1);
    m_points = std::move(combined);
    endInsertRows();
    emit countChanged();
    return true;
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
