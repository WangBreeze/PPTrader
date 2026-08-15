#pragma once

#include "domain/CustomStrategyTypes.h"
#include "domain/MarketTypes.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QHash>
#include <QObject>
#include <QVariantMap>

namespace pp {

class StrategyEngine;
class CustomExecutionStrategy;

// 自定义策略运行时插件的生命周期状态机：
//   Unloaded → Loaded（已编译，未进入模拟执行）→ Active（已加入 StrategyEngine）
//   Active → Loaded（停用，保留编译产物）→ Unloaded（卸载，释放）
enum class CustomStrategyRuntimeState {
    Unloaded,
    Loaded,
    Active
};

inline QString customStrategyRuntimeStateName(CustomStrategyRuntimeState state)
{
    switch (state) {
    case CustomStrategyRuntimeState::Unloaded: return QStringLiteral("未加载");
    case CustomStrategyRuntimeState::Loaded: return QStringLiteral("已加载");
    case CustomStrategyRuntimeState::Active: return QStringLiteral("模拟执行中");
    }
    return QStringLiteral("未知");
}

// 一个已加载/激活的自定义策略运行时单元（插件）。
struct CustomStrategyPlugin {
    CustomStrategyDefinition definition;
    CompiledCustomStrategy plan;
    CustomExecutionStrategy *strategy = nullptr;
    CustomStrategyRuntimeState state = CustomStrategyRuntimeState::Unloaded;
    Instrument instrument;
    QString error;

    QVariantMap toVariantMap() const;
};

// 把「通过校验的自定义策略定义」当作可加载/卸载的插件统一管理生命周期：
// load 编译并缓存（不激活），activate 实例化 CustomExecutionStrategy 并加入
// StrategyEngine，deactivate 退出执行但保留编译产物，unload 完全释放。
// 编译产物与状态只依赖稳定接口（StrategyEngine/CompiledCustomStrategy），
// 与应用内部实现解耦，故支持不重启应用的热插拔。
class CustomStrategyRuntimeManager final : public QObject {
    Q_OBJECT

public:
    explicit CustomStrategyRuntimeManager(StrategyEngine *engine, QObject *parent = nullptr);

    bool load(const CustomStrategyDefinition &definition, const Instrument &instrument,
              QString *error = nullptr);
    bool activate(const QString &strategyId);
    bool deactivate(const QString &strategyId);
    bool unload(const QString &strategyId);

    CustomStrategyRuntimeState state(const QString &strategyId) const;
    const CustomStrategyPlugin *plugin(const QString &strategyId) const;
    QVector<CustomStrategyPlugin> plugins() const;

signals:
    void pluginStateChanged(const QString &strategyId, int state);
    void pluginsChanged();

private:
    StrategyEngine *m_engine = nullptr;
    CustomStrategyCompiler m_compiler;
    QHash<QString, CustomStrategyPlugin> m_plugins;
};

} // namespace pp
