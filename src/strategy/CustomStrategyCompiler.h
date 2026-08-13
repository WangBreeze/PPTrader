#pragma once

#include "domain/CustomStrategyTypes.h"

#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pp {

enum class CustomIndicatorKind { Sma, Ema, Rsi, Macd, Bollinger, Atr, VolumeSma,
                                 Roc, Vwap, PpFlow };
enum class CustomOperandKind { Constant, Open, High, Low, Close, Volume, Indicator };
enum class CustomSignalPriceMode { TriggerClose, NextOpen };
enum class CustomConditionOp {
    And, Or, Not, Greater, GreaterOrEqual, Less, LessOrEqual, Equal,
    CrossesAbove, CrossesBelow
};

struct CompiledCustomIndicator {
    CustomIndicatorKind kind = CustomIndicatorKind::Sma;
    int period = 0;
    int fastPeriod = 0;
    int slowPeriod = 0;
    int signalPeriod = 0;
    double standardDeviations = 0.0;
    QString output;
    QString alias;

    int minimumBars() const;
    QString cacheKey() const;
};

struct CompiledCustomOperand {
    CustomOperandKind kind = CustomOperandKind::Constant;
    double constant = 0.0;
    int indicatorIndex = -1;
};

struct CompiledCustomCondition {
    CustomConditionOp op = CustomConditionOp::Greater;
    QVector<int> children;
    int leftOperand = -1;
    int rightOperand = -1;
};

struct CompiledCustomRules {
    double positionFraction = 1.0;
    double stopLossPercent = 0.0;
    double takeProfitPercent = 0.0;
    int maximumHoldingBars = 0;
    bool tPlusOne = true;

    QVariantMap toVariantMap() const;
};

struct CompiledCustomStrategy {
    QString strategyId;
    QString name;
    int version = 1;
    QVariantMap normalizedParameters;
    QVector<CompiledCustomIndicator> indicators;
    QVector<CompiledCustomOperand> operands;
    QVector<CompiledCustomCondition> conditions;
    int entryCondition = -1;
    int exitCondition = -1;
    int minimumWarmupBars = 1;
    CompiledCustomRules rules;
    CustomSignalPriceMode signalPriceMode = CustomSignalPriceMode::TriggerClose;
};

class CustomStrategyCompiler {
public:
    std::optional<CompiledCustomStrategy> compile(
        const CustomStrategyDefinition &definition, const QVariantMap &runtimeParameters,
        QString *error = nullptr) const;
};

} // namespace pp
