#include "strategy/CustomStrategyRuntimeManager.h"

#include "strategy/CustomExecutionStrategy.h"
#include "strategy/StrategyEngine.h"

namespace pp {

QVariantMap CustomStrategyPlugin::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("strategyId"), definition.strategyId);
    map.insert(QStringLiteral("name"), definition.name);
    map.insert(QStringLiteral("version"), definition.version);
    map.insert(QStringLiteral("symbol"), instrument.symbol);
    map.insert(QStringLiteral("state"), static_cast<int>(state));
    map.insert(QStringLiteral("stateName"), customStrategyRuntimeStateName(state));
    map.insert(QStringLiteral("error"), error);
    return map;
}

CustomStrategyRuntimeManager::CustomStrategyRuntimeManager(StrategyEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

bool CustomStrategyRuntimeManager::load(const CustomStrategyDefinition &definition,
                                        const Instrument &instrument,
                                        QString *error)
{
    if (definition.strategyId.isEmpty()) {
        if (error) *error = QStringLiteral("策略 ID 为空");
        return false;
    }
    if (m_plugins.contains(definition.strategyId)) {
        // 幂等：已加载则直接返回，不重复编译。
        if (error) error->clear();
        return true;
    }

    QString compileError;
    const auto plan = m_compiler.compile(definition, {}, &compileError);
    if (!plan) {
        if (error) *error = compileError;
        return false;
    }

    CustomStrategyPlugin plugin;
    plugin.definition = definition;
    plugin.plan = *plan;
    plugin.instrument = instrument;
    plugin.state = CustomStrategyRuntimeState::Loaded;
    m_plugins.insert(definition.strategyId, plugin);
    emit pluginsChanged();
    emit pluginStateChanged(definition.strategyId, static_cast<int>(plugin.state));
    return true;
}

bool CustomStrategyRuntimeManager::activate(const QString &strategyId)
{
    auto iterator = m_plugins.find(strategyId);
    if (iterator == m_plugins.end()) return false;
    if (iterator->state == CustomStrategyRuntimeState::Active) return true;

    auto *strategy = new CustomExecutionStrategy(iterator->plan, iterator->instrument);
    m_engine->addStrategy(strategy);
    iterator->strategy = strategy;
    iterator->state = CustomStrategyRuntimeState::Active;
    iterator->error.clear();
    emit pluginsChanged();
    emit pluginStateChanged(strategyId, static_cast<int>(iterator->state));
    return true;
}

bool CustomStrategyRuntimeManager::deactivate(const QString &strategyId)
{
    auto iterator = m_plugins.find(strategyId);
    if (iterator == m_plugins.end()) return false;
    if (iterator->state != CustomStrategyRuntimeState::Active) return true;

    m_engine->removeStrategy(strategyId);
    iterator->strategy = nullptr;
    iterator->state = CustomStrategyRuntimeState::Loaded;
    emit pluginsChanged();
    emit pluginStateChanged(strategyId, static_cast<int>(iterator->state));
    return true;
}

bool CustomStrategyRuntimeManager::unload(const QString &strategyId)
{
    auto iterator = m_plugins.find(strategyId);
    if (iterator == m_plugins.end()) return false;
    if (iterator->state == CustomStrategyRuntimeState::Active) {
        deactivate(strategyId);
    }
    m_plugins.erase(iterator);
    emit pluginsChanged();
    emit pluginStateChanged(strategyId,
                            static_cast<int>(CustomStrategyRuntimeState::Unloaded));
    return true;
}

CustomStrategyRuntimeState CustomStrategyRuntimeManager::state(const QString &strategyId) const
{
    const auto iterator = m_plugins.constFind(strategyId);
    if (iterator == m_plugins.constEnd()) return CustomStrategyRuntimeState::Unloaded;
    return iterator->state;
}

const CustomStrategyPlugin *CustomStrategyRuntimeManager::plugin(const QString &strategyId) const
{
    const auto iterator = m_plugins.constFind(strategyId);
    if (iterator == m_plugins.constEnd()) return nullptr;
    return &*iterator;
}

QVector<CustomStrategyPlugin> CustomStrategyRuntimeManager::plugins() const
{
    QVector<CustomStrategyPlugin> result;
    result.reserve(m_plugins.size());
    for (const CustomStrategyPlugin &plugin : m_plugins) {
        result.append(plugin);
    }
    return result;
}

} // namespace pp
