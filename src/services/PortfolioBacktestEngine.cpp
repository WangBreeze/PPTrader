#include "services/PortfolioBacktestEngine.h"

#include "services/PortfolioCircuitBreaker.h"
#include "services/PortfolioExecutor.h"

#include <QDate>
#include <QHash>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

namespace {

QVector<double> normalizedWeights(const PortfolioDefinition &definition,
                                  const AShareIndustrySnapshot &industry,
                                  int *constrainedIndustryGroups,
                                  QString *error)
{
    if (constrainedIndustryGroups) *constrainedIndustryGroups = 0;
    const int legCount = definition.legs.size();
    QVector<double> weights(legCount, 0.0);
    double explicitSum = 0.0;
    int implicitCount = 0;
    for (int index = 0; index < legCount; ++index) {
        if (definition.legs.at(index).targetWeight > 0.0) {
            weights[index] = definition.legs.at(index).targetWeight;
            explicitSum += weights[index];
        } else {
            ++implicitCount;
        }
    }
    if (explicitSum > 1.0) {
        if (error) *error = QStringLiteral("组合显式权重合计不能超过 1");
        return {};
    }
    const double implicitWeight = implicitCount > 0
        ? (1.0 - explicitSum) / implicitCount : 0.0;
    for (int index = 0; index < legCount; ++index) {
        if (weights[index] <= 0.0) weights[index] = implicitWeight;
    }
    if (definition.maximumLegWeight > 0.0
        && definition.maximumLegWeight < 1.0) {
        for (double &weight : weights) {
            weight = std::min(weight, definition.maximumLegWeight);
        }
    }
    if (definition.maximumActiveLegs > 0) {
        QVector<int> order(legCount);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&weights, &definition](int left, int right) {
            if (weights.at(left) != weights.at(right))
                return weights.at(left) > weights.at(right);
            return definition.legs.at(left).instrument.symbol
                < definition.legs.at(right).instrument.symbol;
        });
        for (int rank = definition.maximumActiveLegs; rank < legCount; ++rank) {
            weights[order.at(rank)] = 0.0;
        }
    }
    if (definition.maximumIndustryWeight > 0.0) {
        QHash<QString, QVector<int>> groups;
        for (int index = 0; index < legCount; ++index) {
            const QString industryCode = industry.industryCodeFor(
                definition.legs.at(index).instrument.symbol);
            if (industryCode.isEmpty()) {
                if (error) *error = QStringLiteral("行业快照缺少组合腿：%1")
                                        .arg(definition.legs.at(index)
                                                 .instrument.symbol);
                return {};
            }
            groups[industryCode].append(index);
        }
        for (auto iterator = groups.cbegin(); iterator != groups.cend();
             ++iterator) {
            double groupWeight = 0.0;
            for (int index : iterator.value()) groupWeight += weights.at(index);
            if (groupWeight <= definition.maximumIndustryWeight + 1e-12)
                continue;
            const double scale = definition.maximumIndustryWeight / groupWeight;
            for (int index : iterator.value()) weights[index] *= scale;
            if (constrainedIndustryGroups) ++*constrainedIndustryGroups;
        }
    }
    const double total = std::accumulate(weights.cbegin(), weights.cend(), 0.0);
    if (total <= 0.0) {
        if (error) *error = QStringLiteral("组合权重合计必须大于 0");
        return {};
    }
    // 风险上限裁剪后不再归一化；剩余比例保留为现金，否则会重新突破单腿上限。
    return weights;
}

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

PortfolioResult PortfolioBacktestEngine::run(
    const PortfolioDefinition &definition,
    const QHash<QString, QVector<Candle>> &series, QString *error,
    const AShareCalendarSnapshot &calendar,
    const AShareIndustrySnapshot &industry)
{
    PortfolioResult result;
    result.definition = definition;
    result.riskSummary.marketCalendarCoverageComplete =
        calendar.marketCoverageComplete;
    result.riskSummary.instrumentStatusCoverageComplete =
        calendar.instrumentCoverageComplete;
    result.riskSummary.calendarSnapshotVersion = calendar.snapshotVersion;
    QString validationError;
    if (!definition.isValid(&validationError)) {
        if (error) *error = validationError;
        return result;
    }
    if (definition.maximumIndustryWeight > 0.0
        && (industry.datasetId != definition.industryDatasetId
            || !industry.coverageComplete || !industry.asOfDate.isValid()
            || industry.snapshotVersion.isEmpty())) {
        if (error) *error = QStringLiteral("行业约束需要完整且身份匹配的日期快照");
        return result;
    }
    int constrainedIndustryGroups = 0;
    const QVector<double> weights = normalizedWeights(
        definition, industry, &constrainedIndustryGroups, error);
    if (weights.isEmpty()) return result;
    result.riskSummary.industryConstrainedGroupCount =
        constrainedIndustryGroups;
    result.riskSummary.industryCoverageComplete =
        definition.maximumIndustryWeight > 0.0 && industry.coverageComplete;
    result.riskSummary.industrySnapshotVersion = industry.snapshotVersion;
    if (definition.maximumIndustryWeight > 0.0)
        result.industrySnapshot = industry;

    const int legCount = definition.legs.size();
    PortfolioExecutor::ExecutionState execState;
    execState.legs.resize(legCount);
    execState.cash = definition.initialCapital;
    PortfolioCircuitBreaker circuitBreaker(definition.initialCapital);
    QVector<QHash<QDate, QDateTime>> lastBarByDay(legCount);
    QVector<QHash<QDate, double>> closeByDay(legCount);
    int periodSeconds = 0;
    static const QTimeZone shanghai("Asia/Shanghai");
    for (int index = 0; index < legCount; ++index) {
        const PortfolioLeg &leg = definition.legs.at(index);
        execState.legs[index].instrument = leg.instrument;
        execState.legs[index].weight = weights.at(index);
        const auto found = series.constFind(leg.instrument.symbol);
        if (found == series.constEnd() || found->isEmpty()) {
            if (error) *error = QStringLiteral("组合腿缺少 K 线数据：%1")
                                    .arg(leg.instrument.symbol);
            return result;
        }
        QVector<Candle> candles = *found;
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&leg](const Candle &candle) {
                return candle.instrument.symbol != leg.instrument.symbol
                    || candle.close <= 0.0 || !candle.startTime.isValid();
            }), candles.end());
        std::sort(candles.begin(), candles.end(),
                  [](const Candle &left, const Candle &right) {
            return left.startTime < right.startTime;
        });
        if (candles.isEmpty()) {
            if (error) *error = QStringLiteral("组合腿 K 线全部无效：%1")
                                    .arg(leg.instrument.symbol);
            return result;
        }
        if (periodSeconds == 0) periodSeconds = candles.first().periodSeconds;
        for (const Candle &candle : candles) {
            const QDate day = candle.startTime.toTimeZone(shanghai).date();
            lastBarByDay[index].insert(day, candle.startTime);
            closeByDay[index].insert(day, candle.close);
        }
    }
    if (error) error->clear();

    QSet<QDate> commonDays;
    QSet<QDate> observedDays;
    for (int index = 0; index < legCount; ++index) {
        const QList<QDate> days = lastBarByDay.at(index).keys();
        for (const QDate &day : days) observedDays.insert(day);
        if (index == 0) {
            commonDays = QSet<QDate>(days.cbegin(), days.cend());
        } else {
            QSet<QDate> intersection;
            for (const QDate &day : days) {
                if (commonDays.contains(day)) intersection.insert(day);
            }
            commonDays = intersection;
        }
    }
    QList<QDate> commonTradingDays = commonDays.values();
    std::sort(commonTradingDays.begin(), commonTradingDays.end());
    if (commonTradingDays.isEmpty()) {
        if (error) *error = QStringLiteral("组合腿没有共同交易日");
        return result;
    }
    QList<QDate> valuationDays = observedDays.values();
    if (calendar.marketCoverageComplete) {
        for (const QDate &day : calendar.tradingDays) {
            if (day >= commonTradingDays.first()) observedDays.insert(day);
        }
        valuationDays = observedDays.values();
    }
    std::sort(valuationDays.begin(), valuationDays.end());
    const QDate inceptionDay = commonTradingDays.first();
    valuationDays.erase(std::remove_if(
        valuationDays.begin(), valuationDays.end(),
        [&inceptionDay](const QDate &day) { return day < inceptionDay; }),
        valuationDays.end());
    result.periodSeconds = periodSeconds;

    int commonDayIndex = 0;
    for (const QDate &day : valuationDays) {
        QDateTime barTime;
        for (int index = 0; index < legCount; ++index) {
            execState.legs[index].quoteAvailable = false;
            execState.legs[index].marketStatus = QStringLiteral("missing_unknown");
            if (lastBarByDay.at(index).contains(day)) {
                const QDateTime time = lastBarByDay.at(index).value(day);
                const double close = closeByDay.at(index).value(day);
                execState.legs[index].lastPrice = close;
                execState.legs[index].lastPriceDay = day;
                execState.legs[index].quoteAvailable = true;
                execState.legs[index].marketStatus = QStringLiteral("quoted");
                if (time.isValid()
                    && (!barTime.isValid() || time > barTime)) {
                    barTime = time;
                }
            } else if (calendar.instrumentState(
                           execState.legs.at(index).instrument.symbol, day)
                       == AShareInstrumentDayState::Suspended) {
                execState.legs[index].marketStatus = QStringLiteral("suspended");
                ++result.riskSummary.authoritativeSuspensionPointCount;
            } else {
                ++result.riskSummary.unknownMissingQuotePointCount;
            }
            execState.legs[index].boughtToday = false;
        }
        if (!barTime.isValid() && calendar.marketCoverageComplete
            && calendar.tradingDays.contains(day)) {
            barTime = QDateTime(day, QTime(15, 0), shanghai).toUTC();
            ++result.riskSummary.calendarOnlyValuationPointCount;
        }
        if (!barTime.isValid()) continue;

        double equity = PortfolioExecutor::equity(execState);
        bool circuitBreakerActive = circuitBreaker.evaluate(
            equity, barTime, definition, result.riskSummary);
        if (circuitBreakerActive) setAllTargetWeightsToZero(execState);

        const bool commonTradingDay = commonDays.contains(day);
        const bool scheduledRebalance = commonTradingDay
            && commonDayIndex % definition.rebalanceEveryTradingDays == 0;
        if (circuitBreakerActive || scheduledRebalance) {
            const int firstTrade = result.trades.size();
            PortfolioExecutor::rebalance(execState, equity,
                definition.slippageRate, definition.commissionRate,
                definition.lotSize, definition.tPlusOne, barTime,
                result.trades);
            if (circuitBreakerActive) {
                markCircuitBreakerTrades(result.trades, firstTrade,
                                         result.riskSummary);
            }
        }
        if (commonTradingDay) ++commonDayIndex;

        equity = PortfolioExecutor::equity(execState);
        const bool triggeredAfterRebalance = circuitBreaker.evaluate(
            equity, barTime, definition, result.riskSummary);
        if (!circuitBreakerActive && triggeredAfterRebalance) {
            circuitBreakerActive = true;
            setAllTargetWeightsToZero(execState);
            const int firstTrade = result.trades.size();
            PortfolioExecutor::rebalance(execState, equity,
                definition.slippageRate, definition.commissionRate,
                definition.lotSize, definition.tPlusOne, barTime,
                result.trades);
            markCircuitBreakerTrades(result.trades, firstTrade,
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
        if (definition.maximumIndustryWeight > 0.0 && equity > 0.0) {
            QHash<QString, double> industryValues;
            for (const PortfolioPosition &position : bar.positions) {
                const QString industryCode = industry.industryCodeFor(
                    position.instrument.symbol);
                industryValues[industryCode] += position.marketValue;
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
        result.equityCurve.append(bar);
    }

    if (result.equityCurve.isEmpty()) {
        if (error) *error = QStringLiteral("组合没有可计算的交易日");
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
        const double mean =
            std::accumulate(returns.cbegin(), returns.cend(), 0.0)
            / returns.size();
        double squaredDeviation = 0.0;
        for (double value : returns) {
            squaredDeviation += (value - mean) * (value - mean);
        }
        const double deviation =
            std::sqrt(squaredDeviation / (returns.size() - 1));
        if (deviation > 0.0) {
            summary.sharpeRatio = mean / deviation * std::sqrt(252.0);
        }
    }
    // 占位：H3C.2 由控制器按数据集身份填充；此处记录腿代码便于追溯。
    for (const Instrument &instrument : definition.instruments()) {
        result.datasetVersions.append(instrument.symbol);
    }
    if (circuitBreaker.triggered()) {
        result.riskSummary.pendingCircuitBreakerExitCount =
            static_cast<int>(std::count_if(
                execState.legs.cbegin(), execState.legs.cend(),
                [](const PortfolioExecutor::LegState &leg) {
                    return leg.quantity > 0.0;
                }));
    }
    result.immutableIdentity = {
        {QStringLiteral("portfolioRiskPolicy"),
         QVariantMap{
             {QStringLiteral("maximumLegWeight"),
              definition.maximumLegWeight},
             {QStringLiteral("maximumActiveLegs"),
              definition.maximumActiveLegs},
             {QStringLiteral("maximumConsecutiveMissingBars"),
              definition.maximumConsecutiveMissingBars},
             {QStringLiteral("maximumPortfolioLossPercent"),
              definition.maximumPortfolioLossPercent},
             {QStringLiteral("maximumPortfolioDrawdownPercent"),
              definition.maximumPortfolioDrawdownPercent},
             {QStringLiteral("maximumIndustryWeight"),
              definition.maximumIndustryWeight},
             {QStringLiteral("industryDatasetId"),
              definition.industryDatasetId},
             {QStringLiteral("tPlusOne"), definition.tPlusOne}}},
        {QStringLiteral("riskSummary"), result.riskSummary.toVariantMap()}};
    if (!calendar.snapshotVersion.isEmpty()) {
        result.immutableIdentity.insert(QStringLiteral("calendarSnapshot"),
                                        calendar.toVariantMap());
    }
    if (definition.maximumIndustryWeight > 0.0) {
        result.immutableIdentity.insert(QStringLiteral("industrySnapshot"),
                                        industry.toVariantMap());
    }
    if (error) error->clear();
    return result;
}

} // namespace pp
