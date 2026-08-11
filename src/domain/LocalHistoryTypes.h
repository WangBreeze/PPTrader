#pragma once

#include "domain/MarketTypes.h"

#include <QVariantMap>

namespace pp {

struct LocalHistoryDataset {
    Instrument instrument;
    int periodSeconds = 0;
    int candleCount = 0;
    QDateTime firstTime;
    QDateTime lastTime;
    QString source;
    QString adjustment = QStringLiteral("none");
    QDateTime downloadedAt;
    QString dataVersion;

    QString frequency() const;
    bool isRawTickDerived() const;
    bool isBacktestEligible() const;
    QVariantMap toVariantMap() const;
};

QString localHistoryFrequency(int periodSeconds);

} // namespace pp

Q_DECLARE_METATYPE(pp::LocalHistoryDataset)
