#include "services/BoardPortfolioBacktestEngine.h"

#include "services/BacktestExecutionPolicy.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace pp {

BoardPortfolioBacktestEngine::BoardPortfolioBacktestEngine(
    const BoardPortfolioConfig &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    m_report.initialCapital = config.initialCapital;
    m_report.cash = config.initialCapital;
    m_report.equity = config.initialCapital;
    m_report.peakEquity = config.initialCapital;
}

BoardPortfolioBacktestEngine::SymbolStrategies *
BoardPortfolioBacktestEngine::strategiesFor(const QString &symbol)
{
    const auto found = m_strategies.find(symbol);
    if (found != m_strategies.end()) return found->second.get();

    auto pair = std::make_unique<SymbolStrategies>();
    QString error;
    pair->second = m_registry.create(QStringLiteral("tick_second_board"), {}, &error);
    pair->third = m_registry.create(QStringLiteral("tick_third_board"), {}, &error);
    if (!pair->second || !pair->third) return nullptr;
    pair->second->reset();
    pair->third->reset();
    for (BacktestStrategy *strategy : {pair->second.get(), pair->third.get()}) {
        connect(strategy, &Strategy::signalGenerated, this,
                [this, strategy](const TradeSignal &signal) {
                    handleSignal(*strategy, signal);
                }, Qt::DirectConnection);
    }
    auto *result = pair.get();
    m_strategies.emplace(symbol, std::move(pair));
    return result;
}

bool BoardPortfolioBacktestEngine::accept(const RawTradeTick &raw, QString *error)
{
    if (m_finished || m_config.initialCapital <= 0.0
        || m_config.lotSize <= 0 || m_config.maximumHoldingSymbols <= 0
        || m_config.maximumTotalPositionFraction <= 0.0
        || m_config.maximumTotalPositionFraction > 1.0
        || !raw.timestamp.isValid() || raw.instrument.market != Market::AShare
        || raw.instrument.symbol.isEmpty() || !std::isfinite(raw.price)
        || raw.price <= 0.0 || !std::isfinite(raw.volumeShares)
        || raw.volumeShares < 0.0
        || (m_report.lastObservation.isValid()
            && raw.timestamp < m_report.lastObservation)) {
        if (error) *error = QStringLiteral("共享资金原始 Tick 输入或配置无效／时间倒序");
        return false;
    }
    SymbolStrategies *strategies = strategiesFor(raw.instrument.symbol);
    if (!strategies) {
        if (error) *error = QStringLiteral("二板／三板策略初始化失败");
        return false;
    }
    if (!m_report.firstObservation.isValid()) m_report.firstObservation = raw.timestamp;
    m_report.lastObservation = raw.timestamp;
    ++m_report.observations;
    m_observedSymbols.insert(raw.instrument.symbol);
    auto holding = m_holdings.find(raw.instrument.symbol);
    if (holding != m_holdings.end()) {
        holding->second.lastPrice = raw.price;
        holding->second.lastPriceTime = raw.timestamp;
    }

    m_currentRaw = raw;
    m_accepting = true;
    Tick tick;
    tick.instrument = raw.instrument;
    tick.timestamp = raw.timestamp;
    tick.price = raw.price;
    tick.volume = raw.volumeShares;
    tick.amount = raw.amount;
    tick.rawTradeDirection = raw.direction;
    tick.rawTradeDirectionKnown = isKnownRawTradeDirection(raw.direction);
    strategies->second->onTick(tick);
    strategies->third->onTick(tick);
    m_accepting = false;
    observeEquity();
    return true;
}

void BoardPortfolioBacktestEngine::reject(BacktestStrategy &strategy,
                                           const TradeSignal &signal, bool cash,
                                           bool holdingLimit, bool positionLimit)
{
    if (signal.side == SignalSide::Buy) {
        ++m_report.rejectedBuys;
        if (cash) ++m_report.rejectedForCash;
        if (holdingLimit) ++m_report.rejectedForHoldingLimit;
        if (positionLimit) ++m_report.rejectedForPositionLimit;
    } else {
        ++m_report.rejectedSells;
    }
    strategy.onExecutionRejected(signal);
}

void BoardPortfolioBacktestEngine::handleSignal(BacktestStrategy &strategy,
                                                  const TradeSignal &signal)
{
    if (!m_accepting || signal.instrument.symbol != m_currentRaw.instrument.symbol
        || signal.timestamp != m_currentRaw.timestamp) return;
    const QString symbol = signal.instrument.symbol;
    if (signal.side == SignalSide::Buy) {
        if (!m_config.eligibleEntries.isEmpty()) {
            static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
            const QString key = symbol + QLatin1Char('|')
                + signal.timestamp.toTimeZone(shanghai).date().toString(
                    QStringLiteral("yyyyMMdd"))
                + QLatin1Char('|') + strategy.id();
            if (!m_config.eligibleEntries.contains(key)) {
                ++m_report.ignoredOutOfScopeSignals;
                strategy.onExecutionRejected(signal);
                return;
            }
        }
        ++m_report.buySignals;
        const bool alreadyHeld = m_holdings.contains(symbol);
        const bool holdingLimit = !alreadyHeld
            && m_holdings.size() >= size_t(m_config.maximumHoldingSymbols);
        const double fillPrice = m_currentRaw.price
            * (1.0 + std::max(0.0, m_config.slippageRate));
        const double budget = strategy.positionAmount();
        double quantity = std::floor(
            budget / (fillPrice * (1.0 + std::max(0.0, m_config.commissionRate)))
            / m_config.lotSize) * m_config.lotSize;
        if (strategy.targetPositionShares() > 0) {
            quantity = std::min(quantity, double(strategy.targetPositionShares()));
        }
        const double commission = fillPrice * quantity
            * std::max(0.0, m_config.commissionRate);
        const double cost = fillPrice * quantity + commission;
        const bool cashLimit = cost > m_report.cash + 1e-8;
        const bool positionLimit = currentPositionValue() + fillPrice * quantity
            > currentEquity() * m_config.maximumTotalPositionFraction + 1e-8;
        if (alreadyHeld || holdingLimit || quantity <= 0.0 || cashLimit
            || positionLimit) {
            reject(strategy, signal, cashLimit, holdingLimit, positionLimit);
            return;
        }
        m_report.cash -= cost;
        Holding position;
        position.instrument = signal.instrument;
        position.strategyId = strategy.id();
        position.quantity = quantity;
        position.entryPrice = fillPrice;
        position.lastPrice = m_currentRaw.price;
        position.lastPriceTime = signal.timestamp;
        position.entryCommission = commission;
        position.entryTime = signal.timestamp;
        m_holdings.emplace(symbol, std::move(position));
        PortfolioTrade trade;
        trade.instrument = signal.instrument;
        trade.timestamp = signal.timestamp;
        trade.side = QStringLiteral("buy");
        trade.quantity = quantity;
        trade.price = fillPrice;
        trade.commission = commission;
        trade.amount = cost;
        trade.reason = signal.reason;
        m_report.trades.append(trade);
        ++m_report.buys;
        m_report.maximumObservedHoldingSymbols = std::max(
            m_report.maximumObservedHoldingSymbols, int(m_holdings.size()));
        return;
    }

    ++m_report.sellSignals;
    auto holding = m_holdings.find(symbol);
    if (holding == m_holdings.end() || holding->second.strategyId != strategy.id()
        || !backtestPositionSellable(Market::AShare,
                                    holding->second.entryTime, signal.timestamp)) {
        reject(strategy, signal, false, false, false);
        return;
    }
    const double fillPrice = m_currentRaw.price
        * (1.0 - std::max(0.0, m_config.slippageRate));
    const double proceeds = fillPrice * holding->second.quantity;
    const double commission = proceeds * std::max(0.0, m_config.commissionRate);
    m_report.cash += proceeds - commission;
    PortfolioTrade trade;
    trade.instrument = holding->second.instrument;
    trade.timestamp = signal.timestamp;
    trade.side = QStringLiteral("sell");
    trade.quantity = holding->second.quantity;
    trade.price = fillPrice;
    trade.commission = commission;
    trade.amount = proceeds - commission;
    trade.reason = signal.reason;
    m_report.trades.append(trade);
    m_holdings.erase(holding);
    ++m_report.sells;
}

double BoardPortfolioBacktestEngine::currentPositionValue() const
{
    double value = 0.0;
    for (const auto &entry : m_holdings) {
        value += entry.second.quantity * entry.second.lastPrice;
    }
    return value;
}

double BoardPortfolioBacktestEngine::currentEquity() const
{
    return m_report.cash + currentPositionValue();
}

void BoardPortfolioBacktestEngine::observeEquity()
{
    m_report.equity = currentEquity();
    m_report.peakEquity = std::max(m_report.peakEquity, m_report.equity);
    if (m_report.peakEquity > 0.0) {
        m_report.maximumDrawdownPercent = std::max(
            m_report.maximumDrawdownPercent,
            (m_report.peakEquity - m_report.equity)
                / m_report.peakEquity * 100.0);
    }
}

BoardPortfolioReport BoardPortfolioBacktestEngine::finish()
{
    if (m_finished) return m_report;
    m_finished = true;
    m_report.observedSymbols = m_observedSymbols.size();
    for (const auto &entry : m_strategies) {
        for (const BacktestStrategy *strategy : {entry.second->second.get(),
                                                entry.second->third.get()}) {
            const QVariantMap facts = strategy->evaluationFacts();
            m_report.entryDayOutcomeKnownSignals +=
                facts.value(QStringLiteral("entryDayOutcomeKnownSignals")).toInt();
            m_report.entryDayClosedBoardSignals +=
                facts.value(QStringLiteral("entryDayClosedBoardSignals")).toInt();
        }
    }
    for (const auto &entry : m_holdings) {
        PortfolioPosition position;
        position.instrument = entry.second.instrument;
        position.quantity = entry.second.quantity;
        position.lastPrice = entry.second.lastPrice;
        position.marketValue = position.quantity * position.lastPrice;
        position.weight = m_report.equity > 0.0
            ? position.marketValue / m_report.equity : 0.0;
        position.priceStale = entry.second.lastPriceTime.toUTC().date()
            < m_report.lastObservation.toUTC().date();
        if (position.priceStale) {
            position.marketStatus = QStringLiteral("missing_unknown");
            ++m_report.staleOpenPositions;
        }
        m_report.openPositions.append(position);
    }
    observeEquity();
    return m_report;
}

} // namespace pp
