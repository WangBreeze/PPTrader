#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QVariantList>

namespace pp {

struct AuthorizedFactorImportDiagnostic {
    qint64 lineNumber = 0;
    QString reason;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("lineNumber"), lineNumber},
                {QStringLiteral("reason"), reason}};
    }
};

struct ExternalFactorObservation {
    QDateTime timestamp;
    QString symbol;
    double value = 0.0;
};

struct ExternalFactorDataset {
    QString datasetId;
    QString factorKey;
    QString factorDefinitionSha256;
    QString source = QStringLiteral("authorized_csv");
    QString sourceVersion;
    QString sourceReference;
    QString contentSha256;
    QDateTime importedAt;
    qint64 recordCount = 0;
    int symbolCount = 0;
    int timestampCount = 0;
    QDateTime firstTimestamp;
    QDateTime lastTimestamp;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("factorKey"), factorKey},
                {QStringLiteral("factorDefinitionSha256"),
                 factorDefinitionSha256},
                {QStringLiteral("source"), source},
                {QStringLiteral("sourceVersion"), sourceVersion},
                {QStringLiteral("sourceReference"), sourceReference},
                {QStringLiteral("contentSha256"), contentSha256},
                {QStringLiteral("importedAt"), importedAt},
                {QStringLiteral("recordCount"), recordCount},
                {QStringLiteral("symbolCount"), symbolCount},
                {QStringLiteral("timestampCount"), timestampCount},
                {QStringLiteral("firstTimestamp"), firstTimestamp},
                {QStringLiteral("lastTimestamp"), lastTimestamp}};
    }
};

struct ExternalFactorDatasetBinding {
    QString bindingId;
    QString candidateId;
    int candidateVersion = 0;
    QString candidateContentSha256;
    QString factorDefinitionSha256;
    QString datasetId;
    QString datasetContentSha256;
    QDateTime boundAt;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("bindingId"), bindingId},
                {QStringLiteral("candidateId"), candidateId},
                {QStringLiteral("candidateVersion"), candidateVersion},
                {QStringLiteral("candidateContentSha256"),
                 candidateContentSha256},
                {QStringLiteral("factorDefinitionSha256"),
                 factorDefinitionSha256},
                {QStringLiteral("datasetId"), datasetId},
                {QStringLiteral("datasetContentSha256"),
                 datasetContentSha256},
                {QStringLiteral("boundAt"), boundAt}};
    }
};

struct AuthorizedFactorImportRequest {
    QString filePath;
    QString factorKey;
    QString factorDefinitionSha256;
    QString source;
    QString sourceVersion;
    QString sourceReference;
    QString expectedContentSha256;
    bool previewOnly = false;
};

struct AuthorizedFactorImportResult {
    bool succeeded = false;
    bool cancelled = false;
    bool previewOnly = false;
    bool previewReady = false;
    bool reused = false;
    QString filePath;
    QString contentSha256;
    qint64 linesRead = 0;
    qint64 validRecords = 0;
    qint64 duplicateRecords = 0;
    qint64 writtenRecords = 0;
    qint64 elapsedMilliseconds = 0;
    ExternalFactorDataset dataset;
    QVector<ExternalFactorObservation> observations;
    QVector<AuthorizedFactorImportDiagnostic> diagnostics;

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

Q_DECLARE_METATYPE(pp::AuthorizedFactorImportResult)
