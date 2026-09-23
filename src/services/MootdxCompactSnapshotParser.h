#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace pp {

struct MootdxCompactQuoteRow {
    int symbolCode = 0;
    double price = 0.0;
    double volume = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double previousClose = 0.0;
    double amount = 0.0;
};

struct MootdxCompactSnapshotChunk {
    QString batchId;
    QDateTime timestamp;
    QVector<MootdxCompactQuoteRow> quotes;
    QString error;

    bool isValid() const { return error.isEmpty() && !batchId.isEmpty(); }
};

class MootdxCompactSnapshotParser final {
public:
    // Protocol: S<TAB>batchId<TAB>ISO-8601 UTC<TAB>
    // symbol,price,volume,bid,ask,open,high,low,previousClose,amount[;...]
    static MootdxCompactSnapshotChunk parse(const QByteArray &line);
    static bool parse(const QByteArray &line, MootdxCompactSnapshotChunk *result);
};

} // namespace pp
