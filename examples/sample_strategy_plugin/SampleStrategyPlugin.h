#pragma once

#include "plugin/StrategyPluginInterface.h"

#include <QObject>

namespace pp {

// 示例第三方 C++ 策略插件：证明 QPluginLoader + StrategyPlugin 接口 + 元数据校验链路。
class SampleStrategyPlugin final : public QObject, public StrategyPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "pp.StrategyPlugin/1.0" FILE "sample_strategy_plugin.json")
    Q_INTERFACES(pp::StrategyPlugin)

public:
    QString pluginApiVersion() const override;
    QString pluginId() const override;
    QString pluginName() const override;
    int pluginVersion() const override;
    QString pluginDescription() const override;
    QVector<PluginParameterSpec> parameterSchema() const override;

    Strategy *createStrategy(const QVariantMap &config,
                             QObject *parent = nullptr) const override;
};

} // namespace pp
