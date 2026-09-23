#pragma once

#include "domain/PortfolioTypes.h"
#include "domain/RawTradeTypes.h"
#include "strategy/BacktestStrategyRegistry.h"

#include <QObject>
#include <QHash>
#include <QSet>

#include <map>
#include <memory>

namespace pp {

struct BoardPortfolioConfig {
    double initialCapital = 1000000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
    double maximumTotalPositionFraction = 1.0;
    int maximumHoldingSymbols = 20;
    int lotSize = 100;
    QSet<QString> eligibleEntries; // symbol|yyyyMMdd|strategyId；空集合仅供合成测试使用
};

struct BoardPortfolioReport {
    double initialCapital = 0.0;
    double cash = 0.0;
    double equity = 0.0;
    double peakEquity = 0.0;
    double maximumDrawdownPercent = 0.0;
    qint64 observations = 0;
    int observedSymbols = 0;
    int buySignals = 0;
    int sellSignals = 0;
    int buys = 0;
    int sells = 0;
    int rejectedBuys = 0;
    int rejectedSells = 0;
    int rejectedForCash = 0;
    int rejectedForHoldingLimit = 0;
    int rejectedForPositionLimit = 0;
    int ignoredOutOfScopeSignals = 0;
    int entryDayOutcomeKnownSignals = 0;
    int entryDayClosedBoardSignals = 0;
    int maximumObservedHoldingSymbols = 0;
    int staleOpenPositions = 0;
    QVector<PortfolioPosition> openPositions;
    QVector<PortfolioTrade> trades;
    QDateTime firstObservation;
    QDateTime lastObservation;
};

// 二板与三板是同一组合策略的两个信号设置；各标的独立维护指标状态，
// 所有信号按原始 Tick 时间序列进入同一个现金、仓位和 T+1 账簿。
// 不在结束时伪造卖出：未退出仓位按最后真实成交价估值并单列。
class BoardPortfolioBacktestEngine final : public QObject {
    Q_OBJECT
public:
    explicit BoardPortfolioBacktestEngine(const BoardPortfolioConfig &config,
                                          QObject *parent = nullptr);
    bool accept(const RawTradeTick &raw, QString *error = nullptr);
    BoardPortfolioReport finish();

private:
    struct SymbolStrategies {
        std::unique_ptr<BacktestStrategy> second;
        std::unique_ptr<BacktestStrategy> third;
    };
    struct Holding {
        Instrument instrument;
        QString strategyId;
        double quantity = 0.0;
        double entryPrice = 0.0;
        double lastPrice = 0.0;
        QDateTime lastPriceTime;
        double entryCommission = 0.0;
        QDateTime entryTime;
    };

    SymbolStrategies *strategiesFor(const QString &symbol);
    void handleSignal(BacktestStrategy &strategy, const TradeSignal &signal);
    void reject(BacktestStrategy &strategy, const TradeSignal &signal,
                bool cash, bool holdingLimit, bool positionLimit);
    double currentEquity() const;
    double currentPositionValue() const;
    void observeEquity();

    BoardPortfolioConfig m_config;
    BacktestStrategyRegistry m_registry;
    std::map<QString, std::unique_ptr<SymbolStrategies>> m_strategies;
    std::map<QString, Holding> m_holdings;
    QSet<QString> m_observedSymbols;
    BoardPortfolioReport m_report;
    RawTradeTick m_currentRaw;
    bool m_accepting = false;
    bool m_finished = false;
};

} // namespace pp
