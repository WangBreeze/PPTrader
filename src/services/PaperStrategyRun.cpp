#include "services/PaperStrategyRun.h"
#include "services/MarketSession.h"
#include "services/SimulatedBrokerGateway.h"
#include "services/TradingAccountService.h"
#include "strategy/CustomExecutionStrategy.h"

#include <QTimeZone>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <map>

namespace pp {
namespace {
QDate localDay(const QDateTime &value)
{
    return value.toTimeZone(QTimeZone("Asia/Shanghai")).date();
}
bool positive(double value) { return std::isfinite(value) && value > 0; }
}

struct PaperStrategyRun::State {
    QString runId;
    CompiledCustomStrategy plan;
    struct Stock {
        Instrument instrument;
        CustomExecutionStrategy strategy;
        QDateTime lastBar;
        QDateTime lastQuote;
        QDateTime decisionAt;
        QDate lastBuyDay;
        std::optional<TradeSignal> pending;
        int bars = 0;
        Stock(const CompiledCustomStrategy &plan, const Instrument &value)
            : instrument(value), strategy(plan, value) {}
    };
    std::map<QString, std::unique_ptr<Stock>> stocks;
    PaperStrategyConfig config;
    std::unique_ptr<SimulatedBrokerGateway> ownedBroker;
    std::unique_ptr<TradingAccountService> ownedAccount;
    SimulatedBrokerGateway &broker;
    TradingAccountService &account;
    QDateTime lastBar;
    QDateTime lastQuote;
    int bars = 0;
    int signalCount = 0;
    int rejected = 0;
    int rejectedBuyCash = 0;
    int rejectedBuyPositionLimit = 0;
    int rejectedSellTPlusOne = 0;
    int rejectedSellNoPosition = 0;
    int rejectedSubmit = 0;
    int cancelled = 0;
    bool stopped = false;
    QString error;
    QVariantList events;

    State(const QString &id, const CompiledCustomStrategy &compiled,
          const PaperStrategyConfig &settings, TradingAccountService *sharedAccount,
          SimulatedBrokerGateway *sharedBroker)
        : runId(id), plan(compiled), config(settings),
          ownedBroker(sharedBroker ? nullptr : std::make_unique<SimulatedBrokerGateway>(settings.commissionRate, settings.slippageRate)),
          ownedAccount(sharedAccount ? nullptr : std::make_unique<TradingAccountService>(ownedBroker.get(), settings.initialCapital, settings.commissionRate, settings.slippageRate)),
          broker(sharedBroker ? *sharedBroker : *ownedBroker),
          account(sharedAccount ? *sharedAccount : *ownedAccount) {}

    Position position(const QString &symbol) const {
        for (const auto &value : account.positions())
            if (value.instrument.symbol == symbol) return value;
        return {};
    }
    bool activeOrder(const QString &symbol = {}) const {
        for (const auto &order : account.orders())
            if (isActiveOrder(order.status) && (symbol.isEmpty() || order.request.instrument.symbol == symbol)) return true;
        return false;
    }
    void event(const QString &kind, const QString &message, const QDateTime &time) {
        events.append(QVariantMap{{"kind", kind}, {"message", message},
                                  {"atUtc", time.toUTC().toString(Qt::ISODateWithMs)}});
        if (events.size() > 200) events.removeFirst();
    }
};

PaperStrategyRun::PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                                 const Instrument &instrument, const PaperStrategyConfig &config,
                                 QObject *parent)
    : PaperStrategyRun(runId, plan, QVector<Instrument>{instrument}, config, parent) {}

PaperStrategyRun::PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                                 const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                                 QObject *parent)
    : PaperStrategyRun(runId, plan, instruments, config, parent, nullptr, nullptr) {}

PaperStrategyRun::PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                                 const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                                 TradingAccountService &account, SimulatedBrokerGateway &broker, QObject *parent)
    : PaperStrategyRun(runId, plan, instruments, config, parent, &account, &broker) {}

PaperStrategyRun::PaperStrategyRun(const QString &runId, const CompiledCustomStrategy &plan,
                                 const QVector<Instrument> &instruments, const PaperStrategyConfig &config,
                                 QObject *parent, TradingAccountService *account, SimulatedBrokerGateway *broker)
    : QObject(parent), m_state(std::make_unique<State>(runId, plan, config, account, broker))
{
    auto &s = *m_state;
    if (runId.isEmpty() || instruments.isEmpty() || instruments.size() > 6000
        || !positive(config.initialCapital) || !std::isfinite(config.commissionRate)
        || config.commissionRate < 0 || config.commissionRate > 0.1
        || !std::isfinite(config.slippageRate) || config.slippageRate < 0
        || config.slippageRate > 0.1 || config.maximumOrders < 1
        || config.maximumOrders > 2000 || config.maximumFillSharesPerQuote < 0
        || config.maximumFillSharesPerQuote % 100 != 0 || !plan.crossIndicators.isEmpty()
        || !positive(config.maximumTotalPositionFraction) || config.maximumTotalPositionFraction > 1
        || config.maximumHoldingSymbols < 1 || config.maximumHoldingSymbols > 6000) {
        s.error = QStringLiteral("模拟仓配置无效；仅支持最多 6000 标的 A 股时序指标策略");
        s.stopped = true;
        return;
    }
    for (const auto &instrument : instruments) {
        if (instrument.symbol.isEmpty() || instrument.market != Market::AShare
            || s.stocks.contains(instrument.symbol)) {
            s.error = QStringLiteral("模拟股票池包含重复或无效 A 股标的"); s.stopped = true; return;
        }
        auto stock = std::make_unique<State::Stock>(plan, instrument);
        connect(&stock->strategy, &Strategy::signalGenerated, this,
                [this, symbol = instrument.symbol](const TradeSignal &signal) {
            auto &state = *m_state;
            auto &leg = *state.stocks.at(symbol);
            leg.pending = signal;
            ++state.signalCount;
            state.event(QStringLiteral("signal"), symbol + QStringLiteral(" ") + signal.reason, leg.decisionAt);
        });
        s.stocks.emplace(instrument.symbol, std::move(stock));
    }
    if (config.historicalSourceStart.isValid()
        && !s.account.initializeHistoricalClock(config.historicalSourceStart)) {
        s.error = QStringLiteral("历史源时钟初始化被拒绝；不能重置既有账户交易事实或改写回放起点");
        s.stopped = true;
        return;
    }
    for (const auto &fill : s.account.executions()) {
        const auto found = s.stocks.find(fill.instrument.symbol);
        if (fill.side == OrderSide::Buy && found != s.stocks.end())
            found->second->lastBuyDay = std::max(found->second->lastBuyDay, localDay(fill.timestamp));
    }
    s.broker.login();
    connect(&s.account, &TradingAccountService::operationFailed, this, [this](const QString &error) {
        m_state->error = error;
    });
    connect(&s.account, &TradingAccountService::executionAdded, this, [this](const Execution &fill) {
        auto &state = *m_state;
        const auto found = state.stocks.find(fill.instrument.symbol);
        if (found == state.stocks.end()) return;
        if (fill.side == OrderSide::Buy) found->second->lastBuyDay = localDay(fill.timestamp);
        state.event(QStringLiteral("fill"),
                    QStringLiteral("%1 %2 %3 股 @ %4；费用 %5")
                        .arg(fill.instrument.symbol, orderSideName(fill.side)).arg(fill.quantity).arg(fill.price)
                        .arg(fill.commission), fill.timestamp);
    });
}
PaperStrategyRun::~PaperStrategyRun() { stop(); }

bool PaperStrategyRun::acceptBar(const Candle &candle, const QDateTime &observedAt, bool warmup)
{
    auto &s = *m_state;
    const auto found = s.stocks.find(candle.instrument.symbol);
    if (s.stopped || found == s.stocks.end()) return false;
    auto &leg = *found->second;
    if (candle.periodSeconds != 60 || candle.instrument.market != leg.instrument.market
        || !candle.startTime.isValid() || !observedAt.isValid()
        || candle.startTime.addSecs(180) > observedAt
        || (leg.lastBar.isValid() && candle.startTime <= leg.lastBar)
        || !positive(candle.open) || !positive(candle.high) || !positive(candle.low)
        || !positive(candle.close) || !std::isfinite(candle.volume) || candle.volume < 0
        || candle.high < std::max(candle.open, candle.close)
        || candle.low > std::min(candle.open, candle.close) || candle.high < candle.low) return false;
    if (leg.bars >= 10000) {
        s.error = QStringLiteral("达到 10000 根分钟计算预算，停止并保留账户；请导出本次实验");
        stop();
        return false;
    }
    s.lastBar = std::max(s.lastBar, candle.startTime);
    leg.lastBar = candle.startTime;
    ++leg.bars;
    ++s.bars;
    if (!leg.pending && !s.activeOrder(leg.instrument.symbol)) leg.decisionAt = observedAt;
    leg.strategy.evaluateAccountCandle(candle, s.position(leg.instrument.symbol), leg.lastBuyDay,
                                      !warmup && !leg.pending && !s.activeOrder(leg.instrument.symbol));
    return true;
}

bool PaperStrategyRun::acceptQuote(const Tick &input, const QDateTime &receivedAt)
{
    auto &s = *m_state;
    const auto found = s.stocks.find(input.instrument.symbol);
    if (s.stopped || found == s.stocks.end()) return false;
    auto &leg = *found->second;
    if (input.instrument.market != leg.instrument.market
        || !positive(input.price) || !input.timestamp.isValid() || !receivedAt.isValid()
        || !std::isfinite(input.bid) || !std::isfinite(input.ask) || input.bid < 0 || input.ask < 0
        || (input.bid > 0 && input.ask > 0 && input.bid > input.ask)
        || input.timestamp > receivedAt || input.timestamp.secsTo(receivedAt) > 15
        || (leg.lastQuote.isValid() && input.timestamp <= leg.lastQuote)
        || !MarketSession::isOpen(leg.instrument, input.timestamp)) return false;
    if (leg.pending && input.timestamp <= leg.decisionAt) return false;
    s.lastQuote = std::max(s.lastQuote, input.timestamp);
    leg.lastQuote = input.timestamp;
    Tick quote = input;
    quote.instrument = leg.instrument;
    // 公开快照 volume 是累计量，绝不把它当本次可成交量。
    quote.volume = s.config.maximumFillSharesPerQuote;
    s.account.updateMarketPrice(quote);
    if (!leg.pending) return true;
    const TradeSignal signal = *leg.pending;
    leg.pending.reset();
    if (leg.decisionAt.secsTo(receivedAt) > 90 || localDay(leg.decisionAt) != localDay(receivedAt)) {
        ++s.cancelled;
        s.error = QStringLiteral("信号等待新报价超时，未成交");
        return true;
    }
    if (s.account.orders().size() >= s.config.maximumOrders) {
        s.error = QStringLiteral("达到本次模拟订单预算，已停止，不自动扩大试验");
        stop();
        return true;
    }
    const bool buy = signal.side == SignalSide::Buy;
    const Position held = s.position(leg.instrument.symbol);
    const double reference = buy ? (positive(quote.ask) ? quote.ask : quote.price)
                                 : (positive(quote.bid) ? quote.bid : quote.price);
    const auto account = s.account.snapshot();
    double budget = s.plan.rules.positionAmount > 0 ? s.plan.rules.positionAmount
        : account.totalEquity * s.plan.rules.positionFraction;
    if (s.plan.rules.maxPositionPercent > 0)
        budget = std::min(budget, account.totalEquity * s.plan.rules.maxPositionPercent / 100.0);
    // 总仓位预算包含未成交买单占款；股票池大小不等于持仓数量。
    budget = std::min(budget, std::max(0.0, account.totalEquity * s.config.maximumTotalPositionFraction
                                      - account.marketValue - account.frozenCash));
    QSet<QString> occupied;
    for (const auto &position : s.account.positions())
        if (position.quantity > 0) occupied.insert(position.instrument.symbol);
    for (const auto &order : s.account.orders())
        if (isActiveOrder(order.status) && order.request.side == OrderSide::Buy)
            occupied.insert(order.request.instrument.symbol);
    if (!occupied.contains(leg.instrument.symbol) && occupied.size() >= s.config.maximumHoldingSymbols)
        budget = 0;
    const double availableCash = std::max(0.0, account.cash - account.frozenCash);
    const double oneLotCost = reference * (1 + s.config.slippageRate)
        * (1 + s.config.commissionRate) * 100;
    const bool holdingLimitReached = !occupied.contains(leg.instrument.symbol)
        && occupied.size() >= s.config.maximumHoldingSymbols;
    const double quantity = buy
        ? std::floor(std::min(budget, availableCash) /
            (reference * (1 + s.config.slippageRate) * (1 + s.config.commissionRate)) / 100) * 100
        : held.availableQuantity;
    if (!positive(quantity) || (!buy && leg.lastBuyDay >= localDay(input.timestamp))) {
        ++s.rejected;
        if (buy) {
            if (availableCash + 0.000001 < oneLotCost) {
                ++s.rejectedBuyCash;
                s.error = QStringLiteral("策略未提交：买入可用资金不足一手");
            } else {
                ++s.rejectedBuyPositionLimit;
                s.error = holdingLimitReached
                    ? QStringLiteral("策略未提交：已达到持股数量上限")
                    : QStringLiteral("策略未提交：已达到策略单股或总仓位上限");
            }
        } else if (leg.lastBuyDay >= localDay(input.timestamp)) {
            ++s.rejectedSellTPlusOne;
            s.error = QStringLiteral("策略未提交：A 股 T+1 当日买入不可售");
        } else {
            ++s.rejectedSellNoPosition;
            s.error = QStringLiteral("策略未提交：没有可售持仓");
        }
        s.event(QStringLiteral("blocked"), leg.instrument.symbol + QStringLiteral(" ") + s.error,
                receivedAt);
        return true;
    }
    OrderRequest request;
    request.clientOrderId = s.runId + QStringLiteral("-%1").arg(s.signalCount);
    request.instrument = leg.instrument;
    request.side = buy ? OrderSide::Buy : OrderSide::Sell;
    request.quantity = quantity;
    request.referencePrice = reference;
    request.createdAt = input.timestamp;
    request.expiresAt = input.timestamp.addSecs(60);
    const QString orderId = s.account.submitOrder(request);
    if (orderId.isEmpty()) {
        ++s.rejected;
        ++s.rejectedSubmit;
        if (s.error.isEmpty()) s.error = QStringLiteral("模拟拒绝：订单提交失败");
        s.event(QStringLiteral("rejected"), leg.instrument.symbol + QStringLiteral(" ") + s.error,
                receivedAt);
    }
    return true;
}

void PaperStrategyRun::stop()
{
    auto &s = *m_state;
    s.stopped = true;
    for (auto &[symbol, leg] : s.stocks)
        if (leg->pending) { ++s.cancelled; leg->pending.reset(); }
    if (s.ownedBroker) s.broker.logout();
    else {
        // 只撤销本策略的余单，不清空共享账户、其他委托或已有持仓。
        for (const auto &order : s.account.orders())
            if (isActiveOrder(order.status) && order.request.clientOrderId.startsWith(s.runId + '-'))
                s.account.cancelOrder(order.orderId);
    }
}

bool PaperStrategyRun::isStopped() const { return m_state->stopped; }

QVariantMap PaperStrategyRun::snapshot() const
{
    const auto &s = *m_state;
    const auto account = s.account.snapshot();
    const auto executions = s.account.executions();
    const int buyFills = int(std::count_if(executions.cbegin(), executions.cend(),
        [](const Execution &fill) { return fill.side == OrderSide::Buy; }));
    const int sellFills = int(executions.size()) - buyFills;
    constexpr int maximumDisplayedExecutions = 100;
    QVariantList executionRecords;
    const int firstDisplayedExecution = std::max(0, int(executions.size()) - maximumDisplayedExecutions);
    executionRecords.reserve(int(executions.size()) - firstDisplayedExecution);
    for (int index = firstDisplayedExecution; index < executions.size(); ++index) {
        const auto &fill = executions.at(index);
        executionRecords.append(QVariantMap{
            {"recordNumber", index + 1},
            {"executionId", fill.executionId},
            {"orderId", fill.orderId},
            {"symbol", fill.instrument.symbol},
            {"name", fill.instrument.name},
            {"side", orderSideName(fill.side)},
            {"sideCode", fill.side == OrderSide::Buy ? QStringLiteral("buy") : QStringLiteral("sell")},
            {"quantity", fill.quantity},
            {"price", fill.price},
            {"grossAmount", fill.quantity * fill.price},
            {"commission", fill.commission},
            {"timestampUtc", fill.timestamp.toUTC().toString(Qt::ISODateWithMs)},
            {"timestampLocal", fill.timestamp.toTimeZone(QTimeZone("Asia/Shanghai"))
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))}});
    }
    QVariantList stockStates;
    QVariantList positionRecords;
    QStringList symbols;
    double quantity = 0, available = 0, averageCost = 0;
    int holdingSymbolCount = 0;
    int sellableSymbolCount = 0;
    bool pending = false;
    for (const auto &[symbol, leg] : s.stocks) {
        symbols.append(symbol);
        const Position held = s.position(symbol);
        const QDate day = localDay(leg->lastQuote);
        const double sellable = !day.isValid() || leg->lastBuyDay >= day ? 0.0 : held.availableQuantity;
        quantity += held.quantity; available += sellable;
        if (held.quantity > 0.000001) ++holdingSymbolCount;
        if (sellable > 0.000001) ++sellableSymbolCount;
        if (s.stocks.size() == 1) averageCost = held.averageCost;
        pending = pending || leg->pending.has_value();
        const QVariantMap stockState{{"symbol", symbol}, {"name", leg->instrument.name},
            {"quantity", held.quantity}, {"availableQuantity", sellable},
            {"averageCost", held.averageCost}, {"marketPrice", held.marketPrice},
            {"marketValue", held.marketValue}, {"unrealizedPnl", held.unrealizedPnl},
            {"barCount", leg->bars},
            {"lastBarUtc", leg->lastBar.toString(Qt::ISODateWithMs)},
            {"lastQuoteUtc", leg->lastQuote.toString(Qt::ISODateWithMs)}};
        stockStates.append(stockState);
        if (held.quantity > 0.000001) positionRecords.append(stockState);
    }
    const double totalPnl = account.totalEquity - account.initialEquity;
    const double accountingIdentityDelta = totalPnl - account.realizedPnl - account.unrealizedPnl;
    const double returnPercent = account.initialEquity > 0.0
        ? totalPnl / account.initialEquity * 100.0 : 0.0;
    const double positionPercent = account.totalEquity > 0.0
        ? account.marketValue / account.totalEquity * 100.0 : 0.0;
    const int blockedSignalCount = s.rejectedBuyCash + s.rejectedBuyPositionLimit
        + s.rejectedSellTPlusOne + s.rejectedSellNoPosition;
    return {{"runId", s.runId}, {"strategyId", s.plan.strategyId}, {"version", s.plan.version},
            {"name", s.plan.name}, {"symbol", symbols.join(',')}, {"symbols", symbols},
            {"stockStates", stockStates}, {"positionRecords", positionRecords},
            {"stopped", s.stopped}, {"frozenCash", account.frozenCash},
            {"accountScope", s.ownedAccount ? QStringLiteral("isolated-test") : QStringLiteral("workstation-single-account")},
            {"maximumTotalPositionFraction", s.config.maximumTotalPositionFraction},
            {"maximumHoldingSymbols", s.config.maximumHoldingSymbols},
            {"cash", account.cash}, {"initialCapital", account.initialEquity},
            {"equity", account.totalEquity}, {"marketValue", account.marketValue},
            {"totalPnl", totalPnl}, {"returnPercent", returnPercent},
            {"accountingIdentityDelta", accountingIdentityDelta},
            {"accountingIdentityValid", std::abs(accountingIdentityDelta) < 0.005},
            {"positionPercent", positionPercent}, {"holdingSymbolCount", holdingSymbolCount},
            {"sellableSymbolCount", sellableSymbolCount}, {"quantity", quantity},
            {"availableQuantity", available},
            {"averageCost", averageCost}, {"commission", account.totalCommission},
            {"realizedPnl", account.realizedPnl}, {"unrealizedPnl", account.unrealizedPnl},
            {"signalCount", s.signalCount}, {"rejectedCount", s.rejected},
            {"rejectedBuyCashCount", s.rejectedBuyCash},
            {"rejectedBuyPositionLimitCount", s.rejectedBuyPositionLimit},
            {"rejectedSellTPlusOneCount", s.rejectedSellTPlusOne},
            {"rejectedSellNoPositionCount", s.rejectedSellNoPosition},
            {"rejectedSubmitCount", s.rejectedSubmit},
            {"blockedSignalCount", blockedSignalCount},
            {"orderRejectedCount", s.rejectedSubmit}, {"cancelledCount", s.cancelled},
            {"barCount", s.bars}, {"warmupBars", s.plan.minimumWarmupBars},
            {"orderCount", s.account.orders().size()}, {"fillCount", executions.size()},
            {"buyFillCount", buyFills}, {"sellFillCount", sellFills},
            {"executionRecordCount", executions.size()},
            {"displayedExecutionRecordCount", executionRecords.size()},
            {"executionRecordsTruncated", executions.size() > maximumDisplayedExecutions},
            {"executionRecords", executionRecords},
            {"waitingForQuote", pending}, {"activeOrder", s.activeOrder()},
            {"lastBarUtc", s.lastBar.toString(Qt::ISODateWithMs)},
            {"lastQuoteUtc", s.lastQuote.toString(Qt::ISODateWithMs)},
            {"error", s.error}, {"events", s.events},
            {"fillModel", QStringLiteral("信号之后新报价；固定费用/滑点；非真实盘口撮合")}};
}
} // namespace pp
