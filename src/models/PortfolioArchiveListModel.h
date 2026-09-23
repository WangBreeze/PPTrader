#pragma once

#include "domain/PortfolioTypes.h"

#include <QAbstractListModel>

namespace pp {

class PortfolioArchiveListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        ResultIdRole = Qt::UserRole + 1,
        ResultKindRole,
        PortfolioIdRole,
        NameRole,
        StrategyIdRole,
        StrategyVersionRole,
        CompletedAtRole,
        LegCountRole,
        PeriodSecondsRole,
        BarCountRole,
        SymbolsRole,
        DatasetVersionsRole,
        InitialCapitalRole,
        FinalEquityRole,
        TotalReturnPercentRole,
        MaxDrawdownPercentRole,
        SharpeRatioRole,
        TotalCommissionRole,
        TotalTradesRole
    };

    explicit PortfolioArchiveListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfResultId(const QString &resultId) const;
    void loadSummaries(const QVector<PortfolioArchiveSummary> &summaries);
    void clear();

signals:
    void countChanged();

private:
    QVector<PortfolioArchiveSummary> m_summaries;
};

} // namespace pp
