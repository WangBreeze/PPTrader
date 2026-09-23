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
    case DataModeRole: return backtestDataModeCode(item.dataMode);
    case StartDateRole: return item.startDate;
    case EndDateRole: return item.endDate;
    case FrequencyRole: return item.frequency;
    case RequestedBarsRole: return item.requestedBars;
    case ActualBarsRole: return item.actualBars;
    case SucceededRole: return item.succeeded;
    case ErrorRole: return item.error;
    case DataVersionRole: return item.dataVersion;
    case DatasetVersionRole: return item.datasetVersion;
    case DatasetDayCountRole: return item.datasetDayCount;
    case DatasetRawTickCountRole: return item.datasetRawTickCount;
    case DatasetGapDayCountRole: return item.datasetGapDayCount;
    case DatasetStaleDayCountRole: return item.datasetStaleDayCount;
    case DataQualityStatusRole: return item.dataQualityStatus;
    case DataQualityEligibleRole: return item.dataQualityEligible;
    case DataQualityAssessmentRole: return item.dataQualityAssessment;
    case ArchivedRole: return item.archived;
    case ArchivableRole: return item.archivable;
    case ArchivedResultIdRole: return item.archivedResultId;
    case FinalEquityRole: return item.summary.finalEquity;
    case TotalReturnPercentRole: return item.summary.totalReturnPercent;
    case MaxDrawdownPercentRole: return item.summary.maxDrawdownPercent;
    case WinRatePercentRole: return item.summary.winRatePercent;
    case SharpeRatioRole: return item.summary.sharpeRatio;
    case TotalCommissionRole: return item.summary.totalCommission;
    case TotalTradesRole: return item.summary.totalTrades;
    case HasOpenPositionRole: return item.finalPosition.quantity > 0.0;
    case OpenPositionQuantityRole: return item.finalPosition.quantity;
    case OpenPositionAveragePriceRole: return item.finalPosition.averagePrice;
    case OpenPositionEntryTimeRole: return item.finalPosition.entryTime;
    case ElapsedMillisecondsRole: return item.elapsedMilliseconds;
    case HoldoutEnabledRole: return item.holdoutEnabled;
    case HoldoutTradingDaysRole: return item.holdoutTradingDays;
    case TrainingStartDateRole: return item.trainingStartDate;
    case TrainingEndDateRole: return item.trainingEndDate;
    case HoldoutStartDateRole: return item.holdoutStartDate;
    case HoldoutEndDateRole: return item.holdoutEndDate;
    case TrainingActualTicksRole: return item.trainingActualTicks;
    case HoldoutActualTicksRole: return item.holdoutActualTicks;
    case TrainingDataVersionRole: return item.trainingDataVersion;
    case HoldoutDataVersionRole: return item.holdoutDataVersion;
    case TrainingDatasetVersionRole: return item.trainingDatasetVersion;
    case HoldoutDatasetVersionRole: return item.holdoutDatasetVersion;
    case TrainingTotalReturnPercentRole:
        return item.trainingSummary.totalReturnPercent;
    case TrainingMaxDrawdownPercentRole:
        return item.trainingSummary.maxDrawdownPercent;
    case TrainingTotalTradesRole: return item.trainingSummary.totalTrades;
    case HoldoutTotalReturnPercentRole:
        return item.holdoutSummary.totalReturnPercent;
    case HoldoutMaxDrawdownPercentRole:
        return item.holdoutSummary.maxDrawdownPercent;
    case HoldoutTotalTradesRole: return item.holdoutSummary.totalTrades;
    case WalkForwardEnabledRole: return item.walkForwardEnabled;
    case WalkForwardFoldCountRole: return item.walkForwardFoldCount;
    case ResearchOnlyRole: return item.researchOnly;
    case WalkForwardFoldsRole: {
        QVariantList folds;
        folds.reserve(item.walkForwardFolds.size());
        for (const RawTickWalkForwardFoldResult &fold : item.walkForwardFolds) {
            folds.append(fold.toVariantMap());
        }
        return folds;
    }
    case WalkForwardMeanReturnPercentRole:
        return item.walkForwardMeanReturnPercent;
    case WalkForwardMedianReturnPercentRole:
        return item.walkForwardMedianReturnPercent;
    case WalkForwardWorstReturnPercentRole:
        return item.walkForwardWorstReturnPercent;
    case WalkForwardBestReturnPercentRole:
        return item.walkForwardBestReturnPercent;
    case SegmentedResearchRole: return item.segmentedResearch;
    case ResearchSegmentsRole: {
        QVariantList segments;
        segments.reserve(item.researchSegments.size());
        for (const RawTickResearchSegmentResult &segment : item.researchSegments) {
            segments.append(segment.toVariantMap());
        }
        return segments;
    }
    case ResearchSegmentCountRole: return item.researchSegments.size();
    case SegmentedMeanReturnPercentRole:
        return item.segmentedMeanReturnPercent;
    case SegmentedMedianReturnPercentRole:
        return item.segmentedMedianReturnPercent;
    case SegmentedWorstReturnPercentRole:
        return item.segmentedWorstReturnPercent;
    case SegmentedBestReturnPercentRole:
        return item.segmentedBestReturnPercent;
    case SegmentedWorstDrawdownPercentRole:
        return item.segmentedWorstDrawdownPercent;
    default: return {};
    }
}

QHash<int, QByteArray> BatchBacktestResultListModel::roleNames() const
{
    return {{SymbolRole, "symbol"}, {NameRole, "name"},
            {DataModeRole, "dataMode"}, {StartDateRole, "startDate"},
            {EndDateRole, "endDate"},
            {FrequencyRole, "frequency"}, {RequestedBarsRole, "requestedBars"},
            {ActualBarsRole, "actualBars"}, {SucceededRole, "succeeded"},
            {ErrorRole, "error"}, {DataVersionRole, "dataVersion"},
            {DatasetVersionRole, "datasetVersion"},
            {DatasetDayCountRole, "datasetDayCount"},
            {DatasetRawTickCountRole, "datasetRawTickCount"},
            {DatasetGapDayCountRole, "datasetGapDayCount"},
            {DatasetStaleDayCountRole, "datasetStaleDayCount"},
            {DataQualityStatusRole, "dataQualityStatus"},
            {DataQualityEligibleRole, "dataQualityEligible"},
            {DataQualityAssessmentRole, "dataQualityAssessment"},
            {ArchivedRole, "archived"},
            {ArchivableRole, "archivable"},
            {ArchivedResultIdRole, "archivedResultId"},
            {FinalEquityRole, "finalEquity"},
            {TotalReturnPercentRole, "totalReturnPercent"},
            {MaxDrawdownPercentRole, "maxDrawdownPercent"},
            {WinRatePercentRole, "winRatePercent"}, {SharpeRatioRole, "sharpeRatio"},
            {TotalCommissionRole, "totalCommission"}, {TotalTradesRole, "totalTrades"},
            {HasOpenPositionRole, "hasOpenPosition"},
            {OpenPositionQuantityRole, "openPositionQuantity"},
            {OpenPositionAveragePriceRole, "openPositionAveragePrice"},
            {OpenPositionEntryTimeRole, "openPositionEntryTime"},
            {ElapsedMillisecondsRole, "elapsedMilliseconds"},
            {HoldoutEnabledRole, "holdoutEnabled"},
            {HoldoutTradingDaysRole, "holdoutTradingDays"},
            {TrainingStartDateRole, "trainingStartDate"},
            {TrainingEndDateRole, "trainingEndDate"},
            {HoldoutStartDateRole, "holdoutStartDate"},
            {HoldoutEndDateRole, "holdoutEndDate"},
            {TrainingActualTicksRole, "trainingActualTicks"},
            {HoldoutActualTicksRole, "holdoutActualTicks"},
            {TrainingDataVersionRole, "trainingDataVersion"},
            {HoldoutDataVersionRole, "holdoutDataVersion"},
            {TrainingDatasetVersionRole, "trainingDatasetVersion"},
            {HoldoutDatasetVersionRole, "holdoutDatasetVersion"},
            {TrainingTotalReturnPercentRole, "trainingTotalReturnPercent"},
            {TrainingMaxDrawdownPercentRole, "trainingMaxDrawdownPercent"},
            {TrainingTotalTradesRole, "trainingTotalTrades"},
            {HoldoutTotalReturnPercentRole, "holdoutTotalReturnPercent"},
            {HoldoutMaxDrawdownPercentRole, "holdoutMaxDrawdownPercent"},
            {HoldoutTotalTradesRole, "holdoutTotalTrades"},
            {WalkForwardEnabledRole, "walkForwardEnabled"},
            {WalkForwardFoldCountRole, "walkForwardFoldCount"},
            {ResearchOnlyRole, "researchOnly"},
            {WalkForwardFoldsRole, "walkForwardFolds"},
            {WalkForwardMeanReturnPercentRole, "walkForwardMeanReturnPercent"},
            {WalkForwardMedianReturnPercentRole, "walkForwardMedianReturnPercent"},
            {WalkForwardWorstReturnPercentRole, "walkForwardWorstReturnPercent"},
            {WalkForwardBestReturnPercentRole, "walkForwardBestReturnPercent"},
            {SegmentedResearchRole, "segmentedResearch"},
            {ResearchSegmentsRole, "researchSegments"},
            {ResearchSegmentCountRole, "researchSegmentCount"},
            {SegmentedMeanReturnPercentRole,
             "segmentedMeanReturnPercent"},
            {SegmentedMedianReturnPercentRole,
             "segmentedMedianReturnPercent"},
            {SegmentedWorstReturnPercentRole,
             "segmentedWorstReturnPercent"},
            {SegmentedBestReturnPercentRole,
             "segmentedBestReturnPercent"},
            {SegmentedWorstDrawdownPercentRole,
             "segmentedWorstDrawdownPercent"}};
}

void BatchBacktestResultListModel::updateArchivableState(
    const QString &symbol, bool archivable)
{
    const int row = indexOfSymbol(symbol);
    if (row < 0 || m_results.at(row).archivable == archivable) return;
    m_results[row].archivable = archivable;
    emit dataChanged(index(row), index(row), {ArchivableRole});
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

void BatchBacktestResultListModel::updateArchiveState(
    const QString &symbol, const QString &resultId)
{
    const int row = indexOfSymbol(symbol);
    if (row < 0) return;
    BatchBacktestItemResult &item = m_results[row];
    item.archived = !resultId.isEmpty();
    item.archivedResultId = resultId;
    emit dataChanged(index(row), index(row));
}

} // namespace pp
