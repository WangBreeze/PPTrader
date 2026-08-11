#pragma once

#include "domain/MarketTypes.h"
#include "domain/RawTradeTypes.h"

#include <QVariantList>
#include <QVector>
#include <QStringList>

namespace pp {

enum class TickDataCsvFormat {
    Tick = 0,
    FiveMinute = 1
};

enum class FiveMinuteVolumeUnit {
    Unknown = 0,
    Shares = 1,
    Lots = 2
};

struct TickDataTradeRecord {
    QDateTime timestamp;
    double price = 0.0;
    double volumeShares = 0.0;
    double amount = 0.0;
    RawTradeDirection direction = RawTradeDirection::Neutral;
};

struct TickDataImportDiagnostic {
    QString path;
    qint64 lineNumber = 0;
    QString reason;
    bool fatal = false;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("path"), path},
                {QStringLiteral("lineNumber"), lineNumber},
                {QStringLiteral("reason"), reason},
                {QStringLiteral("fatal"), fatal}};
    }
};

struct TickDataImportRequest {
    QString rootDirectory;
    Instrument instrument;
    QDate tradingDate;
    TickDataCsvFormat format = TickDataCsvFormat::Tick;
    int periodSeconds = 60;
    FiveMinuteVolumeUnit fiveMinuteVolumeUnit = FiveMinuteVolumeUnit::Unknown;
};

struct TickDataImportResult {
    bool succeeded = false;
    bool cancelled = false;
    QString filePath;
    QString source;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    qint64 maximumLineBytes = 0;
    qint64 outOfOrderRecords = 0;
    int expectedContinuousBars = 0;
    int missingContinuousBars = 0;
    QString qualityStatus = QStringLiteral("unknown");
    QStringList qualityMessages;
    QVector<Candle> candles;
    qint64 rawTicksWritten = 0;
    qint64 parseElapsedMilliseconds = 0;
    qint64 rawDatabaseElapsedMilliseconds = 0;
    QVector<TickDataImportDiagnostic> diagnostics;

    QVariantList diagnosticMaps() const
    {
        QVariantList result;
        result.reserve(diagnostics.size());
        for (const TickDataImportDiagnostic &diagnostic : diagnostics) {
            result.append(diagnostic.toVariantMap());
        }
        return result;
    }
};

enum class TickDataBatchItemStatus {
    Missing = 0,
    Ready,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    SkippedExisting
};

inline QString tickDataBatchItemStatusCode(TickDataBatchItemStatus status)
{
    switch (status) {
    case TickDataBatchItemStatus::Missing: return QStringLiteral("missing");
    case TickDataBatchItemStatus::Ready: return QStringLiteral("ready");
    case TickDataBatchItemStatus::Running: return QStringLiteral("running");
    case TickDataBatchItemStatus::Succeeded: return QStringLiteral("succeeded");
    case TickDataBatchItemStatus::Failed: return QStringLiteral("failed");
    case TickDataBatchItemStatus::Cancelled: return QStringLiteral("cancelled");
    case TickDataBatchItemStatus::SkippedExisting: return QStringLiteral("skipped_existing");
    }
    return QStringLiteral("unknown");
}

struct TickDataBatchPlanItem {
    TickDataImportRequest request;
    QString filePath;
    qint64 fileSize = 0;
    TickDataBatchItemStatus status = TickDataBatchItemStatus::Missing;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    int writtenBars = 0;
    int missingContinuousBars = 0;
    int existingBars = 0;
    int expectedBars = 0;
    QString coverageStatus = QStringLiteral("empty");
    bool willOverwrite = false;
    bool calendarCoverageKnown = false;
    QString calendarSource;
    QString calendarReason;
    QString qualityStatus = QStringLiteral("unknown");
    QString source;
    QString error;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("tradingDate"), request.tradingDate.toString(Qt::ISODate)},
                {QStringLiteral("filePath"), filePath},
                {QStringLiteral("fileSize"), fileSize},
                {QStringLiteral("status"), tickDataBatchItemStatusCode(status)},
                {QStringLiteral("linesRead"), linesRead},
                {QStringLiteral("validRecords"), validRecords},
                {QStringLiteral("skippedRecords"), skippedRecords},
                {QStringLiteral("writtenBars"), writtenBars},
                {QStringLiteral("missingContinuousBars"), missingContinuousBars},
                {QStringLiteral("existingBars"), existingBars},
                {QStringLiteral("expectedBars"), expectedBars},
                {QStringLiteral("coverageStatus"), coverageStatus},
                {QStringLiteral("willOverwrite"), willOverwrite},
                {QStringLiteral("calendarCoverageKnown"), calendarCoverageKnown},
                {QStringLiteral("calendarSource"), calendarSource},
                {QStringLiteral("calendarReason"), calendarReason},
                {QStringLiteral("qualityStatus"), qualityStatus},
                {QStringLiteral("source"), source},
                {QStringLiteral("error"), error}};
    }
};

struct TickDataImportRunSummary {
    QString runId;
    QDateTime createdAt;
    QDateTime completedAt;
    Instrument instrument;
    QDate startDate;
    QDate endDate;
    TickDataCsvFormat format = TickDataCsvFormat::Tick;
    int periodSeconds = 60;
    FiveMinuteVolumeUnit fiveMinuteVolumeUnit = FiveMinuteVolumeUnit::Unknown;
    int totalFiles = 0;
    int completedFiles = 0;
    int succeededFiles = 0;
    int failedFiles = 0;
    int missingFiles = 0;
    int cancelledFiles = 0;
    int qualityWarningFiles = 0;
    int skippedExistingFiles = 0;
    int overlapFiles = 0;
    int excludedHolidayDays = 0;
    int calendarFallbackDays = 0;
    QString calendarSource;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    int writtenBars = 0;
    QString status = QStringLiteral("planned");
    QString error;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("runId"), runId},
                {QStringLiteral("createdAt"), createdAt},
                {QStringLiteral("completedAt"), completedAt},
                {QStringLiteral("symbol"), instrument.symbol},
                {QStringLiteral("name"), instrument.name},
                {QStringLiteral("startDate"), startDate.toString(Qt::ISODate)},
                {QStringLiteral("endDate"), endDate.toString(Qt::ISODate)},
                {QStringLiteral("format"), static_cast<int>(format)},
                {QStringLiteral("periodSeconds"), periodSeconds},
                {QStringLiteral("fiveMinuteVolumeUnit"), static_cast<int>(fiveMinuteVolumeUnit)},
                {QStringLiteral("totalFiles"), totalFiles},
                {QStringLiteral("completedFiles"), completedFiles},
                {QStringLiteral("succeededFiles"), succeededFiles},
                {QStringLiteral("failedFiles"), failedFiles},
                {QStringLiteral("missingFiles"), missingFiles},
                {QStringLiteral("cancelledFiles"), cancelledFiles},
                {QStringLiteral("qualityWarningFiles"), qualityWarningFiles},
                {QStringLiteral("skippedExistingFiles"), skippedExistingFiles},
                {QStringLiteral("overlapFiles"), overlapFiles},
                {QStringLiteral("excludedHolidayDays"), excludedHolidayDays},
                {QStringLiteral("calendarFallbackDays"), calendarFallbackDays},
                {QStringLiteral("calendarSource"), calendarSource},
                {QStringLiteral("validRecords"), validRecords},
                {QStringLiteral("skippedRecords"), skippedRecords},
                {QStringLiteral("writtenBars"), writtenBars},
                {QStringLiteral("status"), status},
                {QStringLiteral("error"), error}};
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::TickDataImportResult)
