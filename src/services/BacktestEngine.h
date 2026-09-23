#pragma once

#include "domain/BacktestTypes.h"
#include "models/CandleListModel.h"
#include "strategy/BacktestStrategyRegistry.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace pp {

class RawTickBacktestDataVersionBuilder;

class BacktestEngine final : public QObject {
    Q_OBJECT

public:
    static constexpr int RawTickDisplayEquityPointLimit = 8192;

    using RawTickChunkLoader =
        std::function<bool(QVector<RawTradeTick> *ticks, QString *error)>;

    explicit BacktestEngine(QObject *parent = nullptr);
    ~BacktestEngine() override;

    bool start(const BacktestConfig &config, QVector<Candle> candles,
               std::unique_ptr<BacktestStrategy> providedStrategy = {});
    bool start(const BacktestConfig &config, QVector<RawTradeTick> ticks,
               std::unique_ptr<BacktestStrategy> providedStrategy = {});
    bool startStreamedRawTicks(const BacktestConfig &config,
                               int expectedObservations,
                               RawTickChunkLoader loader);
    void cancel();
    bool isRunning() const;
    double progress() const;
    const BacktestResult &result() const;

signals:
    void pointReady(const pp::CandlePoint &point);
    void tradeCompleted(const pp::BacktestTrade &trade);
    void equityPointReady(const pp::EquityPoint &point);
    void progressChanged(double progress);
    void finished(const pp::PerformanceSummary &summary);
    void cancelled();
    void errorOccurred(const QString &message);
    void rawTickChunkResidencyChanged(int observations);

private:
    void processNextObservation();
    void handleStrategySignal(const TradeSignal &signal);
    void openPosition(const TradeSignal &signal, double marketPrice,
                      const QDateTime &timestamp);
    void closePosition(const TradeSignal &signal, double marketPrice,
                       const QDateTime &timestamp);
    void resetEquityTracking(int expectedObservations, bool rawMode);
    void appendEquityPoint(const QDateTime &timestamp, double marketPrice);
    void reviseLastEquityPointAfterLiquidation();
    void finalizeDisplayEquityCurve();
    void addEquityReturn(double value);
    void removeEquityReturn(double value);
    void finishBacktest();
    bool loadNextRawTickChunk();
    void failRawTickStream(const QString &message);
    PerformanceSummary calculateSummary() const;

    QTimer m_timer;
    BacktestStrategyRegistry m_registry;
    std::unique_ptr<BacktestStrategy> m_strategy;
    BacktestResult m_result;
    QVector<Candle> m_candles;
    QVector<RawTradeTick> m_rawTickChunk;
    RawTickChunkLoader m_rawTickChunkLoader;
    std::unique_ptr<RawTickBacktestDataVersionBuilder>
        m_rawTickDataVersionBuilder;
    RawTradeTick m_currentRawTick;
    RawTradeTick m_lastRawTick;
    BacktestPosition m_position;
    TradeSignal m_currentSignal;
    bool m_hasCurrentSignal = false;
    int m_index = 0;
    int m_rawTickChunkIndex = 0;
    int m_rawTickProcessed = 0;
    int m_rawTickExpected = 0;
    bool m_streamedRawTicks = false;
    bool m_hasCurrentRawTick = false;
    bool m_hasLastRawTick = false;
    double m_cash = 0.0;
    double m_peakEquity = 0.0;
    double m_totalCommission = 0.0;
    QVector<qint64> m_equityCurveObservationIndices;
    EquityPoint m_lastExactEquityPoint;
    EquityPoint m_maxDrawdownEquityPoint;
    qint64 m_equityObservationCount = 0;
    qint64 m_maxDrawdownObservationIndex = -1;
    int m_equitySampleStride = 1;
    bool m_equityCurveSampled = false;
    bool m_hasPreviousEquity = false;
    bool m_lastEquityReturnValid = false;
    double m_previousEquity = 0.0;
    double m_equityBeforeLast = 0.0;
    double m_lastEquityReturn = 0.0;
    qint64 m_equityReturnCount = 0;
    double m_equityReturnMean = 0.0;
    double m_equityReturnM2 = 0.0;
    double m_exactMaxDrawdownPercent = 0.0;
};

} // namespace pp
