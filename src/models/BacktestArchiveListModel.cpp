#include "models/BacktestArchiveListModel.h"

namespace pp {

namespace {

QVariantMap summaryMap(const BacktestArchiveSummary &item)
{
    return {{QStringLiteral("resultId"), item.resultId},
            {QStringLiteral("taskId"), item.taskId},
            {QStringLiteral("completedAt"), item.completedAt},
            {QStringLiteral("strategyId"), item.strategyId},
            {QStringLiteral("strategyVersion"), item.strategyVersion},
            {QStringLiteral("symbol"), item.instrument.symbol},
            {QStringLiteral("name"), item.instrument.name},
            {QStringLiteral("frequency"), item.frequency},
            {QStringLiteral("requestedBars"), item.requestedBars},
            {QStringLiteral("actualBars"), item.actualBars},
            {QStringLiteral("dataVersion"), item.dataVersion},
            {QStringLiteral("datasetVersion"), item.datasetVersion},
            {QStringLiteral("datasetFirstDate"), item.datasetFirstDate},
            {QStringLiteral("datasetLastDate"), item.datasetLastDate},
            {QStringLiteral("datasetDayCount"), item.datasetDayCount},
            {QStringLiteral("datasetRawTickCount"), item.datasetRawTickCount},
            {QStringLiteral("datasetGapDayCount"), item.datasetGapDayCount},
            {QStringLiteral("datasetStaleDayCount"), item.datasetStaleDayCount},
            {QStringLiteral("datasetCalendarSource"), item.datasetCalendarSource},
            {QStringLiteral("datasetCalendarCoverageKnown"),
             item.datasetCalendarCoverageKnown},
            {QStringLiteral("totalReturnPercent"), item.summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), item.summary.maxDrawdownPercent},
            {QStringLiteral("totalTrades"), item.summary.totalTrades},
            {QStringLiteral("customStrategy"), item.strategyId.startsWith(QStringLiteral("custom_"))}};
}

} // namespace

BacktestArchiveListModel::BacktestArchiveListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BacktestArchiveListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_summaries.size();
}

int BacktestArchiveListModel::count() const
{
    return m_summaries.size();
}

QVariant BacktestArchiveListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_summaries.size()) return {};
    const BacktestArchiveSummary &item = m_summaries.at(index.row());
    switch (role) {
    case ResultIdRole: return item.resultId;
    case TaskIdRole: return item.taskId;
    case CompletedAtRole: return item.completedAt;
    case StrategyIdRole: return item.strategyId;
    case StrategyVersionRole: return item.strategyVersion;
    case SymbolRole: return item.instrument.symbol;
    case NameRole: return item.instrument.name;
    case FrequencyRole: return item.frequency;
    case RequestedBarsRole: return item.requestedBars;
    case ActualBarsRole: return item.actualBars;
    case DataVersionRole: return item.dataVersion;
    case DatasetVersionRole: return item.datasetVersion;
    case DatasetFirstDateRole: return item.datasetFirstDate;
    case DatasetLastDateRole: return item.datasetLastDate;
    case DatasetDayCountRole: return item.datasetDayCount;
    case DatasetRawTickCountRole: return item.datasetRawTickCount;
    case DatasetGapDayCountRole: return item.datasetGapDayCount;
    case DatasetStaleDayCountRole: return item.datasetStaleDayCount;
    case DatasetCalendarSourceRole: return item.datasetCalendarSource;
    case DatasetCalendarCoverageKnownRole: return item.datasetCalendarCoverageKnown;
    case TotalReturnPercentRole: return item.summary.totalReturnPercent;
    case MaxDrawdownPercentRole: return item.summary.maxDrawdownPercent;
    case TotalTradesRole: return item.summary.totalTrades;
    case CustomStrategyRole: return item.strategyId.startsWith(QStringLiteral("custom_"));
    default: return {};
    }
}

QHash<int, QByteArray> BacktestArchiveListModel::roleNames() const
{
    return {{ResultIdRole, "resultId"}, {TaskIdRole, "taskId"},
            {CompletedAtRole, "completedAt"}, {StrategyIdRole, "strategyId"},
            {StrategyVersionRole, "strategyVersion"}, {SymbolRole, "symbol"},
            {NameRole, "name"}, {FrequencyRole, "frequency"},
            {RequestedBarsRole, "requestedBars"}, {ActualBarsRole, "actualBars"},
            {DataVersionRole, "dataVersion"},
            {DatasetVersionRole, "datasetVersion"},
            {DatasetFirstDateRole, "datasetFirstDate"},
            {DatasetLastDateRole, "datasetLastDate"},
            {DatasetDayCountRole, "datasetDayCount"},
            {DatasetRawTickCountRole, "datasetRawTickCount"},
            {DatasetGapDayCountRole, "datasetGapDayCount"},
            {DatasetStaleDayCountRole, "datasetStaleDayCount"},
            {DatasetCalendarSourceRole, "datasetCalendarSource"},
            {DatasetCalendarCoverageKnownRole, "datasetCalendarCoverageKnown"},
            {TotalReturnPercentRole, "totalReturnPercent"},
            {MaxDrawdownPercentRole, "maxDrawdownPercent"},
            {TotalTradesRole, "totalTrades"}, {CustomStrategyRole, "customStrategy"}};
}

QVariantMap BacktestArchiveListModel::get(int row) const
{
    return row >= 0 && row < m_summaries.size() ? summaryMap(m_summaries.at(row)) : QVariantMap{};
}

int BacktestArchiveListModel::indexOfResultId(const QString &resultId) const
{
    for (int row = 0; row < m_summaries.size(); ++row) {
        if (m_summaries.at(row).resultId == resultId) return row;
    }
    return -1;
}

void BacktestArchiveListModel::loadSummaries(const QVector<BacktestArchiveSummary> &summaries)
{
    const bool countDidChange = m_summaries.size() != summaries.size();
    beginResetModel();
    m_summaries = summaries;
    endResetModel();
    if (countDidChange) emit countChanged();
}

} // namespace pp
