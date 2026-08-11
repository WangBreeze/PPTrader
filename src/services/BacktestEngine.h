#pragma once

#include "domain/BacktestTypes.h"
#include "models/CandleListModel.h"
#include "strategy/BacktestStrategyRegistry.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace pp {

class BacktestEngine final : public QObject {
    Q_OBJECT

public:
    explicit BacktestEngine(QObject *parent = nullptr);

    bool start(const BacktestConfig &config, QVector<Candle> candles,
               std::unique_ptr<BacktestStrategy> providedStrategy = {});
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

private:
    void processNextCandle();
    void handleStrategySignal(const TradeSignal &signal);
    void openPosition(const TradeSignal &signal, const Candle &candle);
    void closePosition(const TradeSignal &signal, const Candle &candle);
    void appendEquityPoint(const Candle &candle);
    void finishBacktest();
    PerformanceSummary calculateSummary() const;

    QTimer m_timer;
    BacktestStrategyRegistry m_registry;
    std::unique_ptr<BacktestStrategy> m_strategy;
    BacktestResult m_result;
    QVector<Candle> m_candles;
    BacktestPosition m_position;
    TradeSignal m_currentSignal;
    bool m_hasCurrentSignal = false;
    int m_index = 0;
    double m_cash = 0.0;
    double m_peakEquity = 0.0;
    double m_totalCommission = 0.0;
};

} // namespace pp
