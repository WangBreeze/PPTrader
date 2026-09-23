#include "services/CrossSectionalBacktestEngine.h"

#include "services/PortfolioCircuitBreaker.h"
#include "services/PortfolioExecutor.h"
#include "services/FactorResearchAnalyzer.h"
#include "services/FactorOutOfSampleAnalyzer.h"
#include "strategy/CrossSectionalIndicatorRuntime.h"

#include <QDate>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace pp {

namespace {

void setAllTargetWeightsToZero(PortfolioExecutor::ExecutionState &state)
{
    for (PortfolioExecutor::LegState &leg : state.legs) leg.weight = 0.0;
}

void markCircuitBreakerTrades(QVector<PortfolioTrade> &trades, int firstTrade,
                              PortfolioRiskSummary &summary)
{
    for (int index = firstTrade; index < trades.size(); ++index) {
        PortfolioTrade &trade = trades[index];
        if (trade.side != QStringLiteral("sell")) continue;
        trade.reason = QStringLiteral("组合净值/回撤熔断退出");
        ++summary.completedCircuitBreakerExitCount;
    }
}

} // namespace

PortfolioResult CrossSectionalBacktestEngine::run(
    CrossSectionalCustomStrategy &strategy,
    const AlignedBarSeries &alignedSeries,
    const PortfolioDefinition &definition,
    QString *error,
    const AShareCalendarSnapshot &calendar,
    const AShareIndustrySnapshot &industry)
{
    PortfolioResult result;
    result.definition = definition;
    result.periodSeconds = alignedSeries.periodSeconds();
    result.riskSummary.marketCalendarCoverageComplete =
        calendar.marketCoverageComplete;
    result.riskSummary.instrumentStatusCoverageComplete =
        calendar.instrumentCoverageComplete;
    result.riskSummary.calendarSnapshotVersion = calendar.snapshotVersion;

    const QStringList symbols = strategy.plan().universeSymbols;
    if (symbols.isEmpty()) {
        if (error) *error = QStringLiteral("策略 universe 为空");
        return result;
    }
    int factorExposureIndex = -1;
    if (definition.maximumAbsoluteFactorExposure > 0.0) {
        const QString alias = definition.factorExposureAlias.trimmed();
        for (int index = 0;
             index < strategy.plan().crossIndicators.size(); ++index) {
            if (strategy.plan().crossIndicators.at(index).alias == alias) {
                factorExposureIndex = index;
                break;
            }
        }
        if (factorExposureIndex < 0) {
            if (error) {
                *error = QStringLiteral("策略不存在横截面因子别名：%1")
                             .arg(alias);
            }
            return result;
        }
        result.riskSummary.factorExposureAlias = alias;
        result.riskSummary.factorExposureCoverageComplete = true;
    }
    int factorResearchIndex = -1;
    if (!definition.factorResearchAlias.trimmed().isEmpty()) {
        const QString alias = definition.factorResearchAlias.trimmed();
        for (int index = 0;
             index < strategy.plan().crossIndicators.size(); ++index) {
            if (strategy.plan().crossIndicators.at(index).alias == alias) {
                factorResearchIndex = index;
                break;
            }
        }
        if (factorResearchIndex < 0) {
            if (error) {
                *error = QStringLiteral("策略不存在因子研究别名：%1").arg(alias);
            }
            return result;
        }
        QString preprocessingError;
        QString neutralizationError;
        if (!definition.factorPreprocessing.isValid(&preprocessingError)
            || !definition.factorNeutralization.isValid(
                &neutralizationError)) {
            if (error) *error = preprocessingError;
            if (error && error->isEmpty()) *error = neutralizationError;
            return result;
        }
    }
    QHash<QString, QString> industryBySymbol;
    QSet<QString> constrainedIndustries;
    const bool requiresIndustrySnapshot =
        definition.maximumIndustryWeight > 0.0
        || definition.factorNeutralization.enabled();
    if (requiresIndustrySnapshot) {
        if (industry.datasetId != definition.industryDatasetId
            || !industry.coverageComplete || !industry.asOfDate.isValid()
            || industry.snapshotVersion.isEmpty()) {
            if (error) {
                *error = QStringLiteral(
                    "行业约束或因子行业去均值需要完整且身份匹配的日期快照");
            }
            return result;
        }
        for (const QString &symbol : symbols) {
            const QString industryCode = industry.industryCodeFor(symbol);
            if (industryCode.isEmpty()) {
                if (error) {
                    *error = QStringLiteral("行业快照缺少横截面标的：%1")
                                 .arg(symbol);
                }
                return result;
            }
            industryBySymbol.insert(symbol, industryCode);
        }
        result.industrySnapshot = industry;
        result.riskSummary.industryCoverageComplete = true;
        result.riskSummary.industrySnapshotVersion = industry.snapshotVersion;
    }

    strategy.reset();
    PortfolioExecutor::ExecutionState execState;
    execState.legs.resize(symbols.size());
    execState.cash = definition.initialCapital;
    PortfolioCircuitBreaker circuitBreaker(definition.initialCapital);
    for (int index = 0; index < symbols.size(); ++index) {
        const QString &symbol = symbols.at(index);
        const auto definitionLeg = std::find_if(
            definition.legs.cbegin(), definition.legs.cend(),
            [&symbol](const PortfolioLeg &leg) {
                return leg.instrument.symbol == symbol;
            });
        if (definitionLeg != definition.legs.cend())
            execState.legs[index].instrument = definitionLeg->instrument;
        else
            execState.legs[index].instrument.symbol = symbol;
        execState.legs[index].weight = 0.0;
    }
    QVector<int> consecutiveMissingBars(symbols.size(), 0);
    QVector<bool> missingRiskExitPending(symbols.size(), false);
    QVector<FactorResearchFrame> factorResearchFrames;
    QHash<QString, double> pendingFactorValues;
    QHash<QString, double> pendingCloseValues;
    QDateTime pendingFactorTimestamp;

    for (int frameIndex = 0; frameIndex < alignedSeries.frameCount(); ++frameIndex) {
        const AlignedBarFrame *frame = alignedSeries.frameAt(frameIndex);
        if (!frame) continue;
        const QDate day = frame->timestamp.date();
        const QDateTime barTime = frame->timestamp;

        // 更新每条腿价格（有 bar 的腿）；无 bar 的腿保持前值（前向填充，stale 由 lastPriceDay 判断）。
        QVector<bool> executeMissingRiskExit(symbols.size(), false);
        for (int index = 0; index < symbols.size(); ++index) {
            execState.legs[index].boughtToday = false;
            execState.legs[index].quoteAvailable = false;
            execState.legs[index].marketStatus = QStringLiteral("missing_unknown");
            if (frame->hasBar(symbols.at(index))) {
                const Candle *candle = frame->bar(symbols.at(index));
                execState.legs[index].lastPrice = candle->close;
                execState.legs[index].lastPriceDay = day;
                execState.legs[index].quoteAvailable = true;
                execState.legs[index].marketStatus = QStringLiteral("quoted");
                executeMissingRiskExit[index] = missingRiskExitPending.at(index);
                consecutiveMissingBars[index] = 0;
            } else {
                if (calendar.instrumentState(symbols.at(index), day)
                    == AShareInstrumentDayState::Suspended) {
                    execState.legs[index].marketStatus = QStringLiteral("suspended");
                    ++result.riskSummary.authoritativeSuspensionPointCount;
                } else {
                    ++result.riskSummary.unknownMissingQuotePointCount;
                }
                ++consecutiveMissingBars[index];
                result.riskSummary.maximumObservedConsecutiveMissingBars =
                    std::max(result.riskSummary.maximumObservedConsecutiveMissingBars,
                             consecutiveMissingBars.at(index));
                if (definition.maximumConsecutiveMissingBars > 0
                    && consecutiveMissingBars.at(index)
                           >= definition.maximumConsecutiveMissingBars
                    && execState.legs.at(index).quantity > 0.0) {
                    missingRiskExitPending[index] = true;
                }
            }
        }

        double equity = PortfolioExecutor::equity(execState);
        bool circuitBreakerActive = circuitBreaker.evaluate(
            equity, barTime, definition, result.riskSummary);

        // 熔断锁存后不再调用策略或恢复开仓；否则调策略产出目标持仓权重。
        CrossSectionalFrameResult frameResult;
        if (!circuitBreakerActive) frameResult = strategy.evaluateFrame(*frame);
        if (!pendingFactorValues.isEmpty()) {
            FactorResearchFrame researchFrame;
            researchFrame.factorTimestamp = pendingFactorTimestamp;
            researchFrame.returnTimestamp = barTime;
            researchFrame.factorValues = pendingFactorValues;
            if (definition.factorNeutralization.enabled())
                researchFrame.neutralizationGroups = industryBySymbol;
            for (auto iterator = pendingCloseValues.cbegin();
                 iterator != pendingCloseValues.cend(); ++iterator) {
                const Candle *current = frame->bar(iterator.key());
                if (!current || iterator.value() <= 0.0
                    || !std::isfinite(current->close)) {
                    continue;
                }
                researchFrame.forwardReturns.insert(
                    iterator.key(), current->close / iterator.value() - 1.0);
            }
            factorResearchFrames.append(researchFrame);
            pendingFactorValues.clear();
            pendingCloseValues.clear();
        }
        if (!circuitBreakerActive && factorResearchIndex >= 0) {
            if (factorResearchIndex >= frameResult.crossValues.size()) {
                if (error) *error = QStringLiteral("因子研究结果索引越界");
                return result;
            }
            pendingFactorValues = frameResult.crossValues.at(factorResearchIndex);
            pendingFactorTimestamp = barTime;
            for (const QString &symbol : symbols) {
                const Candle *current = frame->bar(symbol);
                if (current && current->close > 0.0
                    && std::isfinite(current->close)) {
                    pendingCloseValues.insert(symbol, current->close);
                }
            }
        }
        bool frameConstrained = false;
        for (int index = 0; index < symbols.size(); ++index) {
            const double requestedWeight =
                frameResult.targetWeights.value(symbols.at(index), 0.0);
            execState.legs[index].weight =
                std::min(requestedWeight, definition.maximumLegWeight);
            if (requestedWeight > definition.maximumLegWeight + 1e-12)
                frameConstrained = true;
            if (missingRiskExitPending.at(index))
                execState.legs[index].weight = 0.0;
        }
        if (circuitBreakerActive) {
            setAllTargetWeightsToZero(execState);
        } else if (definition.maximumActiveLegs > 0) {
            QVector<int> order(symbols.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&execState, &symbols](int left, int right) {
                const double leftWeight = execState.legs.at(left).weight;
                const double rightWeight = execState.legs.at(right).weight;
                if (leftWeight != rightWeight) return leftWeight > rightWeight;
                return symbols.at(left) < symbols.at(right);
            });
            for (int rank = definition.maximumActiveLegs;
                 rank < order.size(); ++rank) {
                if (execState.legs.at(order.at(rank)).weight > 0.0)
                    frameConstrained = true;
                execState.legs[order.at(rank)].weight = 0.0;
            }
        }
        if (!circuitBreakerActive
            && definition.maximumIndustryWeight > 0.0) {
            QHash<QString, QVector<int>> industryGroups;
            for (int index = 0; index < symbols.size(); ++index) {
                if (execState.legs.at(index).weight <= 0.0) continue;
                industryGroups[industryBySymbol.value(symbols.at(index))]
                    .append(index);
            }
            for (auto iterator = industryGroups.cbegin();
                 iterator != industryGroups.cend(); ++iterator) {
                double groupWeight = 0.0;
                for (int index : iterator.value())
                    groupWeight += execState.legs.at(index).weight;
                if (groupWeight
                    <= definition.maximumIndustryWeight + 1e-12) {
                    continue;
                }
                const double scale =
                    definition.maximumIndustryWeight / groupWeight;
                for (int index : iterator.value())
                    execState.legs[index].weight *= scale;
                constrainedIndustries.insert(iterator.key());
                frameConstrained = true;
            }
        }
        QHash<QString, double> standardizedFactorValues;
        if (!circuitBreakerActive && factorExposureIndex >= 0) {
            if (factorExposureIndex >= frameResult.crossValues.size()) {
                if (error) *error = QStringLiteral("横截面因子结果索引越界");
                return result;
            }
            standardizedFactorValues = CrossSectionalIndicatorRuntime::zscore(
                frameResult.crossValues.at(factorExposureIndex));
            double requestedExposure = 0.0;
            for (int index = 0; index < symbols.size(); ++index) {
                const double weight = execState.legs.at(index).weight;
                if (weight <= 0.0) continue;
                const auto factor = standardizedFactorValues.constFind(
                    symbols.at(index));
                if (factor == standardizedFactorValues.cend()) {
                    if (error) {
                        *error = QStringLiteral("目标持仓缺少因子值：%1")
                                     .arg(symbols.at(index));
                    }
                    return result;
                }
                requestedExposure += weight * factor.value();
            }
            const double requestedAbsoluteExposure = std::abs(requestedExposure);
            result.riskSummary.maximumRequestedAbsoluteFactorExposure = std::max(
                result.riskSummary.maximumRequestedAbsoluteFactorExposure,
                requestedAbsoluteExposure);
            if (requestedAbsoluteExposure
                > definition.maximumAbsoluteFactorExposure + 1e-12) {
                const double scale = definition.maximumAbsoluteFactorExposure
                    / requestedAbsoluteExposure;
                for (PortfolioExecutor::LegState &leg : execState.legs) {
                    if (leg.weight > 0.0) leg.weight *= scale;
                }
                ++result.riskSummary.factorExposureConstrainedFrameCount;
                frameConstrained = true;
            }
        }
        if (frameConstrained) ++result.riskSummary.constrainedFrameCount;

        const int firstNewTrade = result.trades.size();
        PortfolioExecutor::rebalance(execState, equity,
            definition.slippageRate, definition.commissionRate,
            definition.lotSize, definition.tPlusOne, barTime, result.trades);
        for (int tradeIndex = firstNewTrade;
             tradeIndex < result.trades.size(); ++tradeIndex) {
            PortfolioTrade &trade = result.trades[tradeIndex];
            const int legIndex = symbols.indexOf(trade.instrument.symbol);
            if (circuitBreakerActive
                && trade.side == QStringLiteral("sell")) {
                trade.reason = QStringLiteral("组合净值/回撤熔断退出");
                ++result.riskSummary.completedCircuitBreakerExitCount;
            } else if (trade.side == QStringLiteral("sell") && legIndex >= 0
                && executeMissingRiskExit.at(legIndex)) {
                trade.reason = QStringLiteral("连续缺失风险退出（恢复报价后成交）");
                ++result.riskSummary.completedMissingRiskExitCount;
            }
        }
        for (int index = 0; index < symbols.size(); ++index) {
            if (executeMissingRiskExit.at(index)
                && execState.legs.at(index).quantity <= 0.0) {
                missingRiskExitPending[index] = false;
            }
        }

        equity = PortfolioExecutor::equity(execState);
        const bool triggeredAfterRebalance = circuitBreaker.evaluate(
            equity, barTime, definition, result.riskSummary);
        if (!circuitBreakerActive && triggeredAfterRebalance) {
            circuitBreakerActive = true;
            setAllTargetWeightsToZero(execState);
            const int firstCircuitTrade = result.trades.size();
            PortfolioExecutor::rebalance(execState, equity,
                definition.slippageRate, definition.commissionRate,
                definition.lotSize, definition.tPlusOne, barTime,
                result.trades);
            markCircuitBreakerTrades(result.trades, firstCircuitTrade,
                                     result.riskSummary);
            equity = PortfolioExecutor::equity(execState);
            circuitBreaker.evaluate(equity, barTime, definition,
                                    result.riskSummary);
        }
        PortfolioBar bar;
        bar.timestamp = barTime;
        bar.equity = equity;
        bar.cash = execState.cash;
        bar.positions = PortfolioExecutor::positions(execState, day, equity);
        QVector<double> activeWeights;
        bool staleValuation = false;
        for (const PortfolioPosition &position : bar.positions) {
            if (position.quantity > 0.0) {
                activeWeights.append(position.weight);
                result.riskSummary.maximumObservedLegWeight = std::max(
                    result.riskSummary.maximumObservedLegWeight,
                    position.weight);
            }
            staleValuation = staleValuation || position.priceStale;
        }
        if (definition.maximumIndustryWeight > 0.0 && equity > 0.0) {
            QHash<QString, double> industryValues;
            for (const PortfolioPosition &position : bar.positions) {
                industryValues[industryBySymbol.value(
                    position.instrument.symbol)] += position.marketValue;
            }
            double maximumWeight = 0.0;
            for (double value : industryValues)
                maximumWeight = std::max(maximumWeight, value / equity);
            result.riskSummary.maximumObservedIndustryWeight = std::max(
                result.riskSummary.maximumObservedIndustryWeight,
                maximumWeight);
            if (maximumWeight > definition.maximumIndustryWeight + 1e-6)
                ++result.riskSummary.industryLimitBreachFrameCount;
        }
        if (factorExposureIndex >= 0 && equity > 0.0) {
            bool measurable = true;
            double actualExposure = 0.0;
            for (const PortfolioPosition &position : bar.positions) {
                if (position.quantity <= 0.0) continue;
                const auto factor = standardizedFactorValues.constFind(
                    position.instrument.symbol);
                if (factor == standardizedFactorValues.cend()) {
                    measurable = false;
                    break;
                }
                actualExposure += position.weight * factor.value();
            }
            if (!measurable) {
                ++result.riskSummary.factorExposureUnmeasuredFrameCount;
                result.riskSummary.factorExposureCoverageComplete = false;
            } else {
                const double actualAbsoluteExposure = std::abs(actualExposure);
                result.riskSummary.maximumObservedAbsoluteFactorExposure = std::max(
                    result.riskSummary.maximumObservedAbsoluteFactorExposure,
                    actualAbsoluteExposure);
                if (actualAbsoluteExposure
                    > definition.maximumAbsoluteFactorExposure + 1e-6) {
                    ++result.riskSummary.factorExposureLimitBreachFrameCount;
                }
            }
        }
        result.riskSummary.maximumObservedActiveLegs = std::max(
            result.riskSummary.maximumObservedActiveLegs,
            static_cast<int>(activeWeights.size()));
        std::sort(activeWeights.begin(), activeWeights.end(), std::greater<double>());
        double top3Weight = 0.0;
        for (int index = 0;
             index < std::min(3, static_cast<int>(activeWeights.size())); ++index)
            top3Weight += activeWeights.at(index);
        result.riskSummary.maximumObservedTop3Weight = std::max(
            result.riskSummary.maximumObservedTop3Weight, top3Weight);
        if (staleValuation)
            ++result.riskSummary.staleValuationPointCount;
        result.equityCurve.append(bar);
    }

    if (result.equityCurve.isEmpty()) {
        if (error) *error = QStringLiteral("多股回测没有可计算的帧");
        return result;
    }
    if (factorResearchIndex >= 0) {
        result.factorResearchSummary = FactorResearchAnalyzer::analyze(
            factorResearchFrames, definition.factorPreprocessing,
            definition.factorResearchCost,
            definition.factorNeutralization);
        result.factorResearchSummary.factorId = definition.factorDefinitionId;
        result.factorResearchSummary.factorVersion =
            definition.factorDefinitionVersion;
        result.factorResearchSummary.factorAlias =
            definition.factorResearchAlias;
        result.factorResearchSummary.outOfSample =
            FactorOutOfSampleAnalyzer::analyze(
                result.factorResearchSummary.timeSeries, {});
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
    int pendingMissingRiskExitCount = 0;
    for (int index = 0; index < missingRiskExitPending.size(); ++index) {
        if (missingRiskExitPending.at(index)
            && execState.legs.at(index).quantity > 0.0) {
            ++pendingMissingRiskExitCount;
        }
    }
    result.riskSummary.pendingMissingRiskExitCount =
        pendingMissingRiskExitCount;
    result.riskSummary.industryConstrainedGroupCount =
        constrainedIndustries.size();
    if (circuitBreaker.triggered()) {
        result.riskSummary.pendingCircuitBreakerExitCount =
            static_cast<int>(std::count_if(
                execState.legs.cbegin(), execState.legs.cend(),
                [](const PortfolioExecutor::LegState &leg) {
                    return leg.quantity > 0.0;
                }));
    }
    if (error) error->clear();
    return result;
}

} // namespace pp
