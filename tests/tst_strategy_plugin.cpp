#include "plugin/StrategyPluginManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace pp {

class StrategyPluginTest : public QObject {
    Q_OBJECT

private slots:
    void manifestParsesEntries();
    void expectedSha256ResolvesFileName();
    void computeFileSha256HashesContent();
    void scanReportsManifestStatus();
    void loadRejectsMissingOrMismatchedManifest();
    void loadsRealPluginAndCreatesStrategy();
};

namespace {
QString writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(content);
    file.close();
    return path;
}

QString sha256Hex(const QByteArray &content)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
}
} // namespace

void StrategyPluginTest::manifestParsesEntries()
{
    QString error;
    const QString text = QStringLiteral(
        "{\"apiVersion\":\"pp.StrategyPlugin/1.0\",\"plugins\":["
        "{\"file\":\"a.dll\",\"sha256\":\"abc123\"},"
        "{\"file\":\"b.dll\",\"sha256\":\"DEF456\"}]}");
    const auto entries = StrategyPluginManifest::parse(text, &error);
    QVERIFY(entries.has_value());
    QCOMPARE(entries->size(), 2);
    QCOMPARE(entries->at(0).file, QStringLiteral("a.dll"));
    QCOMPARE(entries->at(0).sha256, QStringLiteral("abc123"));
    QCOMPARE(entries->at(1).sha256, QStringLiteral("def456")); // 归一化为小写

    QVERIFY(!StrategyPluginManifest::parse(QStringLiteral("{broken"), &error).has_value());
    QVERIFY(!error.isEmpty());
}

void StrategyPluginTest::expectedSha256ResolvesFileName()
{
    QString error;
    const QString text = QStringLiteral(
        "{\"plugins\":[{\"file\":\"sample.dll\",\"sha256\":\"ABCDEF\"}]}");
    const auto expected = StrategyPluginManager::expectedSha256(
        text, QStringLiteral("sample.dll"), &error);
    QVERIFY(expected.has_value());
    QCOMPARE(*expected, QStringLiteral("abcdef"));

    QVERIFY(!StrategyPluginManager::expectedSha256(
        text, QStringLiteral("missing.dll"), &error).has_value());
    QVERIFY(error.contains(QStringLiteral("未登记")));
}

void StrategyPluginTest::computeFileSha256HashesContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("plugin.bin"));
    const QByteArray content("fake plugin content 12345");
    writeFile(path, content);

    QString error;
    const QString hash = StrategyPluginManager::computeFileSha256(path, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(hash, sha256Hex(content));

    QVERIFY(StrategyPluginManager::computeFileSha256(
        directory.filePath(QStringLiteral("missing.bin")), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void StrategyPluginTest::scanReportsManifestStatus()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray pluginContent("dummy dll bytes");
    const QString pluginPath = writeFile(directory.filePath(QStringLiteral("sample.dll")),
                                         pluginContent);
    QVERIFY(!pluginPath.isEmpty());
    const QString manifestText = QStringLiteral(
        "{\"plugins\":[{\"file\":\"sample.dll\",\"sha256\":\"%1\"}]}")
        .arg(sha256Hex(pluginContent));
    writeFile(directory.filePath(QStringLiteral("plugins.manifest")),
              manifestText.toUtf8());

    StrategyPluginManager manager;
    manager.setPluginDirectory(directory.path());
    const auto results = manager.scan();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().fileName, QStringLiteral("sample.dll"));
    QVERIFY(results.first().manifestOk);
    QVERIFY(results.first().error.isEmpty());
}

void StrategyPluginTest::loadRejectsMissingOrMismatchedManifest()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray pluginContent("dummy dll bytes");
    writeFile(directory.filePath(QStringLiteral("sample.dll")), pluginContent);

    // 无 manifest：拒绝。
    StrategyPluginManager manager;
    manager.setPluginDirectory(directory.path());
    QString error;
    QVERIFY(manager.load(QStringLiteral("sample.dll"), &error) == nullptr);
    QVERIFY(!error.isEmpty());

    // manifest 哈希不一致：拒绝。
    const QString wrongManifest = QStringLiteral(
        "{\"plugins\":[{\"file\":\"sample.dll\",\"sha256\":\"deadbeef\"}]}");
    writeFile(directory.filePath(QStringLiteral("plugins.manifest")),
              wrongManifest.toUtf8());
    error.clear();
    QVERIFY(manager.load(QStringLiteral("sample.dll"), &error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("不一致")));

    // manifest 未登记该文件：拒绝。
    const QString missingManifest = QStringLiteral(
        "{\"plugins\":[{\"file\":\"other.dll\",\"sha256\":\"deadbeef\"}]}");
    writeFile(directory.filePath(QStringLiteral("plugins.manifest")),
              missingManifest.toUtf8());
    error.clear();
    QVERIFY(manager.load(QStringLiteral("sample.dll"), &error) == nullptr);
    QVERIFY(error.contains(QStringLiteral("未登记")));
}

void StrategyPluginTest::loadsRealPluginAndCreatesStrategy()
{
#ifdef SAMPLE_PLUGIN_PATH
    const QString dllPath = QStringLiteral(SAMPLE_PLUGIN_PATH);
    if (!QFile::exists(dllPath)) {
        QSKIP("sample_strategy_plugin.dll 未构建，跳过真加载");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString copied = directory.filePath(QStringLiteral("sample_strategy_plugin.dll"));
    QVERIFY(QFile::copy(dllPath, copied));

    QString hashError;
    const QString sha256 = StrategyPluginManager::computeFileSha256(copied, &hashError);
    QVERIFY2(!sha256.isEmpty(), qPrintable(hashError));
    const QString manifestText = QStringLiteral(
        "{\"plugins\":[{\"file\":\"sample_strategy_plugin.dll\",\"sha256\":\"%1\"}]}")
        .arg(sha256);
    writeFile(directory.filePath(QStringLiteral("plugins.manifest")), manifestText.toUtf8());

    StrategyPluginManager manager;
    manager.setPluginDirectory(directory.path());

    QString error;
    StrategyPlugin *plugin = manager.load(QStringLiteral("sample_strategy_plugin.dll"), &error);
    QVERIFY2(plugin != nullptr, qPrintable(error));
    QCOMPARE(plugin->pluginId(), QStringLiteral("pp.sample.threshold"));
    QCOMPARE(plugin->pluginApiVersion(), QStringLiteral("pp.StrategyPlugin/1.0"));

    QVariantMap config;
    config.insert(QStringLiteral("symbol"), QStringLiteral("600519"));
    config.insert(QStringLiteral("threshold"), 100.0);
    config.insert(QStringLiteral("strategyId"), QStringLiteral("sample-threshold-600519"));
    Strategy *strategy = plugin->createStrategy(config);
    QVERIFY(strategy != nullptr);
    QCOMPARE(strategy->id(), QStringLiteral("sample-threshold-600519"));
    QCOMPARE(strategy->subscribedSymbols(), QStringList{QStringLiteral("600519")});
    delete strategy;

    QVERIFY(manager.unload(QStringLiteral("pp.sample.threshold")));
    QVERIFY(manager.loadedPluginIds().isEmpty());
#else
    QSKIP("SAMPLE_PLUGIN_PATH 未定义，跳过真加载");
#endif
}

} // namespace pp

QTEST_GUILESS_MAIN(pp::StrategyPluginTest)

#include "tst_strategy_plugin.moc"
