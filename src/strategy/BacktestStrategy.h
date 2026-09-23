#pragma once

#include "strategy/Strategy.h"

#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace pp {

enum class CandleExecutionBasis { Close, NextOpen };

struct CandleExecutionContext {
    CandleExecutionBasis basis = CandleExecutionBasis::Close;
    QDateTime decisionTime;
};

struct BacktestParameterSpec {
    QString key;
    QString displayName;
    QString type;
    QVariant defaultValue;
    QVariant minimumValue;
    QVariant maximumValue;
    QVariant step;

    QVariantMap toVariantMap() const;
};

struct BacktestStrategyDescriptor {
    QString id;
    QString displayName;
    QString description;
    QString supportedMarket = QStringLiteral("A股");
    int minimumWarmupBars = 0;
    QVector<BacktestParameterSpec> parameters;
    bool supportsRawTicks = false;
    bool supportsCandles = true;

    QVariantMap toVariantMap() const;
};

class BacktestStrategy : public Strategy {
public:
    using Strategy::Strategy;
    ~BacktestStrategy() override = default;

    virtual int minimumWarmupBars() const = 0;
    virtual double positionFraction() const { return 1.0; }
    virtual double positionAmount() const { return 0.0; }
    virtual int targetPositionShares() const { return 0; }
    virtual double maxPositionPercent() const { return 0.0; }
    virtual int strategyVersion() const { return 0; }
    virtual QVariantMap normalizedParameters() const { return {}; }
    virtual QVariantMap ruleSnapshot() const { return {}; }
    // 仅用于回测结果审计；不代替执行层的实际成交与持仓事实。
    virtual QVariantMap evaluationFacts() const { return {}; }
    // 共享资金组合可能拒绝信号；策略必须能撤销仅由该信号预置的持仓状态。
    virtual void onExecutionRejected(const TradeSignal &) {}
    // 仅当前同步 signalGenerated 的成交意图；实际报价由执行层从输入 Bar 解析。
    virtual CandleExecutionContext candleExecutionContext() const { return {}; }
    virtual void reset() = 0;
};

} // namespace pp
