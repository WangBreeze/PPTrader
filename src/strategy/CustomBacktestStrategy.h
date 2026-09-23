#pragma once

#include "strategy/BacktestStrategy.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QDate>

#include <optional>

namespace pp {

class CustomBacktestStrategy final : public BacktestStrategy {
public:
    explicit CustomBacktestStrategy(CompiledCustomStrategy plan, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    int minimumWarmupBars() const override;
    double positionFraction() const override;
    double positionAmount() const override;
    double maxPositionPercent() const override;
    int strategyVersion() const override;
    QVariantMap normalizedParameters() const override;
    QVariantMap ruleSnapshot() const override;
    CandleExecutionContext candleExecutionContext() const override;
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;
    void reset() override;

private:
    std::optional<double> evaluateOperand(int row, const Candle &candle) const;
    std::optional<bool> evaluateCondition(int row, const Candle &candle);

    CompiledCustomStrategy m_plan;
    CustomIndicatorRuntime m_indicators;
    QVector<std::optional<double>> m_previousLeft;
    QVector<std::optional<double>> m_previousRight;
    bool m_hasPosition = false;
    bool m_pendingEntry = false;
    QDateTime m_pendingEntryDecisionTime;
    CandleExecutionContext m_candleExecutionContext;
    double m_entryReferencePrice = 0.0;
    QDate m_entryTradingDate;
    int m_holdingBars = 0;
    QString m_pendingExitReason;
};

} // namespace pp
