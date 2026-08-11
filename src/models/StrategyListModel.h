#pragma once

#include <QAbstractListModel>
#include <QVector>

namespace pp {

struct StrategyRow {
    QString id;
    QString symbol;
    QString name;
    QString description;
    double thresholdPercent = 0.0;
    int cooldownSeconds = 0;
    bool enabled = true;
};

class StrategyListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        SymbolRole,
        NameRole,
        DescriptionRole,
        ThresholdPercentRole,
        CooldownSecondsRole,
        EnabledRole
    };

    explicit StrategyListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void clear();
    void addStrategy(const StrategyRow &row);
    bool setStrategyEnabled(const QString &id, bool enabled);
    bool setStrategyParameters(const QString &id, double thresholdPercent, int cooldownSeconds);

private:
    QVector<StrategyRow> m_rows;
};

} // 命名空间 pp
