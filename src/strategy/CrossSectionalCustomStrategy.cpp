#include "strategy/CrossSectionalCustomStrategy.h"

#include "strategy/CrossSectionalIndicatorRuntime.h"

#include <algorithm>

namespace pp {

CrossSectionalCustomStrategy::CrossSectionalCustomStrategy(CompiledCustomStrategy plan)
    : m_plan(std::move(plan))
{
    for (const QString &symbol : m_plan.universeSymbols) {
        m_legIndicators.insert(symbol, CustomIndicatorRuntime(m_plan.indicators));
    }
}

void CrossSectionalCustomStrategy::reset()
{
    for (auto it = m_legIndicators.begin(); it != m_legIndicators.end(); ++it) {
        it->reset();
    }
}

double CrossSectionalCustomStrategy::fieldValue(const Candle &candle,
                                                CustomOperandKind field) const
{
    switch (field) {
    case CustomOperandKind::Open: return candle.open;
    case CustomOperandKind::High: return candle.high;
    case CustomOperandKind::Low: return candle.low;
    case CustomOperandKind::Close: return candle.close;
    case CustomOperandKind::Volume: return candle.volume;
    default: return candle.close;
    }
}

std::optional<double> CrossSectionalCustomStrategy::indicatorValue(
    const QString &symbol, int indicatorIndex) const
{
    const auto it = m_legIndicators.constFind(symbol);
    if (it == m_legIndicators.constEnd()) return std::nullopt;
    return it->value(indicatorIndex);
}

QHash<QString, double> CrossSectionalCustomStrategy::selectTargetWeights(
    const QHash<QString, double> &scoreValues) const
{
    QHash<QString, double> weights;
    const int count = std::min(m_plan.selection.count,
                               static_cast<int>(scoreValues.size()));
    if (count <= 0) return weights;

    const bool ascending = m_plan.selection.ascending;
    QVector<QString> symbols = scoreValues.keys().toVector();
    std::sort(symbols.begin(), symbols.end(),
              [&scoreValues, ascending](const QString &left, const QString &right) {
        const double leftValue = scoreValues.value(left);
        const double rightValue = scoreValues.value(right);
        if (leftValue != rightValue) {
            return ascending ? leftValue < rightValue : leftValue > rightValue;
        }
        return left < right;
    });

    const double weight = 1.0 / static_cast<double>(count);
    for (int index = 0; index < count; ++index) {
        weights.insert(symbols.at(index), weight);
    }
    return weights;
}

CrossSectionalFrameResult CrossSectionalCustomStrategy::evaluateFrame(
    const AlignedBarFrame &frame)
{
    CrossSectionalFrameResult result;
    result.crossValues.resize(m_plan.crossIndicators.size());

    // 1. 更新每条腿的单标的指标（有 bar 的腿）。
    for (auto it = frame.bars.constBegin(); it != frame.bars.constEnd(); ++it) {
        m_legIndicators[it.key()].append(it.value());
    }

    // 2. 计算每个横截面指标。
    for (int crossIndex = 0; crossIndex < m_plan.crossIndicators.size(); ++crossIndex) {
        const CompiledCrossIndicator &cross = m_plan.crossIndicators.at(crossIndex);
        QHash<QString, double> input;
        if (cross.kind == CustomCrossIndicatorKind::Peer) {
            const Candle *peer = frame.bar(cross.peerSymbol);
            if (peer) input.insert(cross.peerSymbol, fieldValue(*peer, cross.peerField));
            result.crossValues[crossIndex] = input;
            continue;
        }
        for (const QString &symbol : m_plan.universeSymbols) {
            if (cross.indicatorIndex >= 0) {
                const auto value = indicatorValue(symbol, cross.indicatorIndex);
                if (value) input.insert(symbol, *value);
            } else {
                const Candle *candle = frame.bar(symbol);
                if (candle) input.insert(symbol, fieldValue(*candle, cross.field));
            }
        }
        switch (cross.kind) {
        case CustomCrossIndicatorKind::Rank:
            result.crossValues[crossIndex] =
                CrossSectionalIndicatorRuntime::rank(input, cross.ascending);
            break;
        case CustomCrossIndicatorKind::ZScore:
            result.crossValues[crossIndex] =
                CrossSectionalIndicatorRuntime::zscore(input);
            break;
        case CustomCrossIndicatorKind::Percentile:
            result.crossValues[crossIndex] =
                CrossSectionalIndicatorRuntime::percentile(input);
            break;
        case CustomCrossIndicatorKind::Mean: {
            const double value = CrossSectionalIndicatorRuntime::mean(input);
            for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
                result.crossValues[crossIndex].insert(it.key(), value);
            }
            break;
        }
        case CustomCrossIndicatorKind::Median: {
            const double value = CrossSectionalIndicatorRuntime::median(input);
            for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
                result.crossValues[crossIndex].insert(it.key(), value);
            }
            break;
        }
        default:
            break;
        }
    }

    // 3. 产出目标持仓（selection topN）。
    if (m_plan.selection.rankByIndex >= 0 && m_plan.selection.count > 0) {
        result.targetWeights = selectTargetWeights(
            result.crossValues.at(m_plan.selection.rankByIndex));
    }

    return result;
}

} // namespace pp
