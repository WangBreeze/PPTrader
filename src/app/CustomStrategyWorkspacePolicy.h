#pragma once

#include "domain/LocalHistoryTypes.h"

#include <QVariantMap>

namespace pp {

class CustomStrategyWorkspacePolicy final {
public:
    static QVariantMap localRunAvailability(bool definitionValid, bool executable,
                                            const QString &strategyId, int version,
                                            int minimumWarmupBars,
                                            const LocalHistoryDataset *dataset,
                                            int requestedBars, bool backtestRunning);
    static bool resultMatches(const QVariantMap &resultConfiguration,
                              const QString &strategyId, int version);
};

} // namespace pp
