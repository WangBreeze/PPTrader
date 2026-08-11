#pragma once

#include "domain/StrategyExecutionTypes.h"

namespace pp {

class StrategyExecutionStore {
public:
    virtual ~StrategyExecutionStore() = default;

    virtual bool upsertStrategyConfiguration(const StrategyExecutionConfig &config) = 0;
    virtual bool removeStrategyConfiguration(const QString &strategyId) = 0;
    virtual bool upsertStrategyIntent(const StrategyOrderIntent &intent) = 0;
    virtual QVector<StrategyExecutionConfig> loadStrategyConfigurations() = 0;
    virtual QVector<StrategyOrderIntent> loadStrategyIntents() = 0;
};

} // namespace pp
