#include "services/TencentQuoteParser.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>

namespace pp {

namespace {

struct ByteSpan {
    const char *data = nullptr;
    qsizetype size = 0;
};

ByteSpan trimmed(ByteSpan value)
{
    while (value.size > 0
           && (value.data[0] == ' ' || value.data[0] == '\t'
               || value.data[0] == '\r')) {
        ++value.data;
        --value.size;
    }
    while (value.size > 0
           && (value.data[value.size - 1] == ' '
               || value.data[value.size - 1] == '\t'
               || value.data[value.size - 1] == '\r')) {
        --value.size;
    }
    return value;
}

qsizetype findBytes(ByteSpan value, const char *needle, qsizetype needleSize)
{
    if (needleSize <= 0 || value.size < needleSize) return -1;
    for (qsizetype index = 0; index <= value.size - needleSize; ++index) {
        if (std::memcmp(value.data + index, needle,
                        static_cast<size_t>(needleSize)) == 0) {
            return index;
        }
    }
    return -1;
}

double number(ByteSpan value)
{
    if (!value.data || value.size <= 0) return 0.0;
    double parsed = 0.0;
    const auto result = std::from_chars(
        value.data, value.data + value.size, parsed, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == value.data + value.size
        ? parsed : 0.0;
}

int digits(ByteSpan value, qsizetype offset, qsizetype length)
{
    if (!value.data || offset < 0 || length <= 0
        || offset + length > value.size) return -1;
    int result = 0;
    for (qsizetype index = 0; index < length; ++index) {
        const char digit = value.data[offset + index];
        if (digit < '0' || digit > '9') return -1;
        result = result * 10 + digit - '0';
    }
    return result;
}

QDateTime quoteTime(ByteSpan text)
{
    if (text.size != 14) return {};
    const QDate date(digits(text, 0, 4), digits(text, 4, 2), digits(text, 6, 2));
    const QTime time(digits(text, 8, 2), digits(text, 10, 2), digits(text, 12, 2));
    if (!date.isValid() || !time.isValid()) return {};
    static const QTimeZone shanghai("Asia/Shanghai");
    return QDateTime(date, time, shanghai).toUTC();
}

int splitQuoteFields(ByteSpan body, std::array<ByteSpan, 36> *fields)
{
    int fieldIndex = 0;
    qsizetype fieldStart = 0;
    for (qsizetype index = 0; index <= body.size; ++index) {
        if (index != body.size && body.data[index] != '~') continue;
        if (fieldIndex < static_cast<int>(fields->size())) {
            fields->at(static_cast<size_t>(fieldIndex)) =
                {body.data + fieldStart, index - fieldStart};
        }
        ++fieldIndex;
        fieldStart = index + 1;
    }
    return fieldIndex;
}

double summaryAmount(ByteSpan summary)
{
    qsizetype slashCount = 0;
    for (qsizetype index = 0; index < summary.size; ++index) {
        if (summary.data[index] != '/') continue;
        ++slashCount;
        if (slashCount == 2) {
            return number({summary.data + index + 1, summary.size - index - 1});
        }
    }
    return 0.0;
}

} // namespace

QVector<Tick> TencentQuoteParser::parse(
    const QByteArray &payload, const QHash<QString, Instrument> &instruments,
    const QString &batchId)
{
    QVector<Tick> result;
    result.reserve(std::min<qsizetype>(
        instruments.size(), payload.count('\n') + 1));
    qsizetype lineStart = 0;
    while (lineStart < payload.size()) {
        qsizetype lineEnd = payload.indexOf('\n', lineStart);
        if (lineEnd < 0) lineEnd = payload.size();
        const ByteSpan line = trimmed(
            {payload.constData() + lineStart, lineEnd - lineStart});
        lineStart = lineEnd + 1;
        if (line.size < 6 || line.data[0] != 'v' || line.data[1] != '_'
            || line.data[line.size - 1] != ';') continue;
        const qsizetype assignment = findBytes(line, "=\"", 2);
        qsizetype closingQuote = -1;
        for (qsizetype index = line.size - 1; index >= 0; --index) {
            if (line.data[index] == '"') {
                closingQuote = index;
                break;
            }
        }
        if (assignment < 3 || closingQuote <= assignment + 2) continue;
        const ByteSpan providerSymbol {line.data + 2, assignment - 2};
        if (providerSymbol.size < 8) continue;
        const QString symbol = QString::fromLatin1(
            providerSymbol.data + providerSymbol.size - 6, 6);
        const auto instrument = instruments.constFind(symbol);
        if (instrument == instruments.cend()) continue;
        const ByteSpan body {line.data + assignment + 2,
                             closingQuote - assignment - 2};
        std::array<ByteSpan, 36> fields {};
        if (splitQuoteFields(body, &fields) <= 35) continue;
        const double price = number(fields[3]);
        if (price <= 0.0) continue;

        Tick tick;
        tick.instrument = *instrument;
        tick.timestamp = quoteTime(fields[30]);
        tick.batchId = batchId;
        tick.price = price;
        tick.previousClose = number(fields[4]);
        tick.open = number(fields[5]);
        tick.volume = number(fields[6]) * 100.0; // 腾讯 A 股报价成交量单位为手
        tick.bid = number(fields[9]);
        tick.ask = number(fields[19]);
        tick.high = number(fields[33]);
        tick.low = number(fields[34]);
        tick.amount = summaryAmount(fields[35]);
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
