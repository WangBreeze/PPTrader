#include "services/LogService.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>

namespace pp {

namespace {

QString levelName(LogService::Level level)
{
    switch (level) {
    case LogService::Level::Debug: return QStringLiteral("DEBUG");
    case LogService::Level::Info: return QStringLiteral("INFO");
    case LogService::Level::Warning: return QStringLiteral("WARN");
    case LogService::Level::Error: return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

} // namespace

LogService &LogService::instance()
{
    static LogService service;
    return service;
}

void LogService::configure(const QString &filePath,
                           qint64 maxFileSizeBytes,
                           int maxBackupFiles)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_filePath = filePath;
    m_maxFileSizeBytes = std::max<qint64>(1024, maxFileSizeBytes);
    m_maxBackupFiles = std::max(0, maxBackupFiles);

    if (m_filePath.isEmpty()) return;
    QFileInfo info(m_filePath);
    QDir().mkpath(info.absolutePath());
    m_file.setFileName(m_filePath);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

bool LogService::isConfigured() const
{
    QMutexLocker locker(&m_mutex);
    return m_file.isOpen();
}

QString LogService::filePath() const
{
    QMutexLocker locker(&m_mutex);
    return m_filePath;
}

void LogService::log(Level level, const QString &category, const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) return;

    rotateIfNeeded();
    QTextStream stream(&m_file);
    stream << QStringLiteral("%1 [%2] [%3] %4\n")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                   levelName(level), category, message);
    stream.flush();
}

void LogService::debug(const QString &category, const QString &message)
{
    log(Level::Debug, category, message);
}

void LogService::info(const QString &category, const QString &message)
{
    log(Level::Info, category, message);
}

void LogService::warning(const QString &category, const QString &message)
{
    log(Level::Warning, category, message);
}

void LogService::error(const QString &category, const QString &message)
{
    log(Level::Error, category, message);
}

void LogService::rotateIfNeeded()
{
    // 用文件系统实际大小判断（QTextStream 缓冲可能使 m_file.size() 滞后）。
    const qint64 size = QFileInfo(m_filePath).size();
    if (size < m_maxFileSizeBytes) return;

    m_file.close();
    // 删除最老备份。
    if (m_maxBackupFiles > 0) {
        QFile::remove(m_filePath + QStringLiteral(".%1").arg(m_maxBackupFiles));
    }
    // 循环重命名：.N-1 → .N。
    for (int index = m_maxBackupFiles - 1; index >= 1; --index) {
        QFile::rename(m_filePath + QStringLiteral(".%1").arg(index),
                      m_filePath + QStringLiteral(".%1").arg(index + 1));
    }
    // 当前 → .1。
    if (m_maxBackupFiles > 0) {
        const bool renamed = QFile::rename(m_filePath, m_filePath + QStringLiteral(".1"));
        if (!renamed) {
            // rename 失败（如句柄未完全释放），降级为 copy + remove。
            QFile::copy(m_filePath, m_filePath + QStringLiteral(".1"));
            QFile::remove(m_filePath);
        }
    } else {
        QFile::remove(m_filePath);
    }
    m_file.setFileName(m_filePath);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

} // namespace pp
