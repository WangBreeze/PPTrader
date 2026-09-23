#include "models/RealtimeChartWindowListModel.h"

namespace pp {

RealtimeChartWindowListModel::RealtimeChartWindowListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RealtimeChartWindowListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sessions.size();
}

QVariant RealtimeChartWindowListModel::data(
    const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size())
        return {};
    const RealtimeChartSession *session = m_sessions.at(index.row());
    switch (role) {
    case WindowIdRole: return session->windowId();
    case SymbolRole: return session->symbol();
    case InstrumentTitleRole: return session->instrumentTitle();
    case SessionObjectRole:
        return QVariant::fromValue(static_cast<QObject *>(
            const_cast<RealtimeChartSession *>(session)));
    default: return {};
    }
}

QHash<int, QByteArray> RealtimeChartWindowListModel::roleNames() const
{
    return {
        {WindowIdRole, "windowId"},
        {SymbolRole, "symbol"},
        {InstrumentTitleRole, "instrumentTitle"},
        {SessionObjectRole, "sessionObject"}
    };
}

int RealtimeChartWindowListModel::count() const { return m_sessions.size(); }

RealtimeChartSession *RealtimeChartWindowListModel::appendSession(
    const QString &windowId, const Instrument &instrument,
    int refreshOffsetMilliseconds)
{
    const int row = m_sessions.size();
    beginInsertRows({}, row, row);
    auto *session = new RealtimeChartSession(
        windowId, instrument, refreshOffsetMilliseconds, this);
    m_sessions.append(session);
    endInsertRows();
    connect(session, &RealtimeChartSession::stateChanged, this,
            [this, session]() {
        const int row = m_sessions.indexOf(session);
        if (row >= 0)
            emit dataChanged(index(row), index(row));
    });
    emit countChanged();
    return session;
}

RealtimeChartSession *RealtimeChartWindowListModel::sessionById(
    const QString &windowId) const
{
    for (RealtimeChartSession *session : m_sessions) {
        if (session->windowId() == windowId) return session;
    }
    return nullptr;
}

bool RealtimeChartWindowListModel::removeSession(const QString &windowId)
{
    for (int row = 0; row < m_sessions.size(); ++row) {
        RealtimeChartSession *session = m_sessions.at(row);
        if (session->windowId() != windowId) continue;
        beginRemoveRows({}, row, row);
        m_sessions.removeAt(row);
        endRemoveRows();
        session->shutdown();
        session->deleteLater();
        emit countChanged();
        return true;
    }
    return false;
}

void RealtimeChartWindowListModel::setRefreshEnabled(
    bool enabled, bool requestImmediately)
{
    for (RealtimeChartSession *session : m_sessions)
        session->setRefreshEnabled(enabled, requestImmediately);
}

void RealtimeChartWindowListModel::shutdown()
{
    if (m_sessions.isEmpty()) return;
    beginResetModel();
    const QVector<RealtimeChartSession *> sessions = std::move(m_sessions);
    m_sessions.clear();
    endResetModel();
    for (RealtimeChartSession *session : sessions) {
        session->shutdown();
        delete session;
    }
    emit countChanged();
}

QVariantMap RealtimeChartWindowListModel::get(int row) const
{
    if (row < 0 || row >= m_sessions.size()) return {};
    return sessionMap(m_sessions.at(row));
}

QVariantMap RealtimeChartWindowListModel::sessionMap(
    const RealtimeChartSession *session) const
{
    return {
        {QStringLiteral("windowId"), session->windowId()},
        {QStringLiteral("symbol"), session->symbol()},
        {QStringLiteral("instrumentTitle"), session->instrumentTitle()},
        {QStringLiteral("sessionObject"), QVariant::fromValue(
             static_cast<QObject *>(const_cast<RealtimeChartSession *>(session)))}
    };
}

} // namespace pp
