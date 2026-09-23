#pragma once

#include "app/RealtimeChartSession.h"

#include <QAbstractListModel>

namespace pp {

class RealtimeChartWindowListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        WindowIdRole = Qt::UserRole + 1,
        SymbolRole,
        InstrumentTitleRole,
        SessionObjectRole
    };

    explicit RealtimeChartWindowListModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    RealtimeChartSession *appendSession(
        const QString &windowId, const Instrument &instrument,
        int refreshOffsetMilliseconds = 0);
    RealtimeChartSession *sessionById(const QString &windowId) const;
    bool removeSession(const QString &windowId);
    void setRefreshEnabled(bool enabled, bool requestImmediately = false);
    void shutdown();
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    QVariantMap sessionMap(const RealtimeChartSession *session) const;
    QVector<RealtimeChartSession *> m_sessions;
};

} // namespace pp
