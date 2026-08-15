#include "plugin/StrategyPluginManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPluginLoader>

namespace pp {

namespace {

const QString kManifestFileName = QStringLiteral("plugins.manifest");
const QString kApiVersion = QStringLiteral("pp.StrategyPlugin/1.0");

QString manifestPath(const QString &directory)
{
    return QDir(directory).filePath(kManifestFileName);
}

} // namespace

std::optional<QVector<StrategyPluginManifestEntry>>
StrategyPluginManifest::parse(const QString &text, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("清单 JSON 无效：%1").arg(parseError.errorString());
        return std::nullopt;
    }
    const QJsonArray plugins = document.object().value(QStringLiteral("plugins")).toArray();
    QVector<StrategyPluginManifestEntry> entries;
    entries.reserve(plugins.size());
    for (const QJsonValue &value : plugins) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        StrategyPluginManifestEntry entry;
        entry.file = object.value(QStringLiteral("file")).toString();
        entry.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
        if (entry.file.isEmpty() || entry.sha256.isEmpty()) continue;
        entries.append(entry);
    }
    return entries;
}

std::optional<QString> StrategyPluginManager::expectedSha256(const QString &manifestText,
                                                             const QString &fileName,
                                                             QString *error)
{
    const auto entries = StrategyPluginManifest::parse(manifestText, error);
    if (!entries) return std::nullopt;
    for (const StrategyPluginManifestEntry &entry : *entries) {
        if (entry.file == fileName) return entry.sha256;
    }
    if (error) *error = QStringLiteral("清单中未登记该插件文件：%1").arg(fileName);
    return std::nullopt;
}

QString StrategyPluginManager::computeFileSha256(const QString &absolutePath, QString *error)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取插件文件：%1").arg(absolutePath);
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();
    return QString::fromLatin1(hash.result().toHex());
}

StrategyPluginManager::StrategyPluginManager(QObject *parent)
    : QObject(parent)
{
}

void StrategyPluginManager::setPluginDirectory(const QString &directory)
{
    m_directory = directory;
}

QString StrategyPluginManager::pluginDirectory() const
{
    return m_directory;
}

QVector<StrategyPluginManager::ScanResult> StrategyPluginManager::scan()
{
    QVector<ScanResult> results;
    if (m_directory.isEmpty()) return results;

    const QDir directory(m_directory);
    const QStringList files = directory.entryList({QStringLiteral("*.dll"), QStringLiteral("*.so")},
                                                  QDir::Files, QDir::Name);
    QString manifestError;
    QFile manifestFile(manifestPath(m_directory));
    QString manifestText;
    if (manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        manifestText = QString::fromUtf8(manifestFile.readAll());
    }

    for (const QString &file : files) {
        ScanResult result;
        result.fileName = file;
        QString error;
        result.sha256 = computeFileSha256(directory.filePath(file), &error);
        if (result.sha256.isEmpty()) {
            result.error = error;
            results.append(result);
            continue;
        }
        const auto expected = expectedSha256(manifestText, file, &error);
        if (!expected) {
            result.error = error;
            results.append(result);
            continue;
        }
        result.manifestOk = (expected->compare(result.sha256, Qt::CaseInsensitive) == 0);
        if (!result.manifestOk) {
            result.error = QStringLiteral("SHA-256 与清单不一致");
        }
        results.append(result);
    }
    return results;
}

StrategyPlugin *StrategyPluginManager::load(const QString &fileName, QString *error)
{
    if (error) error->clear();
    if (m_directory.isEmpty()) {
        if (error) *error = QStringLiteral("插件目录未配置");
        return nullptr;
    }

    const QDir directory(m_directory);
    const QString absolutePath = directory.filePath(fileName);
    if (!QFileInfo::exists(absolutePath)) {
        if (error) *error = QStringLiteral("插件文件不存在：%1").arg(fileName);
        return nullptr;
    }

    QString hashError;
    const QString actualHash = computeFileSha256(absolutePath, &hashError);
    if (actualHash.isEmpty()) {
        if (error) *error = hashError;
        return nullptr;
    }

    QFile manifestFile(manifestPath(m_directory));
    QString manifestText;
    if (manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        manifestText = QString::fromUtf8(manifestFile.readAll());
    }
    const auto expected = expectedSha256(manifestText, fileName, error);
    if (!expected) return nullptr;
    if (expected->compare(actualHash, Qt::CaseInsensitive) != 0) {
        if (error) *error = QStringLiteral("插件 SHA-256 与清单不一致，拒绝加载：%1").arg(fileName);
        return nullptr;
    }

    auto *loader = new QPluginLoader(absolutePath, this);
    if (!loader->load()) {
        const QString message = loader->errorString();
        delete loader;
        if (error) *error = QStringLiteral("插件加载失败：%1").arg(message);
        return nullptr;
    }

    QObject *root = loader->instance();
    auto *plugin = qobject_cast<StrategyPlugin *>(root);
    if (!plugin) {
        const QString message = QStringLiteral("插件未实现 pp::StrategyPlugin 接口");
        loader->unload();
        delete loader;
        if (error) *error = message;
        return nullptr;
    }

    if (plugin->pluginApiVersion() != kApiVersion) {
        const QString message = QStringLiteral("插件 ABI 版本不兼容：%1")
            .arg(plugin->pluginApiVersion());
        loader->unload();
        delete loader;
        if (error) *error = message;
        return nullptr;
    }
    if (plugin->pluginId().isEmpty()) {
        loader->unload();
        delete loader;
        if (error) *error = QStringLiteral("插件缺少 pluginId");
        return nullptr;
    }

    const QString pluginId = plugin->pluginId();
    if (m_loaders.contains(pluginId)) {
        // 幂等：已加载则卸载旧实例后重新加载。
        unload(pluginId);
    }

    StrategyPluginRecord record;
    record.pluginId = pluginId;
    record.pluginName = plugin->pluginName();
    record.pluginVersion = plugin->pluginVersion();
    record.pluginDescription = plugin->pluginDescription();
    record.apiVersion = plugin->pluginApiVersion();
    record.fileName = fileName;

    m_loaders.insert(pluginId, loader);
    m_instances.insert(pluginId, plugin);
    m_records.insert(pluginId, record);
    emit pluginsChanged();
    return plugin;
}

bool StrategyPluginManager::unload(const QString &pluginId, QString *error)
{
    QPluginLoader *loader = m_loaders.take(pluginId);
    if (!loader) {
        if (error) *error = QStringLiteral("插件未加载：%1").arg(pluginId);
        return false;
    }
    m_instances.remove(pluginId);
    m_records.remove(pluginId);
    const bool ok = loader->unload();
    delete loader;
    emit pluginsChanged();
    if (!ok && error) *error = QStringLiteral("插件卸载失败：%1").arg(pluginId);
    return ok;
}

QVector<StrategyPluginRecord> StrategyPluginManager::loadedPlugins() const
{
    QVector<StrategyPluginRecord> result;
    result.reserve(m_records.size());
    for (const StrategyPluginRecord &record : m_records) {
        result.append(record);
    }
    return result;
}

QStringList StrategyPluginManager::loadedPluginIds() const
{
    return m_records.keys();
}

StrategyPlugin *StrategyPluginManager::plugin(const QString &pluginId) const
{
    return m_instances.value(pluginId, nullptr);
}

} // namespace pp
