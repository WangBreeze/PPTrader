#pragma once

#include "plugin/StrategyPluginInterface.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <optional>

class QPluginLoader;

namespace pp {

// 插件清单（plugins.manifest）：目录级白名单，逐文件记录期望 SHA-256。
// 完整性校验而非作者签名：清单控制「哪些文件可加载」，哈希防篡改。
struct StrategyPluginManifestEntry {
    QString file;
    QString sha256;
};

class StrategyPluginManifest {
public:
    static std::optional<QVector<StrategyPluginManifestEntry>> parse(const QString &text,
                                                                     QString *error = nullptr);
};

// 一个已加载插件的运行时记录。
struct StrategyPluginRecord {
    QString pluginId;
    QString pluginName;
    int pluginVersion = 0;
    QString pluginDescription;
    QString apiVersion;
    QString fileName;
    QString error;
};

class StrategyPluginManager final : public QObject {
    Q_OBJECT

public:
    explicit StrategyPluginManager(QObject *parent = nullptr);

    void setPluginDirectory(const QString &directory);
    QString pluginDirectory() const;

    // 扫描插件目录，返回候选文件（.dll/.so）及其清单校验状态（不加载）。
    struct ScanResult {
        QString fileName;
        QString sha256;
        bool manifestOk = false;
        QString error;
    };
    QVector<ScanResult> scan();

    // 校验清单 + QPluginLoader 加载 + qobject_cast + API 版本校验。
    StrategyPlugin *load(const QString &fileName, QString *error = nullptr);
    // 卸载指定插件（调用方须先停用其创建的策略）。
    bool unload(const QString &pluginId, QString *error = nullptr);

    QVector<StrategyPluginRecord> loadedPlugins() const;
    QStringList loadedPluginIds() const;
    StrategyPlugin *plugin(const QString &pluginId) const;

    // 可测的清单校验：给定 manifest 文本与文件名，返回期望 SHA-256。
    static std::optional<QString> expectedSha256(const QString &manifestText,
                                                 const QString &fileName,
                                                 QString *error = nullptr);
    static QString computeFileSha256(const QString &absolutePath, QString *error = nullptr);

signals:
    void pluginsChanged();

private:
    QString m_directory;
    QHash<QString, QPluginLoader *> m_loaders;      // pluginId → loader（拥有）
    QHash<QString, StrategyPlugin *> m_instances;   // pluginId → instance（不拥有）
    QHash<QString, StrategyPluginRecord> m_records; // pluginId → record
};

} // namespace pp
