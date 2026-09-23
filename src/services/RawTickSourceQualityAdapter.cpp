#include "services/RawTickSourceQualityAdapter.h"

#include "services/HistoricalTickSessionRules.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTime>

#include <cmath>
#include <optional>
#include <utility>

namespace pp {
namespace {

void appendDiagnostic(RawTickSourceQualityReport *report, qint64 lineNumber,
                      const QString &reason, bool fatal,
                      int maximumDiagnostics)
{
    if (!report) return;
    ++report->diagnosticTotalCount;
    if (report->diagnostics.size() >= maximumDiagnostics) {
        report->diagnosticsTruncated = true;
        return;
    }
    report->diagnostics.append(
        {report->filePath, lineNumber, reason, fatal});
}

bool parseFiniteNonNegative(const QByteArray &value, double *result)
{
    bool ok = false;
    const double parsed = value.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(parsed) || parsed < 0.0) return false;
    if (result) *result = parsed;
    return true;
}

std::optional<QTime> parseClock(const QByteArray &value)
{
    const QString text = QString::fromLatin1(value.trimmed());
    QTime time = QTime::fromString(text, QStringLiteral("HH:mm:ss.zzz"));
    if (!time.isValid())
        time = QTime::fromString(text, QStringLiteral("HH:mm:ss"));
    if (!time.isValid()) return std::nullopt;
    return time;
}

bool englishHeader(const QList<QByteArray> &fields)
{
    return fields.size() == 5
        && fields.at(0).trimmed().compare("time", Qt::CaseInsensitive) == 0
        && fields.at(1).trimmed().compare("price", Qt::CaseInsensitive) == 0
        && fields.at(2).trimmed().compare("volume", Qt::CaseInsensitive) == 0
        && fields.at(3).trimmed().compare("amount", Qt::CaseInsensitive) == 0
        && fields.at(4).trimmed().compare("type", Qt::CaseInsensitive) == 0;
}

bool chineseHeader(const QList<QByteArray> &fields)
{
    return fields.size() == 6
        && QString::fromUtf8(fields.at(0).trimmed()) == QStringLiteral("成交时间")
        && QString::fromUtf8(fields.at(1).trimmed()) == QStringLiteral("成交价格")
        && QString::fromUtf8(fields.at(3).trimmed()) == QStringLiteral("成交量")
        && QString::fromUtf8(fields.at(4).trimmed()) == QStringLiteral("成交金额")
        && QString::fromUtf8(fields.at(5).trimmed()) == QStringLiteral("性质");
}

std::optional<RawTradeDirection> directionFor(
    const QByteArray &value, const RawTickSourceQualityRequest &request,
    bool *usedExplicitMapping)
{
    if (usedExplicitMapping) *usedExplicitMapping = false;
    const QString text = QString::fromUtf8(value.trimmed());
    const QString upper = text.toUpper();
    if (upper == QStringLiteral("B") || text == QStringLiteral("买盘"))
        return RawTradeDirection::Buy;
    if (upper == QStringLiteral("S") || text == QStringLiteral("卖盘"))
        return RawTradeDirection::Sell;
    if (upper == QStringLiteral("M") || text == QStringLiteral("中性盘"))
        return RawTradeDirection::Neutral;
    const auto mapped = request.explicitDirectionMappings.constFind(text);
    if (mapped != request.explicitDirectionMappings.cend()) {
        if (usedExplicitMapping) *usedExplicitMapping = true;
        return mapped.value();
    }
    return std::nullopt;
}

} // namespace

RawTickSourceQualityReport RawTickSourceQualityAdapter::inspect(
    const RawTickSourceQualityRequest &request)
{
    RawTickSourceQualityReport report;
    report.filePath = request.filePath;
    report.symbol = request.instrument.symbol.trimmed();
    report.tradingDate = request.tradingDate;
    report.volumeUnit = rawTickSourceVolumeUnitName(request.volumeUnit);
    report.directionMappingVersion = request.directionMappingVersion.trimmed();
    const int maximumDiagnostics = qBound(1, request.maximumDiagnostics, 10000);

    if (report.filePath.trimmed().isEmpty()
        || report.symbol.isEmpty()
        || request.instrument.market != Market::AShare
        || !report.tradingDate.isValid()) {
        report.messages.append(QStringLiteral("源文件检查请求无效"));
        appendDiagnostic(&report, 0, report.messages.last(), true,
                         maximumDiagnostics);
        return report;
    }

    QFile file(report.filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        report.actionCode = QFileInfo::exists(report.filePath)
            ? QStringLiteral("unreadable_source_file")
            : QStringLiteral("missing_source_file");
        report.messages.append(QStringLiteral("无法以只读方式打开源文件"));
        appendDiagnostic(&report, 0, report.messages.last(), true,
                         maximumDiagnostics);
        return report;
    }

    report.fileSize = QFileInfo(file).size();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    bool layoutDetected = false;
    bool chineseLayout = false;
    std::optional<QTime> previousAcceptedTime;
    while (!file.atEnd()) {
        const QByteArray rawLine = file.readLine();
        hash.addData(rawLine);
        ++report.linesRead;
        report.maximumLineBytes = qMax(report.maximumLineBytes,
                                       static_cast<qint64>(rawLine.size()));
        QByteArray line = rawLine.trimmed();
        if (report.linesRead == 1 && line.startsWith("\xEF\xBB\xBF"))
            line.remove(0, 3);
        if (line.isEmpty()) {
            ++report.skippedRecords;
            appendDiagnostic(&report, report.linesRead,
                             QStringLiteral("空行"), false,
                             maximumDiagnostics);
            continue;
        }
        const QList<QByteArray> fields = line.split(',');
        if (!layoutDetected) {
            layoutDetected = true;
            if (englishHeader(fields)) {
                report.detectedFormat = QStringLiteral("english_tick5");
                ++report.metadataLines;
                continue;
            }
            if (chineseHeader(fields)) {
                report.detectedFormat = QStringLiteral("chinese_tick6");
                chineseLayout = true;
                ++report.metadataLines;
                continue;
            }
            if (fields.size() == 5)
                report.detectedFormat = QStringLiteral("headerless_tick5");
        }

        const int expectedFields = chineseLayout ? 6 : 5;
        if (fields.size() != expectedFields) {
            ++report.skippedRecords;
            appendDiagnostic(&report, report.linesRead,
                             QStringLiteral("逐笔记录列数与识别格式不一致"),
                             false, maximumDiagnostics);
            continue;
        }
        const int volumeField = chineseLayout ? 3 : 2;
        const int amountField = chineseLayout ? 4 : 3;
        const int directionField = chineseLayout ? 5 : 4;
        double price = 0.0;
        double volume = 0.0;
        double amount = 0.0;
        const auto time = parseClock(fields.at(0));
        if (!time.has_value()
            || !parseFiniteNonNegative(fields.at(1), &price) || price <= 0.0
            || !parseFiniteNonNegative(fields.at(volumeField), &volume)
            || !parseFiniteNonNegative(fields.at(amountField), &amount)) {
            ++report.skippedRecords;
            appendDiagnostic(&report, report.linesRead,
                             QStringLiteral("时间、价格、成交量或成交额无效"),
                             false, maximumDiagnostics);
            continue;
        }
        bool usedExplicitMapping = false;
        const auto direction = directionFor(fields.at(directionField), request,
                                            &usedExplicitMapping);
        if (!direction.has_value()) {
            ++report.skippedRecords;
            appendDiagnostic(&report, report.linesRead,
                             QStringLiteral("成交方向没有受控映射"), false,
                             maximumDiagnostics);
            continue;
        }
        if (!HistoricalTickSessionRules::accepts(*time)) {
            ++report.skippedRecords;
            ++report.outsideSessionRecords;
            appendDiagnostic(&report, report.linesRead,
                             QStringLiteral("记录位于 A 股有效时段之外"),
                             false, maximumDiagnostics);
            continue;
        }
        if (previousAcceptedTime.has_value() && *time < *previousAcceptedTime)
            ++report.outOfOrderRecords;
        previousAcceptedTime = time;
        ++report.validRecords;
        report.totalSourceVolume += volume;
        if (request.volumeUnit == RawTickSourceVolumeUnit::Lots)
            report.normalizedVolumeShares += volume * 100.0;
        else if (request.volumeUnit == RawTickSourceVolumeUnit::Shares)
            report.normalizedVolumeShares += volume;
        if (usedExplicitMapping) ++report.mappedDirectionRecords;
        switch (*direction) {
        case RawTradeDirection::Buy: ++report.buyRecords; break;
        case RawTradeDirection::Sell: ++report.sellRecords; break;
        case RawTradeDirection::Neutral: ++report.neutralRecords; break;
        }
    }
    report.contentSha256 = QStringLiteral("sha256:")
        + QString::fromLatin1(hash.result().toHex());
    report.inspected = true;
    const qint64 assessedRecords = report.validRecords + report.skippedRecords;
    report.skippedRecordRatio = assessedRecords > 0
        ? static_cast<double>(report.skippedRecords) / assessedRecords : 0.0;

    const bool volumeKnown = request.volumeUnit != RawTickSourceVolumeUnit::Unknown;
    const bool mappingVersionKnown = report.mappedDirectionRecords == 0
        || !report.directionMappingVersion.isEmpty();
    const bool rejected = report.validRecords == 0 || !volumeKnown
        || !mappingVersionKnown || report.skippedRecordRatio > 0.01;
    report.qualityStatus = rejected ? QStringLiteral("reject")
        : (report.skippedRecords > 0 || report.outOfOrderRecords > 0)
            ? QStringLiteral("warning") : QStringLiteral("pass");
    report.normalizationReady = !rejected;
    if (!volumeKnown)
        report.messages.append(QStringLiteral("成交量单位未知，不能规范化"));
    if (!mappingVersionKnown)
        report.messages.append(QStringLiteral(
            "使用了显式成交方向映射，但未提供映射合同版本"));
    if (report.validRecords == 0)
        report.messages.append(QStringLiteral("没有可规范化的有效逐笔记录"));
    if (report.skippedRecordRatio > 0.01)
        report.messages.append(QStringLiteral("坏行率超过 1%，必须清洁重导"));
    if (report.outsideSessionRecords > 0)
        report.messages.append(QStringLiteral("存在 A 股有效时段之外的记录"));
    if (report.outOfOrderRecords > 0)
        report.messages.append(QStringLiteral("存在时间倒序记录"));
    if (!volumeKnown) {
        report.actionCode = QStringLiteral("specify_volume_unit");
    } else if (!mappingVersionKnown) {
        report.actionCode = QStringLiteral("provide_direction_mapping_version");
    } else if (report.validRecords == 0
               || report.skippedRecordRatio > 0.01) {
        report.actionCode = QStringLiteral("clean_reexport");
    } else if (report.skippedRecords > 0 || report.outOfOrderRecords > 0) {
        report.actionCode = QStringLiteral("review_quality_warnings");
    } else {
        report.actionCode = QStringLiteral("ready");
    }
    return report;
}

RawTickSourceQualityManifest RawTickSourceQualityAdapter::inspectBatch(
    const QVector<RawTickSourceQualityRequest> &requests)
{
    RawTickSourceQualityManifest manifest;
    manifest.totalFiles = requests.size();
    manifest.reports.reserve(requests.size());
    for (const RawTickSourceQualityRequest &request : requests) {
        RawTickSourceQualityReport report = inspect(request);
        if (report.inspected) ++manifest.inspectedFiles;
        if (report.normalizationReady) ++manifest.readyFiles;
        if (report.qualityStatus == QStringLiteral("warning"))
            ++manifest.warningFiles;
        if (report.qualityStatus == QStringLiteral("reject"))
            ++manifest.rejectedFiles;
        manifest.totalBytes += report.fileSize;
        manifest.totalValidRecords += report.validRecords;
        manifest.totalSkippedRecords += report.skippedRecords;
        manifest.reports.append(std::move(report));
    }
    return manifest;
}

} // namespace pp
