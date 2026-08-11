#include "services/TencentQuoteParser.h"

#include <QDateTime>
#include <QTimeZone>

namespace pp {

namespace {

double numberAt(const QList<QByteArray> &fields, int index)
{
    if (index < 0 || index >= fields.size()) return 0.0;
    bool ok = false;
    const double value = fields.at(index).toDouble(&ok);
    return ok ? value : 0.0;
}

QDateTime quoteTime(const QByteArray &text)
{
    const QDateTime parsed = QDateTime::fromString(
        QString::fromLatin1(text), QStringLiteral("yyyyMMddHHmmss"));
    if (!parsed.isValid()) return {};
    return QDateTime(parsed.date(), parsed.time(), QTimeZone("Asia/Shanghai")).toUTC();
}

} // namespace

QVector<Tick> TencentQuoteParser::parse(
    const QByteArray &payload, const QHash<QString, Instrument> &instruments,
    const QString &batchId)
{
    QVector<Tick> result;
    const QList<QByteArray> lines = payload.split('\n');
    result.reserve(lines.size());
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (!line.startsWith("v_") || !line.endsWith(';')) continue;
        const qsizetype assignment = line.indexOf("=\"");
        const qsizetype closingQuote = line.lastIndexOf('"');
        if (assignment < 3 || closingQuote <= assignment + 2) continue;
        const QByteArray providerSymbol = line.mid(2, assignment - 2);
        if (providerSymbol.size() < 8) continue;
        const QString symbol = QString::fromLatin1(providerSymbol.right(6));
        const auto instrument = instruments.constFind(symbol);
        if (instrument == instruments.cend()) continue;
        const QByteArray body = line.mid(assignment + 2, closingQuote - assignment - 2);
        const QList<QByteArray> fields = body.split('~');
        if (fields.size() <= 35) continue;
        const double price = numberAt(fields, 3);
        if (price <= 0.0) continue;

        Tick tick;
        tick.instrument = *instrument;
        tick.timestamp = quoteTime(fields.at(30));
        tick.batchId = batchId;
        tick.price = price;
        tick.previousClose = numberAt(fields, 4);
        tick.open = numberAt(fields, 5);
        tick.volume = numberAt(fields, 6) * 100.0; // 腾讯 A 股报价成交量单位为手
        tick.bid = numberAt(fields, 9);
        tick.ask = numberAt(fields, 19);
        tick.high = numberAt(fields, 33);
        tick.low = numberAt(fields, 34);
        const QList<QByteArray> summary = fields.at(35).split('/');
        tick.amount = numberAt(summary, 2);
        result.append(tick);
    }
    return result;
}

QString TencentQuoteParser::providerCode(const Instrument &instrument)
{
    if (instrument.market != Market::AShare || instrument.symbol.size() != 6)
        return {};
    if (instrument.venue.contains(QStringLiteral("北交"))
        || instrument.symbol.startsWith(QStringLiteral("43"))
        || instrument.symbol.startsWith(QStringLiteral("82"))
        || instrument.symbol.startsWith(QStringLiteral("83"))
        || instrument.symbol.startsWith(QStringLiteral("87"))
        || instrument.symbol.startsWith(QStringLiteral("88"))
        || instrument.symbol.startsWith(QStringLiteral("92"))) {
        return {};
    }
    if (instrument.venue.contains(QStringLiteral("上交"))
        || instrument.symbol.startsWith(QStringLiteral("6"))) {
        return QStringLiteral("sh") + instrument.symbol;
    }
    if (instrument.venue.contains(QStringLiteral("深交"))
        || instrument.symbol.startsWith(QStringLiteral("0"))
        || instrument.symbol.startsWith(QStringLiteral("3"))) {
        return QStringLiteral("sz") + instrument.symbol;
    }
    return {};
}

} // namespace pp
