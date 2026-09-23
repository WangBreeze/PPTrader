#pragma once

#include "domain/AuthorizedFactorImportTypes.h"
#include "domain/ExternalFactorResearchTypes.h"
#include "domain/MarketTypes.h"

#include <QHash>
#include <QVector>

namespace pp {

class ExternalFactorResearchEngine final {
public:
    static ExternalFactorResearchResult analyze(
        const ExternalFactorResearchRequest &request,
        const QVector<ExternalFactorObservation> &observations,
        const QHash<QString, QVector<Candle>> &candlesBySymbol);
};

} // namespace pp
