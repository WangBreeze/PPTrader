#pragma once

#include "domain/MarketTypes.h"

#include <QDate>
#include <QMetaType>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace pp {

struct HistoricalProviderCapability {
    QString providerId;
    QString displayName;
    QVector<Market> markets;
    QStringList venues;
    QStringList frequencies;
    QStringList adjustments;
    QDate earliestDate;
    int maximumBarsPerRequest = 0;
    int rateLimitPerMinute = 0;
    int priority = 100;
    bool authenticationRequired = false;
    bool configured = false;
    bool supportsSuspended = false;
    bool supportsDelisted = false;
    QString configurationMessage;

    bool supports(const Instrument &instrument, const QString &frequency,
                  const QString &adjustment) const;
    QVariantMap toVariantMap() const;
};

QString historicalAdjustmentName(const QString &adjustment);

} // namespace pp

Q_DECLARE_METATYPE(pp::HistoricalProviderCapability)
