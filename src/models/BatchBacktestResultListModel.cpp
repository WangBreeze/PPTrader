#include "models/BatchBacktestResultListModel.h"

#include <algorithm>

namespace pp {

BatchBacktestResultListModel::BatchBacktestResultListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BatchBacktestResultListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

int BatchBacktestResultListModel::count() const { return m_results.size(); }

QVariant BatchBacktestResultListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) return {};
    const BatchBacktestItemResult &item = m_results.at(index.row());
    switch (role) {
    case SymbolRole: return item.instrument.symbol;
    case NameRole: return item.instrument.name;
    case FrequencyRole: return item.frequency;
    case RequestedBarsRole: return item.requestedBars;
    case ActualBarsRole: return item.actualBars;
    case SucceededRole: return item.succeeded;
    case ErrorRole: return item.error;
    case DataVersionRole: return item.dataVersion;
    case FinalEquityRole: return item.summary.finalEquity;
    case TotalReturnPercentRole: return item.summary.totalReturnPercent;
    case MaxDrawdownPercentRole: return item.summary.maxDrawdownPercent;
    case WinRatePercentRole: return item.summary.winRatePercent;
    case SharpeRatioRole: return item.summary.sharpeRatio;
    case TotalCommissionRole: return item.summary.totalCommission;
    case TotalTradesRole: return item.summary.totalTrades;
    case ElapsedMillisecondsRole: return item.elapsedMilliseconds;
    default: return {};
    }
}

QHash<int, QByteArray> BatchBacktestResultListModel::roleNames() const
{
    return {{SymbolRole, "symbol"}, {NameRole, "name"},
            {FrequencyRole, "frequency"}, {RequestedBarsRole, "requestedBars"},
            {ActualBarsRole, "actualBars"}, {SucceededRole, "succeeded"},
            {ErrorRole, "error"}, {DataVersionRole, "dataVersion"},
            {FinalEquityRole, "finalEquity"},
            {TotalReturnPercentRole, "totalReturnPercent"},
            {MaxDrawdownPercentRole, "maxDrawdownPercent"},
            {WinRatePercentRole, "winRatePercent"}, {SharpeRatioRole, "sharpeRatio"},
            {TotalCommissionRole, "totalCommission"}, {TotalTradesRole, "totalTrades"},
            {ElapsedMillisecondsRole, "elapsedMilliseconds"}};
}

QVariantMap BatchBacktestResultListModel::get(int row) const
{
    return row >= 0 && row < m_results.size() ? m_results.at(row).toVariantMap()
                                                : QVariantMap{};
}

int BatchBacktestResultListModel::indexOfSymbol(const QString &symbol) const
{
    for (int row = 0; row < m_results.size(); ++row) {
        if (m_results.at(row).instrument.symbol == symbol) return row;
    }
    return -1;
}

void BatchBacktestResultListModel::clear()
{
    if (m_results.isEmpty()) return;
    beginResetModel();
    m_results.clear();
    endResetModel();
    emit countChanged();
}

void BatchBacktestResultListModel::addResult(const BatchBacktestItemResult &result)
{
    const auto position = std::lower_bound(
        m_results.cbegin(), m_results.cend(), result.instrument.symbol,
        [](const BatchBacktestItemResult &item, const QString &symbol) {
            return item.instrument.symbol < symbol;
        });
    const int row = static_cast<int>(position - m_results.cbegin());
    beginInsertRows(QModelIndex(), row, row);
    m_results.insert(row, result);
    endInsertRows();
    emit countChanged();
}

} // namespace pp
