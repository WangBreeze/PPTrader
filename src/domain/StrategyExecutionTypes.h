#pragma once

#include "domain/TradingTypes.h"

namespace pp {

enum class StrategyExecutionMode {
    Disabled,
    ManualApproval,
    Automatic
};

enum class OrderIntentStatus {
    Created,
    PendingApproval,
    Approved,
    Submitted,
    Rejected,
    Failed,
    Expired
};

struct StrategyExecutionConfig {
    QString strategyId;
    bool enabled = false;
    StrategyExecutionMode mode = StrategyExecutionMode::ManualApproval;
    OrderType orderType = OrderType::Market;
    TimeInForce timeInForce = TimeInForce::Day;
    double fixedOrderValue = 10000.0;
    double maxPositionPercent = 10.0;
    bool sellAll = true;
    int cooldownSeconds = 30;
    int approvalTimeoutSeconds = 300;
    double limitOffsetBps = 5.0;
    int lotSize = 0;
    int orderTimeoutSeconds = 0;
    int maxOrdersPerDay = 0;
    double maxDailyOrderValue = 0.0;
};

struct StrategyOrderIntent {
    QString intentId;
    QString strategyId;
    TradeSignal signal;
    OrderRequest orderRequest;
    OrderIntentStatus status = OrderIntentStatus::Created;
    QString orderId;
    QString message;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime expiresAt;
};

inline QString strategyExecutionModeName(StrategyExecutionMode mode)
{
    switch (mode) {
    case StrategyExecutionMode::Disabled: return QStringLiteral("禁用");
    case StrategyExecutionMode::ManualApproval: return QStringLiteral("人工审批");
    case StrategyExecutionMode::Automatic: return QStringLiteral("自动执行");
    }
    return QStringLiteral("未知");
}

inline QString orderIntentStatusName(OrderIntentStatus status)
{
    switch (status) {
    case OrderIntentStatus::Created: return QStringLiteral("已创建");
    case OrderIntentStatus::PendingApproval: return QStringLiteral("待审批");
    case OrderIntentStatus::Approved: return QStringLiteral("已批准");
    case OrderIntentStatus::Submitted: return QStringLiteral("已提交");
    case OrderIntentStatus::Rejected: return QStringLiteral("已拒绝");
    case OrderIntentStatus::Failed: return QStringLiteral("提交失败");
    case OrderIntentStatus::Expired: return QStringLiteral("已过期");
    }
    return QStringLiteral("未知");
}

} // namespace pp

Q_DECLARE_METATYPE(pp::StrategyExecutionConfig)
Q_DECLARE_METATYPE(pp::StrategyOrderIntent)
