#pragma once

#include "domain/AShareIndustryTypes.h"

#include <QMetaType>
#include <QVariantList>

namespace pp {

struct AShareIndustryImportDiagnostic {
    qint64 lineNumber = 0;
    QString reason;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("lineNumber"), lineNumber},
                {QStringLiteral("reason"), reason}};
    }
};

struct AShareIndustryImportRequest {
    QString filePath;
    QString classification;
    QString source;
    QString sourceVersion;
    QString revisionReason;
    QString expectedFileContentSha256;
    bool previewOnly = false;
};

struct AShareIndustryImportResult {
    bool succeeded = false;
    bool cancelled = false;
    bool previewOnly = false;
    bool previewReady = false;
    bool reused = false;
    bool requiresRevisionReason = false;
    QString filePath;
    QString fileContentSha256;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 writtenRecords = 0;
    qint64 elapsedMilliseconds = 0;
    AShareIndustryDataset dataset;
    QVector<AShareIndustryImportDiagnostic> diagnostics;

    QVariantList diagnosticMaps() const
    {
        QVariantList result;
        result.reserve(diagnostics.size());
        for (const auto &diagnostic : diagnostics)
            result.append(diagnostic.toVariantMap());
        return result;
    }
};

} // namespace pp

Q_DECLARE_METATYPE(pp::AShareIndustryImportResult)
