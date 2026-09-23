#pragma once

#include "domain/BacktestDataQualityTypes.h"

namespace pp {

class BacktestDataQualityGate final {
public:
    static BacktestDataQualityAssessment assess(
        const BacktestDatasetIdentity &identity,
        const QDate &requestedStartDate,
        const QDate &requestedEndDate,
        const QVector<AShareInstrumentDayRecord> &instrumentDays = {},
        const QVector<RawTickImportDayAudit> &importAudits = {},
        bool corporateActionCoverageKnown = false,
        const QVector<QDate> &corporateActionDates = {},
        const BacktestDataQualityPolicy &policy = {});
};

} // namespace pp
