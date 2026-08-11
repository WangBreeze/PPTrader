#pragma once

#include "domain/BacktestTypes.h"

#include <QAbstractListModel>

namespace pp {

class BacktestArchiveListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        ResultIdRole = Qt::UserRole + 1,
        TaskIdRole,
        CompletedAtRole,
        StrategyIdRole,
        StrategyVersionRole,
        SymbolRole,
        NameRole,
        FrequencyRole,
        RequestedBarsRole,
        ActualBarsRole,
        DataVersionRole,
        DatasetVersionRole,
        DatasetFirstDateRole,
        DatasetLastDateRole,
        DatasetDayCountRole,
        DatasetRawTickCountRole,
        DatasetGapDayCountRole,
        DatasetStaleDayCountRole,
        DatasetCalendarSourceRole,
        DatasetCalendarCoverageKnownRole,
        TotalReturnPercentRole,
        MaxDrawdownPercentRole,
        TotalTradesRole,
        CustomStrategyRole
    };

    explicit BacktestArchiveListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfResultId(const QString &resultId) const;
    void loadSummaries(const QVector<BacktestArchiveSummary> &summaries);

signals:
    void countChanged();

private:
    QVector<BacktestArchiveSummary> m_summaries;
};

} // namespace pp
