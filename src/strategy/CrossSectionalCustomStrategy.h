#pragma once

#include "services/AlignedBarSeries.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace pp {

// 每帧求值结果：目标持仓（等权）与各横截面指标每只股票的值（供复盘）。
struct CrossSectionalFrameResult {
    QHash<QString, double> targetWeights;
    QVector<QHash<QString, double>> crossValues;
};

// 多股横截面自定义策略：逐帧求值 DSL v3，产出目标持仓权重。
// 单标的时序指标用每条腿一个 CustomIndicatorRuntime；横截面用 CrossSectionalIndicatorRuntime。
class CrossSectionalCustomStrategy {
public:
    explicit CrossSectionalCustomStrategy(CompiledCustomStrategy plan);

    void reset();
    CrossSectionalFrameResult evaluateFrame(const AlignedBarFrame &frame);
    const CompiledCustomStrategy &plan() const { return m_plan; }

private:
    double fieldValue(const Candle &candle, CustomOperandKind field) const;
    std::optional<double> indicatorValue(const QString &symbol, int indicatorIndex) const;
    QHash<QString, double> selectTargetWeights(
        const QHash<QString, double> &scoreValues) const;

    CompiledCustomStrategy m_plan;
    QHash<QString, CustomIndicatorRuntime> m_legIndicators;
};

} // namespace pp
