#include "services/TradingAccountService.h"

#include <cmath>
#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace pp {

TradingAccountService::TradingAccountService(BrokerGateway *gateway, double initialCapital,
                                             double commissionRate, double slippageRate,
                                             QObject *parent)
    : QObject(parent)
    , m_gateway(gateway)
    , m_commissionRate(std::max(0.0, commissionRate))
    , m_slippageRate(std::max(0.0, slippageRate))
{
    Q_ASSERT(m_gateway);
    connect(m_gateway, &BrokerGateway::orderUpdated,
            this, &TradingAccountService::handleOrderUpdated);
    connect(m_gateway, &BrokerGateway::executionReceived,
            this, &TradingAccountService::handleExecution);
    connect(m_gateway, &BrokerGateway::orderRejected,
            this, &TradingAccountService::operationFailed);
    reset(initialCapital);
}

QString TradingAccountService::submitOrder(OrderRequest request)
{
    const QDate tradingDay = request.createdAt.isValid()
        ? request.createdAt.toLocalTime().date() : QDate::currentDate();
    ensureTradingDay(tradingDay);
    QString error;
    if (!validateOrder(request, &error)) {
        emit operationFailed(error);
        return {};
    }
    if (!request.createdAt.isValid()) {
        request.createdAt = QDateTime::currentDateTimeUtc();
    }
    return m_gateway->submitOrder(request);
}

void TradingAccountService::cancelOrder(const QString &orderId)
{
    m_gateway->cancelOrder(orderId);
}

void TradingAccountService::updateMarketPrice(const Tick &tick)
{
    ensureTradingDay(tick.timestamp.isValid()
        ? tick.timestamp.toLocalTime().date() : QDate::currentDate());
    bool positionUpdated = false;
    auto positionIterator = m_positions.find(tick.instrument.symbol);
    if (positionIterator != m_positions.end() && tick.price > 0.0) {
        positionIterator->marketPrice = tick.price;
        positionIterator->marketValue = positionIterator->quantity * tick.price;
        positionIterator->unrealizedPnl =
            (tick.price - positionIterator->averageCost) * positionIterator->quantity;
        emit positionChanged(*positionIterator);
        positionUpdated = true;
    }
    m_gateway->updateMarketPrice(tick);
    if (positionUpdated) recalculateAccount();
}

bool TradingAccountService::initializeHistoricalClock(const QDateTime &sourceStart)
{
    if (!sourceStart.isValid() || !m_orders.isEmpty() || !m_executions.isEmpty()
        || !m_positions.isEmpty() || !m_reservations.isEmpty()) return false;
    if (m_historicalClockStart.isValid()) return m_historicalClockStart == sourceStart;
    m_historicalClockStart = sourceStart.toUTC();
    m_snapshot.tradingDay = sourceStart.toTimeZone(QTimeZone("Asia/Shanghai")).date();
    m_snapshot.dayStartEquity = m_snapshot.totalEquity;
    m_snapshot.dailyPnl = 0;
    // 只初始化空账户的交易日，不重置资金，也不发出账户重置信号。
    emit accountChanged(m_snapshot);
    return true;
}

void TradingAccountService::reset(double initialCapital)
{
    const QList<QString> orderIds = m_reservations.keys();
    for (const QString &orderId : orderIds) {
        m_gateway->cancelOrder(orderId);
    }
    m_orders.clear();
    m_orderSequence.clear();
    m_executions.clear();
    m_positions.clear();
    m_symbolRealizedPnl.clear();
    m_reservations.clear();
    m_snapshot = {};
    m_historicalClockStart = {};
    m_snapshot.tradingDay = QDate::currentDate();
    m_snapshot.initialEquity = std::max(0.0, initialCapital);
    m_snapshot.dayStartEquity = m_snapshot.initialEquity;
    m_snapshot.cash = m_snapshot.initialEquity;
    m_snapshot.totalEquity = m_snapshot.initialEquity;
    emit accountReset();
    emit accountChanged(m_snapshot);
}

void TradingAccountService::restoreTradingState(const TradingState &state)
{
    if (!state.hasAccountSnapshot) {
        reset();
        return;
    }

    m_orders.clear();
    m_orderSequence.clear();
    m_executions = state.executions;
    m_positions.clear();
    m_symbolRealizedPnl.clear();
    m_reservations.clear();
    m_snapshot = state.account;
    m_snapshot.frozenCash = 0.0;
    if (!m_snapshot.tradingDay.isValid()
        || m_snapshot.tradingDay != QDate::currentDate()
        || m_snapshot.dayStartEquity <= 0.0) {
        m_snapshot.tradingDay = QDate::currentDate();
        m_snapshot.dayStartEquity = m_snapshot.totalEquity;
        m_snapshot.dailyPnl = 0.0;
    }
    emit accountReset();

    for (Order order : state.orders) {
        if (isActiveOrder(order.status)) {
            order.status = OrderStatus::Cancelled;
            order.message = QStringLiteral("应用启动恢复时自动撤销未完成模拟委托");
            order.updatedAt = QDateTime::currentDateTimeUtc();
        }
        m_orders.insert(order.orderId, order);
        m_orderSequence.append(order.orderId);
        emit orderChanged(order);
    }
    for (const Execution &execution : std::as_const(m_executions)) {
        emit executionAdded(execution);
    }
    for (Position position : state.positions) {
        if (position.quantity <= 0.000001) continue;
        position.availableQuantity = position.quantity;
        m_positions.insert(position.instrument.symbol, position);
        m_symbolRealizedPnl.insert(position.instrument.symbol, position.realizedPnl);
        emit positionChanged(position);
    }
    recalculateAccount();
}

RiskLimits TradingAccountService::riskLimits() const { return m_riskService.limits(); }
void TradingAccountService::setRiskLimits(const RiskLimits &limits) { m_riskService.setLimits(limits); }
void TradingAccountService::setExchangeRuleService(ExchangeRuleService *service)
{
    m_exchangeRuleService = service ? service : &m_defaultExchangeRuleService;
}

AccountSnapshot TradingAccountService::snapshot() const { return m_snapshot; }

QVector<Order> TradingAccountService::orders() const
{
    QVector<Order> result;
    result.reserve(m_orderSequence.size());
    for (const QString &orderId : m_orderSequence) {
        result.append(m_orders.value(orderId));
    }
    return result;
}

QVector<Execution> TradingAccountService::executions() const { return m_executions; }

QVector<Position> TradingAccountService::positions() const
{
    QVector<Position> result = m_positions.values();
    std::sort(result.begin(), result.end(), [](const Position &left, const Position &right) {
        return left.instrument.symbol < right.instrument.symbol;
    });
    return result;
}

bool TradingAccountService::validateOrder(const OrderRequest &request, QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!m_gateway->isLoggedIn()) return fail(QStringLiteral("券商账户未登录"));
    if (request.instrument.symbol.isEmpty() || request.quantity <= 0.0) {
        return fail(QStringLiteral("标的或委托数量无效"));
    }
    if (request.type == OrderType::Limit && request.limitPrice <= 0.0) {
        return fail(QStringLiteral("限价单价格必须大于 0"));
    }

    ExchangeRuleContext exchangeContext;
    exchangeContext.evaluationTime = request.createdAt.isValid()
        ? request.createdAt : QDateTime::currentDateTimeUtc();
    const RiskCheckResult exchangeResult = m_exchangeRuleService->validate(request, exchangeContext);
    if (!exchangeResult.accepted) return fail(exchangeResult.message);

    const double estimatedPrice = request.type == OrderType::Limit
        ? request.limitPrice : request.referencePrice;
    if (estimatedPrice <= 0.0) return fail(QStringLiteral("缺少有效参考行情"));

    int activeOrderCount = 0;
    for (const Order &order : std::as_const(m_orders)) {
        if (isActiveOrder(order.status)) ++activeOrderCount;
    }
    std::optional<Position> currentPosition;
    const auto riskPosition = m_positions.constFind(request.instrument.symbol);
    if (riskPosition != m_positions.constEnd()) currentPosition = *riskPosition;
    const RiskCheckResult riskResult = m_riskService.check(
        request, m_snapshot, currentPosition, activeOrderCount);
    if (!riskResult.accepted) return fail(riskResult.message);

    if (request.side == OrderSide::Buy) {
        const double estimatedFillPrice = request.type == OrderType::Market
            ? estimatedPrice * (1.0 + m_slippageRate) : estimatedPrice;
        const double requiredCash = estimatedFillPrice * request.quantity
            * (1.0 + m_commissionRate);
        const double availableCash = m_snapshot.cash - m_snapshot.frozenCash;
        if (requiredCash > availableCash + 0.000001) {
            return fail(QStringLiteral("可用资金不足"));
        }
    } else {
        const auto positionIterator = m_positions.constFind(request.instrument.symbol);
        if (positionIterator == m_positions.constEnd()
            || request.quantity > positionIterator->availableQuantity + 0.000001) {
            return fail(QStringLiteral("可用持仓不足"));
        }
    }
    return true;
}

void TradingAccountService::handleOrderUpdated(const Order &order)
{
    const bool isNewOrder = !m_orders.contains(order.orderId);
    if (isNewOrder) {
        m_orderSequence.append(order.orderId);
    }
    m_orders.insert(order.orderId, order);

    if (isNewOrder && isActiveOrder(order.status)) {
        reserveForOrder(order);
    }
    if (!isActiveOrder(order.status)) {
        releaseReservation(order.orderId);
    }
    emit orderChanged(order);
    recalculateAccount();
}

void TradingAccountService::handleExecution(const Execution &execution)
{
    const auto orderIterator = m_orders.constFind(execution.orderId);
    if (orderIterator == m_orders.constEnd() || execution.quantity <= 0.0) return;

    Reservation reservation = m_reservations.value(execution.orderId);
    if (reservation.remainingQuantity > 0.0) {
        const double ratio = std::min(1.0, execution.quantity / reservation.remainingQuantity);
        if (reservation.side == OrderSide::Buy) {
            const double releasedCash = reservation.reservedCash * ratio;
            m_snapshot.frozenCash = std::max(0.0, m_snapshot.frozenCash - releasedCash);
            reservation.reservedCash -= releasedCash;
        } else {
            reservation.reservedQuantity = std::max(
                0.0, reservation.reservedQuantity - execution.quantity);
        }
        reservation.remainingQuantity = std::max(
            0.0, reservation.remainingQuantity - execution.quantity);
        if (reservation.remainingQuantity <= 0.000001) {
            m_reservations.remove(execution.orderId);
        } else {
            m_reservations.insert(execution.orderId, reservation);
        }
    }

    Position position = m_positions.value(execution.instrument.symbol);
    position.instrument = execution.instrument;
    if (execution.side == OrderSide::Buy) {
        const double previousCost = position.averageCost * position.quantity;
        const double addedCost = execution.price * execution.quantity + execution.commission;
        position.quantity += execution.quantity;
        position.availableQuantity += execution.quantity;
        position.averageCost = position.quantity > 0.0
            ? (previousCost + addedCost) / position.quantity : 0.0;
        position.marketPrice = execution.price;
        m_snapshot.cash -= addedCost;
    } else {
        const double netPnl = (execution.price - position.averageCost) * execution.quantity
            - execution.commission;
        m_snapshot.cash += execution.price * execution.quantity - execution.commission;
        position.quantity = std::max(0.0, position.quantity - execution.quantity);
        position.availableQuantity = std::min(position.availableQuantity, position.quantity);
        m_symbolRealizedPnl[execution.instrument.symbol] += netPnl;
        m_snapshot.realizedPnl += netPnl;
    }

    m_snapshot.totalCommission += execution.commission;
    m_executions.append(execution);
    position.realizedPnl = m_symbolRealizedPnl.value(execution.instrument.symbol);
    position.marketValue = position.quantity * position.marketPrice;
    position.unrealizedPnl = (position.marketPrice - position.averageCost) * position.quantity;

    if (position.quantity <= 0.000001) {
        position.quantity = 0.0;
        position.availableQuantity = 0.0;
        position.marketValue = 0.0;
        position.unrealizedPnl = 0.0;
        m_positions.remove(execution.instrument.symbol);
    } else {
        m_positions.insert(execution.instrument.symbol, position);
    }

    emit executionAdded(execution);
    emit positionChanged(position);
    recalculateAccount();
}

void TradingAccountService::reserveForOrder(const Order &order)
{
    Reservation reservation;
    reservation.side = order.request.side;
    reservation.symbol = order.request.instrument.symbol;
    reservation.remainingQuantity = order.request.quantity - order.filledQuantity;
    if (reservation.side == OrderSide::Buy) {
        const double estimatedPrice = order.request.type == OrderType::Limit
            ? order.request.limitPrice
            : order.request.referencePrice * (1.0 + m_slippageRate);
        reservation.reservedCash = estimatedPrice * reservation.remainingQuantity
            * (1.0 + m_commissionRate);
        m_snapshot.frozenCash += reservation.reservedCash;
    } else {
        reservation.reservedQuantity = reservation.remainingQuantity;
        auto positionIterator = m_positions.find(reservation.symbol);
        if (positionIterator != m_positions.end()) {
            positionIterator->availableQuantity = std::max(
                0.0, positionIterator->availableQuantity - reservation.reservedQuantity);
            emit positionChanged(*positionIterator);
        }
    }
    m_reservations.insert(order.orderId, reservation);
}

void TradingAccountService::releaseReservation(const QString &orderId)
{
    const auto reservationIterator = m_reservations.find(orderId);
    if (reservationIterator == m_reservations.end()) return;
    const Reservation reservation = *reservationIterator;
    if (reservation.side == OrderSide::Buy) {
        m_snapshot.frozenCash = std::max(
            0.0, m_snapshot.frozenCash - reservation.reservedCash);
    } else {
        auto positionIterator = m_positions.find(reservation.symbol);
        if (positionIterator != m_positions.end()) {
            positionIterator->availableQuantity = std::min(
                positionIterator->quantity,
                positionIterator->availableQuantity + reservation.reservedQuantity);
            emit positionChanged(*positionIterator);
        }
    }
    m_reservations.erase(reservationIterator);
}

void TradingAccountService::recalculateAccount()
{
    m_snapshot.marketValue = 0.0;
    m_snapshot.unrealizedPnl = 0.0;
    for (const Position &position : std::as_const(m_positions)) {
        m_snapshot.marketValue += position.marketValue;
        m_snapshot.unrealizedPnl += position.unrealizedPnl;
    }
    m_snapshot.totalEquity = m_snapshot.cash + m_snapshot.marketValue;
    m_snapshot.dailyPnl = m_snapshot.dayStartEquity > 0.0
        ? m_snapshot.totalEquity - m_snapshot.dayStartEquity : 0.0;
    emit accountChanged(m_snapshot);
}

void TradingAccountService::ensureTradingDay(const QDate &tradingDay)
{
    if (!tradingDay.isValid() || m_snapshot.tradingDay == tradingDay) return;
    if (m_snapshot.tradingDay.isValid() && tradingDay < m_snapshot.tradingDay) return;
    recalculateAccount();
    m_snapshot.tradingDay = tradingDay;
    m_snapshot.dayStartEquity = m_snapshot.totalEquity;
    m_snapshot.dailyPnl = 0.0;
    emit accountChanged(m_snapshot);
}

} // namespace pp
