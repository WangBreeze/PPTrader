#include "services/CrossSectionalBacktestEngine.h"

#include "services/PortfolioExecutor.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

PortfolioResult CrossSectionalBacktestEngine::run(
    CrossSectionalCustomStrategy &strategy,
    const AlignedBarSeries &alignedSeries,
    const PortfolioDefinition &definition,
    QString *error)
{
    PortfolioResult result;
    result.definition = definition;
    result.periodSeconds = alignedSeries.periodSeconds();

    const QStringList symbols = strategy.plan().universeSymbols;
    if (symbols.isEmpty()) {
        if (error) *error = QStringLiteral("策略 universe 为空");
        return result;
    }

    strategy.reset();
    PortfolioExecutor::ExecutionState execState;
    execState.legs.resize(symbols.size());
    execState.cash = definition.initialCapital;
    for (int index = 0; index < symbols.size(); ++index) {
        execState.legs[index].instrument.symbol = symbols.at(index);
        execState.legs[index].weight = 0.0;
    }

    for (int frameIndex = 0; frameIndex < alignedSeries.frameCount(); ++frameIndex) {
        const AlignedBarFrame *frame = alignedSeries.frameAt(frameIndex);
        if (!frame) continue;
        const QDate day = frame->timestamp.date();
        const QDateTime barTime = frame->timestamp;

        // 更新每条腿价格（有 bar 的腿）；无 bar 的腿保持前值（前向填充，stale 由 lastPriceDay 判断）。
        for (int index = 0; index < symbols.size(); ++index) {
            execState.legs[index].boughtToday = false;
            if (frame->hasBar(symbols.at(index))) {
                const Candle *candle = frame->bar(symbols.at(index));
                execState.legs[index].lastPrice = candle->close;
                execState.legs[index].lastPriceDay = day;
            }
        }

        // 调策略，产出目标持仓权重。
        const CrossSectionalFrameResult frameResult = strategy.evaluateFrame(*frame);
        for (int index = 0; index < symbols.size(); ++index) {
            execState.legs[index].weight =
                frameResult.targetWeights.value(symbols.at(index), 0.0);
        }

        double equity = PortfolioExecutor::equity(execState);
        PortfolioExecutor::rebalance(execState, equity,
            definition.slippageRate, definition.commissionRate,
            definition.lotSize, definition.tPlusOne, barTime, result.trades);

        equity = PortfolioExecutor::equity(execState);
        PortfolioBar bar;
        bar.timestamp = barTime;
        bar.equity = equity;
        bar.cash = execState.cash;
        bar.positions = PortfolioExecutor::positions(execState, day, equity);
        result.equityCurve.append(bar);
    }

    if (result.equityCurve.isEmpty()) {
        if (error) *error = QStringLiteral("多股回测没有可计算的帧");
        return result;
    }

    PerformanceSummary &summary = result.summary;
    summary.initialCapital = definition.initialCapital;
    summary.finalEquity = result.equityCurve.last().equity;
    summary.totalReturnPercent = summary.initialCapital > 0.0
        ? (summary.finalEquity / summary.initialCapital - 1.0) * 100.0 : 0.0;
    summary.totalCommission = execState.totalCommission;
    summary.totalTrades = result.trades.size();
    double peak = summary.initialCapital;
    QVector<double> returns;
    for (const PortfolioBar &bar : result.equityCurve) {
        peak = std::max(peak, bar.equity);
        summary.maxDrawdownPercent = std::max(
            summary.maxDrawdownPercent,
            peak > 0.0 ? (peak - bar.equity) / peak * 100.0 : 0.0);
    }
    for (int index = 1; index < result.equityCurve.size(); ++index) {
        const double previous = result.equityCurve.at(index - 1).equity;
        const double current = result.equityCurve.at(index).equity;
        if (previous > 0.0) returns.append(current / previous - 1.0);
    }
    if (returns.size() > 1) {
        const double mean = std::accumulate(returns.cbegin(), returns.cend(), 0.0)
            / returns.size();
        double squaredDeviation = 0.0;
        for (double value : returns) {
            squaredDeviation += (value - mean) * (value - mean);
        }
        const double deviation = std::sqrt(squaredDeviation / (returns.size() - 1));
        if (deviation > 0.0) {
            summary.sharpeRatio = mean / deviation * std::sqrt(252.0);
        }
    }
    for (const QString &symbol : symbols) {
        result.datasetVersions.append(symbol);
    }
    if (error) error->clear();
    return result;
}

} // namespace pp
