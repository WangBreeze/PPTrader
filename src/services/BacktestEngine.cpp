#include "services/BacktestEngine.h"

#include "services/BacktestDataVersion.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

BacktestEngine::BacktestEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BacktestTrade>();
    qRegisterMetaType<EquityPoint>();
    qRegisterMetaType<PerformanceSummary>();

    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &BacktestEngine::processNextCandle);
}

bool BacktestEngine::start(const BacktestConfig &config, QVector<Candle> candles,
                           std::unique_ptr<BacktestStrategy> providedStrategy)
{
    if (isRunning()) {
        cancel();
    }

    candles.erase(std::remove_if(candles.begin(), candles.end(), [&config](const Candle &candle) {
        return candle.instrument.symbol != config.instrument.symbol
            || candle.close <= 0.0
            || !candle.startTime.isValid();
    }), candles.end());
    std::sort(candles.begin(), candles.end(), [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    });
    if (config.sampleFrom.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) {
                return candle.startTime < config.sampleFrom;
            }), candles.end());
    }
    if (config.sampleTo.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) {
                return candle.startTime > config.sampleTo;
            }), candles.end());
    }

    QString strategyError;
    std::unique_ptr<BacktestStrategy> strategy = std::move(providedStrategy);
    QVariantMap normalizedParameters;
    if (strategy) {
        if (!config.strategyId.startsWith(QStringLiteral("custom_"))
            || strategy->id() != config.strategyId
            || strategy->strategyVersion() != config.strategyVersion
            || strategy->normalizedParameters() != config.strategyParameters
            || strategy->ruleSnapshot() != config.ruleSnapshot
            || config.strategyVersion < 1 || config.ruleSnapshot.isEmpty()) {
            emit errorOccurred(QStringLiteral("自定义回测策略身份或规则快照无效"));
            return false;
        }
        normalizedParameters = config.strategyParameters;
    } else {
        strategy = m_registry.create(config.strategyId, config.strategyParameters, &strategyError);
        if (!strategy) {
            emit errorOccurred(strategyError);
            return false;
        }
        if (!m_registry.normalizeParameters(config.strategyId, config.strategyParameters,
                                            &normalizedParameters, &strategyError)) {
            emit errorOccurred(strategyError);
            return false;
        }
    }
    if (config.instrument.market != Market::AShare) {
        emit errorOccurred(QStringLiteral("当前内置回测策略仅支持 A 股"));
        return false;
    }

    if (config.instrument.symbol.isEmpty() || config.initialCapital <= 0.0
        || candles.size() < strategy->minimumWarmupBars()) {
        emit errorOccurred(QStringLiteral("回测配置无效或历史 K 线不足"));
        return false;
    }

    m_result = {};
    m_result.config = config;
    m_result.config.strategyParameters = normalizedParameters;
    m_result.config.dataVersion = computeBacktestDataVersion(
        candles, config.instrument.symbol, config.periodSeconds);
    m_result.config.actualBars = candles.size();
    m_result.config.from = candles.first().startTime;
    m_result.config.to = candles.last().startTime;
    m_candles = std::move(candles);
    m_position = {};
    m_currentSignal = {};
    m_hasCurrentSignal = false;
    m_index = 0;
    m_cash = config.initialCapital;
    m_peakEquity = config.initialCapital;
    m_totalCommission = 0.0;
    if (m_strategy) disconnect(m_strategy.get(), nullptr, this, nullptr);
    m_strategy = std::move(strategy);
    m_strategy->reset();
    connect(m_strategy.get(), &Strategy::signalGenerated,
            this, &BacktestEngine::handleStrategySignal, Qt::DirectConnection);

    emit progressChanged(0.0);
    m_timer.start();
    return true;
}

void BacktestEngine::cancel()
{
    if (!m_timer.isActive()) {
        return;
    }
    m_timer.stop();
    emit cancelled();
}

bool BacktestEngine::isRunning() const
{
    return m_timer.isActive();
}

double BacktestEngine::progress() const
{
    return m_candles.isEmpty() ? 0.0 : static_cast<double>(m_index) / m_candles.size();
}

const BacktestResult &BacktestEngine::result() const
{
    return m_result;
}

void BacktestEngine::processNextCandle()
{
    if (m_index >= m_candles.size()) {
        finishBacktest();
        return;
    }

    const Candle &candle = m_candles.at(m_index);
    m_hasCurrentSignal = false;
    m_strategy->onCandle(candle);
    appendEquityPoint(candle);

    CandlePoint point;
    point.candle = candle;
    point.hasSignal = m_hasCurrentSignal;
    if (m_hasCurrentSignal) {
        point.signal = m_currentSignal;
    }
    emit pointReady(point);

    ++m_index;
    emit progressChanged(progress());
    if (m_index >= m_candles.size()) {
        finishBacktest();
    }
}

void BacktestEngine::handleStrategySignal(const TradeSignal &signal)
{
    if (m_index < 0 || m_index >= m_candles.size()) {
        return;
    }

    const Candle &candle = m_candles.at(m_index);
    m_currentSignal = signal;
    m_hasCurrentSignal = true;
    if (signal.side == SignalSide::Buy && m_position.quantity <= 0.0) {
        openPosition(signal, candle);
    } else if (signal.side == SignalSide::Sell && m_position.quantity > 0.0) {
        closePosition(signal, candle);
    }
}

void BacktestEngine::openPosition(const TradeSignal &signal, const Candle &candle)
{
    const BacktestConfig &config = m_result.config;
    const double fillPrice = candle.close * (1.0 + std::max(0.0, config.slippageRate));
    const int lotSize = std::max(1, config.lotSize);
    const double fixedAmount = m_strategy->positionAmount();
    double budget = 0.0;
    if (fixedAmount > 0.0) {
        budget = std::min(fixedAmount, m_cash);
    } else {
        const double fraction = std::clamp(m_strategy->positionFraction(), 0.01, 1.0);
        budget = m_cash * fraction;
    }
    const double maxPercent = m_strategy->maxPositionPercent();
    if (maxPercent > 0.0) {
        budget = std::min(budget, m_cash * maxPercent / 100.0);
    }
    const double affordable = budget / (fillPrice * (1.0 + std::max(0.0, config.commissionRate)));
    const double quantity = std::floor(affordable / lotSize) * lotSize;
    if (quantity <= 0.0) {
        return;
    }

    const double commission = fillPrice * quantity * std::max(0.0, config.commissionRate);
    m_cash -= fillPrice * quantity + commission;
    m_totalCommission += commission;
    m_position.quantity = quantity;
    m_position.averagePrice = fillPrice;
    m_position.entryCommission = commission;
    m_position.entryTime = candle.startTime;
    m_position.entryReason = signal.reason;
}

void BacktestEngine::closePosition(const TradeSignal &signal, const Candle &candle)
{
    const BacktestConfig &config = m_result.config;
    const double fillPrice = candle.close * (1.0 - std::max(0.0, config.slippageRate));
    const double quantity = m_position.quantity;
    const double exitCommission = fillPrice * quantity * std::max(0.0, config.commissionRate);
    const double grossPnl = (fillPrice - m_position.averagePrice) * quantity;
    const double commission = m_position.entryCommission + exitCommission;

    m_cash += fillPrice * quantity - exitCommission;
    m_totalCommission += exitCommission;

    BacktestTrade trade;
    trade.instrument = config.instrument;
    trade.entryTime = m_position.entryTime;
    trade.exitTime = candle.startTime;
    trade.entryPrice = m_position.averagePrice;
    trade.exitPrice = fillPrice;
    trade.quantity = quantity;
    trade.grossPnl = grossPnl;
    trade.netPnl = grossPnl - commission;
    trade.commission = commission;
    trade.returnPercent = m_position.averagePrice > 0.0
        ? (fillPrice / m_position.averagePrice - 1.0) * 100.0 : 0.0;
    trade.entryReason = m_position.entryReason;
    trade.exitReason = signal.reason;
    m_result.trades.append(trade);
    emit tradeCompleted(trade);

    m_position = {};
}

void BacktestEngine::appendEquityPoint(const Candle &candle)
{
    EquityPoint point;
    point.timestamp = candle.startTime;
    point.cash = m_cash;
    point.positionValue = m_position.quantity * candle.close;
    point.equity = point.cash + point.positionValue;
    m_peakEquity = std::max(m_peakEquity, point.equity);
    point.drawdownPercent = m_peakEquity > 0.0
        ? (m_peakEquity - point.equity) / m_peakEquity * 100.0 : 0.0;
    m_result.equityCurve.append(point);
    emit equityPointReady(point);
}

void BacktestEngine::finishBacktest()
{
    if (!m_timer.isActive() && m_index < m_candles.size()) {
        return;
    }
    m_timer.stop();

    if (m_position.quantity > 0.0 && !m_candles.isEmpty()) {
        const Candle &lastCandle = m_candles.last();
        TradeSignal signal;
        signal.instrument = m_result.config.instrument;
        signal.timestamp = lastCandle.startTime;
        signal.side = SignalSide::Sell;
        signal.price = lastCandle.close;
        signal.reason = QStringLiteral("回测结束平仓");
        closePosition(signal, lastCandle);

        if (!m_result.equityCurve.isEmpty()) {
            EquityPoint &lastPoint = m_result.equityCurve.last();
            lastPoint.cash = m_cash;
            lastPoint.positionValue = 0.0;
            lastPoint.equity = m_cash;
        }
    }

    m_result.summary = calculateSummary();
    m_result.candles = m_candles;
    emit progressChanged(1.0);
    emit finished(m_result.summary);
}

PerformanceSummary BacktestEngine::calculateSummary() const
{
    PerformanceSummary summary;
    summary.initialCapital = m_result.config.initialCapital;
    summary.finalEquity = m_result.equityCurve.isEmpty()
        ? m_cash : m_result.equityCurve.last().equity;
    summary.totalReturnPercent = summary.initialCapital > 0.0
        ? (summary.finalEquity / summary.initialCapital - 1.0) * 100.0 : 0.0;
    summary.totalCommission = m_totalCommission;
    summary.totalTrades = m_result.trades.size();

    for (const BacktestTrade &trade : m_result.trades) {
        if (trade.netPnl > 0.0) {
            ++summary.winningTrades;
        } else if (trade.netPnl < 0.0) {
            ++summary.losingTrades;
        }
    }
    summary.winRatePercent = summary.totalTrades > 0
        ? static_cast<double>(summary.winningTrades) / summary.totalTrades * 100.0 : 0.0;

    QVector<double> returns;
    for (int index = 1; index < m_result.equityCurve.size(); ++index) {
        const double previous = m_result.equityCurve.at(index - 1).equity;
        const double current = m_result.equityCurve.at(index).equity;
        if (previous > 0.0) {
            returns.append(current / previous - 1.0);
        }
    }
    if (returns.size() > 1) {
        const double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
        double squaredDeviation = 0.0;
        for (double value : returns) {
            squaredDeviation += (value - mean) * (value - mean);
        }
        const double deviation = std::sqrt(squaredDeviation / (returns.size() - 1));
        if (deviation > 0.0) {
            const double periodsPerYear = m_result.config.periodSeconds >= 86400
                ? 252.0 : 252.0 * 4.0 * 60.0 * 60.0 / std::max(1, m_result.config.periodSeconds);
            summary.sharpeRatio = mean / deviation * std::sqrt(periodsPerYear);
        }
    }

    for (const EquityPoint &point : m_result.equityCurve) {
        summary.maxDrawdownPercent = std::max(summary.maxDrawdownPercent, point.drawdownPercent);
    }
    return summary;
}

} // namespace pp
