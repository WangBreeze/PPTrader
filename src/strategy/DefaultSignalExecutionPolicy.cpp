#include "strategy/DefaultSignalExecutionPolicy.h"

namespace pp {

namespace {

RiskCheckResult denied(const QString &code, const QString &message)
{
    return {false, code, message};
}

} // namespace

RiskCheckResult DefaultSignalExecutionPolicy::evaluate(
    const StrategyExecutionConfig &config, const TradeSignal &signal,
    const AccountSnapshot &account, const std::optional<Position> &position,
    const QDateTime &lastSubmissionTime, const QDateTime &evaluationTime) const
{
    if (!config.enabled || config.mode == StrategyExecutionMode::Disabled) {
        return denied(QStringLiteral("EXECUTION_DISABLED"), QStringLiteral("策略自动执行未启用"));
    }
    if (config.strategyId.isEmpty() || signal.instrument.symbol.isEmpty()
        || signal.price <= 0.0 || !evaluationTime.isValid()) {
        return denied(QStringLiteral("INVALID_SIGNAL"), QStringLiteral("策略信号参数无效"));
    }
    if (config.cooldownSeconds > 0 && lastSubmissionTime.isValid()
        && lastSubmissionTime.secsTo(evaluationTime) < config.cooldownSeconds) {
        return denied(QStringLiteral("EXECUTION_COOLDOWN"), QStringLiteral("策略执行仍处于冷却期"));
    }
    if (account.totalEquity <= 0.0 || account.cash < 0.0) {
        return denied(QStringLiteral("INVALID_ACCOUNT"), QStringLiteral("账户状态不允许执行策略信号"));
    }
    if (signal.side == SignalSide::Sell
        && (!position || position->availableQuantity <= 0.0)) {
        return denied(QStringLiteral("NO_SELLABLE_POSITION"), QStringLiteral("没有可卖持仓"));
    }
    return {true, QStringLiteral("OK"), QStringLiteral("策略执行规则检查通过")};
}

} // namespace pp
