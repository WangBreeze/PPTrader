#pragma once

#include "domain/MarketTypes.h"

#include <QString>
#include <QVector>

namespace pp {

struct AShareUniverseLoadResult {
    QVector<Instrument> instruments;
    QString cachePath;
    QString error;
    int sourceCount = 0;
    int rejectedCount = 0;
};

class AShareUniverseLoader final {
public:
    static QString findDefaultCachePath();
    static AShareUniverseLoadResult load(const QString &cachePath = {});
    static QString venueForCode(const QString &code, const QString &marketLabel = {});
};

} // namespace pp
