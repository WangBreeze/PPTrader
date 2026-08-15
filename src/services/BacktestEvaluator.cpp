#include "services/BacktestEvaluator.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

namespace {

PerformanceSummary calculateSummary(const BacktestConfig &config,
                                    const QVector<BacktestTrade> &trades,
                                    const QVector<EquityPoint> &equityCurve,
                                    double cash,
                                    double totalCommission)
{
    PerformanceSummary summary;
    summary.initialCapital = config.initialCapital;
    summary.finalEquity = equityCurve.isEmpty() ? cash : equityCurve.last().equity;
    summary.totalReturnPercent = summary.initialCapital > 0.0
        ? (summary.finalEquity / summary.initialCapital - 1.0) * 100.0 : 0.0;
    summary.totalCommission = totalCommission;
    summary.totalTrades = trades.size();

    for (const BacktestTrade &trade : trades) {
        if (trade.netPnl > 0.0) {
            ++summary.winningTrades;
        } else if (trade.netPnl < 0.0) {
            ++summary.losingTrades;
        }
    }
    summary.winRatePercent = summary.totalTrades > 0
        ? static_cast<double>(summary.winningTrades) / summary.totalTrades * 100.0 : 0.0;

    QVector<double> returns;
    returns.reserve(std::max(0, static_cast<int>(equityCurve.size()) - 1));
    for (int index = 1; index < equityCurve.size(); ++index) {
        const double previous = equityCurve.at(index - 1).equity;
        const double current = equityCurve.at(index).equity;
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
            const double periodsPerYear = config.periodSeconds >= 86400
                ? 252.0 : 252.0 * 4.0 * 60.0 * 60.0 / std::max(1, config.periodSeconds);
            summary.sharpeRatio = mean / deviation * std::sqrt(periodsPerYear);
        }
    }

    for (const EquityPoint &point : equityCurve) {
        summary.maxDrawdownPercent = std::max(summary.maxDrawdownPercent, point.drawdownPercent);
    }
    return summary;
}

} // namespace

PerformanceSummary BacktestEvaluator::run(const BacktestConfig &config,
                                          QVector<Candle> candles,
                                          BacktestStrategy &strategy)
{
    // 过滤 + 排序（与 BacktestEngine::start 一致）。
    candles.erase(std::remove_if(candles.begin(), candles.end(),
        [&config](const Candle &candle) {
            return candle.instrument.symbol != config.instrument.symbol
                || candle.close <= 0.0 || !candle.startTime.isValid();
        }), candles.end());
    std::sort(candles.begin(), candles.end(), [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    });
    if (config.sampleFrom.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) { return candle.startTime < config.sampleFrom; }),
            candles.end());
    }
    if (config.sampleTo.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) { return candle.startTime > config.sampleTo; }),
            candles.end());
    }

    double cash = config.initialCapital;
    double peakEquity = config.initialCapital;
    double totalCommission = 0.0;
    BacktestPosition position;
    QVector<BacktestTrade> trades;
    QVector<EquityPoint> equityCurve;

    strategy.reset();

    const int lotSize = std::max(1, config.lotSize);
    const Candle *currentCandle = nullptr;

    auto openPosition = [&](const TradeSignal &signal) {
        if (!currentCandle) return;
        const Candle &candle = *currentCandle;
        const double fillPrice = candle.close * (1.0 + std::max(0.0, config.slippageRate));
        const double fixedAmount = strategy.positionAmount();
        double budget = 0.0;
        if (fixedAmount > 0.0) {
            budget = std::min(fixedAmount, cash);
        } else {
            budget = cash * std::clamp(strategy.positionFraction(), 0.01, 1.0);
        }
        const double maxPercent = strategy.maxPositionPercent();
        if (maxPercent > 0.0) {
            budget = std::min(budget, cash * maxPercent / 100.0);
        }
        const double affordable = budget / (fillPrice * (1.0 + std::max(0.0, config.commissionRate)));
        const double quantity = std::floor(affordable / lotSize) * lotSize;
        if (quantity <= 0.0) return;

        const double commission = fillPrice * quantity * std::max(0.0, config.commissionRate);
        cash -= fillPrice * quantity + commission;
        totalCommission += commission;
        position.quantity = quantity;
        position.averagePrice = fillPrice;
        position.entryCommission = commission;
        position.entryTime = candle.startTime;
        position.entryReason = signal.reason;
    };

    auto closePosition = [&](const TradeSignal &signal) {
        if (!currentCandle) return;
        const Candle &candle = *currentCandle;
        const double fillPrice = candle.close * (1.0 - std::max(0.0, config.slippageRate));
        const double quantity = position.quantity;
        const double exitCommission = fillPrice * quantity * std::max(0.0, config.commissionRate);
        const double grossPnl = (fillPrice - position.averagePrice) * quantity;
        const double commission = position.entryCommission + exitCommission;
        cash += fillPrice * quantity - exitCommission;
        totalCommission += exitCommission;

        BacktestTrade trade;
        trade.instrument = config.instrument;
        trade.entryTime = position.entryTime;
        trade.exitTime = candle.startTime;
        trade.entryPrice = position.averagePrice;
        trade.exitPrice = fillPrice;
        trade.quantity = quantity;
        trade.grossPnl = grossPnl;
        trade.netPnl = grossPnl - commission;
        trade.commission = commission;
        trade.returnPercent = position.averagePrice > 0.0
            ? (fillPrice / position.averagePrice - 1.0) * 100.0 : 0.0;
        trade.entryReason = position.entryReason;
        trade.exitReason = signal.reason;
        trades.append(trade);
        position = {};
    };

    auto appendEquityPoint = [&](const Candle &candle) {
        EquityPoint point;
        point.timestamp = candle.startTime;
        point.cash = cash;
        point.positionValue = position.quantity * candle.close;
        point.equity = point.cash + point.positionValue;
        peakEquity = std::max(peakEquity, point.equity);
        point.drawdownPercent = peakEquity > 0.0
            ? (peakEquity - point.equity) / peakEquity * 100.0 : 0.0;
        equityCurve.append(point);
    };

    // 捕获策略信号（onCandle 内同步 emit）。
    QObject::connect(&strategy, &Strategy::signalGenerated, [&](const TradeSignal &signal) {
        if (signal.side == SignalSide::Buy && position.quantity <= 0.0) {
            openPosition(signal);
        } else if (signal.side == SignalSide::Sell && position.quantity > 0.0) {
            closePosition(signal);
        }
    });

    for (const Candle &candle : std::as_const(candles)) {
        currentCandle = &candle;
        strategy.onCandle(candle);
        appendEquityPoint(candle);
    }
    currentCandle = nullptr;

    // 回测结束平仓。
    if (position.quantity > 0.0 && !candles.isEmpty()) {
        const Candle &lastCandle = candles.last();
        currentCandle = &lastCandle;
        TradeSignal signal;
        signal.instrument = config.instrument;
        signal.timestamp = lastCandle.startTime;
        signal.side = SignalSide::Sell;
        signal.price = lastCandle.close;
        signal.reason = QStringLiteral("回测结束平仓");
        closePosition(signal);
        currentCandle = nullptr;
        if (!equityCurve.isEmpty()) {
            equityCurve.last().cash = cash;
            equityCurve.last().positionValue = 0.0;
            equityCurve.last().equity = cash;
        }
    }

    return calculateSummary(config, trades, equityCurve, cash, totalCommission);
}

} // namespace pp
