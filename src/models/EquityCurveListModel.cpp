#include "models/EquityCurveListModel.h"

namespace pp {

EquityCurveListModel::EquityCurveListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EquityCurveListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_points.size();
}

int EquityCurveListModel::count() const
{
    return m_points.size();
}

QVariant EquityCurveListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size()) {
        return {};
    }

    const EquityPoint &point = m_points.at(index.row());
    switch (role) {
    case TimestampRole: return point.timestamp;
    case EquityRole: return point.equity;
    case CashRole: return point.cash;
    case PositionValueRole: return point.positionValue;
    case DrawdownPercentRole: return point.drawdownPercent;
    default: return {};
    }
}

QHash<int, QByteArray> EquityCurveListModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {EquityRole, "equity"},
        {CashRole, "cash"},
        {PositionValueRole, "positionValue"},
        {DrawdownPercentRole, "drawdownPercent"},
    };
}

QVariantMap EquityCurveListModel::get(int row) const
{
    if (row < 0 || row >= m_points.size()) return {};
    const EquityPoint &point = m_points.at(row);
    return {{QStringLiteral("timestamp"), point.timestamp},
            {QStringLiteral("equity"), point.equity},
            {QStringLiteral("cash"), point.cash},
            {QStringLiteral("positionValue"), point.positionValue},
            {QStringLiteral("drawdownPercent"), point.drawdownPercent}};
}

void EquityCurveListModel::addPoint(const EquityPoint &point)
{
    const int row = m_points.size();
    beginInsertRows({}, row, row);
    m_points.append(point);
    endInsertRows();
    emit countChanged();
}

void EquityCurveListModel::loadPoints(const QVector<EquityPoint> &points)
{
    const bool countDidChange = m_points.size() != points.size();
    beginResetModel();
    m_points = points;
    endResetModel();
    if (countDidChange) emit countChanged();
}

void EquityCurveListModel::clear()
{
    if (m_points.isEmpty()) {
        return;
    }
    beginResetModel();
    m_points.clear();
    endResetModel();
    emit countChanged();
}

} // namespace pp
