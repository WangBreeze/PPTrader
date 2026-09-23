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
    QString datasetVersion;
    QDate datasetFirstDate;
    QDate datasetLastDate;
    int datasetDayCount = 0;
    int datasetRawTickCount = 0;
    int datasetGapDayCount = 0;
    int datasetStaleDayCount = 0;

    QString frequency() const;
    bool isRawTickDerived() const;
    bool isBacktestEligible() const;
    QVariantMap toVariantMap() const;
};

QString localHistoryFrequency(int periodSeconds);
int localHistoryPeriodSeconds(const QString &frequency);

} // namespace pp

Q_DECLARE_METATYPE(pp::LocalHistoryDataset)
