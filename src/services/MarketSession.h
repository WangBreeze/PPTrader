#pragma once

#include "domain/MarketTypes.h"

namespace pp {

class MarketSession {
public:
    static bool isOpen(const Instrument &instrument, const QDateTime &utcNow);
    static QString statusText(const Instrument &instrument, const QDateTime &utcNow);
};

} // 命名空间 pp
