#pragma once

#include "domain/FactorResearchTypes.h"

namespace pp {

class FactorOutOfSampleAnalyzer final {
public:
    static FactorOutOfSampleSummary analyze(
        const QVector<FactorResearchPoint> &timeSeries,
        const FactorOutOfSamplePolicy &policy = {});
};

} // namespace pp
