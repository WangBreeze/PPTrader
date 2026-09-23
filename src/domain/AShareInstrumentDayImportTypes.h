#pragma once

#include "domain/AShareCalendarTypes.h"

#include <QDateTime>
#include <QMetaType>
#include <QVariantList>

namespace pp {

struct AShareInstrumentDayImportDiagnostic {
    qint64 lineNumber = 0;
    QString reason;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("lineNumber"), lineNumber},
                {QStringLiteral("reason"), reason}};
    }
};

struct AShareInstrumentDayImportRequest {
    QString filePath;
    QString source;        // SSE / SZSE
    QString sourceVersion; // 官方公告号、导出日期或数据版本
    QString revisionReason;
    QString expectedContentSha256;
    bool previewOnly = false;
};

struct AShareInstrumentDayImportChangeSummary {
    bool valid = false;
    qint64 newRecords = 0;
    qint64 unchangedRecords = 0;
    qint64 correctedRecords = 0;
    QString error;

    qint64 totalRecords() const
    {
        return newRecords + unchangedRecords + correctedRecords;
    }
};

struct AShareInstrumentDayImportRun {
    QString runId;
    QDateTime importedAt;
    QString fileName;
    QString source;
    QString sourceVersion;
    QString contentSha256;
    qint64 linesRead = 0;
    qint64 recordCount = 0;
    QDate firstDate;
    QDate lastDate;
    qint64 newRecords = 0;
    qint64 unchangedRecords = 0;
    qint64 correctedRecords = 0;
    QString revisionReason;
    QString status = QStringLiteral("succeeded");
    QString error;

    bool isValid() const
    {
        return !runId.trimmed().isEmpty() && importedAt.isValid()
            && !fileName.trimmed().isEmpty()
            && (source == QStringLiteral("SSE")
                || source == QStringLiteral("SZSE"))
            && !sourceVersion.trimmed().isEmpty()
            && contentSha256.startsWith(QStringLiteral("sha256:"))
            && recordCount > 0 && firstDate.isValid() && lastDate.isValid()
            && firstDate <= lastDate;
    }

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("runId"), runId},
                {QStringLiteral("importedAt"), importedAt},
                {QStringLiteral("fileName"), fileName},
                {QStringLiteral("source"), source},
                {QStringLiteral("sourceVersion"), sourceVersion},
                {QStringLiteral("contentSha256"), contentSha256},
                {QStringLiteral("linesRead"), linesRead},
                {QStringLiteral("recordCount"), recordCount},
                {QStringLiteral("firstDate"), firstDate},
                {QStringLiteral("lastDate"), lastDate},
                {QStringLiteral("newRecords"), newRecords},
                {QStringLiteral("unchangedRecords"), unchangedRecords},
                {QStringLiteral("correctedRecords"), correctedRecords},
                {QStringLiteral("revisionReason"), revisionReason},
                {QStringLiteral("status"), status},
                {QStringLiteral("error"), error}};
    }
};

struct AShareInstrumentDayImportResult {
    bool succeeded = false;
    bool cancelled = false;
    bool reused = false;
    bool previewOnly = false;
    bool previewReady = false;
    bool requiresRevisionReason = false;
    QString filePath;
    QString source;
    QString sourceVersion;
    QString contentSha256;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 writtenRecords = 0;
    qint64 newRecords = 0;
    qint64 unchangedRecords = 0;
    qint64 correctedRecords = 0;
    QDate firstDate;
    QDate lastDate;
    qint64 elapsedMilliseconds = 0;
    QString runId;
    QVector<AShareInstrumentDayRecord> records;
    QVector<AShareInstrumentDayImportDiagnostic> diagnostics;

    QVariantList diagnosticMaps() const
    {
        QVariantList values;
        values.reserve(diagnostics.size());
        for (const auto &diagnostic : diagnostics)
            values.append(diagnostic.toVariantMap());
        return values;
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::AShareInstrumentDayImportResult)
