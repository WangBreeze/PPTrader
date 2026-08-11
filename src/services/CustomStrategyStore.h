#pragma once

#include "domain/CustomStrategyTypes.h"

#include <QVector>

namespace pp {

class CustomStrategyStore {
public:
    virtual ~CustomStrategyStore() = default;

    virtual bool insertCustomStrategy(const CustomStrategyDefinition &definition) = 0;
    virtual QVector<CustomStrategyDefinition> loadCustomStrategies() = 0;
};

} // namespace pp
