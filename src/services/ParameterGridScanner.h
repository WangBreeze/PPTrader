#pragma once

#include "domain/BacktestTypes.h"
#include "domain/CustomStrategyTypes.h"
#include "domain/MarketTypes.h"

#include <QVariantMap>
#include <QVector>

#include <atomic>

namespace pp {

// 一个参数维度的候选值。
struct ParameterGridAxis {
    QString key;
    QVector<double> values;
};

// 单个参数组合的回测结果。
struct ParameterGridResult {
    QVariantMap parameters;
    PerformanceSummary summary;

    QVariantMap toVariantMap() const;
};

// 参数网格扫描（同步核心）：展开笛卡尔积，对每个组合编译策略并跑一次
// 同步回测（BacktestEvaluator），按收益率降序返回。供参数优化/扫描使用。
class ParameterGridScanner {
public:
    // 展开参数网格为参数组合列表（笛卡尔积）。
    static QVector<QVariantMap> expandGrid(const QVector<ParameterGridAxis> &axes);

    static QVector<ParameterGridResult> scan(
        const CustomStrategyDefinition &definition,
        const QVector<ParameterGridAxis> &axes,
        const QVector<Candle> &candles,
        const BacktestConfig &baseConfig,
        int maximumCombinations = 512,
        const std::atomic_bool *cancelFlag = nullptr,
        QString *error = nullptr);
};

} // namespace pp
