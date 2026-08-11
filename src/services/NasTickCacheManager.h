#pragma once

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace pp {

struct NasTickCacheSummary {
    int files = 0;
    qint64 bytes = 0;
    QDateTime oldestModified;
    QDateTime newestModified;
    int removedFiles = 0;
    qint64 removedBytes = 0;

    QVariantMap toVariantMap() const;
};

class NasTickCacheManager final {
public:
    void setRootDirectory(const QString &rootDirectory);
    QString rootDirectory() const;
    NasTickCacheSummary scan() const;
    bool pruneOlderThan(int maximumAgeDays, NasTickCacheSummary *summary,
                        QString *error = nullptr,
                        const QDateTime &nowUtc = QDateTime::currentDateTimeUtc()) const;

private:
    QString m_rootDirectory;
};

} // namespace pp
