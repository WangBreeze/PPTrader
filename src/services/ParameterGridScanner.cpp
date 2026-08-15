#include "services/ParameterGridScanner.h"

#include "services/BacktestEvaluator.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomStrategyCompiler.h"

#include <algorithm>

namespace pp {

QVariantMap ParameterGridResult::toVariantMap() const
{
    return {{QStringLiteral("parameters"), parameters},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("finalEquity"), summary.finalEquity}};
}

QVector<QVariantMap> ParameterGridScanner::expandGrid(const QVector<ParameterGridAxis> &axes)
{
    QVector<QVariantMap> combinations;
    combinations.append(QVariantMap{});
    for (const ParameterGridAxis &axis : axes) {
        QVector<QVariantMap> next;
        next.reserve(combinations.size() * std::max<qsizetype>(1, axis.values.size()));
        for (const QVariantMap &combination : combinations) {
            for (double value : axis.values) {
                QVariantMap extended = combination;
                extended.insert(axis.key, value);
                next.append(extended);
            }
        }
        combinations = std::move(next);
    }
    return combinations;
}

QVector<ParameterGridResult> ParameterGridScanner::scan(
    const CustomStrategyDefinition &definition,
    const QVector<ParameterGridAxis> &axes,
    const QVector<Candle> &candles,
    const BacktestConfig &baseConfig,
    int maximumCombinations,
    const std::atomic_bool *cancelFlag,
    QString *error)
{
    if (error) error->clear();
    QVector<ParameterGridResult> results;

    const QVector<QVariantMap> combinations = expandGrid(axes);
    if (combinations.isEmpty()) {
        if (error) *error = QStringLiteral("参数网格为空");
        return results;
    }
    if (combinations.size() > maximumCombinations) {
        if (error) {
            *error = QStringLiteral("参数组合过多：%1（上限 %2）")
                .arg(combinations.size()).arg(maximumCombinations);
        }
        return results;
    }

    CustomStrategyCompiler compiler;
    results.reserve(combinations.size());
    for (const QVariantMap &parameters : combinations) {
        if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
            if (error) *error = QStringLiteral("参数网格扫描已取消");
            results.clear();
            return results;
        }
        QString compileError;
        const auto plan = compiler.compile(definition, parameters, &compileError);
        if (!plan) {
            // 组合参数无效则跳过，记录为空摘要的失败项。
            ParameterGridResult failed;
            failed.parameters = parameters;
            failed.summary.totalReturnPercent = 0.0;
            results.append(failed);
            continue;
        }
        CustomBacktestStrategy strategy(*plan);
        const PerformanceSummary summary = BacktestEvaluator::run(baseConfig, candles, strategy);
        ParameterGridResult result;
        result.parameters = parameters;
        result.summary = summary;
        results.append(result);
    }

    std::sort(results.begin(), results.end(),
        [](const ParameterGridResult &left, const ParameterGridResult &right) {
            return left.summary.totalReturnPercent > right.summary.totalReturnPercent;
        });
    return results;
}

} // namespace pp
