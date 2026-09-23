#include "domain/RawTickSourceQualityTypes.h"

#include <QJsonDocument>

namespace pp {

QVariantMap RawTickSourceQualityReport::toVariantMap() const
{
    QVariantList diagnosticValues;
    diagnosticValues.reserve(diagnostics.size());
    for (const TickDataImportDiagnostic &diagnostic : diagnostics)
        diagnosticValues.append(diagnostic.toVariantMap());
    return {{QStringLiteral("inspected"), inspected},
            {QStringLiteral("normalizationReady"), normalizationReady},
            {QStringLiteral("filePath"), filePath},
            {QStringLiteral("symbol"), symbol},
            {QStringLiteral("tradingDate"), tradingDate.toString(Qt::ISODate)},
            {QStringLiteral("fileSize"), fileSize},
            {QStringLiteral("contentSha256"), contentSha256},
            {QStringLiteral("detectedFormat"), detectedFormat},
            {QStringLiteral("volumeUnit"), volumeUnit},
            {QStringLiteral("directionMappingVersion"), directionMappingVersion},
            {QStringLiteral("qualityStatus"), qualityStatus},
            {QStringLiteral("actionCode"), actionCode},
            {QStringLiteral("linesRead"), linesRead},
            {QStringLiteral("metadataLines"), metadataLines},
            {QStringLiteral("validRecords"), validRecords},
            {QStringLiteral("skippedRecords"), skippedRecords},
            {QStringLiteral("outsideSessionRecords"), outsideSessionRecords},
            {QStringLiteral("outOfOrderRecords"), outOfOrderRecords},
            {QStringLiteral("maximumLineBytes"), maximumLineBytes},
            {QStringLiteral("buyRecords"), buyRecords},
            {QStringLiteral("sellRecords"), sellRecords},
            {QStringLiteral("neutralRecords"), neutralRecords},
            {QStringLiteral("mappedDirectionRecords"), mappedDirectionRecords},
            {QStringLiteral("diagnosticTotalCount"), diagnosticTotalCount},
            {QStringLiteral("diagnosticsTruncated"), diagnosticsTruncated},
            {QStringLiteral("totalSourceVolume"), totalSourceVolume},
            {QStringLiteral("normalizedVolumeShares"), normalizedVolumeShares},
            {QStringLiteral("skippedRecordRatio"), skippedRecordRatio},
            {QStringLiteral("messages"), messages},
            {QStringLiteral("diagnostics"), diagnosticValues}};
}

QVariantMap RawTickSourceQualityManifest::toVariantMap() const
{
    QVariantList reportValues;
    reportValues.reserve(reports.size());
    for (const RawTickSourceQualityReport &report : reports)
        reportValues.append(report.toVariantMap());
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("totalFiles"), totalFiles},
            {QStringLiteral("inspectedFiles"), inspectedFiles},
            {QStringLiteral("readyFiles"), readyFiles},
            {QStringLiteral("warningFiles"), warningFiles},
            {QStringLiteral("rejectedFiles"), rejectedFiles},
            {QStringLiteral("totalBytes"), totalBytes},
            {QStringLiteral("totalValidRecords"), totalValidRecords},
            {QStringLiteral("totalSkippedRecords"), totalSkippedRecords},
            {QStringLiteral("reports"), reportValues}};
}

QByteArray RawTickSourceQualityManifest::toJson() const
{
    return QJsonDocument::fromVariant(toVariantMap()).toJson(
        QJsonDocument::Indented);
}

} // namespace pp
