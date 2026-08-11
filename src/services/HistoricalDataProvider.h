#pragma once

#include "domain/HistoricalProviderTypes.h"
#include "domain/MarketTypes.h"

#include <QObject>

namespace pp {

class HistoricalDataProvider : public QObject {
    Q_OBJECT

public:
    explicit HistoricalDataProvider(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~HistoricalDataProvider() override = default;
    virtual HistoricalProviderCapability capability() const
    {
        HistoricalProviderCapability result;
        result.providerId = QStringLiteral("legacy");
        result.displayName = QStringLiteral("兼容历史数据源");
        result.markets = {Market::AShare, Market::USStock,
                          Market::Crypto, Market::HongKong};
        result.frequencies = {QStringLiteral("1m"), QStringLiteral("5m"),
                              QStringLiteral("15m"), QStringLiteral("30m"),
                              QStringLiteral("60m"), QStringLiteral("day")};
        result.adjustments = {QStringLiteral("none")};
        result.configured = true;
        return result;
    }
    virtual void fetch(quint64 requestId, const Instrument &instrument,
                       const QString &frequency, int count) = 0;
    virtual void fetchAdjusted(quint64 requestId, const Instrument &instrument,
                               const QString &frequency, int count,
                               const QString &adjustment)
    {
        if (adjustment.isEmpty() || adjustment == QStringLiteral("none")) {
            fetch(requestId, instrument, frequency, count);
            return;
        }
        emit fetchError(requestId, QStringLiteral("该历史数据源不支持复权方式：%1")
                                       .arg(adjustment));
    }

signals:
    void candlesReady(quint64 requestId, const QVector<pp::Candle> &candles);
    void fetchError(quint64 requestId, const QString &message);
};

} // namespace pp
