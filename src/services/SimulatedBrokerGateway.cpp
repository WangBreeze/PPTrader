#include "services/SimulatedBrokerGateway.h"

#include <QUuid>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

SimulatedBrokerGateway::SimulatedBrokerGateway(double commissionRate, double slippageRate,
                                               double liquidityParticipationRate, QObject *parent)
    : BrokerGateway(parent)
    , m_commissionRate(std::max(0.0, commissionRate))
    , m_slippageRate(std::max(0.0, slippageRate))
    , m_liquidityParticipationRate(std::clamp(liquidityParticipationRate, 0.0, 1.0))
{
    qRegisterMetaType<Order>();
    qRegisterMetaType<Execution>();
}

QString SimulatedBrokerGateway::brokerName() const
{
    return QStringLiteral("模拟券商");
}

bool SimulatedBrokerGateway::isLoggedIn() const
{
    return m_loggedIn;
}

void SimulatedBrokerGateway::login()
{
    if (m_loggedIn) return;
    m_loggedIn = true;
    emit loginStateChanged(true, QStringLiteral("模拟账户已登录"));
}

void SimulatedBrokerGateway::logout()
{
    if (!m_loggedIn) return;
    const QList<QString> orderIds = m_orders.keys();
    for (const QString &orderId : orderIds) {
        if (isActiveOrder(m_orders.value(orderId).status)) {
            cancelOrder(orderId);
        }
    }
    m_loggedIn = false;
    emit loginStateChanged(false, QStringLiteral("模拟账户已退出"));
}

QString SimulatedBrokerGateway::submitOrder(const OrderRequest &request)
{
    if (!m_loggedIn) {
        emit orderRejected(QStringLiteral("券商账户未登录"));
        return {};
    }
    if (request.instrument.symbol.isEmpty() || request.quantity <= 0.0
        || (request.type == OrderType::Limit && request.limitPrice <= 0.0)) {
        emit orderRejected(QStringLiteral("订单参数无效"));
        return {};
    }

    Order order;
    order.orderId = QStringLiteral("PAPER-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    order.request = request;
    if (!order.request.createdAt.isValid()) {
        order.request.createdAt = QDateTime::currentDateTimeUtc();
    }
    if (order.request.timeInForce == TimeInForce::Day && !order.request.expiresAt.isValid()) {
        order.request.expiresAt = defaultExpiry(order.request);
    }
    order.status = OrderStatus::Accepted;
    order.updatedAt = QDateTime::currentDateTimeUtc();
    order.message = QStringLiteral("订单已受理");
    m_orders.insert(order.orderId, order);
    emit orderUpdated(order);
    double availableLiquidity = std::numeric_limits<double>::infinity();
    const auto tickIterator = m_latestTicks.constFind(request.instrument.symbol);
    if (tickIterator != m_latestTicks.constEnd() && tickIterator->volume > 0.0) {
        availableLiquidity = tickIterator->volume * m_liquidityParticipationRate;
    }
    tryFill(order.orderId, &availableLiquidity);
    return order.orderId;
}

void SimulatedBrokerGateway::cancelOrder(const QString &orderId)
{
    auto iterator = m_orders.find(orderId);
    if (iterator == m_orders.end()) {
        emit orderRejected(QStringLiteral("未找到订单：%1").arg(orderId));
        return;
    }
    if (!isActiveOrder(iterator->status)) {
        emit orderRejected(QStringLiteral("订单当前状态不可撤销"));
        return;
    }
    iterator->status = OrderStatus::Cancelled;
    iterator->message = QStringLiteral("用户撤单");
    iterator->updatedAt = QDateTime::currentDateTimeUtc();
    emit orderUpdated(*iterator);
}

void SimulatedBrokerGateway::updateMarketPrice(const Tick &tick)
{
    if (tick.instrument.symbol.isEmpty() || tick.price <= 0.0) return;
    m_latestTicks.insert(tick.instrument.symbol, tick);
    expireOrders(tick.timestamp.isValid() ? tick.timestamp : QDateTime::currentDateTimeUtc());
    double availableLiquidity = tick.volume > 0.0
        ? tick.volume * m_liquidityParticipationRate
        : std::numeric_limits<double>::infinity();
    const QList<QString> orderIds = m_orders.keys();
    for (const QString &orderId : orderIds) {
        if (isActiveOrder(m_orders.value(orderId).status)) {
            tryFill(orderId, &availableLiquidity);
        }
    }
}

void SimulatedBrokerGateway::expireOrders(const QDateTime &currentTime)
{
    if (!currentTime.isValid()) return;
    const QList<QString> orderIds = m_orders.keys();
    for (const QString &orderId : orderIds) {
        auto iterator = m_orders.find(orderId);
        if (iterator == m_orders.end() || !isActiveOrder(iterator->status)) continue;
        if (iterator->request.expiresAt.isValid()
            && iterator->request.expiresAt <= currentTime) {
            cancelRemaining(*iterator, QStringLiteral("订单已到期，剩余数量自动撤销"));
        }
    }
}

double SimulatedBrokerGateway::commissionRate() const { return m_commissionRate; }
double SimulatedBrokerGateway::slippageRate() const { return m_slippageRate; }

void SimulatedBrokerGateway::tryFill(const QString &orderId, double *availableLiquidity)
{
    auto orderIterator = m_orders.find(orderId);
    if (orderIterator == m_orders.end() || !isActiveOrder(orderIterator->status)) return;

    Order &order = *orderIterator;
    const auto tickIterator = m_latestTicks.constFind(order.request.instrument.symbol);
    if (tickIterator == m_latestTicks.constEnd()) {
        if (order.request.expiresAt.isValid()
            && order.request.expiresAt <= QDateTime::currentDateTimeUtc()) {
            cancelRemaining(order, QStringLiteral("订单已到期，剩余数量自动撤销"));
            return;
        }
        if (order.request.timeInForce == TimeInForce::ImmediateOrCancel
            || order.request.timeInForce == TimeInForce::FillOrKill) {
            cancelRemaining(order, QStringLiteral("立即成交订单缺少行情，已撤销"));
        } else if (order.request.type == OrderType::Market) {
            rejectOrder(order, QStringLiteral("缺少可用行情，市价单无法成交"));
        }
        return;
    }

    const Tick &tick = *tickIterator;
    const QDateTime evaluationTime = tick.timestamp.isValid()
        ? tick.timestamp : QDateTime::currentDateTimeUtc();
    if (order.request.expiresAt.isValid() && order.request.expiresAt <= evaluationTime) {
        cancelRemaining(order, QStringLiteral("订单已到期，剩余数量自动撤销"));
        return;
    }
    const double quotePrice = order.request.side == OrderSide::Buy
        ? (tick.ask > 0.0 ? tick.ask : tick.price)
        : (tick.bid > 0.0 ? tick.bid : tick.price);
    double fillPrice = order.request.side == OrderSide::Buy
        ? quotePrice * (1.0 + m_slippageRate)
        : quotePrice * (1.0 - m_slippageRate);

    if (order.request.type == OrderType::Limit) {
        const bool marketable = order.request.side == OrderSide::Buy
            ? fillPrice <= order.request.limitPrice
            : fillPrice >= order.request.limitPrice;
        if (!marketable) {
            if (order.request.timeInForce == TimeInForce::ImmediateOrCancel
                || order.request.timeInForce == TimeInForce::FillOrKill) {
                cancelRemaining(order, QStringLiteral("立即成交订单未满足价格条件，已撤销"));
            }
            return;
        }
    }

    const double remainingQuantity = order.request.quantity - order.filledQuantity;
    const double liquidity = availableLiquidity
        ? std::max(0.0, *availableLiquidity) : std::numeric_limits<double>::infinity();
    if (order.request.timeInForce == TimeInForce::FillOrKill
        && liquidity + 0.000001 < remainingQuantity) {
        cancelRemaining(order, QStringLiteral("无法一次全部成交，FOK 订单已撤销"));
        return;
    }
    const double fillQuantity = std::min(remainingQuantity, liquidity);
    if (fillQuantity <= 0.000001) {
        if (order.request.timeInForce == TimeInForce::ImmediateOrCancel
            || order.request.timeInForce == TimeInForce::FillOrKill) {
            cancelRemaining(order, QStringLiteral("无可用流动性，立即成交订单已撤销"));
        }
        return;
    }

    Execution execution;
    execution.executionId = QStringLiteral("EXEC-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    execution.orderId = order.orderId;
    execution.instrument = order.request.instrument;
    execution.side = order.request.side;
    execution.quantity = fillQuantity;
    execution.price = fillPrice;
    execution.commission = fillPrice * execution.quantity * m_commissionRate;
    execution.timestamp = tick.timestamp.isValid() ? tick.timestamp : QDateTime::currentDateTimeUtc();

    const double previousFilledQuantity = order.filledQuantity;
    order.filledQuantity += execution.quantity;
    order.averageFillPrice = order.filledQuantity > 0.0
        ? (order.averageFillPrice * previousFilledQuantity + fillPrice * execution.quantity)
            / order.filledQuantity
        : 0.0;
    order.updatedAt = execution.timestamp;
    if (availableLiquidity && std::isfinite(*availableLiquidity)) {
        *availableLiquidity = std::max(0.0, *availableLiquidity - execution.quantity);
    }
    emit executionReceived(execution);
    if (order.filledQuantity + 0.000001 >= order.request.quantity) {
        order.status = OrderStatus::Filled;
        order.message = QStringLiteral("全部成交");
    } else if (order.request.timeInForce == TimeInForce::ImmediateOrCancel) {
        order.status = OrderStatus::Cancelled;
        order.message = QStringLiteral("IOC 部分成交，剩余数量已撤销");
    } else {
        order.status = OrderStatus::PartiallyFilled;
        order.message = QStringLiteral("部分成交，等待剩余数量");
    }
    emit orderUpdated(order);
}

void SimulatedBrokerGateway::rejectOrder(Order &order, const QString &reason)
{
    order.status = OrderStatus::Rejected;
    order.message = reason;
    order.updatedAt = QDateTime::currentDateTimeUtc();
    emit orderUpdated(order);
    emit orderRejected(reason);
}

void SimulatedBrokerGateway::cancelRemaining(Order &order, const QString &reason)
{
    order.status = OrderStatus::Cancelled;
    order.message = reason;
    order.updatedAt = QDateTime::currentDateTimeUtc();
    emit orderUpdated(order);
}

QDateTime SimulatedBrokerGateway::defaultExpiry(const OrderRequest &request) const
{
    const QDateTime createdAt = request.createdAt.isValid()
        ? request.createdAt : QDateTime::currentDateTimeUtc();
    if (request.instrument.market == Market::Crypto) return createdAt.addDays(1);

    const QTimeZone zone = request.instrument.market == Market::USStock
        ? QTimeZone("America/New_York") : QTimeZone("Asia/Shanghai");
    const QTime closeTime = request.instrument.market == Market::HongKong
        || request.instrument.market == Market::USStock ? QTime(16, 0) : QTime(15, 0);
    return QDateTime(createdAt.toTimeZone(zone).date(), closeTime, zone).toUTC();
}

} // namespace pp
