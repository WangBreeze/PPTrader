#include "services/StorageProfile.h"

#include "services/SqliteLocalDataStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>

namespace pp {

QString StorageProfile::defaultDatabasePath()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/data/pptrader.db"));
}

bool StorageProfile::validateLocalDatabasePath(const QString &path, QString *error)
{
    if (error) error->clear();
    const QString clean = QDir::cleanPath(path.trimmed());
    if (clean.isEmpty() || !QDir::isAbsolutePath(clean)) {
        if (error) *error = QStringLiteral("数据库路径必须是本机绝对路径");
        return false;
    }
    const QString normalized = QDir::fromNativeSeparators(clean);
    if (normalized.startsWith(QStringLiteral("//"))) {
        if (error) *error = QStringLiteral("SQLite 不支持直接打开 SMB/NFS/UNC 网络共享路径");
        return false;
    }
    const QFileInfo info(clean);
    if (info.exists() && !info.isFile()) {
        if (error) *error = QStringLiteral("数据库目标必须是文件路径");
        return false;
    }
    return true;
}

QString StorageProfile::configuredDatabasePath()
{
    const QString environmentPath = qEnvironmentVariable(
        "PPTRADER_DATABASE_PATH").trimmed();
    QString error;
    if (validateLocalDatabasePath(environmentPath, &error))
        return QDir::cleanPath(environmentPath);

    QSettings settings;
    const QString configured = settings.value(
        QStringLiteral("storage/databasePath")).toString();
    return validateLocalDatabasePath(configured, &error)
        ? QDir::cleanPath(configured) : defaultDatabasePath();
}

void StorageProfile::saveDatabasePath(const QString &path)
{
    QSettings settings;
    settings.setValue(QStringLiteral("storage/databasePath"), QDir::cleanPath(path));
    settings.sync();
}

StorageRelocationResult DatabaseRelocationService::copyAndVerify(
    const QString &sourcePath, const QString &targetPath,
    const QString &expectedDatabaseUuid)
{
    StorageRelocationResult result;
    result.sourcePath = QDir::cleanPath(sourcePath);
    result.targetPath = QDir::cleanPath(targetPath);
    QString validationError;
    if (!StorageProfile::validateLocalDatabasePath(result.sourcePath, &validationError)
        || !StorageProfile::validateLocalDatabasePath(result.targetPath, &validationError)) {
        result.error = validationError;
        return result;
    }
    if (!QFileInfo::exists(result.sourcePath)) {
        result.error = QStringLiteral("源数据库不存在");
        return result;
    }
    if (QFileInfo::exists(result.targetPath)) {
        result.error = QStringLiteral("目标数据库已存在；为避免覆盖，请选择新文件名");
        return result;
    }
    if (QFileInfo(result.sourcePath).absoluteFilePath().compare(
            QFileInfo(result.targetPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        result.error = QStringLiteral("目标数据库路径与当前路径相同");
        return result;
    }

    const QString targetDirectory = QFileInfo(result.targetPath).absolutePath();
    if (!QDir().mkpath(targetDirectory)) {
        result.error = QStringLiteral("无法创建目标数据库目录");
        return result;
    }
    const QString temporaryPath = result.targetPath + QStringLiteral(".moving-%1.tmp")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QFile::copy(result.sourcePath, temporaryPath)) {
        result.error = QStringLiteral("复制数据库到目标目录失败");
        return result;
    }

    {
        SqliteLocalDataStore verificationStore;
        if (!verificationStore.initialize(temporaryPath)) {
            QFile::remove(temporaryPath);
            result.error = QStringLiteral("目标数据库无法打开或迁移");
            return result;
        }
        QString integrityError;
        if (!verificationStore.verifyIntegrity(&integrityError)
            || (!expectedDatabaseUuid.isEmpty()
                && verificationStore.databaseUuid() != expectedDatabaseUuid)) {
            verificationStore.close();
            QFile::remove(temporaryPath);
            result.error = integrityError.isEmpty()
                ? QStringLiteral("目标数据库身份与源库不一致") : integrityError;
            return result;
        }
        result.databaseUuid = verificationStore.databaseUuid();
        verificationStore.close();
    }
    if (!QFile::rename(temporaryPath, result.targetPath)) {
        QFile::remove(temporaryPath);
        result.error = QStringLiteral("无法原子确认目标数据库文件");
        return result;
    }
    result.succeeded = true;
    return result;
}

} // namespace pp
