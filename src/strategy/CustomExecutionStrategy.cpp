#include "strategy/CustomExecutionStrategy.h"

#include <QTimeZone>

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

CustomExecutionStrategy::CustomExecutionStrategy(CompiledCustomStrategy plan,
                                                 const Instrument &instrument,
                                                 QObject *parent)
    : Strategy(parent)
    , m_plan(std::move(plan))
    , m_instrument(instrument)
    , m_indicators(m_plan.indicators)
    , m_previousLeft(m_plan.conditions.size())
    , m_previousRight(m_plan.conditions.size())
{
}

QString CustomExecutionStrategy::id() const
{
    return m_plan.strategyId;
}

QString CustomExecutionStrategy::displayName() const
{
    return QStringLiteral("%1 v%2").arg(m_plan.name).arg(m_plan.version);
}

QStringList CustomExecutionStrategy::subscribedSymbols() const
{
    return m_instrument.symbol.isEmpty() ? QStringList{} : QStringList{m_instrument.symbol};
}

void CustomExecutionStrategy::onTick(const Tick &tick)
{
    Q_UNUSED(tick)
    // 该策略按 Bar 条件驱动；实时 Bar 由 Candle 流（dispatchCandle）投递。
}

void CustomExecutionStrategy::onCandle(const Candle &candle)
{
    QMutexLocker locker(&m_mutex);
    processCandle(candle, false, true);
}

void CustomExecutionStrategy::evaluateAccountCandle(
    const Candle &candle, const Position &position, const QDate &lastBuyDay, bool allowSignals)
{
    QMutexLocker locker(&m_mutex);
    const bool heldBefore = m_hasPosition;
    m_hasPosition = position.quantity > 0.000001;
    m_entryReferencePrice = m_hasPosition ? position.averageCost : 0.0;
    m_entryTradingDate = m_hasPosition ? lastBuyDay : QDate{};
    m_pendingEntry = false;
    if (!m_hasPosition || !heldBefore) {
        m_holdingBars = 0;
        m_pendingExitReason.clear();
    }
    processCandle(candle, true, allowSignals);
}

void CustomExecutionStrategy::processCandle(const Candle &candle, bool accountDriven,
                                          bool allowSignals)
{

    m_indicators.append(candle);
    const std::optional<bool> entry = evaluateCondition(m_plan.entryCondition, candle);
    const std::optional<bool> exit = evaluateCondition(m_plan.exitCondition, candle);
    // 补回分钟和等待成交期间仍推进持仓时长，但不能补发历史交易意图。
    if (accountDriven && m_hasPosition) ++m_holdingBars;
    if (!allowSignals || m_indicators.barCount() < m_plan.minimumWarmupBars
        || !std::isfinite(candle.close) || candle.close <= 0.0) {
        return;
    }

    static const QTimeZone shanghaiTimeZone("Asia/Shanghai");
    const QDate tradingDate = candle.startTime.toTimeZone(shanghaiTimeZone).date();
    auto emitSignal = [&](SignalSide side, const QString &reason, double price) {
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
                   QStringLiteral("进入条件成立（下一根开盘价成交）"), candle.open);
    }

    if (!m_hasPosition) {
        if (!entry.value_or(false)) return;
        if (accountDriven) {
            emitSignal(SignalSide::Buy, QStringLiteral("进入条件成立，等待后续报价模拟撮合"),
                       candle.close);
            return;
        }
        if (m_plan.signalPriceMode == CustomSignalPriceMode::NextOpen) {
            m_pendingEntry = true;
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

    if (!accountDriven) ++m_holdingBars;
    if (!m_pendingExitReason.isEmpty() && tradingDate > m_entryTradingDate) {
        emitSignal(SignalSide::Sell,
                   m_pendingExitReason + QStringLiteral("（T+1 延迟执行）"), candle.close);
        if (accountDriven) return;
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
    if (accountDriven) return;
    m_hasPosition = false;
    m_entryReferencePrice = 0.0;
    m_entryTradingDate = {};
    m_holdingBars = 0;
    m_pendingExitReason.clear();
}

std::optional<double> CustomExecutionStrategy::evaluateOperand(
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

std::optional<bool> CustomExecutionStrategy::evaluateCondition(int row, const Candle &candle)
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
