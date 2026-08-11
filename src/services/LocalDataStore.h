#pragma once

#include "domain/CrossSectionalTypes.h"
#include "domain/BacktestTypes.h"
#include "domain/LocalHistoryTypes.h"
#include "domain/MarketTypes.h"
#include "domain/TradingTypes.h"

#include <QObject>
#include <QVector>

#include <optional>

namespace pp {

class LocalDataStore : public QObject {
    Q_OBJECT

public:
    explicit LocalDataStore(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~LocalDataStore() override = default;

    virtual bool appendTick(const Tick &tick) = 0;
    virtual bool appendTicks(const QVector<Tick> &ticks)
    {
        for (const Tick &tick : ticks) {
            if (!appendTick(tick)) return false;
        }
        return true;
    }
    virtual bool appendMarketSnapshot(const MarketSnapshot &snapshot) = 0;
    virtual std::optional<MarketSnapshot> loadMarketSnapshotAtOrBefore(
        const QDateTime &requestedTime) = 0;
    virtual bool appendCandle(const Candle &candle) = 0;
    virtual bool upsertCandles(const QVector<Candle> &candles) = 0;
    virtual bool upsertHistoricalDataset(const QVector<Candle> &candles,
                                         int requestedBars,
                                         const QString &dataVersion)
    {
        Q_UNUSED(requestedBars)
        Q_UNUSED(dataVersion)
        return upsertCandles(candles);
    }
    virtual bool appendSignal(const TradeSignal &signal) = 0;
    virtual int tickCount() const = 0;
    virtual int signalCount() const = 0;
    virtual QVector<Candle> loadCandles(const Instrument &instrument, int periodSeconds, const QDateTime &from, const QDateTime &to) = 0;
    virtual QVector<Candle> loadRecentCandles(const Instrument &instrument, int periodSeconds, int limit) = 0;
    virtual QVector<Candle> loadRecentCandlesFromSource(
        const Instrument &instrument, int periodSeconds, int limit,
        const QString &source) = 0;
    virtual QVector<LocalHistoryDataset> localHistoryCatalog() = 0;
    virtual bool saveBacktestArchive(const BacktestArchiveRecord &record) = 0;
    virtual QVector<BacktestArchiveSummary> loadBacktestArchiveSummaries() = 0;
    virtual std::optional<BacktestArchiveRecord> loadBacktestArchive(
        const QString &resultId) = 0;
    virtual bool upsertOrder(const Order &order) = 0;
    virtual bool appendExecution(const Execution &execution) = 0;
    virtual bool saveTradingState(const AccountSnapshot &account, const QVector<Position> &positions) = 0;
    virtual TradingState loadTradingState() = 0;

signals:
    void writeFailed(const QString &message);
};

} // 命名空间 pp
