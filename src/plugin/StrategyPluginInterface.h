#pragma once

#include "strategy/Strategy.h"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace pp {

// 插件可创建策略的单个参数定义，供宿主动态生成参数表单。
struct PluginParameterSpec {
    QString key;
    QString displayName;
    QString type; // "integer" | "real" | "string" | "boolean"
    QVariant defaultValue;
    QVariant minimumValue;
    QVariant maximumValue;
    QVariant step;

    QVariantMap toVariantMap() const
    {
        return {{QStringLiteral("key"), key},
                {QStringLiteral("displayName"), displayName},
                {QStringLiteral("type"), type},
                {QStringLiteral("defaultValue"), defaultValue},
                {QStringLiteral("minimumValue"), minimumValue},
                {QStringLiteral("maximumValue"), maximumValue},
                {QStringLiteral("step"), step}};
    }
};

// 第三方 C++ 策略插件接口：插件导出元数据 + 参数 schema + 策略工厂。
// 插件实现该接口并用 Q_PLUGIN_METADATA 声明、Q_INTERFACES(pp::StrategyPlugin) 注册，
// 由宿主通过 QPluginLoader 加载；策略实例通过 StrategyEngine 接入模拟执行。
class StrategyPlugin {
public:
    virtual ~StrategyPlugin() = default;

    // 稳定接口 key：加载时校验与宿主 ABI 兼容，不匹配则拒绝加载。
    virtual QString pluginApiVersion() const = 0;
    virtual QString pluginId() const = 0;
    virtual QString pluginName() const = 0;
    virtual int pluginVersion() const = 0;
    virtual QString pluginDescription() const = 0;

    // 可创建策略的参数定义（供 UI 动态生成表单）；无参数插件可返回空。
    virtual QVector<PluginParameterSpec> parameterSchema() const { return {}; }

    // 创建一个 Strategy 实例（调用方取得所有权，通过 parent 管理生命周期）。
    virtual Strategy *createStrategy(const QVariantMap &config,
                                     QObject *parent = nullptr) const = 0;
};

} // namespace pp

#define ppStrategyPlugin_iid "pp.StrategyPlugin/1.0"
Q_DECLARE_INTERFACE(pp::StrategyPlugin, "pp.StrategyPlugin/1.0")

