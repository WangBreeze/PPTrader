#include "SampleStrategyPlugin.h"

#include "SampleThresholdStrategy.h"

#include "domain/MarketTypes.h"

namespace pp {

QString SampleStrategyPlugin::pluginApiVersion() const
{
    return QStringLiteral("pp.StrategyPlugin/1.0");
}

QString SampleStrategyPlugin::pluginId() const
{
    return QStringLiteral("pp.sample.threshold");
}

QString SampleStrategyPlugin::pluginName() const
{
    return QStringLiteral("示例阈值策略插件");
}

int SampleStrategyPlugin::pluginVersion() const
{
    return 1;
}

QString SampleStrategyPlugin::pluginDescription() const
{
    return QStringLiteral("第三方 C++ 策略插件示例：close 上穿/下穿固定阈值。");
}

QVector<PluginParameterSpec> SampleStrategyPlugin::parameterSchema() const
{
    PluginParameterSpec threshold;
    threshold.key = QStringLiteral("threshold");
    threshold.displayName = QStringLiteral("阈值");
    threshold.type = QStringLiteral("real");
    threshold.defaultValue = 100.0;
    threshold.minimumValue = 1.0;
    threshold.maximumValue = 10000.0;
    threshold.step = 1.0;
    return {threshold};
}

Strategy *SampleStrategyPlugin::createStrategy(const QVariantMap &config,
                                               QObject *parent) const
{
    Instrument instrument;
    instrument.symbol = config.value(QStringLiteral("symbol"), QStringLiteral("600519")).toString();
    instrument.name = config.value(QStringLiteral("name"), instrument.symbol).toString();
    instrument.market = Market::AShare;

    const double threshold = config.value(QStringLiteral("threshold"), 100.0).toDouble();
    const QString strategyId = config.value(
        QStringLiteral("strategyId"),
        QStringLiteral("sample-threshold-%1").arg(instrument.symbol)).toString();

    return new SampleThresholdStrategy(instrument, threshold, strategyId, parent);
}

} // namespace pp
