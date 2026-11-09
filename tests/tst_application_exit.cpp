#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

// 构建系统通过该宏给出实际可执行文件路径（含平台后缀与输出目录）。
// 保留回退分支，便于脱离 CMake 手工编译该测试。
#ifndef PPTRADER_APP_EXECUTABLE
#define PPTRADER_APP_EXECUTABLE ""
#endif

class ApplicationExitTest : public QObject {
    Q_OBJECT
private slots:
    void wizardClosesWithinBudgetAfterRepeatedAccessibility_data()
    {
        QTest::addColumn<int>("activationCount");
        QTest::addColumn<bool>("cancelOffline");
        QTest::newRow("ordinary-close") << 0 << false;
        QTest::newRow("repeated-accessibility") << 10000 << false;
        QTest::newRow("cancel-offline-then-close") << 10000 << true;
    }
    void wizardClosesWithinBudgetAfterRepeatedAccessibility()
    {
        QFETCH(int, activationCount);
        QFETCH(bool, cancelOffline);
        QTemporaryDir evidence;
        QVERIFY(evidence.isValid());
        const QString report = evidence.filePath(QStringLiteral("wizard.json"));
        QProcess process;
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PATH"),
            QLibraryInfo::path(QLibraryInfo::BinariesPath)
                + QDir::listSeparator() + qEnvironmentVariable("PATH"));
        environment.insert(QStringLiteral("QT_PLUGIN_PATH"),
            QLibraryInfo::path(QLibraryInfo::PluginsPath));
        environment.insert(QStringLiteral("PPTRADER_AUTOSTART_LIVE"), QStringLiteral("0"));
        // 退出测试验证真实 Main/QML/Controller 与实际子 PID 的自然回收，
        // 但不依赖测试时桌面是否锁定；原生 Windows 无障碍滚动另由
        // accessible_scrollbar_lifecycle 覆盖。
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.remove(QStringLiteral("PPTRADER_ACCEPTANCE_DISABLE_ACCESSIBILITY"));
        process.setProcessEnvironment(environment);
        QStringList arguments{QStringLiteral("-platform"), QStringLiteral("offscreen"),
            QStringLiteral("--wizard-contract-probe"), report,
            QStringLiteral("--exit-activation-count"), QString::number(activationCount)};
        if (cancelOffline) arguments.append(QStringLiteral("--exit-cancel-offline"));
        const QString applicationExecutable = []() -> QString {
            const QString configured = QString::fromUtf8(PPTRADER_APP_EXECUTABLE);
            if (!configured.isEmpty() && QFileInfo::exists(configured)) return configured;
            const QString fallback = QCoreApplication::applicationDirPath()
                + QStringLiteral("/PPTrader")
#ifdef Q_OS_WIN
                + QStringLiteral(".exe")
#endif
                ;
            return fallback;
        }();
        // 以被测程序所在目录为工作目录，使 data/、scripts/ 等运行时相对路径
        // 在 Windows 与 Linux（bin/ 布局）下都指向部署副本。
        process.setWorkingDirectory(QFileInfo(applicationExecutable).absolutePath());
        process.start(applicationExecutable, arguments);
        QVERIFY(process.waitForStarted());
        const qint64 childPid = process.processId();
        qInfo() << "started isolated PPTrader PID" << childPid;
        QElapsedTimer startup;
        startup.start();
        QByteArray trace;
        const auto readTrace = [&] {
            QFile log(report + QStringLiteral(".log"));
            if (log.open(QIODevice::ReadOnly)) trace = log.readAll();
        };
        while (startup.elapsed() < 60000 && process.state() != QProcess::NotRunning) {
            process.waitForFinished(50);
            readTrace();
            if (trace.contains("native-closed")) break;
        }
        // 快速平台（例如 offscreen）可能在首次循环判断前已自然退出；
        // 退出后的最终日志仍是有效证据，不能被竞态误报为“未启动”。
        readTrace();
        if (!trace.contains("native-closed") && process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
            qWarning() << "startup failed; cleaned owned PID" << childPid;
        }
        QVERIFY2(trace.contains("native-closed"),
                 qPrintable(QStringLiteral("未观察到真实向导关闭：%1 %2")
                     .arg(QString::fromUtf8(trace), QString::fromUtf8(process.readAllStandardError()))));
        QElapsedTimer closing;
        closing.start();
        const bool exited = process.state() == QProcess::NotRunning || process.waitForFinished(20000);
        qInfo().noquote() << QStringLiteral("PID %1，关闭后等待 %2 ms，自然退出 %3")
            .arg(childPid).arg(closing.elapsed()).arg(exited);
        // QProcess 只拥有本测试创建的子进程；失败时清理不记作正常退出。
        if (!exited) { process.kill(); process.waitForFinished(5000); }
        QFile finalTrace(report + QStringLiteral(".log"));
        if (finalTrace.open(QIODevice::ReadOnly))
            qInfo().noquote() << QString::fromUtf8(finalTrace.readAll());
        QVERIFY2(exited, "真实向导窗口关闭后进程仍残留超过20秒；已清理本测试子进程");
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QFile result(report);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(result.readAll()).object().value(QStringLiteral("passed")).toBool());
    }
};

QTEST_GUILESS_MAIN(ApplicationExitTest)
#include "tst_application_exit.moc"
