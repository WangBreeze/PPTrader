#include "services/MootdxCompactSnapshotParser.h"

#include <QByteArray>

#include <charconv>
#include <cmath>

namespace pp {
namespace {

bool parseNumber(const char *begin, const char *end, double *value)
{
    if (!value || begin == end) return false;
    const auto parsed = std::from_chars(begin, end, *value);
    return parsed.ec == std::errc{} && parsed.ptr == end && std::isfinite(*value);
}

bool parseSymbol(const char *begin, const char *end, int *symbolCode)
{
    if (!symbolCode || end - begin != 6) return false;
    int value = 0;
    for (const char *cursor = begin; cursor != end; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return false;
        value = value * 10 + (*cursor - '0');
    }
    *symbolCode = value;
    return true;
}

} // namespace

MootdxCompactSnapshotChunk MootdxCompactSnapshotParser::parse(const QByteArray &line)
{
    MootdxCompactSnapshotChunk result;
    parse(line, &result);
    return result;
}

bool MootdxCompactSnapshotParser::parse(
    const QByteArray &line, MootdxCompactSnapshotChunk *resultPointer)
{
    if (!resultPointer) return false;
    MootdxCompactSnapshotChunk &result = *resultPointer;
    result.batchId.clear();
    result.timestamp = {};
    result.quotes.clear();
    result.error.clear();
    if (!line.startsWith("S\t")) {
        result.error = QStringLiteral("紧凑行情块缺少 S 前缀");
        return false;
    }
    const qsizetype batchEnd = line.indexOf('\t', 2);
    const qsizetype timestampEnd = batchEnd < 0 ? -1 : line.indexOf('\t', batchEnd + 1);
    if (batchEnd <= 2 || timestampEnd <= batchEnd + 1 || timestampEnd + 1 >= line.size()) {
        result.error = QStringLiteral("紧凑行情块头部字段不完整");
        return false;
    }
    result.batchId = QString::fromLatin1(line.constData() + 2, batchEnd - 2);
    result.timestamp = QDateTime::fromString(
        QString::fromLatin1(line.constData() + batchEnd + 1,
                            timestampEnd - batchEnd - 1),
        Qt::ISODateWithMs);
    if (!result.timestamp.isValid()) {
        result.error = QStringLiteral("紧凑行情块时间无效");
        return false;
    }

    result.quotes.reserve(line.count(';') + 1);
    qsizetype rowStart = timestampEnd + 1;
    int rowNumber = 0;
    while (rowStart < line.size()) {
        ++rowNumber;
        qsizetype rowEnd = line.indexOf(';', rowStart);
        if (rowEnd < 0) rowEnd = line.size();
        const char *fieldBegin = line.constData() + rowStart;
        const char *rowLimit = line.constData() + rowEnd;
        const char *fieldEnds[10] {};
        const char *cursor = fieldBegin;
        for (int field = 0; field < 9; ++field) {
            const char *separator = cursor;
            while (separator != rowLimit && *separator != ',') ++separator;
            if (separator == rowLimit) {
                result.error = QStringLiteral("紧凑行情块第 %1 行字段不足").arg(rowNumber);
                result.quotes.clear();
                return false;
            }
            fieldEnds[field] = separator;
            cursor = separator + 1;
        }
        fieldEnds[9] = rowLimit;
        int symbolCode = 0;
        if (!parseSymbol(fieldBegin, fieldEnds[0], &symbolCode)) {
            result.error = QStringLiteral("紧凑行情块第 %1 行代码无效").arg(rowNumber);
            result.quotes.clear();
            return false;
        }

        MootdxCompactQuoteRow quote;
        quote.symbolCode = symbolCode;
        double *values[] = {&quote.price, &quote.volume, &quote.bid, &quote.ask,
                            &quote.open, &quote.high, &quote.low,
                            &quote.previousClose, &quote.amount};
        const char *numberBegin = fieldEnds[0] + 1;
        for (int field = 0; field < 9; ++field) {
            if (!parseNumber(numberBegin, fieldEnds[field + 1], values[field])) {
                result.error = QStringLiteral("紧凑行情块第 %1 行第 %2 个数值无效")
                                   .arg(rowNumber).arg(field + 1);
                result.quotes.clear();
                return false;
            }
            numberBegin = fieldEnds[field + 1] + 1;
        }
        if (quote.price > 0.0) result.quotes.append(std::move(quote));
        rowStart = rowEnd + 1;
    }
    if (result.quotes.isEmpty()) result.error = QStringLiteral("紧凑行情块没有有效报价");
    return result.isValid();
}

} // namespace pp
