#pragma once

#include "strategy/CustomStrategyCompiler.h"
#include "domain/TradingTypes.h"

#include <QObject>
#include <memory>

namespace pp {

class TradingAccountService;
class SimulatedBrokerGateway;

struct PaperStrategyConfig {
    double initialCapital = 100000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
    int maximumOrders = 2000;
    int maximumFillSharesPerQuote = 0; // 0：不模拟盘口深度，结果必须披露此假设。
    double maximumTotalPositionFraction = 1.0;
    int maximumHoldingSymbols = 6000;
    QDateTime historicalSourceStart; // 历史回放空账户的源时钟；事件保留原始时刻。
};

// 固定策略引用工作台账户；多标的共用账簿，各自维护指标和信号状态。
class PaperStrategyRun final : public QObject {
    Q_OBJECT
public:
    PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                     const Instrument &instrument, const PaperStrategyConfig &config,
                     QObject *parent = nullptr);
    PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                     const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                     QObject *parent = nullptr);
    PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                     const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                     TradingAccountService &account, SimulatedBrokerGateway &broker,
                     QObject *parent = nullptr);
    ~PaperStrategyRun() override;
    bool acceptBar(const Candle &candle, const QDateTime &observedAt, bool warmup = false);
    bool acceptQuote(const Tick &quote, const QDateTime &receivedAt);
    void stop();
    bool isStopped() const;
    QVariantMap snapshot() const;

private:
    PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                     const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                     QObject *parent, TradingAccountService *account, SimulatedBrokerGateway *broker);
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace pp
