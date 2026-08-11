#include "services/NasTickCacheManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace pp {

QVariantMap NasTickCacheSummary::toVariantMap() const
{
    return {{QStringLiteral("files"), files},
            {QStringLiteral("bytes"), bytes},
            {QStringLiteral("oldestModified"), oldestModified},
            {QStringLiteral("newestModified"), newestModified},
            {QStringLiteral("removedFiles"), removedFiles},
            {QStringLiteral("removedBytes"), removedBytes}};
}

void NasTickCacheManager::setRootDirectory(const QString &rootDirectory)
{
    m_rootDirectory = QDir(rootDirectory).absolutePath();
}

QString NasTickCacheManager::rootDirectory() const { return m_rootDirectory; }

NasTickCacheSummary NasTickCacheManager::scan() const
{
    NasTickCacheSummary result;
    const QRegularExpression datePattern(QStringLiteral("^[0-9]{8}$"));
    const QRegularExpression filePattern(QStringLiteral("^[0-9]{6}\\.csv$"));
    const QDir root(m_rootDirectory);
    for (const QFileInfo &dateDirectory : root.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!datePattern.match(dateDirectory.fileName()).hasMatch()) continue;
        const QDir directory(dateDirectory.absoluteFilePath());
        for (const QFileInfo &file : directory.entryInfoList(
                 QDir::Files | QDir::NoSymLinks)) {
            if (!filePattern.match(file.fileName()).hasMatch()) continue;
            ++result.files;
            result.bytes += file.size();
            const QDateTime modified = file.lastModified().toUTC();
            if (!result.oldestModified.isValid() || modified < result.oldestModified)
                result.oldestModified = modified;
            if (!result.newestModified.isValid() || modified > result.newestModified)
                result.newestModified = modified;
        }
    }
    return result;
}

bool NasTickCacheManager::pruneOlderThan(int maximumAgeDays,
                                         NasTickCacheSummary *summary,
                                         QString *error,
                                         const QDateTime &nowUtc) const
{
    if (error) error->clear();
    if (summary) *summary = {};
    if (maximumAgeDays < 1 || maximumAgeDays > 3650 || !nowUtc.isValid()
        || m_rootDirectory.isEmpty()) {
        if (error) *error = QStringLiteral("NAS 暂存保留天数或根目录无效");
        return false;
    }
    NasTickCacheSummary removed;
    const QDateTime cutoff = nowUtc.toUTC().addDays(-maximumAgeDays);
    const QRegularExpression datePattern(QStringLiteral("^[0-9]{8}$"));
    const QRegularExpression filePattern(QStringLiteral("^[0-9]{6}\\.csv$"));
    QDir root(m_rootDirectory);
    for (const QFileInfo &dateDirectory : root.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (!datePattern.match(dateDirectory.fileName()).hasMatch()) continue;
        QDir directory(dateDirectory.absoluteFilePath());
        for (const QFileInfo &file : directory.entryInfoList(
                 QDir::Files | QDir::NoSymLinks)) {
            if (!filePattern.match(file.fileName()).hasMatch()
                || file.lastModified().toUTC() >= cutoff) continue;
            const qint64 bytes = file.size();
            if (!QFile::remove(file.absoluteFilePath())) {
                if (error) *error = QStringLiteral("无法删除过期 NAS 暂存文件");
                return false;
            }
            ++removed.removedFiles;
            removed.removedBytes += bytes;
        }
        root.rmdir(dateDirectory.fileName());
    }
    const NasTickCacheSummary remaining = scan();
    removed.files = remaining.files;
    removed.bytes = remaining.bytes;
    removed.oldestModified = remaining.oldestModified;
    removed.newestModified = remaining.newestModified;
    if (summary) *summary = removed;
    return true;
}

} // namespace pp
