#include "strategy/BacktestStrategyRegistry.h"

#include "strategy/BollingerMeanReversionStrategy.h"
#include "strategy/MacdTrendStrategy.h"
#include "strategy/RsiMeanReversionStrategy.h"
#include "strategy/SmaCrossoverStrategy.h"

#include <cmath>

namespace pp {

namespace {

BacktestParameterSpec integerParameter(const QString &key, const QString &name,
                                       int defaultValue, int minimum, int maximum)
{
    return {key, name, QStringLiteral("integer"), defaultValue, minimum, maximum, 1};
}

BacktestParameterSpec realParameter(const QString &key, const QString &name,
                                    double defaultValue, double minimum,
                                    double maximum, double step)
{
    return {key, name, QStringLiteral("real"), defaultValue, minimum, maximum, step};
}

} // namespace

QVector<BacktestStrategyDescriptor> BacktestStrategyRegistry::descriptors() const
{
    return {
        {QStringLiteral("sma_crossover"), QStringLiteral("SMA 双均线"),
         QStringLiteral("快速均线上穿慢速均线买入，下穿卖出。"), QStringLiteral("A股"), 20,
         {integerParameter(QStringLiteral("fastPeriod"), QStringLiteral("快速周期"), 5, 2, 120),
          integerParameter(QStringLiteral("slowPeriod"), QStringLiteral("慢速周期"), 20, 3, 250)}},
        {QStringLiteral("rsi_mean_reversion"), QStringLiteral("RSI 均值回归"),
         QStringLiteral("RSI 进入超卖区买入，进入超买区卖出。"), QStringLiteral("A股"), 15,
         {integerParameter(QStringLiteral("period"), QStringLiteral("RSI 周期"), 14, 2, 120),
          realParameter(QStringLiteral("oversold"), QStringLiteral("超卖阈值"), 30.0, 1.0, 49.0, 1.0),
          realParameter(QStringLiteral("overbought"), QStringLiteral("超买阈值"), 70.0, 51.0, 99.0, 1.0)}},
        {QStringLiteral("macd_trend"), QStringLiteral("MACD 趋势"),
         QStringLiteral("MACD 线上穿信号线买入，下穿卖出。"), QStringLiteral("A股"), 35,
         {integerParameter(QStringLiteral("fastPeriod"), QStringLiteral("快速周期"), 12, 2, 120),
          integerParameter(QStringLiteral("slowPeriod"), QStringLiteral("慢速周期"), 26, 3, 250),
          integerParameter(QStringLiteral("signalPeriod"), QStringLiteral("信号周期"), 9, 2, 120)}},
        {QStringLiteral("bollinger_mean_reversion"), QStringLiteral("布林带均值回归"),
         QStringLiteral("跌破下轨买入，回归中轨卖出。"), QStringLiteral("A股"), 20,
         {integerParameter(QStringLiteral("period"), QStringLiteral("统计周期"), 20, 2, 250),
          realParameter(QStringLiteral("standardDeviations"), QStringLiteral("标准差倍数"),
                        2.0, 0.5, 5.0, 0.1)}}
    };
}

std::optional<BacktestStrategyDescriptor> BacktestStrategyRegistry::descriptor(
    const QString &strategyId) const
{
    const QString normalizedId = strategyId.trimmed();
    for (const BacktestStrategyDescriptor &item : descriptors()) {
        if (item.id == normalizedId) return item;
    }
    return std::nullopt;
}

QVariantList BacktestStrategyRegistry::catalog() const
{
    QVariantList result;
    const QVector<BacktestStrategyDescriptor> items = descriptors();
    result.reserve(items.size());
    for (const BacktestStrategyDescriptor &item : items) result.append(item.toVariantMap());
    return result;
}

bool BacktestStrategyRegistry::normalizeParameters(const QString &strategyId,
                                                   const QVariantMap &parameters,
                                                   QVariantMap *normalized,
                                                   QString *error) const
{
    const auto item = descriptor(strategyId);
    if (!item) {
        if (error) *error = QStringLiteral("未知回测策略：%1").arg(strategyId);
        return false;
    }

    for (auto iterator = parameters.cbegin(); iterator != parameters.cend(); ++iterator) {
        bool known = false;
        for (const BacktestParameterSpec &spec : item->parameters) {
            if (spec.key == iterator.key()) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (error) *error = QStringLiteral("策略参数不存在：%1").arg(iterator.key());
            return false;
        }
    }

    QVariantMap result;
    for (const BacktestParameterSpec &spec : item->parameters) {
        const QVariant value = parameters.value(spec.key, spec.defaultValue);
        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (!ok || !std::isfinite(numeric)) {
            if (error) *error = QStringLiteral("策略参数不是有效数字：%1").arg(spec.displayName);
            return false;
        }
        const double minimum = spec.minimumValue.toDouble();
        const double maximum = spec.maximumValue.toDouble();
        if (numeric < minimum || numeric > maximum) {
            if (error) {
                *error = QStringLiteral("策略参数超出范围：%1，允许 %2–%3")
                    .arg(spec.displayName).arg(minimum).arg(maximum);
            }
            return false;
        }
        if (spec.type == QStringLiteral("integer")) {
            const int integer = value.toInt(&ok);
            if (!ok || std::abs(numeric - integer) > 1e-9) {
                if (error) *error = QStringLiteral("策略参数必须为整数：%1").arg(spec.displayName);
                return false;
            }
            result.insert(spec.key, integer);
        } else {
            result.insert(spec.key, numeric);
        }
    }

    if ((strategyId == QStringLiteral("sma_crossover")
         || strategyId == QStringLiteral("macd_trend"))
        && result.value(QStringLiteral("fastPeriod")).toInt()
            >= result.value(QStringLiteral("slowPeriod")).toInt()) {
        if (error) *error = QStringLiteral("快速周期必须小于慢速周期");
        return false;
    }
    if (strategyId == QStringLiteral("rsi_mean_reversion")
        && result.value(QStringLiteral("oversold")).toDouble()
            >= result.value(QStringLiteral("overbought")).toDouble()) {
        if (error) *error = QStringLiteral("超卖阈值必须小于超买阈值");
        return false;
    }

    if (normalized) *normalized = result;
    if (error) error->clear();
    return true;
}

std::unique_ptr<BacktestStrategy> BacktestStrategyRegistry::create(
    const QString &strategyId, const QVariantMap &parameters, QString *error) const
{
    QVariantMap values;
    if (!normalizeParameters(strategyId, parameters, &values, error)) return {};

    if (strategyId == QStringLiteral("sma_crossover")) {
        return std::make_unique<SmaCrossoverStrategy>(
            values.value(QStringLiteral("fastPeriod")).toInt(),
            values.value(QStringLiteral("slowPeriod")).toInt());
    }
    if (strategyId == QStringLiteral("rsi_mean_reversion")) {
        return std::make_unique<RsiMeanReversionStrategy>(
            values.value(QStringLiteral("period")).toInt(),
            values.value(QStringLiteral("oversold")).toDouble(),
            values.value(QStringLiteral("overbought")).toDouble());
    }
    if (strategyId == QStringLiteral("macd_trend")) {
        return std::make_unique<MacdTrendStrategy>(
            values.value(QStringLiteral("fastPeriod")).toInt(),
            values.value(QStringLiteral("slowPeriod")).toInt(),
            values.value(QStringLiteral("signalPeriod")).toInt());
    }
    if (strategyId == QStringLiteral("bollinger_mean_reversion")) {
        return std::make_unique<BollingerMeanReversionStrategy>(
            values.value(QStringLiteral("period")).toInt(),
            values.value(QStringLiteral("standardDeviations")).toDouble());
    }

    if (error) *error = QStringLiteral("未知回测策略：%1").arg(strategyId);
    return {};
}

} // namespace pp
