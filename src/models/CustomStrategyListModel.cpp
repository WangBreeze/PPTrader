#include "models/CustomStrategyListModel.h"

#include <algorithm>

namespace pp {

CustomStrategyListModel::CustomStrategyListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CustomStrategyListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_definitions.size();
}

int CustomStrategyListModel::count() const
{
    return m_definitions.size();
}

QVariant CustomStrategyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_definitions.size()) return {};
    const CustomStrategyDefinition &item = m_definitions.at(index.row());
    switch (role) {
    case StrategyIdRole: return item.strategyId;
    case VersionRole: return item.version;
    case NameRole: return item.name;
    case DescriptionRole: return item.description;
    case AuthorRole: return item.author;
    case MarketRole: return item.market;
    case ExecutionScopeRole: return item.executionScope;
    case CreatedAtRole: return item.createdAt;
    case UpdatedAtRole: return item.updatedAt;
    case LatestRole: return isLatest(index.row());
    case StageRole: return QStringLiteral("backtest_ready");
    case ExecutableRole: return true;
    default: return {};
    }
}

QHash<int, QByteArray> CustomStrategyListModel::roleNames() const
{
    return {{StrategyIdRole, "strategyId"}, {VersionRole, "version"},
            {NameRole, "name"}, {DescriptionRole, "description"},
            {AuthorRole, "author"}, {MarketRole, "market"},
            {ExecutionScopeRole, "executionScope"}, {CreatedAtRole, "createdAt"},
            {UpdatedAtRole, "updatedAt"}, {LatestRole, "latest"},
            {StageRole, "stage"}, {ExecutableRole, "executable"}};
}

QVariantMap CustomStrategyListModel::get(int row) const
{
    if (row < 0 || row >= m_definitions.size()) return {};
    QVariantMap result = m_definitions.at(row).toVariantMap();
    result.insert(QStringLiteral("latest"), isLatest(row));
    return result;
}

int CustomStrategyListModel::indexOf(const QString &strategyId, int version) const
{
    for (int row = 0; row < m_definitions.size(); ++row) {
        const CustomStrategyDefinition &item = m_definitions.at(row);
        if (item.strategyId == strategyId && (version <= 0 || item.version == version)) return row;
    }
    return -1;
}

void CustomStrategyListModel::loadDefinitions(
    const QVector<CustomStrategyDefinition> &definitions)
{
    QVector<CustomStrategyDefinition> sorted = definitions;
    std::sort(sorted.begin(), sorted.end(), [](const auto &left, const auto &right) {
        if (left.strategyId != right.strategyId) return left.strategyId < right.strategyId;
        return left.version > right.version;
    });
    const bool countDidChange = sorted.size() != m_definitions.size();
    beginResetModel();
    m_definitions = std::move(sorted);
    endResetModel();
    if (countDidChange) emit countChanged();
}

bool CustomStrategyListModel::isLatest(int row) const
{
    if (row < 0 || row >= m_definitions.size()) return false;
    const QString &id = m_definitions.at(row).strategyId;
    return row == 0 || m_definitions.at(row - 1).strategyId != id;
}

} // namespace pp
