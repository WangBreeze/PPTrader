#pragma once

#include "domain/PortfolioTypes.h"

#include <QAbstractListModel>

namespace pp {

class FactorResearchComparisonListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int maximumCount READ maximumCount CONSTANT)
    Q_PROPERTY(int multipleTestingCandidateCount
               READ multipleTestingCandidateCount
               NOTIFY multipleTestingChanged)
    Q_PROPERTY(QString multipleTestingCandidateSetSha256
               READ multipleTestingCandidateSetSha256
               NOTIFY multipleTestingChanged)
    Q_PROPERTY(QString multipleTestingStatusText
               READ multipleTestingStatusText
               NOTIFY multipleTestingChanged)

public:
    enum Role {
        ResultIdRole = Qt::UserRole + 1,
        NameRole,
        CompletedAtRole,
        FactorIdRole,
        FactorVersionRole,
        FactorAliasRole,
        StrategyIdRole,
        StrategyVersionRole,
        PeriodSecondsRole,
        LegCountRole,
        EvaluatedFrameCountRole,
        CoverageRatioRole,
        MeanIcRole,
        MeanRankIcRole,
        IcInformationRatioRole,
        RankIcInformationRatioRole,
        IcirAvailableRole,
        RankIcirAvailableRole,
        IcInferenceAvailableRole,
        RankIcInferenceAvailableRole,
        InferenceFrameCountRole,
        HacLagRole,
        IcConfidenceLowerRole,
        IcConfidenceUpperRole,
        RankIcConfidenceLowerRole,
        RankIcConfidenceUpperRole,
        IcTwoSidedPValueRole,
        RankIcTwoSidedPValueRole,
        TemporalStabilityAvailableRole,
        IcSameSignAcrossSegmentsRole,
        RankIcSameSignAcrossSegmentsRole,
        TemporalSegmentsRole,
        TopMinusBottomMeanReturnRole,
        TopMinusBottomMeanNetReturnRole,
        MeanOneWayTurnoverRole,
        OneWayCostBpsRole,
        ResearchMethodVersionRole,
        NeutralizationMethodRole,
        ControlledComparableRole,
        ComparabilityLabelRole,
        ComparabilityReasonRole,
        ComparisonBasisSha256Role,
        OutOfSampleAvailableRole,
        OutOfSampleEvaluationFrameCountRole,
        OutOfSampleWindowCountRole,
        OutOfSampleMeanIcRole,
        OutOfSampleIcInferenceAvailableRole,
        OutOfSampleIcConfidenceLowerRole,
        OutOfSampleIcConfidenceUpperRole,
        OutOfSampleIcTwoSidedPValueRole,
        MultipleTestingEligibleRole,
        HolmAdjustedPValueRole,
        BhAdjustedPValueRole,
        HolmRejectedRole,
        BhRejectedRole,
        MultipleTestingCandidateSetSha256Role
    };

    explicit FactorResearchComparisonListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    int maximumCount() const;
    int multipleTestingCandidateCount() const;
    QString multipleTestingCandidateSetSha256() const;
    QString multipleTestingStatusText() const;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool contains(const QString &resultId) const;
    Q_INVOKABLE bool removeArchive(const QString &resultId);
    Q_INVOKABLE void clear();

    bool addArchive(const PortfolioArchiveRecord &record,
                    QString *error = nullptr);

signals:
    void countChanged();
    void multipleTestingChanged();

private:
    struct Entry {
        PortfolioArchiveRecord archive;
        QVariantMap basis;
        QString basisSha256;
        bool controlledComparable = false;
        QString comparabilityLabel;
        QString comparabilityReason;
        bool multipleTestingEligible = false;
        double holmAdjustedPValue = 0.0;
        double bhAdjustedPValue = 0.0;
        bool holmRejected = false;
        bool bhRejected = false;
        QString multipleTestingCandidateSetSha256;
    };

    static QVariantMap comparisonBasis(const PortfolioArchiveRecord &record);
    static QString basisSha256(const QVariantMap &basis);
    static QString comparisonDifference(const QVariantMap &baseline,
                                        const QVariantMap &candidate);
    static QVariantMap entryMap(const Entry &entry);
    void refreshComparability();
    void refreshMultipleTesting();

    QVector<Entry> m_entries;
    int m_multipleTestingCandidateCount = 0;
    QString m_multipleTestingCandidateSetSha256;
};

} // namespace pp
