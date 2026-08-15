#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace pp {

// 轻量文件日志服务：分级（Debug/Info/Warning/Error）、按大小滚动（保留有限备份）。
// 线程安全（内部互斥锁）；单例供各层就近记录，无需层层传递。
class LogService {
public:
    enum class Level { Debug, Info, Warning, Error };

    static LogService &instance();

    // 配置日志文件路径与滚动策略；重新调用会关闭旧文件并切换到新路径。
    void configure(const QString &filePath,
                   qint64 maxFileSizeBytes = 5 * 1024 * 1024,
                   int maxBackupFiles = 3);
    bool isConfigured() const;
    QString filePath() const;

    void log(Level level, const QString &category, const QString &message);
    void debug(const QString &category, const QString &message);
    void info(const QString &category, const QString &message);
    void warning(const QString &category, const QString &message);
    void error(const QString &category, const QString &message);

private:
    LogService() = default;
    ~LogService() = default;
    LogService(const LogService &) = delete;
    LogService &operator=(const LogService &) = delete;

    void rotateIfNeeded();

    mutable QMutex m_mutex;
    QFile m_file;
    QString m_filePath;
    qint64 m_maxFileSizeBytes = 5 * 1024 * 1024;
    int m_maxBackupFiles = 3;
};

} // namespace pp
