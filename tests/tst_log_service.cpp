#include "services/LogService.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace pp {

class LogServiceTest : public QObject {
    Q_OBJECT

private slots:
    void configureWritesLogLines();
    void rotateBySize();
    void reconfigureSwitchesPath();
};

namespace {
QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}
} // namespace

void LogServiceTest::configureWritesLogLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logPath = directory.filePath(QStringLiteral("pptrader.log"));

    LogService::instance().configure(logPath, 10 * 1024 * 1024, 3);
    QVERIFY(LogService::instance().isConfigured());
    QCOMPARE(LogService::instance().filePath(), logPath);

    LogService::instance().info(QStringLiteral("test"), QStringLiteral("第一条消息"));
    LogService::instance().error(QStringLiteral("test"), QStringLiteral("错误消息"));

    const QString content = readFile(logPath);
    QVERIFY(content.contains(QStringLiteral("[INFO]")));
    QVERIFY(content.contains(QStringLiteral("[ERROR]")));
    QVERIFY(content.contains(QStringLiteral("[test]")));
    QVERIFY(content.contains(QStringLiteral("第一条消息")));
    QVERIFY(content.contains(QStringLiteral("错误消息")));
}

void LogServiceTest::rotateBySize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logPath = directory.filePath(QStringLiteral("pptrader.log"));
    const QString backupPath = logPath + QStringLiteral(".1");

    // 小尺寸上限（>= 1024 下限），写入多行触发滚动。
    LogService::instance().configure(logPath, 1024, 1);
    for (int index = 0; index < 24; ++index) {
        LogService::instance().info(QStringLiteral("rotate"),
                                    QStringLiteral("第 %1 条日志，内容足够长以触发滚动").arg(index));
    }

    // 主文件存在，且滚动产生 .1 备份。
    QVERIFY(QFile::exists(logPath));
    QVERIFY(QFile::exists(backupPath));
    // 主文件内容远小于全部日志（滚动后只保留最近部分）。
    const QString current = readFile(logPath);
    QVERIFY(!current.isEmpty());
    QVERIFY(current.size() < 2000);
    // 备份里包含早期日志。
    const QString backup = readFile(backupPath);
    QVERIFY(backup.contains(QStringLiteral("第 0 条")));
}

void LogServiceTest::reconfigureSwitchesPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.log"));
    const QString secondPath = directory.filePath(QStringLiteral("second.log"));

    LogService::instance().configure(firstPath);
    LogService::instance().info(QStringLiteral("test"), QStringLiteral("first"));
    QVERIFY(QFile::exists(firstPath));

    LogService::instance().configure(secondPath);
    QCOMPARE(LogService::instance().filePath(), secondPath);
    LogService::instance().info(QStringLiteral("test"), QStringLiteral("second"));
    QVERIFY(QFile::exists(secondPath));
    QVERIFY(readFile(secondPath).contains(QStringLiteral("second")));
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::LogServiceTest)

#include "tst_log_service.moc"
