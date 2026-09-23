#pragma once

#include "domain/MarketTypes.h"
#include "domain/RawTradeDirection.h"
#include "domain/TickDataImportTypes.h"

#include <QHash>
#include <QVariantMap>
#include <QVector>

namespace pp {

enum class RawTickSourceVolumeUnit {
    Unknown = 0,
    Lots = 1,
    Shares = 2
};

inline QString rawTickSourceVolumeUnitName(RawTickSourceVolumeUnit unit)
{
    switch (unit) {
    case RawTickSourceVolumeUnit::Lots: return QStringLiteral("lots");
    case RawTickSourceVolumeUnit::Shares: return QStringLiteral("shares");
    case RawTickSourceVolumeUnit::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

struct RawTickSourceQualityRequest {
    QString filePath;
    Instrument instrument;
    QDate tradingDate;
    RawTickSourceVolumeUnit volumeUnit = RawTickSourceVolumeUnit::Unknown;
    QHash<QString, RawTradeDirection> explicitDirectionMappings;
    QString directionMappingVersion;
    int maximumDiagnostics = 200;
};

struct RawTickSourceQualityReport {
    bool inspected = false;
    bool normalizationReady = false;
    QString filePath;
    QString symbol;
    QDate tradingDate;
    qint64 fileSize = 0;
    QString contentSha256;
    QString detectedFormat = QStringLiteral("unknown");
    QString volumeUnit = QStringLiteral("unknown");
    QString directionMappingVersion;
    QString qualityStatus = QStringLiteral("reject");
    QString actionCode = QStringLiteral("invalid_request");
    qint64 linesRead = 0;
    qint64 metadataLines = 0;
    qint64 validRecords = 0;
    qint64 skippedRecords = 0;
    qint64 outsideSessionRecords = 0;
    qint64 outOfOrderRecords = 0;
    qint64 maximumLineBytes = 0;
    qint64 buyRecords = 0;
    qint64 sellRecords = 0;
    qint64 neutralRecords = 0;
    qint64 mappedDirectionRecords = 0;
    qint64 diagnosticTotalCount = 0;
    bool diagnosticsTruncated = false;
    double totalSourceVolume = 0.0;
    double normalizedVolumeShares = 0.0;
    double skippedRecordRatio = 0.0;
    QStringList messages;
    QVector<TickDataImportDiagnostic> diagnostics;

    QVariantMap toVariantMap() const;
};

struct RawTickSourceQualityManifest {
    int totalFiles = 0;
    int inspectedFiles = 0;
    int readyFiles = 0;
    int warningFiles = 0;
    int rejectedFiles = 0;
    qint64 totalBytes = 0;
    qint64 totalValidRecords = 0;
    qint64 totalSkippedRecords = 0;
    QVector<RawTickSourceQualityReport> reports;

    QVariantMap toVariantMap() const;
    QByteArray toJson() const;
};

} // namespace pp
