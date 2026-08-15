#pragma once

#include "strategy/Strategy.h"

#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace pp {

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

    QVariantMap toVariantMap() const;
};

class BacktestStrategy : public Strategy {
public:
    using Strategy::Strategy;
    ~BacktestStrategy() override = default;

    virtual int minimumWarmupBars() const = 0;
    virtual double positionFraction() const { return 1.0; }
    virtual double positionAmount() const { return 0.0; }
    virtual double maxPositionPercent() const { return 0.0; }
    virtual int strategyVersion() const { return 0; }
    virtual QVariantMap normalizedParameters() const { return {}; }
    virtual QVariantMap ruleSnapshot() const { return {}; }
    virtual void reset() = 0;
};

} // namespace pp
