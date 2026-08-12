#include "services/CustomStrategyPreviewService.h"

#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"

#include <optional>

namespace pp {

namespace {

class PreviewSignalCollector final : public QObject {
    Q_OBJECT
public:
    QVector<CustomStrategyPreviewService::PreviewResult::PreviewSignal> events;

public slots:
    void onSignal(const TradeSignal &signal)
    {
        CustomStrategyPreviewService::PreviewResult::PreviewSignal previewSignal;
        previewSignal.barIndex = -1;
        previewSignal.side = signal.side == SignalSide::Buy ? 0 : 1;
        previewSignal.price = signal.price;
        previewSignal.reason = signal.reason;
        events.append(previewSignal);
    }
};

} // namespace

CustomStrategyPreviewService::PreviewResult CustomStrategyPreviewService::buildPreview(
    const CustomStrategyDefinition &definition,
    const QVariantMap &runtimeParameters,
    const QVector<Candle> &candles,
    QString *error)
{
    PreviewResult result;
    CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, runtimeParameters, error);
    if (!plan) return result;

    for (const CompiledCustomIndicator &indicator : plan->indicators) {
        QString label = indicator.alias;
        if (label.isEmpty()) {
            QString kindName;
            switch (indicator.kind) {
            case CustomIndicatorKind::Sma: kindName = QStringLiteral("SMA"); break;
            case CustomIndicatorKind::Ema: kindName = QStringLiteral("EMA"); break;
            case CustomIndicatorKind::Rsi: kindName = QStringLiteral("RSI"); break;
            case CustomIndicatorKind::Macd: kindName = QStringLiteral("MACD"); break;
            case CustomIndicatorKind::Bollinger: kindName = QStringLiteral("BOLLINGER"); break;
            case CustomIndicatorKind::Atr: kindName = QStringLiteral("ATR"); break;
            case CustomIndicatorKind::VolumeSma: kindName = QStringLiteral("VOLUME_SMA"); break;
            case CustomIndicatorKind::Roc: kindName = QStringLiteral("ROC"); break;
            case CustomIndicatorKind::Vwap: kindName = QStringLiteral("VWAP"); break;
            case CustomIndicatorKind::PpFlow: kindName = QStringLiteral("PP_FLOW"); break;
            }
            if (indicator.period > 0) label = kindName + QString::number(indicator.period);
            else label = kindName;
        }
        result.seriesAliases.append(indicator.alias.isEmpty()
            ? QStringLiteral("s%1").arg(result.seriesAliases.size()) : indicator.alias);
        result.seriesLabels.append(label);
    }

    CustomIndicatorRuntime runtime(plan->indicators);
    CustomBacktestStrategy strategy(*plan);
    PreviewSignalCollector collector;
    QObject::connect(&strategy, &Strategy::signalGenerated,
                     &collector, &PreviewSignalCollector::onSignal);

    for (const Candle &candle : candles) {
        runtime.append(candle);
        strategy.onCandle(candle);
        PreviewResult::PreviewBar bar;
        bar.time = candle.startTime;
        bar.open = candle.open;
        bar.high = candle.high;
        bar.low = candle.low;
        bar.close = candle.close;
        bar.volume = candle.volume;
        for (int index = 0; index < plan->indicators.size(); ++index) {
            bar.seriesValues.append(runtime.value(index));
        }
        result.bars.append(bar);
        if (collector.events.size() > result.signalEvents.size()) {
            PreviewResult::PreviewSignal previewSignal = collector.events.last();
            previewSignal.barIndex = result.bars.size() - 1;
            result.signalEvents.append(previewSignal);
        }
    }
    return result;
}

} // namespace pp

#include "CustomStrategyPreviewService.moc"
