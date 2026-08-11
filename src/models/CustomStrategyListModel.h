#pragma once

#include "domain/CustomStrategyTypes.h"

#include <QAbstractListModel>

namespace pp {

class CustomStrategyListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        StrategyIdRole = Qt::UserRole + 1,
        VersionRole,
        NameRole,
        DescriptionRole,
        AuthorRole,
        MarketRole,
        ExecutionScopeRole,
        CreatedAtRole,
        UpdatedAtRole,
        LatestRole,
        StageRole,
        ExecutableRole
    };

    explicit CustomStrategyListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString &strategyId, int version = 0) const;
    void loadDefinitions(const QVector<CustomStrategyDefinition> &definitions);

signals:
    void countChanged();

private:
    bool isLatest(int row) const;
    QVector<CustomStrategyDefinition> m_definitions;
};

} // namespace pp
