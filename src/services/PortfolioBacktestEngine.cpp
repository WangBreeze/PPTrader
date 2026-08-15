#include "services/PortfolioBacktestEngine.h"

#include <QDate>
#include <QHash>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

namespace {

struct LegState {
    Instrument instrument;
    double quantity = 0.0;
    double lastPrice = 0.0;
    QDate lastPriceDay;
    double weight = 0.0;
    bool boughtToday = false;
};

QVector<double> normalizedWeights(const PortfolioDefinition &definition,
                                  QString *error)
{
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
                  [&weights](int left, int right) {
            return weights.at(left) > weights.at(right);
        });
        for (int rank = definition.maximumActiveLegs; rank < legCount; ++rank) {
            weights[order.at(rank)] = 0.0;
        }
    }
    const double total = std::accumulate(weights.cbegin(), weights.cend(), 0.0);
    if (total <= 0.0) {
        if (error) *error = QStringLiteral("组合权重合计必须大于 0");
        return {};
    }
    for (double &weight : weights) weight /= total;
    return weights;
}

} // namespace

PortfolioResult PortfolioBacktestEngine::run(
    const PortfolioDefinition &definition,
    const QHash<QString, QVector<Candle>> &series, QString *error)
{
    PortfolioResult result;
    result.definition = definition;
    QString validationError;
    if (!definition.isValid(&validationError)) {
        if (error) *error = validationError;
        return result;
    }
    const QVector<double> weights = normalizedWeights(definition, error);
    if (weights.isEmpty()) return result;

    const int legCount = definition.legs.size();
    QVector<LegState> states(legCount);
    QVector<QHash<QDate, QDateTime>> lastBarByDay(legCount);
    QVector<QHash<QDate, double>> closeByDay(legCount);
    int periodSeconds = 0;
    static const QTimeZone shanghai("Asia/Shanghai");
    for (int index = 0; index < legCount; ++index) {
        const PortfolioLeg &leg = definition.legs.at(index);
        states[index].instrument = leg.instrument;
        states[index].weight = weights.at(index);
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
    std::sort(valuationDays.begin(), valuationDays.end());
    const QDate inceptionDay = commonTradingDays.first();
    valuationDays.erase(std::remove_if(
        valuationDays.begin(), valuationDays.end(),
        [&inceptionDay](const QDate &day) { return day < inceptionDay; }),
        valuationDays.end());
    result.periodSeconds = periodSeconds;

    double cash = definition.initialCapital;
    double totalCommission = 0.0;
    int commonDayIndex = 0;
    for (const QDate &day : valuationDays) {
        QDateTime barTime;
        for (int index = 0; index < legCount; ++index) {
            if (lastBarByDay.at(index).contains(day)) {
                const QDateTime time = lastBarByDay.at(index).value(day);
                const double close = closeByDay.at(index).value(day);
                states[index].lastPrice = close;
                states[index].lastPriceDay = day;
                if (time.isValid()
                    && (!barTime.isValid() || time > barTime)) {
                    barTime = time;
                }
            }
            states[index].boughtToday = false;
        }
        if (!barTime.isValid()) continue;

        double equity = cash;
        for (const LegState &state : states) {
            equity += state.quantity * state.lastPrice;
        }

        const bool commonTradingDay = commonDays.contains(day);
        if (commonTradingDay
            && commonDayIndex % definition.rebalanceEveryTradingDays == 0) {
            // 先卖后买：超配腿按可卖数量卖至目标市值。
            for (int index = 0; index < legCount; ++index) {
                LegState &state = states[index];
                if (state.quantity <= 0.0) continue;
                const double target = equity * state.weight;
                const double current = state.quantity * state.lastPrice;
                if (current <= target) continue;
                const double fillPrice =
                    state.lastPrice * (1.0 - definition.slippageRate);
                const double sellable = state.quantity;
                const double desired = (current - target) / fillPrice;
                const int lot = std::max(1, definition.lotSize);
                const double quantity = std::floor(desired / lot) * lot;
                if (quantity <= 0.0 || quantity > sellable) continue;
                const double commission =
                    fillPrice * quantity * definition.commissionRate;
                cash += fillPrice * quantity - commission;
                totalCommission += commission;
                state.quantity -= quantity;
                PortfolioTrade trade;
                trade.instrument = state.instrument;
                trade.timestamp = barTime;
                trade.side = QStringLiteral("sell");
                trade.quantity = quantity;
                trade.price = fillPrice;
                trade.commission = commission;
                trade.amount = fillPrice * quantity;
                trade.reason = QStringLiteral("组合再平衡卖出");
                result.trades.append(trade);
            }
            // 后买：低配腿用可用现金买至目标市值。
            for (int index = 0; index < legCount; ++index) {
                LegState &state = states[index];
                if (state.weight <= 0.0) continue;
                const double target = equity * state.weight;
                const double current = state.quantity * state.lastPrice;
                if (current >= target) continue;
                const double fillPrice =
                    state.lastPrice * (1.0 + definition.slippageRate);
                const int lot = std::max(1, definition.lotSize);
                const double spend = std::min(target - current, cash);
                const double affordable =
                    spend / (fillPrice * (1.0 + definition.commissionRate));
                const double quantity = std::floor(affordable / lot) * lot;
                if (quantity <= 0.0) continue;
                const double cost = fillPrice * quantity;
                const double commission = cost * definition.commissionRate;
                if (cost + commission > cash + 1e-9) continue;
                cash -= cost + commission;
                totalCommission += commission;
                state.quantity += quantity;
                if (definition.tPlusOne) state.boughtToday = true;
                PortfolioTrade trade;
                trade.instrument = state.instrument;
                trade.timestamp = barTime;
                trade.side = QStringLiteral("buy");
                trade.quantity = quantity;
                trade.price = fillPrice;
                trade.commission = commission;
                trade.amount = cost;
                trade.reason = QStringLiteral("组合再平衡买入");
                result.trades.append(trade);
            }
        }
        if (commonTradingDay) ++commonDayIndex;

        equity = cash;
        PortfolioBar bar;
        bar.timestamp = barTime;
        for (const LegState &state : states) {
            equity += state.quantity * state.lastPrice;
        }
        bar.equity = equity;
        bar.cash = cash;
        for (const LegState &state : states) {
            PortfolioPosition position;
            position.instrument = state.instrument;
            position.quantity = state.quantity;
            position.lastPrice = state.lastPrice;
            position.marketValue = state.quantity * state.lastPrice;
            position.weight = equity > 0.0
                ? position.marketValue / equity : 0.0;
            position.priceStale = state.quantity > 0.0
                && state.lastPriceDay.isValid() && state.lastPriceDay < day;
            bar.positions.append(position);
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
    summary.totalCommission = totalCommission;
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
    if (error) error->clear();
    return result;
}

} // namespace pp
