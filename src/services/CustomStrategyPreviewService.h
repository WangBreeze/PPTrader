#pragma once

#include "domain/CustomStrategyTypes.h"
#include "domain/MarketTypes.h"

#include <QVector>

namespace pp {

// 只读预览：把已编译的自定义策略在给定 K 线上重放，
// 返回指标序列（按别名）和 B/S 信号，供向导预览图使用。
class CustomStrategyPreviewService {
public:
    struct PreviewResult {
        QVector<QString> seriesAliases;
        QVector<QString> seriesLabels;
        struct PreviewBar {
            QDateTime time;
            double open = 0.0;
            double high = 0.0;
            double low = 0.0;
            double close = 0.0;
            double volume = 0.0;
            QVector<std::optional<double>> seriesValues;
        };
        QVector<PreviewBar> bars;
        struct PreviewSignal {
            int barIndex = -1;
            int side = 0; // 0=Buy, 1=Sell
            double price = 0.0;
            QString reason;
        };
        QVector<PreviewSignal> signalEvents;
    };

    static PreviewResult buildPreview(
        const CustomStrategyDefinition &definition,
        const QVariantMap &runtimeParameters,
        const QVector<Candle> &candles,
        QString *error = nullptr);
};

} // namespace pp
