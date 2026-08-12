#include "models/PortfolioArchiveListModel.h"

namespace pp {

namespace {

QVariantMap summaryMap(const PortfolioArchiveSummary &item)
{
    return item.toVariantMap();
}

} // namespace

PortfolioArchiveListModel::PortfolioArchiveListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PortfolioArchiveListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_summaries.size();
}

int PortfolioArchiveListModel::count() const { return m_summaries.size(); }

QVariant PortfolioArchiveListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_summaries.size()) return {};
    const PortfolioArchiveSummary &item = m_summaries.at(index.row());
    switch (role) {
    case ResultIdRole: return item.resultId;
    case PortfolioIdRole: return item.portfolioId;
    case NameRole: return item.name;
    case CompletedAtRole: return item.completedAt;
    case LegCountRole: return item.legCount;
    case PeriodSecondsRole: return item.periodSeconds;
    case BarCountRole: return item.barCount;
    case SymbolsRole: return item.symbols;
    case DatasetVersionsRole: return item.datasetVersions;
    case InitialCapitalRole: return item.summary.initialCapital;
    case FinalEquityRole: return item.summary.finalEquity;
    case TotalReturnPercentRole: return item.summary.totalReturnPercent;
    case MaxDrawdownPercentRole: return item.summary.maxDrawdownPercent;
    case SharpeRatioRole: return item.summary.sharpeRatio;
    case TotalCommissionRole: return item.summary.totalCommission;
    case TotalTradesRole: return item.summary.totalTrades;
    default: return {};
    }
}

QHash<int, QByteArray> PortfolioArchiveListModel::roleNames() const
{
    return {{ResultIdRole, "resultId"}, {PortfolioIdRole, "portfolioId"},
            {NameRole, "name"}, {CompletedAtRole, "completedAt"},
            {LegCountRole, "legCount"}, {PeriodSecondsRole, "periodSeconds"},
            {BarCountRole, "barCount"}, {SymbolsRole, "symbols"},
            {DatasetVersionsRole, "datasetVersions"},
            {InitialCapitalRole, "initialCapital"},
            {FinalEquityRole, "finalEquity"},
            {TotalReturnPercentRole, "totalReturnPercent"},
            {MaxDrawdownPercentRole, "maxDrawdownPercent"},
            {SharpeRatioRole, "sharpeRatio"},
            {TotalCommissionRole, "totalCommission"},
            {TotalTradesRole, "totalTrades"}};
}

QVariantMap PortfolioArchiveListModel::get(int row) const
{
    return row >= 0 && row < m_summaries.size()
        ? summaryMap(m_summaries.at(row)) : QVariantMap{};
}

int PortfolioArchiveListModel::indexOfResultId(const QString &resultId) const
{
    for (int row = 0; row < m_summaries.size(); ++row) {
        if (m_summaries.at(row).resultId == resultId) return row;
    }
    return -1;
}

void PortfolioArchiveListModel::loadSummaries(
    const QVector<PortfolioArchiveSummary> &summaries)
{
    const bool countDidChange = m_summaries.size() != summaries.size();
    beginResetModel();
    m_summaries = summaries;
    endResetModel();
    if (countDidChange) emit countChanged();
}

void PortfolioArchiveListModel::clear()
{
    if (m_summaries.isEmpty()) return;
    beginResetModel();
    m_summaries.clear();
    endResetModel();
    emit countChanged();
}

} // namespace pp
