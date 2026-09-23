#pragma once

#include "domain/BatchBacktestTypes.h"

#include <QAbstractListModel>

namespace pp {

class BatchBacktestResultListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        NameRole,
        DataModeRole,
        StartDateRole,
        EndDateRole,
        FrequencyRole,
        RequestedBarsRole,
        ActualBarsRole,
        SucceededRole,
        ErrorRole,
        DataVersionRole,
        DatasetVersionRole,
        DatasetDayCountRole,
        DatasetRawTickCountRole,
        DatasetGapDayCountRole,
        DatasetStaleDayCountRole,
        DataQualityStatusRole,
        DataQualityEligibleRole,
        DataQualityAssessmentRole,
        ArchivedRole,
        ArchivableRole,
        ArchivedResultIdRole,
        FinalEquityRole,
        TotalReturnPercentRole,
        MaxDrawdownPercentRole,
        WinRatePercentRole,
        SharpeRatioRole,
        TotalCommissionRole,
        TotalTradesRole,
        HasOpenPositionRole,
        OpenPositionQuantityRole,
        OpenPositionAveragePriceRole,
        OpenPositionEntryTimeRole,
        ElapsedMillisecondsRole,
        HoldoutEnabledRole,
        HoldoutTradingDaysRole,
        TrainingStartDateRole,
        TrainingEndDateRole,
        HoldoutStartDateRole,
        HoldoutEndDateRole,
        TrainingActualTicksRole,
        HoldoutActualTicksRole,
        TrainingDataVersionRole,
        HoldoutDataVersionRole,
        TrainingDatasetVersionRole,
        HoldoutDatasetVersionRole,
        TrainingTotalReturnPercentRole,
        TrainingMaxDrawdownPercentRole,
        TrainingTotalTradesRole,
        HoldoutTotalReturnPercentRole,
        HoldoutMaxDrawdownPercentRole,
        HoldoutTotalTradesRole,
        WalkForwardEnabledRole,
        WalkForwardFoldCountRole,
        ResearchOnlyRole,
        WalkForwardFoldsRole,
        WalkForwardMeanReturnPercentRole,
        WalkForwardMedianReturnPercentRole,
        WalkForwardWorstReturnPercentRole,
        WalkForwardBestReturnPercentRole,
        SegmentedResearchRole,
        ResearchSegmentsRole,
        ResearchSegmentCountRole,
        SegmentedMeanReturnPercentRole,
        SegmentedMedianReturnPercentRole,
        SegmentedWorstReturnPercentRole,
        SegmentedBestReturnPercentRole,
        SegmentedWorstDrawdownPercentRole
    };

    explicit BatchBacktestResultListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfSymbol(const QString &symbol) const;
    void clear();
    void addResult(const BatchBacktestItemResult &result);
    void updateArchiveState(const QString &symbol, const QString &resultId);
    void updateArchivableState(const QString &symbol, bool archivable);

signals:
    void countChanged();

private:
    QVector<BatchBacktestItemResult> m_results;
};

} // namespace pp
