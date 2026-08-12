#pragma once

#include <QDate>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace pp {

struct NasCatalogEntry {
    QString symbol;
    QDate tradingDate;
    QString format;
    qint64 size = 0;
    QString sha256;
    QString version;
    QString downloadPath;

    QVariantMap toVariantMap() const;
};

struct NasCatalogResult {
    bool succeeded = false;
    int schemaVersion = 0;
    int requestedSymbols = 0;
    int availableSymbols = 0;
    int missingSymbols = 0;
    qint64 totalBytes = 0;
    QVector<NasCatalogEntry> entries;
    QString error;
};

struct NasDownloadResult {
    bool succeeded = false;
    bool fromCache = false;
    NasCatalogEntry entry;
    QString localRoot;
    QString localFilePath;
    qint64 receivedBytes = 0;
    QString error;
};

struct NasChangesResult {
    bool succeeded = false;
    int schemaVersion = 0;
    int totalDirectories = 0;
    qint64 latestDirectoryAt = 0;
    QStringList changedDates;
    QVector<qint64> changedAt;
    QString error;

    QVariantMap toVariantMap() const;
};

enum class NasSyncItemStatus {
    Ready = 0,
    Downloading,
    Importing,
    Succeeded,
    Failed,
    Cancelled
};

struct NasSyncPlanItem {
    NasCatalogEntry entry;
    NasSyncItemStatus status = NasSyncItemStatus::Ready;
    qint64 rawTicksWritten = 0;
    int candleBarsWritten = 0;
    bool fromCache = false;
    qint64 transferElapsedMilliseconds = 0;
    qint64 parseElapsedMilliseconds = 0;
    qint64 rawDatabaseElapsedMilliseconds = 0;
    qint64 candleDatabaseElapsedMilliseconds = 0;
    qint64 importElapsedMilliseconds = 0;
    QString error;

    QVariantMap toVariantMap() const;
};

struct NasSyncSummary {
    int total = 0;
    int completed = 0;
    int succeeded = 0;
    int failed = 0;
    int pending = 0;
    int symbols = 0;
    qint64 totalBytes = 0;
    qint64 succeededBytes = 0;
    qint64 rawTicksWritten = 0;
    qint64 candleBarsWritten = 0;
    qint64 transferElapsedMilliseconds = 0;
    qint64 parseElapsedMilliseconds = 0;
    qint64 rawDatabaseElapsedMilliseconds = 0;
    qint64 candleDatabaseElapsedMilliseconds = 0;
    qint64 importElapsedMilliseconds = 0;
    int cacheHits = 0;

    QVariantMap toVariantMap() const;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::NasCatalogResult)
Q_DECLARE_METATYPE(pp::NasDownloadResult)
Q_DECLARE_METATYPE(pp::NasChangesResult)
