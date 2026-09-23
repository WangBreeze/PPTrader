#pragma once

#include "strategy/Strategy.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"
#include "domain/TradingTypes.h"

#include <QDate>
#include <QMutex>

#include <optional>

namespace pp {

// 通过校验的自定义策略的实时/模拟执行运行时：消费 Candle 序列，按 DSL 规则
// 评估入场/出场、止损/止盈、最大持仓 Bar 与 T+1，产出 TradeSignal。
// 仓位金额与冷却/审批等执行参数走 StrategyExecutionConfig，不进 DSL。
// onCandle 由 StrategyEngine 线程池并发驱动，故内部状态用互斥锁保护。
class CustomExecutionStrategy final : public Strategy {
    Q_OBJECT

public:
    explicit CustomExecutionStrategy(CompiledCustomStrategy plan,
                                     const Instrument &instrument,
                                     QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QStringList subscribedSymbols() const override;
    // 模拟仓同步调用：持仓由成交账簿决定；预热不产生订单意图。
    void evaluateAccountCandle(const Candle &candle, const Position &position,
                              const QDate &lastBuyDay, bool allowSignals);

public slots:
    void onTick(const Tick &tick) override;
    void onCandle(const Candle &candle) override;

private:
    void processCandle(const Candle &candle, bool accountDriven, bool allowSignals);
    std::optional<double> evaluateOperand(int row, const Candle &candle) const;
    std::optional<bool> evaluateCondition(int row, const Candle &candle);

    CompiledCustomStrategy m_plan;
    Instrument m_instrument;
    CustomIndicatorRuntime m_indicators;
    QVector<std::optional<double>> m_previousLeft;
    QVector<std::optional<double>> m_previousRight;
    mutable QMutex m_mutex;
    bool m_hasPosition = false;
    bool m_pendingEntry = false;
    double m_entryReferencePrice = 0.0;
    QDate m_entryTradingDate;
    int m_holdingBars = 0;
    QString m_pendingExitReason;
};

} // namespace pp
