#pragma once

#include "domain/CustomStrategyTypes.h"

#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace pp {

enum class CustomIndicatorKind { Sma, Ema, Rsi, Macd, Bollinger, Atr, VolumeSma,
                                 Roc, Vwap, PpFlow, Wma, Bias, Cci, Wr, Donchian, Obv, Mfi, Kdj };
enum class CustomOperandKind { Constant, Open, High, Low, Close, Volume, Indicator,
                               Cross };
enum class CustomCrossIndicatorKind { Rank, ZScore, Percentile, Mean, Median, Peer };
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
    int kPeriod = 0;
    int dPeriod = 0;

    int minimumBars() const;
    QString cacheKey() const;
};

// 横截面指标：在同一帧的 n 只股票之间计算的统计。
struct CompiledCrossIndicator {
    CustomCrossIndicatorKind kind = CustomCrossIndicatorKind::Rank;
    QString alias;
    CustomOperandKind field = CustomOperandKind::Close; // of.field 时使用
    int indicatorIndex = -1;                            // of.indicator 时使用
    bool ascending = false;
    QString peerSymbol;                                 // PEER 专用
    CustomOperandKind peerField = CustomOperandKind::Close; // PEER 专用
};

struct CompiledSelection {
    int rankByIndex = -1; // crossIndicators 索引
    int count = 0;
    bool ascending = false;
};

struct CompiledCustomOperand {
    CustomOperandKind kind = CustomOperandKind::Constant;
    double constant = 0.0;
    int indicatorIndex = -1;
    int crossIndex = -1; // 引用 crossIndicators 索引
};

struct CompiledCustomCondition {
    CustomConditionOp op = CustomConditionOp::Greater;
    QVector<int> children;
    int leftOperand = -1;
    int rightOperand = -1;
};

struct CompiledCustomRules {
    double positionFraction = 1.0;
    double positionAmount = 0.0;     // fixedAmount 金额（元），0 表示用 fixedPercent
    double maxPositionPercent = 0.0; // 最大持仓市值占比（%），0 表示无上限
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
    QVector<CompiledCrossIndicator> crossIndicators;
    CompiledSelection selection;
    QStringList universeSymbols;
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
