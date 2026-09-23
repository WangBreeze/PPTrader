#pragma once

#include "domain/FactorResearchTypes.h"

namespace pp {

class FactorResearchAnalyzer {
public:
    static FactorResearchSummary analyze(
        const QVector<FactorResearchFrame> &frames,
        const FactorPreprocessingPolicy &policy,
        const FactorResearchCostPolicy &costPolicy = {},
        const FactorNeutralizationPolicy &neutralizationPolicy = {});
};

} // namespace pp
