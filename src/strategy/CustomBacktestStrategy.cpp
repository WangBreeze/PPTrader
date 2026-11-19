#include "strategy/CustomBacktestStrategy.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

std::optional<double> fieldValue(CustomOperandKind kind, const Candle &candle)
{
    double value = 0.0;
    bool allowZero = false;
    switch (kind) {
    case CustomOperandKind::Open: value = candle.open; break;
    case CustomOperandKind::High: value = candle.high; break;
    case CustomOperandKind::Low: value = candle.low; break;
    case CustomOperandKind::Close: value = candle.close; break;
    case CustomOperandKind::Volume: value = candle.volume; allowZero = true; break;
    default: return std::nullopt;
    }
    if (!std::isfinite(value) || (allowZero ? value < 0.0 : value <= 0.0)) return std::nullopt;
    return value;
}

} // namespace

CustomBacktestStrategy::CustomBacktestStrategy(
    CompiledCustomStrategy plan, CustomBacktestInputMode inputMode, QObject *parent)
    : BacktestStrategy(parent)
    , m_plan(std::move(plan))
    , m_inputMode(inputMode)
    , m_indicators(m_plan.indicators)
    , m_previousLeft(m_plan.conditions.size())
    , m_previousRight(m_plan.conditions.size())
{
}

QString CustomBacktestStrategy::id() const
{
    return m_plan.strategyId;
}

QString CustomBacktestStrategy::displayName() const
{
    return QStringLiteral("%1 v%2").arg(m_plan.name).arg(m_plan.version);
}

int CustomBacktestStrategy::minimumWarmupBars() const
{
    return m_plan.minimumWarmupBars;
}

double CustomBacktestStrategy::positionFraction() const
{
    return m_plan.rules.positionFraction;
}

double CustomBacktestStrategy::positionAmount() const
{
    return m_plan.rules.positionAmount;
}

double CustomBacktestStrategy::maxPositionPercent() const
{
    return m_plan.rules.maxPositionPercent;
}

int CustomBacktestStrategy::strategyVersion() const
{
    return m_plan.version;
}

QVariantMap CustomBacktestStrategy::normalizedParameters() const
{
    return m_plan.normalizedParameters;
}

QVariantMap CustomBacktestStrategy::ruleSnapshot() const
{
    QVariantMap snapshot = m_plan.ruleSnapshot();
    if (m_inputMode == CustomBacktestInputMode::RawTicks) {
        snapshot.insert(QStringLiteral("rawTickPolicyVersion"), 1);
        snapshot.insert(QStringLiteral("rawTickIndicatorBasis"),
                        QStringLiteral("one_trade_one_observation"));
        snapshot.insert(QStringLiteral("rawTickExecutionBasis"),
                        QStringLiteral("next_observed_trade"));
    }
    return snapshot;
}

bool CustomBacktestStrategy::supportsProvidedRawTicks() const
{
    return m_inputMode == CustomBacktestInputMode::RawTicks
        && m_plan.signalPriceMode == CustomSignalPriceMode::NextOpen
        && m_plan.crossIndicators.isEmpty()
        && m_plan.selection.count == 0
        && m_plan.universeSymbols.isEmpty();
}

void CustomBacktestStrategy::onTick(const Tick &tick)
{
    if (!supportsProvidedRawTicks() || !tick.timestamp.isValid()
        || !std::isfinite(tick.price) || tick.price <= 0.0
        || !std::isfinite(tick.volume) || tick.volume < 0.0) return;

    static const QTimeZone shanghaiTimeZone("Asia/Shanghai");
    const QDate tradingDate = tick.timestamp.toTimeZone(shanghaiTimeZone).date();
    auto emitTickSignal = [&](SignalSide side, const QString &reason) {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = QStringLiteral("自定义策略 %1 v%2 · %3（下一笔 Tick 成交口径）")
            .arg(m_plan.name).arg(m_plan.version).arg(reason);
        emit signalGenerated(signal);
    };

    // 先在当前成交上执行上一笔已形成的意图，再把当前成交放入指标窗口。
    if (m_tickPendingEntry) {
        m_tickPendingEntry = false;
        m_hasPosition = true;
        m_entryReferencePrice = tick.price;
        m_entryTradingDate = tradingDate;
        m_holdingBars = 0;
        emitTickSignal(SignalSide::Buy,
                       QStringLiteral("进入条件于 %1 成立")
                           .arg(m_tickEntryDecisionTime.toString(Qt::ISODateWithMs)));
        m_tickEntryDecisionTime = {};
    } else if (!m_tickPendingExitReason.isEmpty()
               && tradingDate > m_entryTradingDate) {
        const QString reason = QStringLiteral("%1；信号于 %2 成立")
            .arg(m_tickPendingExitReason,
                 m_tickExitDecisionTime.toString(Qt::ISODateWithMs));
        m_hasPosition = false;
        m_entryReferencePrice = 0.0;
        m_entryTradingDate = {};
        m_holdingBars = 0;
        m_tickPendingExitReason.clear();
        m_tickExitDecisionTime = {};
        emitTickSignal(SignalSide::Sell, reason);
    }

    Candle observation;
    observation.instrument = tick.instrument;
    observation.startTime = tick.timestamp;
    observation.periodSeconds = 0;
    observation.open = observation.high = observation.low = observation.close = tick.price;
    observation.volume = tick.volume;
    observation.directionalVolumeKnown = tick.rawTradeDirectionKnown;
    if (tick.rawTradeDirectionKnown) {
        if (tick.rawTradeDirection == RawTradeDirection::Buy)
            observation.buyVolume = tick.volume;
        else if (tick.rawTradeDirection == RawTradeDirection::Sell)
            observation.sellVolume = tick.volume;
        else observation.neutralVolume = tick.volume;
    }
    observation.source = QStringLiteral("raw_trade_tick_observation_v1");
    m_indicators.append(observation);
    const std::optional<bool> entry = evaluateCondition(m_plan.entryCondition, observation);
    const std::optional<bool> exit = evaluateCondition(m_plan.exitCondition, observation);
    if (m_indicators.barCount() < minimumWarmupBars()) return;

    if (!m_hasPosition) {
        if (entry.value_or(false)) {
            m_tickPendingEntry = true;
            m_tickEntryDecisionTime = tick.timestamp;
        }
        return;
    }

    ++m_holdingBars;
    if (!m_tickPendingExitReason.isEmpty()) return;
    if (m_plan.rules.stopLossPercent > 0.0
        && tick.price <= m_entryReferencePrice
            * (1.0 - m_plan.rules.stopLossPercent / 100.0)) {
        m_tickPendingExitReason = QStringLiteral("止损规则触发");
    } else if (m_plan.rules.takeProfitPercent > 0.0
               && tick.price >= m_entryReferencePrice
                   * (1.0 + m_plan.rules.takeProfitPercent / 100.0)) {
        m_tickPendingExitReason = QStringLiteral("止盈规则触发");
    } else if (m_plan.rules.maximumHoldingBars > 0
               && m_holdingBars >= m_plan.rules.maximumHoldingBars) {
        m_tickPendingExitReason = QStringLiteral("最大持仓 Tick 规则触发");
    } else if (exit.value_or(false)) {
        m_tickPendingExitReason = QStringLiteral("退出条件成立");
    }
    if (!m_tickPendingExitReason.isEmpty()) m_tickExitDecisionTime = tick.timestamp;
}

CandleExecutionContext CustomBacktestStrategy::candleExecutionContext() const
{
    return m_candleExecutionContext;
}

void CustomBacktestStrategy::onCandle(const Candle &candle)
{
    if (m_inputMode != CustomBacktestInputMode::Candles) return;
    m_indicators.append(candle);
    const std::optional<bool> entry = evaluateCondition(m_plan.entryCondition, candle);
    const std::optional<bool> exit = evaluateCondition(m_plan.exitCondition, candle);
    if (m_indicators.barCount() < minimumWarmupBars()
        || !std::isfinite(candle.close) || candle.close <= 0.0) {
        return;
    }

    static const QTimeZone shanghaiTimeZone("Asia/Shanghai");
    const QDate tradingDate = candle.startTime.toTimeZone(shanghaiTimeZone).date();
    auto emitSignal = [&](SignalSide side, const QString &reason, double price,
                          bool nextOpen = false) {
        m_candleExecutionContext = {
            nextOpen ? CandleExecutionBasis::NextOpen : CandleExecutionBasis::Close,
            nextOpen ? m_pendingEntryDecisionTime : candle.startTime};
        TradeSignal signal;
        signal.instrument = candle.instrument;
        signal.timestamp = candle.startTime;
        signal.side = side;
        signal.price = price;
        signal.volume = candle.volume;
        signal.reason = QStringLiteral("自定义策略 %1 v%2 · %3")
            .arg(m_plan.name).arg(m_plan.version).arg(reason);
        emit signalGenerated(signal);
    };

    if (m_pendingEntry) {
        m_pendingEntry = false;
        m_hasPosition = true;
        m_entryReferencePrice = candle.open;
        m_entryTradingDate = tradingDate;
        m_holdingBars = 0;
        m_pendingExitReason.clear();
        emitSignal(SignalSide::Buy,
                   QStringLiteral("进入条件成立（下一根开盘价成交）"), candle.open, true);
    }

    if (!m_hasPosition) {
        if (!entry.value_or(false)) return;
        if (m_plan.signalPriceMode == CustomSignalPriceMode::NextOpen) {
            m_pendingEntry = true;
            m_pendingEntryDecisionTime = candle.startTime;
            return;
        }
        m_hasPosition = true;
        m_entryReferencePrice = candle.close;
        m_entryTradingDate = tradingDate;
        m_holdingBars = 0;
        m_pendingExitReason.clear();
        emitSignal(SignalSide::Buy, QStringLiteral("进入条件成立"), candle.close);
        return;
    }

    ++m_holdingBars;
    if (!m_pendingExitReason.isEmpty() && tradingDate > m_entryTradingDate) {
        const QString reason = m_pendingExitReason
            + QStringLiteral("（T+1 延迟执行）");
        emitSignal(SignalSide::Sell, reason, candle.close);
        m_hasPosition = false;
        m_entryReferencePrice = 0.0;
        m_entryTradingDate = {};
        m_holdingBars = 0;
        m_pendingExitReason.clear();
        return;
    }

    QString exitReason;
    if (m_plan.rules.stopLossPercent > 0.0
        && candle.close <= m_entryReferencePrice
            * (1.0 - m_plan.rules.stopLossPercent / 100.0)) {
        exitReason = QStringLiteral("止损规则触发");
    } else if (m_plan.rules.takeProfitPercent > 0.0
               && candle.close >= m_entryReferencePrice
                   * (1.0 + m_plan.rules.takeProfitPercent / 100.0)) {
        exitReason = QStringLiteral("止盈规则触发");
    } else if (m_plan.rules.maximumHoldingBars > 0
               && m_holdingBars >= m_plan.rules.maximumHoldingBars) {
        exitReason = QStringLiteral("最大持仓 Bar 规则触发");
    } else if (exit.value_or(false)) {
        exitReason = QStringLiteral("退出条件成立");
    }
    if (exitReason.isEmpty()) return;

    if (m_plan.rules.tPlusOne && tradingDate <= m_entryTradingDate) {
        if (m_pendingExitReason.isEmpty()) m_pendingExitReason = exitReason;
        return;
    }

    emitSignal(SignalSide::Sell, exitReason, candle.close);
    m_hasPosition = false;
    m_entryReferencePrice = 0.0;
    m_entryTradingDate = {};
    m_holdingBars = 0;
    m_pendingExitReason.clear();
}

void CustomBacktestStrategy::onExecutionRejected(const TradeSignal &signal)
{
    if (signal.side != SignalSide::Buy) return;
    m_hasPosition = false;
    m_entryReferencePrice = 0.0;
    m_entryTradingDate = {};
    m_holdingBars = 0;
    m_tickPendingExitReason.clear();
    m_tickExitDecisionTime = {};
}

void CustomBacktestStrategy::reset()
{
    m_indicators.reset();
    m_previousLeft.fill(std::nullopt);
    m_previousRight.fill(std::nullopt);
    m_hasPosition = false;
    m_pendingEntry = false;
    m_pendingEntryDecisionTime = {};
    m_candleExecutionContext = {};
    m_entryReferencePrice = 0.0;
    m_entryTradingDate = {};
    m_holdingBars = 0;
    m_pendingExitReason.clear();
    m_tickPendingEntry = false;
    m_tickEntryDecisionTime = {};
    m_tickPendingExitReason.clear();
    m_tickExitDecisionTime = {};
}

std::optional<double> CustomBacktestStrategy::evaluateOperand(
    int row, const Candle &candle) const
{
    if (row < 0 || row >= m_plan.operands.size()) return std::nullopt;
    const CompiledCustomOperand &operand = m_plan.operands.at(row);
    if (operand.kind == CustomOperandKind::Constant) return operand.constant;
    if (operand.kind == CustomOperandKind::Indicator) {
        return m_indicators.value(operand.indicatorIndex);
    }
    return fieldValue(operand.kind, candle);
}

std::optional<bool> CustomBacktestStrategy::evaluateCondition(int row, const Candle &candle)
{
    if (row < 0 || row >= m_plan.conditions.size()) return std::nullopt;
    const CompiledCustomCondition &condition = m_plan.conditions.at(row);
    if (condition.op == CustomConditionOp::And || condition.op == CustomConditionOp::Or) {
        bool sawMissing = false;
        bool result = condition.op == CustomConditionOp::And;
        for (int child : condition.children) {
            const std::optional<bool> value = evaluateCondition(child, candle);
            if (!value) sawMissing = true;
            else if (condition.op == CustomConditionOp::And) result = result && *value;
            else result = result || *value;
        }
        if (condition.op == CustomConditionOp::And && !result) return false;
        if (condition.op == CustomConditionOp::Or && result) return true;
        return sawMissing ? std::optional<bool>{} : std::optional<bool>{result};
    }
    if (condition.op == CustomConditionOp::Not) {
        const std::optional<bool> value = evaluateCondition(condition.children.value(0, -1), candle);
        return value ? std::optional<bool>{!*value} : std::optional<bool>{};
    }

    const std::optional<double> left = evaluateOperand(condition.leftOperand, candle);
    const std::optional<double> right = evaluateOperand(condition.rightOperand, candle);
    if (condition.op == CustomConditionOp::CrossesAbove
        || condition.op == CustomConditionOp::CrossesBelow) {
        const std::optional<double> previousLeft = m_previousLeft.at(row);
        const std::optional<double> previousRight = m_previousRight.at(row);
        m_previousLeft[row] = left;
        m_previousRight[row] = right;
        if (!left || !right || !previousLeft || !previousRight) return std::nullopt;
        if (condition.op == CustomConditionOp::CrossesAbove) {
            return *previousLeft <= *previousRight && *left > *right;
        }
        return *previousLeft >= *previousRight && *left < *right;
    }
    if (!left || !right) return std::nullopt;
    switch (condition.op) {
    case CustomConditionOp::Greater: return *left > *right;
    case CustomConditionOp::GreaterOrEqual: return *left >= *right;
    case CustomConditionOp::Less: return *left < *right;
    case CustomConditionOp::LessOrEqual: return *left <= *right;
    case CustomConditionOp::Equal: {
        const double scale = std::max({1.0, std::abs(*left), std::abs(*right)});
        return std::abs(*left - *right) <= scale * 1.0e-9;
    }
    default: return std::nullopt;
    }
}

} // namespace pp
