#pragma once

#include "domain/MarketTypes.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace pp {

class TencentQuoteParser final {
public:
    static QVector<Tick> parse(const QByteArray &payload,
                               const QHash<QString, Instrument> &instruments,
                               const QString &batchId);
    static QString providerCode(const Instrument &instrument);
};

} // namespace pp
