#pragma once

#include "domain/MarketTypes.h"

#include <QDate>
#include <QVariantMap>
#include <QVector>

namespace pp {

enum class AShareInstrumentDayState {
    Unknown = 0,
    Tradable = 1,
    Suspended = 2
};

inline QString aShareInstrumentDayStateName(AShareInstrumentDayState state)
{
    switch (state) {
    case AShareInstrumentDayState::Tradable:
        return QStringLiteral("tradable");
    case AShareInstrumentDayState::Suspended:
        return QStringLiteral("suspended");
    case AShareInstrumentDayState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

struct AShareInstrumentDayRecord {
    Instrument instrument;
    QDate tradingDate;
    AShareInstrumentDayState state = AShareInstrumentDayState::Unknown;
    QString reason;
    QString source;
    QString sourceVersion;

    bool isValid() const
    {
        return instrument.market == Market::AShare
            && !instrument.symbol.trimmed().isEmpty()
            && tradingDate.isValid()
            && state != AShareInstrumentDayState::Unknown
            && !source.trimmed().isEmpty()
            && !sourceVersion.trimmed().isEmpty();
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("symbol"), instrument.symbol},
                {QStringLiteral("name"), instrument.name},
                {QStringLiteral("venue"), instrument.venue},
                {QStringLiteral("tradingDate"), tradingDate},
                {QStringLiteral("state"), aShareInstrumentDayStateName(state)},
                {QStringLiteral("reason"), reason},
                {QStringLiteral("source"), source},
                {QStringLiteral("sourceVersion"), sourceVersion}};
    }
};

struct AShareCalendarSnapshot {
    QDate firstDate;
    QDate lastDate;
    QVector<QDate> tradingDays;
    QVector<AShareInstrumentDayRecord> instrumentDays;
    bool marketCoverageComplete = false;
    bool instrumentCoverageComplete = false;
    QString marketSource;
    QString snapshotVersion;
    int suspendedInstrumentDays = 0;
    int unknownInstrumentDays = 0;

    AShareInstrumentDayState instrumentState(const QString &symbol,
                                             const QDate &date) const
    {
        for (const AShareInstrumentDayRecord &record : instrumentDays) {
            if (record.instrument.symbol == symbol
                && record.tradingDate == date) {
                return record.state;
            }
        }
        return AShareInstrumentDayState::Unknown;
    }

    QVariantMap toVariantMap() const
    {
        QVariantList days;
        days.reserve(tradingDays.size());
        for (const QDate &date : tradingDays) days.append(date);
        QVariantList statuses;
        statuses.reserve(instrumentDays.size());
        for (const AShareInstrumentDayRecord &record : instrumentDays)
            statuses.append(record.toVariantMap());
        return {{QStringLiteral("firstDate"), firstDate},
                {QStringLiteral("lastDate"), lastDate},
                {QStringLiteral("tradingDays"), days},
                {QStringLiteral("instrumentDays"), statuses},
                {QStringLiteral("marketCoverageComplete"), marketCoverageComplete},
                {QStringLiteral("instrumentCoverageComplete"),
                 instrumentCoverageComplete},
                {QStringLiteral("marketSource"), marketSource},
                {QStringLiteral("snapshotVersion"), snapshotVersion},
                {QStringLiteral("suspendedInstrumentDays"),
                 suspendedInstrumentDays},
                {QStringLiteral("unknownInstrumentDays"),
                 unknownInstrumentDays}};
    }
};

} // namespace pp
