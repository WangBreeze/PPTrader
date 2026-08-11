#include "strategy/StrategyExecutionCoordinator.h"

#include <QUuid>

#include <algorithm>

namespace pp {

StrategyExecutionCoordinator::StrategyExecutionCoordinator(
    OrderSubmissionPort *orderPort, PositionSizer *positionSizer,
    SignalExecutionPolicy *executionPolicy, StrategyExecutionStore *store, QObject *parent)
    : QObject(parent)
    , m_orderPort(orderPort)
    , m_positionSizer(positionSizer)
    , m_executionPolicy(executionPolicy)
    , m_store(store)
{
    Q_ASSERT(m_orderPort);
    Q_ASSERT(m_positionSizer);
    Q_ASSERT(m_executionPolicy);
    qRegisterMetaType<StrategyOrderIntent>();
}

bool StrategyExecutionCoordinator::setConfiguration(const StrategyExecutionConfig &config)
{
    if (config.strategyId.isEmpty()) return false;
    StrategyExecutionConfig normalized = config;
    normalized.fixedOrderValue = std::max(0.0, normalized.fixedOrderValue);
    normalized.maxPositionPercent = std::max(0.0, normalized.maxPositionPercent);
    normalized.cooldownSeconds = std::max(0, normalized.cooldownSeconds);
    normalized.approvalTimeoutSeconds = std::max(0, normalized.approvalTimeoutSeconds);
    normalized.limitOffsetBps = std::max(0.0, normalized.limitOffsetBps);
    normalized.lotSize = std::max(0, normalized.lotSize);
    normalized.orderTimeoutSeconds = std::max(0, normalized.orderTimeoutSeconds);
    normalized.maxOrdersPerDay = std::max(0, normalized.maxOrdersPerDay);
    normalized.maxDailyOrderValue = std::max(0.0, normalized.maxDailyOrderValue);
    if (m_store && !m_store->upsertStrategyConfiguration(normalized)) return false;
    m_configurations.insert(normalized.strategyId, normalized);
    return true;
}

void StrategyExecutionCoordinator::removeConfiguration(const QString &strategyId)
{
    m_configurations.remove(strategyId);
    m_lastSubmissionTimes.remove(strategyId);
    m_dailyUsage.remove(strategyId);
    if (m_store) m_store->removeStrategyConfiguration(strategyId);
}

std::optional<StrategyExecutionConfig> StrategyExecutionCoordinator::configuration(
    const QString &strategyId) const
{
    const auto iterator = m_configurations.constFind(strategyId);
    if (iterator == m_configurations.constEnd()) return std::nullopt;
    return *iterator;
}

QVector<StrategyExecutionConfig> StrategyExecutionCoordinator::configurations() const
{
    QVector<StrategyExecutionConfig> result;
    result.reserve(m_configurations.size());
    for (const StrategyExecutionConfig &config : m_configurations) result.append(config);
    std::sort(result.begin(), result.end(),
        [](const StrategyExecutionConfig &left, const StrategyExecutionConfig &right) {
            return left.strategyId < right.strategyId;
        });
    return result;
}

QString StrategyExecutionCoordinator::processSignal(const QString &strategyId,
                                                    const TradeSignal &signal)
{
    StrategyOrderIntent intent;
    intent.intentId = QStringLiteral("INTENT-%1").arg(
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    intent.strategyId = strategyId;
    intent.signal = signal;
    intent.createdAt = signal.timestamp.isValid()
        ? signal.timestamp : QDateTime::currentDateTimeUtc();
    intent.updatedAt = intent.createdAt;
    m_intents.insert(intent.intentId, intent);
    m_intentSequence.append(intent.intentId);

    StrategyOrderIntent &storedIntent = m_intents[intent.intentId];
    const StrategyExecutionConfig initialConfig = m_configurations.value(strategyId);
    if (initialConfig.approvalTimeoutSeconds > 0) {
        storedIntent.expiresAt = storedIntent.createdAt.addSecs(
            initialConfig.approvalTimeoutSeconds);
    }
    if (m_store) m_store->upsertStrategyIntent(storedIntent);
    emit intentUpdated(storedIntent);
    QString error;
    if (!evaluateAndBuildOrder(storedIntent, &error)) {
        updateIntent(storedIntent, OrderIntentStatus::Rejected, error);
        emit executionRejected(storedIntent);
        return storedIntent.intentId;
    }

    const StrategyExecutionConfig config = m_configurations.value(strategyId);
    if (config.mode == StrategyExecutionMode::ManualApproval) {
        updateIntent(storedIntent, OrderIntentStatus::PendingApproval,
                     QStringLiteral("等待人工批准"));
        emit approvalRequired(storedIntent);
    } else {
        submitIntent(storedIntent);
    }
    return storedIntent.intentId;
}

bool StrategyExecutionCoordinator::approveIntent(const QString &intentId)
{
    auto iterator = m_intents.find(intentId);
    if (iterator == m_intents.end() || iterator->status != OrderIntentStatus::PendingApproval) {
        return false;
    }
    if (iterator->expiresAt.isValid()
        && iterator->expiresAt < QDateTime::currentDateTimeUtc()) {
        updateIntent(*iterator, OrderIntentStatus::Expired, QStringLiteral("审批意图已过期"));
        emit executionRejected(*iterator);
        return false;
    }
    updateIntent(*iterator, OrderIntentStatus::Approved, QStringLiteral("人工审批通过"));
    return submitIntent(*iterator);
}

bool StrategyExecutionCoordinator::rejectIntent(const QString &intentId, const QString &reason)
{
    auto iterator = m_intents.find(intentId);
    if (iterator == m_intents.end() || iterator->status != OrderIntentStatus::PendingApproval) {
        return false;
    }
    updateIntent(*iterator, OrderIntentStatus::Rejected,
                 reason.isEmpty() ? QStringLiteral("人工审批拒绝") : reason);
    emit executionRejected(*iterator);
    return true;
}

std::optional<StrategyOrderIntent> StrategyExecutionCoordinator::intent(const QString &intentId) const
{
    const auto iterator = m_intents.constFind(intentId);
    if (iterator == m_intents.constEnd()) return std::nullopt;
    return *iterator;
}

QVector<StrategyOrderIntent> StrategyExecutionCoordinator::intents() const
{
    QVector<StrategyOrderIntent> result;
    result.reserve(m_intentSequence.size());
    for (const QString &intentId : m_intentSequence) {
        result.append(m_intents.value(intentId));
    }
    return result;
}

void StrategyExecutionCoordinator::restore()
{
    if (!m_store) return;
    m_configurations.clear();
    m_intents.clear();
    m_intentSequence.clear();
    m_lastSubmissionTimes.clear();
    m_dailyUsage.clear();
    for (const StrategyExecutionConfig &config : m_store->loadStrategyConfigurations()) {
        if (!config.strategyId.isEmpty()) m_configurations.insert(config.strategyId, config);
    }
    for (StrategyOrderIntent restoredIntent : m_store->loadStrategyIntents()) {
        if (restoredIntent.intentId.isEmpty()) continue;
        if (restoredIntent.status == OrderIntentStatus::Created
            || restoredIntent.status == OrderIntentStatus::PendingApproval
            || restoredIntent.status == OrderIntentStatus::Approved) {
            restoredIntent.status = OrderIntentStatus::Expired;
            restoredIntent.message = QStringLiteral("应用启动恢复时终止未完成执行意图");
            restoredIntent.updatedAt = QDateTime::currentDateTimeUtc();
            m_store->upsertStrategyIntent(restoredIntent);
        }
        m_intents.insert(restoredIntent.intentId, restoredIntent);
        m_intentSequence.append(restoredIntent.intentId);
        if (restoredIntent.status == OrderIntentStatus::Submitted) {
            m_lastSubmissionTimes.insert(restoredIntent.strategyId, restoredIntent.updatedAt);
            const QDate tradingDay = restoredIntent.updatedAt.toLocalTime().date();
            if (tradingDay == QDate::currentDate()) {
                DailyStrategyUsage &usage = m_dailyUsage[restoredIntent.strategyId];
                usage.tradingDay = tradingDay;
                ++usage.submittedOrders;
                const double price = restoredIntent.orderRequest.type == OrderType::Limit
                    ? restoredIntent.orderRequest.limitPrice
                    : restoredIntent.orderRequest.referencePrice;
                usage.submittedValue += price * restoredIntent.orderRequest.quantity;
            }
        }
        emit intentUpdated(restoredIntent);
    }
}

std::optional<Position> StrategyExecutionCoordinator::findPosition(const QString &symbol) const
{
    for (const Position &position : m_orderPort->positions()) {
        if (position.instrument.symbol == symbol) return position;
    }
    return std::nullopt;
}

bool StrategyExecutionCoordinator::evaluateAndBuildOrder(StrategyOrderIntent &intent, QString *error)
{
    const auto configIterator = m_configurations.constFind(intent.strategyId);
    if (configIterator == m_configurations.constEnd()) {
        if (error) *error = QStringLiteral("策略未配置执行规则");
        return false;
    }

    const StrategyExecutionConfig &config = *configIterator;
    const AccountSnapshot account = m_orderPort->snapshot();
    const std::optional<Position> position = findPosition(intent.signal.instrument.symbol);
    const QDateTime evaluationTime = intent.signal.timestamp.isValid()
        ? intent.signal.timestamp : QDateTime::currentDateTimeUtc();
    const RiskCheckResult decision = m_executionPolicy->evaluate(
        config, intent.signal, account, position,
        m_lastSubmissionTimes.value(intent.strategyId), evaluationTime);
    if (!decision.accepted) {
        if (error) *error = decision.message;
        return false;
    }

    const double quantity = m_positionSizer->calculateQuantity(
        config, intent.signal, account, position);
    if (quantity <= 0.0) {
        if (error) *error = QStringLiteral("仓位计算结果为 0，忽略本次信号");
        return false;
    }

    OrderRequest request;
    request.clientOrderId = intent.intentId;
    request.instrument = intent.signal.instrument;
    request.side = intent.signal.side == SignalSide::Buy ? OrderSide::Buy : OrderSide::Sell;
    request.type = config.orderType;
    request.timeInForce = config.timeInForce;
    request.quantity = quantity;
    request.referencePrice = intent.signal.price;
    request.createdAt = evaluationTime;
    if (config.orderTimeoutSeconds > 0) {
        request.expiresAt = evaluationTime.addSecs(config.orderTimeoutSeconds);
    }
    if (request.type == OrderType::Limit) {
        const double offset = config.limitOffsetBps / 10000.0;
        request.limitPrice = request.side == OrderSide::Buy
            ? intent.signal.price * (1.0 + offset)
            : intent.signal.price * (1.0 - offset);
    }

    const QDate tradingDay = evaluationTime.toLocalTime().date();
    DailyStrategyUsage &usage = m_dailyUsage[intent.strategyId];
    if (usage.tradingDay != tradingDay) {
        usage = {};
        usage.tradingDay = tradingDay;
    }
    const double estimatedOrderPrice = request.type == OrderType::Limit
        ? request.limitPrice : request.referencePrice;
    const double estimatedOrderValue = estimatedOrderPrice * request.quantity;
    if (config.maxOrdersPerDay > 0 && usage.submittedOrders >= config.maxOrdersPerDay) {
        if (error) *error = QStringLiteral("策略当日订单次数已达到预算上限");
        return false;
    }
    if (config.maxDailyOrderValue > 0.0
        && usage.submittedValue + estimatedOrderValue > config.maxDailyOrderValue + 0.000001) {
        if (error) *error = QStringLiteral("策略当日累计订单金额将超过预算");
        return false;
    }
    intent.orderRequest = request;
    return true;
}

bool StrategyExecutionCoordinator::submitIntent(StrategyOrderIntent &intent)
{
    QString error;
    if (!evaluateAndBuildOrder(intent, &error)) {
        updateIntent(intent, OrderIntentStatus::Rejected, error);
        emit executionRejected(intent);
        return false;
    }

    const QString orderId = m_orderPort->submitOrder(intent.orderRequest);
    if (orderId.isEmpty()) {
        updateIntent(intent, OrderIntentStatus::Failed, QStringLiteral("订单提交端口拒绝请求"));
        emit executionRejected(intent);
        return false;
    }
    intent.orderId = orderId;
    const QDateTime submissionTime = intent.orderRequest.createdAt.isValid()
        ? intent.orderRequest.createdAt : QDateTime::currentDateTimeUtc();
    m_lastSubmissionTimes.insert(intent.strategyId, submissionTime);
    DailyStrategyUsage &usage = m_dailyUsage[intent.strategyId];
    const QDate tradingDay = submissionTime.toLocalTime().date();
    if (usage.tradingDay != tradingDay) {
        usage = {};
        usage.tradingDay = tradingDay;
    }
    ++usage.submittedOrders;
    const double submittedPrice = intent.orderRequest.type == OrderType::Limit
        ? intent.orderRequest.limitPrice : intent.orderRequest.referencePrice;
    usage.submittedValue += submittedPrice * intent.orderRequest.quantity;
    updateIntent(intent, OrderIntentStatus::Submitted, QStringLiteral("策略订单已提交"));
    emit orderSubmitted(intent);
    return true;
}

void StrategyExecutionCoordinator::updateIntent(StrategyOrderIntent &intent,
                                                OrderIntentStatus status,
                                                const QString &message)
{
    intent.status = status;
    intent.message = message;
    intent.updatedAt = QDateTime::currentDateTimeUtc();
    if (m_store) m_store->upsertStrategyIntent(intent);
    emit intentUpdated(intent);
}

} // namespace pp
