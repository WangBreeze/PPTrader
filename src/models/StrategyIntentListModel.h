#pragma once

#include "domain/StrategyExecutionTypes.h"

#include <QAbstractListModel>

namespace pp {

class StrategyIntentListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingCountChanged)

public:
    enum Role {
        IntentIdRole = Qt::UserRole + 1,
        StrategyIdRole,
        SymbolRole,
        SignalSideRole,
        SignalPriceRole,
        SignalReasonRole,
        OrderSideRole,
        OrderTypeRole,
        TimeInForceRole,
        QuantityRole,
        LimitPriceRole,
        ReferencePriceRole,
        StatusRole,
        OrderIdRole,
        MessageRole,
        CreatedAtRole,
        UpdatedAtRole,
        ExpiresAtRole,
        OrderExpiresAtRole,
        PendingRole,
        SignalSideValueRole,
        OrderSideValueRole,
        OrderTypeValueRole,
        TimeInForceValueRole,
        StatusValueRole
    };
    Q_ENUM(Role)

    explicit StrategyIntentListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const;
    int pendingCount() const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsertIntent(const StrategyOrderIntent &intent);
    void loadIntents(const QVector<StrategyOrderIntent> &intents);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfIntent(const QString &intentId) const;
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void pendingCountChanged();

private:
    QVector<StrategyOrderIntent> m_intents;
};

} // namespace pp
