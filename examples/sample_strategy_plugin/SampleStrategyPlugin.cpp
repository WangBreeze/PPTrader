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
