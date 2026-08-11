#pragma once

#include "domain/MarketTypes.h"

#include <QVector>

namespace pp {

enum class OrderSide {
    Buy,
    Sell
};

enum class OrderType {
    Market,
    Limit
};

enum class TimeInForce {
    Day,
    GoodTillCancelled,
    ImmediateOrCancel,
    FillOrKill
};

enum class OrderStatus {
    Pending,
    Accepted,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};

struct OrderRequest {
    QString clientOrderId;
    Instrument instrument;
    OrderSide side = OrderSide::Buy;
    OrderType type = OrderType::Market;
    TimeInForce timeInForce = TimeInForce::Day;
    double quantity = 0.0;
    double limitPrice = 0.0;
    double referencePrice = 0.0;
    QDateTime createdAt;
    QDateTime expiresAt;
};

struct Order {
    QString orderId;
    OrderRequest request;
    OrderStatus status = OrderStatus::Pending;
    double filledQuantity = 0.0;
    double averageFillPrice = 0.0;
    QString message;
    QDateTime updatedAt;
};

struct Execution {
    QString executionId;
    QString orderId;
    Instrument instrument;
    OrderSide side = OrderSide::Buy;
    double quantity = 0.0;
    double price = 0.0;
    double commission = 0.0;
    QDateTime timestamp;
};

struct Position {
    Instrument instrument;
    double quantity = 0.0;
    double availableQuantity = 0.0;
    double averageCost = 0.0;
    double marketPrice = 0.0;
    double marketValue = 0.0;
    double unrealizedPnl = 0.0;
    double realizedPnl = 0.0;
};

struct AccountSnapshot {
    QDate tradingDay;
    double initialEquity = 0.0;
    double dayStartEquity = 0.0;
    double cash = 0.0;
    double frozenCash = 0.0;
    double marketValue = 0.0;
    double totalEquity = 0.0;
    double realizedPnl = 0.0;
    double unrealizedPnl = 0.0;
    double totalCommission = 0.0;
    double dailyPnl = 0.0;
};

struct RiskLimits {
    double maxOrderValue = 200000.0;
    double maxPositionValue = 500000.0;
    double maxDailyLoss = 50000.0;
    int maxActiveOrders = 20;
};

struct RiskCheckResult {
    bool accepted = false;
    QString code;
    QString message;
};

struct TradingState {
    bool hasAccountSnapshot = false;
    AccountSnapshot account;
    QVector<Order> orders;
    QVector<Execution> executions;
    QVector<Position> positions;
};

inline QString orderSideName(OrderSide side)
{
    return side == OrderSide::Buy ? QStringLiteral("买入") : QStringLiteral("卖出");
}

inline QString orderTypeName(OrderType type)
{
    return type == OrderType::Market ? QStringLiteral("市价") : QStringLiteral("限价");
}

inline QString timeInForceName(TimeInForce timeInForce)
{
    switch (timeInForce) {
    case TimeInForce::Day: return QStringLiteral("当日有效");
    case TimeInForce::GoodTillCancelled: return QStringLiteral("撤销前有效");
    case TimeInForce::ImmediateOrCancel: return QStringLiteral("立即成交剩余撤销");
    case TimeInForce::FillOrKill: return QStringLiteral("全部成交否则撤销");
    }
    return QStringLiteral("未知");
}

inline QString orderStatusName(OrderStatus status)
{
    switch (status) {
    case OrderStatus::Pending: return QStringLiteral("待提交");
    case OrderStatus::Accepted: return QStringLiteral("已受理");
    case OrderStatus::PartiallyFilled: return QStringLiteral("部分成交");
    case OrderStatus::Filled: return QStringLiteral("全部成交");
    case OrderStatus::Cancelled: return QStringLiteral("已撤单");
    case OrderStatus::Rejected: return QStringLiteral("已拒绝");
    }
    return QStringLiteral("未知");
}

inline bool isActiveOrder(OrderStatus status)
{
    return status == OrderStatus::Pending
        || status == OrderStatus::Accepted
        || status == OrderStatus::PartiallyFilled;
}

} // namespace pp

Q_DECLARE_METATYPE(pp::OrderRequest)
Q_DECLARE_METATYPE(pp::Order)
Q_DECLARE_METATYPE(pp::Execution)
Q_DECLARE_METATYPE(pp::Position)
Q_DECLARE_METATYPE(pp::AccountSnapshot)
Q_DECLARE_METATYPE(pp::RiskLimits)
Q_DECLARE_METATYPE(pp::RiskCheckResult)
