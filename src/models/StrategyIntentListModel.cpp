#include "models/StrategyIntentListModel.h"

#include <algorithm>

namespace pp {

namespace {

bool intentComesBefore(const StrategyOrderIntent &left, const StrategyOrderIntent &right)
{
    if (left.createdAt.isValid() != right.createdAt.isValid()) return left.createdAt.isValid();
    if (left.createdAt != right.createdAt) return left.createdAt > right.createdAt;
    return left.intentId < right.intentId;
}

QVector<int> allIntentRoles()
{
    QVector<int> roles;
    for (int role = StrategyIntentListModel::IntentIdRole;
         role <= StrategyIntentListModel::StatusValueRole; ++role) {
        roles.append(role);
    }
    return roles;
}

} // namespace

StrategyIntentListModel::StrategyIntentListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int StrategyIntentListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_intents.size();
}

int StrategyIntentListModel::count() const { return m_intents.size(); }

int StrategyIntentListModel::pendingCount() const
{
    return static_cast<int>(std::count_if(m_intents.cbegin(), m_intents.cend(),
        [](const StrategyOrderIntent &intent) {
            return intent.status == OrderIntentStatus::PendingApproval;
        }));
}

QVariant StrategyIntentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_intents.size()) return {};
    const StrategyOrderIntent &intent = m_intents.at(index.row());
    switch (role) {
    case IntentIdRole: return intent.intentId;
    case StrategyIdRole: return intent.strategyId;
    case SymbolRole: return intent.signal.instrument.symbol;
    case SignalSideRole: return signalSideName(intent.signal.side);
    case SignalPriceRole: return intent.signal.price;
    case SignalReasonRole: return intent.signal.reason;
    case OrderSideRole: return orderSideName(intent.orderRequest.side);
    case OrderTypeRole: return orderTypeName(intent.orderRequest.type);
    case TimeInForceRole: return timeInForceName(intent.orderRequest.timeInForce);
    case QuantityRole: return intent.orderRequest.quantity;
    case LimitPriceRole: return intent.orderRequest.limitPrice;
    case ReferencePriceRole: return intent.orderRequest.referencePrice;
    case StatusRole: return orderIntentStatusName(intent.status);
    case OrderIdRole: return intent.orderId;
    case MessageRole: return intent.message;
    case CreatedAtRole: return intent.createdAt;
    case UpdatedAtRole: return intent.updatedAt;
    case ExpiresAtRole: return intent.expiresAt;
    case OrderExpiresAtRole: return intent.orderRequest.expiresAt;
    case PendingRole: return intent.status == OrderIntentStatus::PendingApproval;
    case SignalSideValueRole: return static_cast<int>(intent.signal.side);
    case OrderSideValueRole: return static_cast<int>(intent.orderRequest.side);
    case OrderTypeValueRole: return static_cast<int>(intent.orderRequest.type);
    case TimeInForceValueRole: return static_cast<int>(intent.orderRequest.timeInForce);
    case StatusValueRole: return static_cast<int>(intent.status);
    default: return {};
    }
}

QHash<int, QByteArray> StrategyIntentListModel::roleNames() const
{
    return {{IntentIdRole, "intentId"}, {StrategyIdRole, "strategyId"},
            {SymbolRole, "symbol"}, {SignalSideRole, "signalSide"},
            {SignalPriceRole, "signalPrice"}, {SignalReasonRole, "signalReason"},
            {OrderSideRole, "orderSide"}, {OrderTypeRole, "orderType"},
            {TimeInForceRole, "timeInForce"}, {QuantityRole, "quantity"},
            {LimitPriceRole, "limitPrice"}, {ReferencePriceRole, "referencePrice"},
            {StatusRole, "status"}, {OrderIdRole, "orderId"},
            {MessageRole, "message"}, {CreatedAtRole, "createdAt"},
            {UpdatedAtRole, "updatedAt"}, {ExpiresAtRole, "expiresAt"},
            {OrderExpiresAtRole, "orderExpiresAt"}, {PendingRole, "pending"},
            {SignalSideValueRole, "signalSideValue"},
            {OrderSideValueRole, "orderSideValue"},
            {OrderTypeValueRole, "orderTypeValue"},
            {TimeInForceValueRole, "timeInForceValue"},
            {StatusValueRole, "statusValue"}};
}

void StrategyIntentListModel::upsertIntent(const StrategyOrderIntent &intent)
{
    if (intent.intentId.isEmpty()) return;
    for (int row = 0; row < m_intents.size(); ++row) {
        if (m_intents.at(row).intentId != intent.intentId) continue;
        const bool wasPending = m_intents.at(row).status == OrderIntentStatus::PendingApproval;
        QVector<StrategyOrderIntent> remaining = m_intents;
        remaining.removeAt(row);
        const auto position = std::lower_bound(remaining.cbegin(), remaining.cend(), intent,
                                               intentComesBefore);
        const int destinationRow = static_cast<int>(position - remaining.cbegin());
        if (destinationRow != row) {
            beginMoveRows({}, row, row, {}, destinationRow > row ? destinationRow + 1 : destinationRow);
            m_intents.move(row, destinationRow);
            endMoveRows();
            row = destinationRow;
        }
        m_intents[row] = intent;
        emit dataChanged(index(row), index(row), allIntentRoles());
        if (wasPending != (intent.status == OrderIntentStatus::PendingApproval)) {
            emit pendingCountChanged();
        }
        return;
    }
    const auto position = std::lower_bound(m_intents.cbegin(), m_intents.cend(), intent,
                                           intentComesBefore);
    const int row = static_cast<int>(position - m_intents.cbegin());
    beginInsertRows({}, row, row);
    m_intents.insert(row, intent);
    endInsertRows();
    emit countChanged();
    if (intent.status == OrderIntentStatus::PendingApproval) emit pendingCountChanged();
}

void StrategyIntentListModel::loadIntents(const QVector<StrategyOrderIntent> &intents)
{
    const int previousCount = count();
    const int previousPendingCount = pendingCount();
    QVector<StrategyOrderIntent> normalized;
    normalized.reserve(intents.size());
    for (const StrategyOrderIntent &intent : intents) {
        if (intent.intentId.isEmpty()) continue;
        auto existing = std::find_if(normalized.begin(), normalized.end(),
            [&intent](const StrategyOrderIntent &candidate) {
                return candidate.intentId == intent.intentId;
            });
        if (existing == normalized.end()) {
            normalized.append(intent);
        } else if (!existing->updatedAt.isValid() || intent.updatedAt >= existing->updatedAt) {
            *existing = intent;
        }
    }
    std::sort(normalized.begin(), normalized.end(), intentComesBefore);
    beginResetModel();
    m_intents = std::move(normalized);
    endResetModel();
    if (previousCount != count()) emit countChanged();
    if (previousPendingCount != pendingCount()) emit pendingCountChanged();
}

QVariantMap StrategyIntentListModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= m_intents.size()) return result;
    const QModelIndex modelIndex = index(row);
    const auto names = roleNames();
    for (auto iterator = names.cbegin(); iterator != names.cend(); ++iterator) {
        result.insert(QString::fromLatin1(iterator.value()), data(modelIndex, iterator.key()));
    }
    return result;
}

int StrategyIntentListModel::indexOfIntent(const QString &intentId) const
{
    for (int row = 0; row < m_intents.size(); ++row) {
        if (m_intents.at(row).intentId == intentId) return row;
    }
    return -1;
}

void StrategyIntentListModel::clear()
{
    if (m_intents.isEmpty()) return;
    const bool hadPendingIntents = pendingCount() > 0;
    beginResetModel();
    m_intents.clear();
    endResetModel();
    emit countChanged();
    if (hadPendingIntents) emit pendingCountChanged();
}

} // namespace pp
