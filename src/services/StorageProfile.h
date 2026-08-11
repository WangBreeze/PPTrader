#pragma once

#include <QString>

namespace pp {

struct StorageRelocationResult {
    bool succeeded = false;
    QString sourcePath;
    QString targetPath;
    QString databaseUuid;
    QString error;
};

class StorageProfile final {
public:
    static QString defaultDatabasePath();
    static QString configuredDatabasePath();
    static bool validateLocalDatabasePath(const QString &path, QString *error = nullptr);
    static void saveDatabasePath(const QString &path);
};

class DatabaseRelocationService final {
public:
    // 调用前必须关闭源库的全部 SQLite 连接。复制成功后保留源文件，待用户
    // 独立确认新库后再决定是否清理，避免不可恢复的移动。
    static StorageRelocationResult copyAndVerify(const QString &sourcePath,
                                                 const QString &targetPath,
                                                 const QString &expectedDatabaseUuid);
};

} // namespace pp
