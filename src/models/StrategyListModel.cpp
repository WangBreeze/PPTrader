#include "models/StrategyListModel.h"

namespace pp {

StrategyListModel::StrategyListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StrategyListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_rows.size();
}

QVariant StrategyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }

    const StrategyRow &row = m_rows.at(index.row());
    switch (role) {
    case IdRole:
        return row.id;
    case SymbolRole:
        return row.symbol;
    case NameRole:
        return row.name;
    case DescriptionRole:
        return row.description;
    case ThresholdPercentRole:
        return row.thresholdPercent;
    case CooldownSecondsRole:
        return row.cooldownSeconds;
    case EnabledRole:
        return row.enabled;
    default:
        return {};
    }
}

bool StrategyListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return false;
    }

    StrategyRow &row = m_rows[index.row()];

    if (role == EnabledRole) {
        const bool enabled = value.toBool();
        if (row.enabled == enabled) {
            return true;
        }

        row.enabled = enabled;
        emit dataChanged(index, index, {EnabledRole});
        return true;
    }

    if (role == ThresholdPercentRole) {
        const double thresholdPercent = value.toDouble();
        if (qFuzzyCompare(row.thresholdPercent, thresholdPercent)) {
            return true;
        }

        row.thresholdPercent = thresholdPercent;
        row.description = QStringLiteral("阈值 %1%，冷却 %2 秒").arg(row.thresholdPercent, 0, 'f', 2).arg(row.cooldownSeconds);
        emit dataChanged(index, index, {ThresholdPercentRole, DescriptionRole});
        return true;
    }

    if (role == CooldownSecondsRole) {
        const int cooldownSeconds = value.toInt();
        if (row.cooldownSeconds == cooldownSeconds) {
            return true;
        }

        row.cooldownSeconds = cooldownSeconds;
        row.description = QStringLiteral("阈值 %1%，冷却 %2 秒").arg(row.thresholdPercent, 0, 'f', 2).arg(row.cooldownSeconds);
        emit dataChanged(index, index, {CooldownSecondsRole, DescriptionRole});
        return true;
    }

    return false;
}

Qt::ItemFlags StrategyListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> StrategyListModel::roleNames() const
{
    return {
        {IdRole, "strategyId"},
        {SymbolRole, "symbol"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {ThresholdPercentRole, "thresholdPercent"},
        {CooldownSecondsRole, "cooldownSeconds"},
        {EnabledRole, "enabled"},
    };
}

void StrategyListModel::clear()
{
    if (m_rows.isEmpty()) {
        return;
    }

    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void StrategyListModel::addStrategy(const StrategyRow &row)
{
    const int nextRow = m_rows.size();
    beginInsertRows({}, nextRow, nextRow);
    m_rows.push_back(row);
    endInsertRows();
}

bool StrategyListModel::setStrategyEnabled(const QString &id, bool enabled)
{
    for (int rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
        if (m_rows.at(rowIndex).id == id) {
            return setData(index(rowIndex), enabled, EnabledRole);
        }
    }

    return false;
}

bool StrategyListModel::setStrategyParameters(const QString &id, double thresholdPercent, int cooldownSeconds)
{
    for (int rowIndex = 0; rowIndex < m_rows.size(); ++rowIndex) {
        if (m_rows.at(rowIndex).id == id) {
            StrategyRow &row = m_rows[rowIndex];
            row.thresholdPercent = thresholdPercent;
            row.cooldownSeconds = cooldownSeconds;
            row.description = QStringLiteral("阈值 %1%，冷却 %2 秒").arg(row.thresholdPercent, 0, 'f', 2).arg(row.cooldownSeconds);
            const QModelIndex changedIndex = index(rowIndex);
            emit dataChanged(changedIndex, changedIndex, {ThresholdPercentRole, CooldownSecondsRole, DescriptionRole});
            return true;
        }
    }

    return false;
}

} // 命名空间 pp
