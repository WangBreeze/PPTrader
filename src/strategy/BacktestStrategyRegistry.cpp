#include "strategy/BacktestStrategyRegistry.h"

#include "strategy/BollingerMeanReversionStrategy.h"
#include "strategy/MacdTrendStrategy.h"
#include "strategy/RsiMeanReversionStrategy.h"
#include "strategy/SmaCrossoverStrategy.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

namespace {

class TickMomentumBacktestStrategy final : public BacktestStrategy {
public:
    TickMomentumBacktestStrategy(int lookbackTicks, double thresholdPercent,
                                 int cooldownSeconds, int minimumHoldSeconds)
        : m_lookbackTicks(std::max(1, lookbackTicks))
        , m_thresholdPercent(std::max(0.0001, thresholdPercent))
        , m_cooldownSeconds(std::max(0, cooldownSeconds))
        , m_minimumHoldSeconds(std::max(0, minimumHoldSeconds))
    {
    }

    QString id() const override { return QStringLiteral("tick_momentum"); }
    QString displayName() const override { return QStringLiteral("逐笔动量"); }
    int minimumWarmupBars() const override { return m_lookbackTicks + 1; }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("lookbackTicks"), m_lookbackTicks},
                {QStringLiteral("thresholdPercent"), m_thresholdPercent},
                {QStringLiteral("cooldownSeconds"), m_cooldownSeconds},
                {QStringLiteral("minimumHoldSeconds"), m_minimumHoldSeconds}};
    }
    void reset() override
    {
        m_prices.clear();
        m_lastSignalTime = {};
        m_entrySignalTime = {};
        m_entryTradingDate = {};
        m_pendingExitReason.clear();
        m_longSignalActive = false;
    }
    void onCandle(const Candle &) override {}
    void onTick(const Tick &tick) override
    {
        if (tick.price <= 0.0 || !tick.timestamp.isValid()) return;
        static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
        const QDate tradingDate = tick.timestamp.toTimeZone(shanghai).date();
        if (m_longSignalActive && !m_pendingExitReason.isEmpty()
            && m_entryTradingDate.isValid() && tradingDate > m_entryTradingDate) {
            const QString reason = m_pendingExitReason + QStringLiteral("（T+1 延迟执行）");
            m_lastSignalTime = tick.timestamp;
            m_entrySignalTime = {};
            m_entryTradingDate = {};
            m_pendingExitReason.clear();
            m_longSignalActive = false;
            emitSignal(tick, SignalSide::Sell, reason);
            return;
        }
        m_prices.append(tick.price);
        if (m_prices.size() <= m_lookbackTicks) return;
        const double reference = m_prices.at(m_prices.size() - m_lookbackTicks - 1);
        if (m_prices.size() > m_lookbackTicks + 1) m_prices.removeFirst();
        if (reference <= 0.0) return;
        const double changePercent = (tick.price / reference - 1.0) * 100.0;
        if (std::abs(changePercent) < m_thresholdPercent) return;
        const bool buySignal = changePercent > 0.0;
        if (buySignal == m_longSignalActive) return;
        const QString reason = QStringLiteral("%1 笔逐笔动量 %2%")
            .arg(m_lookbackTicks).arg(changePercent, 0, 'f', 3);
        if (!buySignal && m_entryTradingDate.isValid()
            && tradingDate <= m_entryTradingDate) {
            if (m_pendingExitReason.isEmpty()) m_pendingExitReason = reason;
            return;
        }
        if (!buySignal && m_entrySignalTime.isValid()
            && m_entrySignalTime.secsTo(tick.timestamp) < m_minimumHoldSeconds) {
            return;
        }
        if (m_lastSignalTime.isValid()
            && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
            return;
        }
        m_lastSignalTime = tick.timestamp;
        m_longSignalActive = buySignal;
        m_entrySignalTime = buySignal ? tick.timestamp : QDateTime{};
        m_entryTradingDate = buySignal ? tradingDate : QDate{};
        m_pendingExitReason.clear();
        emitSignal(tick, buySignal ? SignalSide::Buy : SignalSide::Sell, reason);
    }

private:
    void emitSignal(const Tick &tick, SignalSide side, const QString &reason)
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = reason;
        emit signalGenerated(signal);
    }

    int m_lookbackTicks = 1;
    double m_thresholdPercent = 0.1;
    int m_cooldownSeconds = 0;
    int m_minimumHoldSeconds = 0;
    QVector<double> m_prices;
    QDateTime m_lastSignalTime;
    QDateTime m_entrySignalTime;
    QDate m_entryTradingDate;
    QString m_pendingExitReason;
    bool m_longSignalActive = false;
};

class TickMeanReversionBacktestStrategy final : public BacktestStrategy {
public:
    TickMeanReversionBacktestStrategy(int lookbackTicks,
                                      double entryDeviationPercent,
                                      double exitDeviationPercent,
                                      int cooldownSeconds,
                                      int minimumHoldSeconds)
        : m_lookbackTicks(std::max(2, lookbackTicks))
        , m_entryDeviationPercent(std::max(0.0001, entryDeviationPercent))
        , m_exitDeviationPercent(exitDeviationPercent)
        , m_cooldownSeconds(std::max(0, cooldownSeconds))
        , m_minimumHoldSeconds(std::max(0, minimumHoldSeconds))
    {
    }

    QString id() const override { return QStringLiteral("tick_mean_reversion"); }
    QString displayName() const override { return QStringLiteral("逐笔均值回归"); }
    int minimumWarmupBars() const override { return m_lookbackTicks + 1; }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("lookbackTicks"), m_lookbackTicks},
                {QStringLiteral("entryDeviationPercent"), m_entryDeviationPercent},
                {QStringLiteral("exitDeviationPercent"), m_exitDeviationPercent},
                {QStringLiteral("cooldownSeconds"), m_cooldownSeconds},
                {QStringLiteral("minimumHoldSeconds"), m_minimumHoldSeconds}};
    }
    void reset() override
    {
        m_prices.clear();
        m_lastSignalTime = {};
        m_entrySignalTime = {};
        m_entryTradingDate = {};
        m_pendingExitReason.clear();
        m_longSignalActive = false;
    }
    void onCandle(const Candle &) override {}
    void onTick(const Tick &tick) override
    {
        if (tick.price <= 0.0 || !tick.timestamp.isValid()) return;
        static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
        const QDate tradingDate = tick.timestamp.toTimeZone(shanghai).date();
        if (m_longSignalActive && !m_pendingExitReason.isEmpty()
            && m_entryTradingDate.isValid() && tradingDate > m_entryTradingDate) {
            const QString reason = m_pendingExitReason + QStringLiteral("（T+1 延迟执行）");
            m_lastSignalTime = tick.timestamp;
            m_entrySignalTime = {};
            m_entryTradingDate = {};
            m_pendingExitReason.clear();
            m_longSignalActive = false;
            emitSignal(tick, SignalSide::Sell, reason);
            return;
        }

        bool buySignal = false;
        bool sellSignal = false;
        double deviationPercent = 0.0;
        if (m_prices.size() == m_lookbackTicks) {
            const double mean = std::accumulate(m_prices.cbegin(), m_prices.cend(), 0.0)
                / static_cast<double>(m_prices.size());
            if (mean > 0.0) {
                deviationPercent = (tick.price / mean - 1.0) * 100.0;
                buySignal = !m_longSignalActive
                    && deviationPercent <= -m_entryDeviationPercent;
                sellSignal = m_longSignalActive
                    && deviationPercent >= m_exitDeviationPercent;
            }
        }
        m_prices.append(tick.price);
        if (m_prices.size() > m_lookbackTicks) m_prices.removeFirst();
        if (!buySignal && !sellSignal) return;

        const QString reason = buySignal
            ? QStringLiteral("%1 笔均值偏离 %2%")
                  .arg(m_lookbackTicks).arg(deviationPercent, 0, 'f', 3)
            : QStringLiteral("%1 笔价格回归均值 %2%")
                  .arg(m_lookbackTicks).arg(deviationPercent, 0, 'f', 3);
        if (sellSignal && m_entryTradingDate.isValid()
            && tradingDate <= m_entryTradingDate) {
            if (m_pendingExitReason.isEmpty()) m_pendingExitReason = reason;
            return;
        }
        if (sellSignal && m_entrySignalTime.isValid()
            && m_entrySignalTime.secsTo(tick.timestamp) < m_minimumHoldSeconds) {
            return;
        }
        if (m_lastSignalTime.isValid()
            && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
            return;
        }
        m_lastSignalTime = tick.timestamp;
        m_longSignalActive = buySignal;
        m_entrySignalTime = buySignal ? tick.timestamp : QDateTime{};
        m_entryTradingDate = buySignal ? tradingDate : QDate{};
        m_pendingExitReason.clear();
        emitSignal(tick, buySignal ? SignalSide::Buy : SignalSide::Sell, reason);
    }

private:
    void emitSignal(const Tick &tick, SignalSide side, const QString &reason)
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = reason;
        emit signalGenerated(signal);
    }

    int m_lookbackTicks = 2;
    double m_entryDeviationPercent = 0.3;
    double m_exitDeviationPercent = 0.0;
    int m_cooldownSeconds = 0;
    int m_minimumHoldSeconds = 0;
    QVector<double> m_prices;
    QDateTime m_lastSignalTime;
    QDateTime m_entrySignalTime;
    QDate m_entryTradingDate;
    QString m_pendingExitReason;
    bool m_longSignalActive = false;
};

class TickVolumeBreakoutBacktestStrategy final : public BacktestStrategy {
public:
    TickVolumeBreakoutBacktestStrategy(int lookbackTicks,
                                       double breakoutPercent,
                                       double breakdownPercent,
                                       double volumeMultiplier,
                                       int cooldownSeconds,
                                       int minimumHoldSeconds)
        : m_lookbackTicks(std::max(2, lookbackTicks))
        , m_breakoutPercent(std::max(0.0001, breakoutPercent))
        , m_breakdownPercent(std::max(0.0001, breakdownPercent))
        , m_volumeMultiplier(std::max(0.1, volumeMultiplier))
        , m_cooldownSeconds(std::max(0, cooldownSeconds))
        , m_minimumHoldSeconds(std::max(0, minimumHoldSeconds))
    {
    }

    QString id() const override { return QStringLiteral("tick_volume_breakout"); }
    QString displayName() const override { return QStringLiteral("逐笔放量突破"); }
    int minimumWarmupBars() const override { return m_lookbackTicks + 1; }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("lookbackTicks"), m_lookbackTicks},
                {QStringLiteral("breakoutPercent"), m_breakoutPercent},
                {QStringLiteral("breakdownPercent"), m_breakdownPercent},
                {QStringLiteral("volumeMultiplier"), m_volumeMultiplier},
                {QStringLiteral("cooldownSeconds"), m_cooldownSeconds},
                {QStringLiteral("minimumHoldSeconds"), m_minimumHoldSeconds}};
    }
    void reset() override
    {
        m_prices.clear();
        m_volumes.clear();
        m_lastSignalTime = {};
        m_entrySignalTime = {};
        m_entryTradingDate = {};
        m_pendingExitReason.clear();
        m_longSignalActive = false;
    }
    void onCandle(const Candle &) override {}
    void onTick(const Tick &tick) override
    {
        if (tick.price <= 0.0 || tick.volume < 0.0 || !tick.timestamp.isValid()) return;
        static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
        const QDate tradingDate = tick.timestamp.toTimeZone(shanghai).date();
        if (m_longSignalActive && !m_pendingExitReason.isEmpty()
            && m_entryTradingDate.isValid() && tradingDate > m_entryTradingDate) {
            const QString reason = m_pendingExitReason + QStringLiteral("（T+1 延迟执行）");
            m_lastSignalTime = tick.timestamp;
            m_entrySignalTime = {};
            m_entryTradingDate = {};
            m_pendingExitReason.clear();
            m_longSignalActive = false;
            emitSignal(tick, SignalSide::Sell, reason);
            return;
        }

        bool buySignal = false;
        bool sellSignal = false;
        double volumeRatio = 0.0;
        if (m_prices.size() == m_lookbackTicks) {
            const double priorHigh = *std::max_element(m_prices.cbegin(), m_prices.cend());
            const double priorLow = *std::min_element(m_prices.cbegin(), m_prices.cend());
            const double averageVolume = std::accumulate(
                m_volumes.cbegin(), m_volumes.cend(), 0.0)
                / static_cast<double>(m_volumes.size());
            volumeRatio = averageVolume > 0.0 ? tick.volume / averageVolume : 0.0;
            buySignal = !m_longSignalActive && averageVolume > 0.0
                && tick.price >= priorHigh * (1.0 + m_breakoutPercent / 100.0)
                && volumeRatio >= m_volumeMultiplier;
            sellSignal = m_longSignalActive
                && tick.price <= priorLow * (1.0 - m_breakdownPercent / 100.0);
        }
        m_prices.append(tick.price);
        m_volumes.append(tick.volume);
        if (m_prices.size() > m_lookbackTicks) {
            m_prices.removeFirst();
            m_volumes.removeFirst();
        }
        if (!buySignal && !sellSignal) return;

        const QString reason = buySignal
            ? QStringLiteral("%1 笔放量突破，成交量 %2 倍")
                  .arg(m_lookbackTicks).arg(volumeRatio, 0, 'f', 2)
            : QStringLiteral("%1 笔滚动低点跌破")
                  .arg(m_lookbackTicks);
        if (sellSignal && m_entryTradingDate.isValid()
            && tradingDate <= m_entryTradingDate) {
            if (m_pendingExitReason.isEmpty()) m_pendingExitReason = reason;
            return;
        }
        if (sellSignal && m_entrySignalTime.isValid()
            && m_entrySignalTime.secsTo(tick.timestamp) < m_minimumHoldSeconds) {
            return;
        }
        if (m_lastSignalTime.isValid()
            && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
            return;
        }
        m_lastSignalTime = tick.timestamp;
        m_longSignalActive = buySignal;
        m_entrySignalTime = buySignal ? tick.timestamp : QDateTime{};
        m_entryTradingDate = buySignal ? tradingDate : QDate{};
        m_pendingExitReason.clear();
        emitSignal(tick, buySignal ? SignalSide::Buy : SignalSide::Sell, reason);
    }

private:
    void emitSignal(const Tick &tick, SignalSide side, const QString &reason)
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = reason;
        emit signalGenerated(signal);
    }

    int m_lookbackTicks = 2;
    double m_breakoutPercent = 0.1;
    double m_breakdownPercent = 0.1;
    double m_volumeMultiplier = 1.5;
    int m_cooldownSeconds = 0;
    int m_minimumHoldSeconds = 0;
    QVector<double> m_prices;
    QVector<double> m_volumes;
    QDateTime m_lastSignalTime;
    QDateTime m_entrySignalTime;
    QDate m_entryTradingDate;
    QString m_pendingExitReason;
    bool m_longSignalActive = false;
};

class TickDirectionalImbalanceBacktestStrategy final : public BacktestStrategy {
public:
    TickDirectionalImbalanceBacktestStrategy(
        int lookbackTicks, double entryImbalancePercent,
        double exitImbalancePercent, double minimumDirectionalCoveragePercent,
        int cooldownSeconds, int minimumHoldSeconds)
        : m_lookbackTicks(std::max(2, lookbackTicks))
        , m_entryImbalancePercent(std::clamp(entryImbalancePercent, 0.0001, 100.0))
        , m_exitImbalancePercent(std::clamp(exitImbalancePercent, -100.0, 100.0))
        , m_minimumDirectionalCoveragePercent(
              std::clamp(minimumDirectionalCoveragePercent, 0.0, 100.0))
        , m_cooldownSeconds(std::max(0, cooldownSeconds))
        , m_minimumHoldSeconds(std::max(0, minimumHoldSeconds))
    {
    }

    QString id() const override
    {
        return QStringLiteral("tick_directional_imbalance");
    }
    QString displayName() const override { return QStringLiteral("逐笔方向失衡"); }
    int minimumWarmupBars() const override { return m_lookbackTicks; }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("lookbackTicks"), m_lookbackTicks},
                {QStringLiteral("entryImbalancePercent"), m_entryImbalancePercent},
                {QStringLiteral("exitImbalancePercent"), m_exitImbalancePercent},
                {QStringLiteral("minimumDirectionalCoveragePercent"),
                 m_minimumDirectionalCoveragePercent},
                {QStringLiteral("cooldownSeconds"), m_cooldownSeconds},
                {QStringLiteral("minimumHoldSeconds"), m_minimumHoldSeconds}};
    }
    void reset() override
    {
        m_signedVolumes.clear();
        m_directionalVolumes.clear();
        m_totalVolumes.clear();
        m_lastSignalTime = {};
        m_entrySignalTime = {};
        m_entryTradingDate = {};
        m_pendingExitReason.clear();
        m_longSignalActive = false;
    }
    void onCandle(const Candle &) override {}
    void onTick(const Tick &tick) override
    {
        if (tick.price <= 0.0 || tick.volume < 0.0 || !tick.timestamp.isValid()) return;
        static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
        const QDate tradingDate = tick.timestamp.toTimeZone(shanghai).date();
        if (m_longSignalActive && !m_pendingExitReason.isEmpty()
            && m_entryTradingDate.isValid() && tradingDate > m_entryTradingDate) {
            const QString reason = m_pendingExitReason + QStringLiteral("（T+1 延迟执行）");
            m_lastSignalTime = tick.timestamp;
            m_entrySignalTime = {};
            m_entryTradingDate = {};
            m_pendingExitReason.clear();
            m_longSignalActive = false;
            emitSignal(tick, SignalSide::Sell, reason);
            return;
        }
        if (!tick.rawTradeDirectionKnown) return;

        double signedVolume = 0.0;
        double directionalVolume = 0.0;
        if (tick.rawTradeDirection == RawTradeDirection::Buy) {
            signedVolume = tick.volume;
            directionalVolume = tick.volume;
        } else if (tick.rawTradeDirection == RawTradeDirection::Sell) {
            signedVolume = -tick.volume;
            directionalVolume = tick.volume;
        }
        m_signedVolumes.append(signedVolume);
        m_directionalVolumes.append(directionalVolume);
        m_totalVolumes.append(tick.volume);
        if (m_signedVolumes.size() > m_lookbackTicks) {
            m_signedVolumes.removeFirst();
            m_directionalVolumes.removeFirst();
            m_totalVolumes.removeFirst();
        }
        if (m_signedVolumes.size() < m_lookbackTicks) return;

        const double directionalTotal = std::accumulate(
            m_directionalVolumes.cbegin(), m_directionalVolumes.cend(), 0.0);
        const double total = std::accumulate(
            m_totalVolumes.cbegin(), m_totalVolumes.cend(), 0.0);
        const double coveragePercent = total > 0.0
            ? directionalTotal / total * 100.0 : 0.0;
        if (directionalTotal <= 0.0
            || coveragePercent < m_minimumDirectionalCoveragePercent) {
            return;
        }
        const double imbalancePercent = std::accumulate(
            m_signedVolumes.cbegin(), m_signedVolumes.cend(), 0.0)
            / directionalTotal * 100.0;
        const bool buySignal = !m_longSignalActive
            && imbalancePercent >= m_entryImbalancePercent;
        const bool sellSignal = m_longSignalActive
            && imbalancePercent <= m_exitImbalancePercent;
        if (!buySignal && !sellSignal) return;

        const QString reason = QStringLiteral(
            "%1 笔方向失衡 %2%，方向成交覆盖 %3%")
            .arg(m_lookbackTicks)
            .arg(imbalancePercent, 0, 'f', 2)
            .arg(coveragePercent, 0, 'f', 2);
        if (sellSignal && m_entryTradingDate.isValid()
            && tradingDate <= m_entryTradingDate) {
            if (m_pendingExitReason.isEmpty()) m_pendingExitReason = reason;
            return;
        }
        if (sellSignal && m_entrySignalTime.isValid()
            && m_entrySignalTime.secsTo(tick.timestamp) < m_minimumHoldSeconds) {
            return;
        }
        if (m_lastSignalTime.isValid()
            && m_lastSignalTime.secsTo(tick.timestamp) < m_cooldownSeconds) {
            return;
        }
        m_lastSignalTime = tick.timestamp;
        m_longSignalActive = buySignal;
        m_entrySignalTime = buySignal ? tick.timestamp : QDateTime{};
        m_entryTradingDate = buySignal ? tradingDate : QDate{};
        m_pendingExitReason.clear();
        emitSignal(tick, buySignal ? SignalSide::Buy : SignalSide::Sell, reason);
    }

private:
    void emitSignal(const Tick &tick, SignalSide side, const QString &reason)
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = reason;
        emit signalGenerated(signal);
    }

    int m_lookbackTicks = 2;
    double m_entryImbalancePercent = 30.0;
    double m_exitImbalancePercent = -10.0;
    double m_minimumDirectionalCoveragePercent = 50.0;
    int m_cooldownSeconds = 0;
    int m_minimumHoldSeconds = 0;
    QVector<double> m_signedVolumes;
    QVector<double> m_directionalVolumes;
    QVector<double> m_totalVolumes;
    QDateTime m_lastSignalTime;
    QDateTime m_entrySignalTime;
    QDate m_entryTradingDate;
    QString m_pendingExitReason;
    bool m_longSignalActive = false;
};

// 二板/三板研究策略只读取本标的已到达的原始 Tick。前日收盘板数、
// 09:35 快照和成交容量均在事件发生时确定，不能使用目标日收盘筛选入场。
class TickBoardContinuationBacktestStrategy final : public BacktestStrategy {
public:
    TickBoardContinuationBacktestStrategy(int targetBoardNumber,
                                          double minimumEarlyReturnPercent,
                                          double maximumEarlyDrawdownPercent,
                                          int maximumPriorSealMinutes,
                                          int entryDeadlineMinutesAfterOpen,
                                          double participationPercent,
                                          double maximumPositionAmount,
                                          bool excludeTouchedBeforeSignal)
        : m_targetBoardNumber(targetBoardNumber)
        , m_minimumEarlyReturnPercent(minimumEarlyReturnPercent)
        , m_maximumEarlyDrawdownPercent(maximumEarlyDrawdownPercent)
        , m_maximumPriorSealMinutes(maximumPriorSealMinutes)
        , m_entryDeadlineSeconds(9 * 3600 + 30 * 60
                                 + entryDeadlineMinutesAfterOpen * 60)
        , m_participationPercent(participationPercent)
        , m_maximumPositionAmount(maximumPositionAmount)
        , m_excludeTouchedBeforeSignal(excludeTouchedBeforeSignal)
    {
    }

    QString id() const override
    {
        return m_targetBoardNumber == 2 ? QStringLiteral("tick_second_board")
                                        : QStringLiteral("tick_third_board");
    }
    QString displayName() const override
    {
        return m_targetBoardNumber == 2 ? QStringLiteral("原始 Tick 二板接力")
                                        : QStringLiteral("原始 Tick 三板接力");
    }
    int minimumWarmupBars() const override { return 4; }
    double positionAmount() const override { return m_currentPositionAmount; }
    int targetPositionShares() const override { return m_plannedShares; }
    QVariantMap evaluationFacts() const override
    {
        return {{QStringLiteral("entryDayOutcomeKnownSignals"), m_entryOutcomesKnown},
                {QStringLiteral("entryDayClosedBoardSignals"), m_entryDaysClosedAtLimit}};
    }
    void onExecutionRejected(const TradeSignal &signal) override
    {
        if (signal.side != SignalSide::Buy && signal.side != SignalSide::Sell) return;
        m_holding = false;
        m_plannedShares = 0;
        m_currentPositionAmount = 0.0;
        m_entryDate = {};
        m_entryDayClosedAtLimit = false;
    }
    QVariantMap normalizedParameters() const override
    {
        return {{QStringLiteral("minimumEarlyReturnPercent"), m_minimumEarlyReturnPercent},
                {QStringLiteral("maximumEarlyDrawdownPercent"), m_maximumEarlyDrawdownPercent},
                {QStringLiteral("maximumPriorSealMinutes"), m_maximumPriorSealMinutes},
                {QStringLiteral("entryDeadlineMinutesAfterOpen"),
                 (m_entryDeadlineSeconds - 9 * 3600 - 30 * 60) / 60},
                {QStringLiteral("participationPercent"), m_participationPercent},
                {QStringLiteral("maximumPositionAmount"), m_maximumPositionAmount},
                {QStringLiteral("excludeTouchedBeforeSignal"),
                 int(m_excludeTouchedBeforeSignal)}};
    }
    void reset() override
    {
        m_day = {};
        m_previousClose = 0.0;
        m_priorFinalSealSeconds = -1;
        m_closedBoardStreak = 0;
        m_candidate = false;
        m_signalDecisionMade = false;
        m_entryArmed = false;
        m_currentPositionAmount = 0.0;
        m_plannedShares = 0;
        m_holding = false;
        m_entryDate = {};
        m_entryDayClosedAtLimit = false;
        m_entryOutcomesKnown = 0;
        m_entryDaysClosedAtLimit = 0;
    }
    void onCandle(const Candle &) override {}
    void onTick(const Tick &tick) override
    {
        if (!tick.timestamp.isValid() || tick.price <= 0.0 || tick.volume < 0.0
            || tick.instrument.market != Market::AShare
            || !isMainBoard(tick.instrument.symbol)
            || tick.instrument.name.contains(QStringLiteral("ST"), Qt::CaseInsensitive)
            || tick.instrument.name.contains(QStringLiteral("退"))) {
            return;
        }
        static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
        const QDateTime localTime = tick.timestamp.toTimeZone(shanghai);
        const QDate tradingDate = localTime.date();
        const QTime time = localTime.time();
        const int seconds = QTime(0, 0).secsTo(time);
        if (seconds < 9 * 3600 + 30 * 60 || seconds > 15 * 3600) return;

        if (!m_day.date.isValid() || tradingDate != m_day.date) {
            if (m_day.date.isValid() && tradingDate <= m_day.date) return;
            const QDate previousDate = m_day.date;
            if (previousDate.isValid()) finishDay();
            const bool contiguous = previousDate.isValid()
                && previousDate.daysTo(tradingDate) <= 4;
            if (!contiguous) m_closedBoardStreak = 0;
            m_day = {};
            m_day.date = tradingDate;
            m_day.referenceValid = contiguous && m_previousClose > 0.0;
            if (m_day.referenceValid) {
                m_day.limit = roundedLimit(m_previousClose, 1.10);
                m_day.lowerLimit = roundedLimit(m_previousClose, 0.90);
            }
            m_candidate = m_day.referenceValid
                && (m_targetBoardNumber == 2 ? m_closedBoardStreak == 1
                                             : m_closedBoardStreak >= 2)
                && m_priorFinalSealSeconds >= 0
                && m_priorFinalSealSeconds <= 9 * 3600 + 30 * 60
                                                + m_maximumPriorSealMinutes * 60;
            m_signalDecisionMade = false;
            m_entryArmed = false;
        }

        updateDay(tick.price, seconds);
        if (m_holding && tradingDate > m_entryDate && seconds >= kSignalSeconds
            && m_day.referenceValid && tick.price > m_day.lowerLimit + 0.004
            && tick.volume * m_participationPercent / 100.0 >= m_plannedShares) {
            emitSignal(tick, SignalSide::Sell,
                       m_entryDayClosedAtLimit
                           ? QStringLiteral("目标日封板命中；次日可售 Tick 退出")
                           : QStringLiteral("目标日未封板；次日可售 Tick 退出"));
            m_holding = false;
            m_plannedShares = 0;
            m_currentPositionAmount = 0.0;
            return;
        }

        if (seconds <= kSignalSeconds) return;
        if (!m_signalDecisionMade) {
            m_signalDecisionMade = true;
            const double earlyReturn = m_day.earlyLast > 0.0 && m_previousClose > 0.0
                ? (m_day.earlyLast / m_previousClose - 1.0) * 100.0 : -100.0;
            const double earlyDrawdown = m_day.earlyHigh > 0.0
                ? (m_day.earlyHigh - m_day.earlyLast) / m_day.earlyHigh * 100.0
                : 100.0;
            m_entryArmed = m_candidate && !m_holding
                && earlyReturn >= m_minimumEarlyReturnPercent
                && earlyDrawdown <= m_maximumEarlyDrawdownPercent
                && (!m_excludeTouchedBeforeSignal || !m_day.touchedBySignal);
        }
        if (!m_entryArmed || m_holding || seconds > m_entryDeadlineSeconds
            || tick.price >= m_day.limit - 0.004) return;

        // 回测引擎会在当前 Tick 执行；仅允许低于涨停价的真实成交，
        // 并把仓位限制在该笔成交量的参与率以内，避免一笔小成交吃满资金。
        m_currentPositionAmount = std::min(
            m_maximumPositionAmount,
            tick.price * tick.volume * m_participationPercent / 100.0);
        m_plannedShares = static_cast<int>(
            std::floor(m_currentPositionAmount / (tick.price * 1.002) / 100.0)) * 100;
        if (m_plannedShares < 100) return;
        m_holding = true;
        m_entryDate = tradingDate;
        m_entryDayClosedAtLimit = false;
        m_entryArmed = false;
        emitSignal(tick, SignalSide::Buy,
                   QStringLiteral("%1板接力；09:35 前因子达标，涨停价下方成交")
                       .arg(m_targetBoardNumber));
    }

private:
    struct DayState {
        QDate date;
        bool referenceValid = false;
        double limit = 0.0;
        double lowerLimit = 0.0;
        double close = 0.0;
        double earlyLast = 0.0;
        double earlyHigh = 0.0;
        bool atLimit = false;
        bool touchedBySignal = false;
        int finalSealSeconds = -1;
    };

    static constexpr int kSignalSeconds = 9 * 3600 + 35 * 60;

    static bool isMainBoard(const QString &symbol)
    {
        return symbol.size() == 6
            && (symbol.startsWith(QStringLiteral("000"))
                || symbol.startsWith(QStringLiteral("001"))
                || symbol.startsWith(QStringLiteral("002"))
                || symbol.startsWith(QStringLiteral("003"))
                || symbol.startsWith(QStringLiteral("600"))
                || symbol.startsWith(QStringLiteral("601"))
                || symbol.startsWith(QStringLiteral("603"))
                || symbol.startsWith(QStringLiteral("605")));
    }
    static double roundedLimit(double previousClose, double factor)
    {
        return std::round((previousClose * factor + 1e-9) * 100.0) / 100.0;
    }
    void updateDay(double price, int seconds)
    {
        m_day.close = price;
        if (seconds <= kSignalSeconds) {
            m_day.earlyLast = price;
            m_day.earlyHigh = std::max(m_day.earlyHigh, price);
        }
        const bool atLimit = m_day.referenceValid
            && std::abs(price - m_day.limit) < 0.005;
        if (atLimit && !m_day.atLimit) m_day.finalSealSeconds = seconds;
        if (!atLimit && m_day.atLimit) m_day.finalSealSeconds = -1;
        m_day.atLimit = atLimit;
        if (atLimit && seconds <= kSignalSeconds) m_day.touchedBySignal = true;
    }
    void finishDay()
    {
        const bool closedAtLimit = m_day.referenceValid && m_day.atLimit;
        m_closedBoardStreak = closedAtLimit ? m_closedBoardStreak + 1 : 0;
        m_priorFinalSealSeconds = closedAtLimit ? m_day.finalSealSeconds : -1;
        if (m_holding && m_day.date == m_entryDate) {
            m_entryDayClosedAtLimit = closedAtLimit;
            ++m_entryOutcomesKnown;
            if (closedAtLimit) ++m_entryDaysClosedAtLimit;
        }
        m_previousClose = m_day.close;
    }
    void emitSignal(const Tick &tick, SignalSide side, const QString &reason)
    {
        TradeSignal signal;
        signal.instrument = tick.instrument;
        signal.timestamp = tick.timestamp;
        signal.side = side;
        signal.price = tick.price;
        signal.volume = tick.volume;
        signal.reason = reason;
        emit signalGenerated(signal);
    }

    int m_targetBoardNumber = 2;
    double m_minimumEarlyReturnPercent = 0.0;
    double m_maximumEarlyDrawdownPercent = 0.0;
    int m_maximumPriorSealMinutes = 0;
    int m_entryDeadlineSeconds = 0;
    double m_participationPercent = 1.0;
    double m_maximumPositionAmount = 0.0;
    bool m_excludeTouchedBeforeSignal = true;
    DayState m_day;
    double m_previousClose = 0.0;
    int m_priorFinalSealSeconds = -1;
    int m_closedBoardStreak = 0;
    bool m_candidate = false;
    bool m_signalDecisionMade = false;
    bool m_entryArmed = false;
    double m_currentPositionAmount = 0.0;
    int m_plannedShares = 0;
    bool m_holding = false;
    QDate m_entryDate;
    bool m_entryDayClosedAtLimit = false;
    int m_entryOutcomesKnown = 0;
    int m_entryDaysClosedAtLimit = 0;
};

BacktestParameterSpec integerParameter(const QString &key, const QString &name,
                                       int defaultValue, int minimum, int maximum)
{
    return {key, name, QStringLiteral("integer"), defaultValue, minimum, maximum, 1};
}

BacktestParameterSpec realParameter(const QString &key, const QString &name,
                                    double defaultValue, double minimum,
                                    double maximum, double step)
{
    return {key, name, QStringLiteral("real"), defaultValue, minimum, maximum, step};
}

} // namespace

QVector<BacktestStrategyDescriptor> BacktestStrategyRegistry::descriptors() const
{
    return {
        {QStringLiteral("sma_crossover"), QStringLiteral("SMA 双均线"),
         QStringLiteral("快速均线上穿慢速均线买入，下穿卖出。"), QStringLiteral("A股"), 20,
         {integerParameter(QStringLiteral("fastPeriod"), QStringLiteral("快速周期"), 5, 2, 120),
          integerParameter(QStringLiteral("slowPeriod"), QStringLiteral("慢速周期"), 20, 3, 250)}},
        {QStringLiteral("rsi_mean_reversion"), QStringLiteral("RSI 均值回归"),
         QStringLiteral("RSI 进入超卖区买入，进入超买区卖出。"), QStringLiteral("A股"), 15,
         {integerParameter(QStringLiteral("period"), QStringLiteral("RSI 周期"), 14, 2, 120),
          realParameter(QStringLiteral("oversold"), QStringLiteral("超卖阈值"), 30.0, 1.0, 49.0, 1.0),
          realParameter(QStringLiteral("overbought"), QStringLiteral("超买阈值"), 70.0, 51.0, 99.0, 1.0)}},
        {QStringLiteral("macd_trend"), QStringLiteral("MACD 趋势"),
         QStringLiteral("MACD 线上穿信号线买入，下穿卖出。"), QStringLiteral("A股"), 35,
         {integerParameter(QStringLiteral("fastPeriod"), QStringLiteral("快速周期"), 12, 2, 120),
          integerParameter(QStringLiteral("slowPeriod"), QStringLiteral("慢速周期"), 26, 3, 250),
          integerParameter(QStringLiteral("signalPeriod"), QStringLiteral("信号周期"), 9, 2, 120)}},
        {QStringLiteral("bollinger_mean_reversion"), QStringLiteral("布林带均值回归"),
         QStringLiteral("跌破下轨买入，回归中轨卖出。"), QStringLiteral("A股"), 20,
         {integerParameter(QStringLiteral("period"), QStringLiteral("统计周期"), 20, 2, 250),
          realParameter(QStringLiteral("standardDeviations"), QStringLiteral("标准差倍数"),
                        2.0, 0.5, 5.0, 0.1)}, false, true},
        {QStringLiteral("tick_momentum"), QStringLiteral("逐笔动量"),
         QStringLiteral("比较当前成交价与指定笔数前成交价，仅用于原始 Tick 回测。"),
         QStringLiteral("A股"), 21,
         {integerParameter(QStringLiteral("lookbackTicks"), QStringLiteral("回看笔数"),
                           20, 1, 10000),
          realParameter(QStringLiteral("thresholdPercent"), QStringLiteral("动量阈值(%)"),
                        0.2, 0.0001, 20.0, 0.01),
          integerParameter(QStringLiteral("cooldownSeconds"), QStringLiteral("冷却秒数"),
                           30, 0, 3600),
          integerParameter(QStringLiteral("minimumHoldSeconds"), QStringLiteral("最小持仓秒数"),
                           60, 0, 86400)}, true, false},
        {QStringLiteral("tick_mean_reversion"), QStringLiteral("逐笔均值回归"),
         QStringLiteral("成交价偏离滚动均值后入场，回归均值后退出，仅用于原始 Tick 回测。"),
         QStringLiteral("A股"), 51,
         {integerParameter(QStringLiteral("lookbackTicks"), QStringLiteral("均值窗口笔数"),
                           50, 2, 10000),
          realParameter(QStringLiteral("entryDeviationPercent"), QStringLiteral("入场偏离(%)"),
                        0.3, 0.0001, 20.0, 0.01),
          realParameter(QStringLiteral("exitDeviationPercent"), QStringLiteral("退出偏离(%)"),
                        0.0, -20.0, 20.0, 0.01),
          integerParameter(QStringLiteral("cooldownSeconds"), QStringLiteral("冷却秒数"),
                           30, 0, 3600),
          integerParameter(QStringLiteral("minimumHoldSeconds"), QStringLiteral("最小持仓秒数"),
                           60, 0, 86400)}, true, false},
        {QStringLiteral("tick_volume_breakout"), QStringLiteral("逐笔放量突破"),
         QStringLiteral("成交价突破滚动高点且单笔成交量放大时入场，跌破滚动低点后退出，仅用于原始 Tick 回测。"),
         QStringLiteral("A股"), 51,
         {integerParameter(QStringLiteral("lookbackTicks"), QStringLiteral("突破窗口笔数"),
                           50, 2, 10000),
          realParameter(QStringLiteral("breakoutPercent"), QStringLiteral("向上突破幅度(%)"),
                        0.1, 0.0001, 20.0, 0.01),
          realParameter(QStringLiteral("breakdownPercent"), QStringLiteral("向下跌破幅度(%)"),
                        0.1, 0.0001, 20.0, 0.01),
          realParameter(QStringLiteral("volumeMultiplier"), QStringLiteral("成交量放大倍数"),
                        1.5, 0.1, 100.0, 0.1),
          integerParameter(QStringLiteral("cooldownSeconds"), QStringLiteral("冷却秒数"),
                           30, 0, 3600),
          integerParameter(QStringLiteral("minimumHoldSeconds"), QStringLiteral("最小持仓秒数"),
                           60, 0, 86400)}, true, false},
        {QStringLiteral("tick_directional_imbalance"), QStringLiteral("逐笔方向失衡"),
         QStringLiteral("按原始 B/S/M 成交方向计算滚动方向成交量失衡；M 不冒充买卖方向，仅用于原始 Tick 回测。"),
         QStringLiteral("A股"), 50,
         {integerParameter(QStringLiteral("lookbackTicks"), QStringLiteral("方向窗口笔数"),
                           50, 2, 10000),
          realParameter(QStringLiteral("entryImbalancePercent"), QStringLiteral("入场失衡阈值(%)"),
                        30.0, 0.0001, 100.0, 1.0),
          realParameter(QStringLiteral("exitImbalancePercent"), QStringLiteral("退出失衡阈值(%)"),
                        -10.0, -100.0, 100.0, 1.0),
          realParameter(QStringLiteral("minimumDirectionalCoveragePercent"),
                        QStringLiteral("最小方向成交覆盖(%)"), 50.0, 0.0, 100.0, 1.0),
          integerParameter(QStringLiteral("cooldownSeconds"), QStringLiteral("冷却秒数"),
                           30, 0, 3600),
          integerParameter(QStringLiteral("minimumHoldSeconds"), QStringLiteral("最小持仓秒数"),
                           60, 0, 86400)}, true, false},
        {QStringLiteral("tick_second_board"), QStringLiteral("原始 Tick 二板接力"),
         QStringLiteral("前日首板收盘封板，目标日 09:35 因子达标后仅在涨停价下方买入；次日可售时退出。研究策略，不保证排队成交。"),
         QStringLiteral("A股"), 4,
         {realParameter(QStringLiteral("minimumEarlyReturnPercent"), QStringLiteral("09:35 最低涨幅(%)"),
                        1.0, -10.0, 9.9, 0.1),
          realParameter(QStringLiteral("maximumEarlyDrawdownPercent"), QStringLiteral("09:35 最大回撤(%)"),
                        3.0, 0.0, 20.0, 0.1),
          integerParameter(QStringLiteral("maximumPriorSealMinutes"), QStringLiteral("前日最晚封板分钟"),
                           90, 0, 330),
          integerParameter(QStringLiteral("entryDeadlineMinutesAfterOpen"), QStringLiteral("最晚入场分钟"),
                           30, 6, 330),
          realParameter(QStringLiteral("participationPercent"), QStringLiteral("单笔成交参与率(%)"),
                        1.0, 0.1, 10.0, 0.1),
          realParameter(QStringLiteral("maximumPositionAmount"), QStringLiteral("单笔最高金额"),
                        50000.0, 1000.0, 1000000.0, 1000.0),
          integerParameter(QStringLiteral("excludeTouchedBeforeSignal"), QStringLiteral("排除信号前已触板"),
                           1, 0, 1)}, true, false},
        {QStringLiteral("tick_third_board"), QStringLiteral("原始 Tick 三板接力"),
         QStringLiteral("前日已连续两板及以上，目标日 09:35 因子达标后仅在涨停价下方买入；次日可售时退出。研究策略，不保证排队成交。"),
         QStringLiteral("A股"), 4,
         {realParameter(QStringLiteral("minimumEarlyReturnPercent"), QStringLiteral("09:35 最低涨幅(%)"),
                        1.0, -10.0, 9.9, 0.1),
          realParameter(QStringLiteral("maximumEarlyDrawdownPercent"), QStringLiteral("09:35 最大回撤(%)"),
                        3.0, 0.0, 20.0, 0.1),
          integerParameter(QStringLiteral("maximumPriorSealMinutes"), QStringLiteral("前日最晚封板分钟"),
                           60, 0, 330),
          integerParameter(QStringLiteral("entryDeadlineMinutesAfterOpen"), QStringLiteral("最晚入场分钟"),
                           30, 6, 330),
          realParameter(QStringLiteral("participationPercent"), QStringLiteral("单笔成交参与率(%)"),
                        1.0, 0.1, 10.0, 0.1),
          realParameter(QStringLiteral("maximumPositionAmount"), QStringLiteral("单笔最高金额"),
                        50000.0, 1000.0, 1000000.0, 1000.0),
          integerParameter(QStringLiteral("excludeTouchedBeforeSignal"), QStringLiteral("排除信号前已触板"),
                           1, 0, 1)}, true, false}
    };
}

std::optional<BacktestStrategyDescriptor> BacktestStrategyRegistry::descriptor(
    const QString &strategyId) const
{
    const QString normalizedId = strategyId.trimmed();
    for (const BacktestStrategyDescriptor &item : descriptors()) {
        if (item.id == normalizedId) return item;
    }
    return std::nullopt;
}

QVariantList BacktestStrategyRegistry::catalog() const
{
    QVariantList result;
    const QVector<BacktestStrategyDescriptor> items = descriptors();
    result.reserve(items.size());
    for (const BacktestStrategyDescriptor &item : items) result.append(item.toVariantMap());
    return result;
}

bool BacktestStrategyRegistry::supportsDataMode(
    const QString &strategyId, const QString &dataMode) const
{
    const auto item = descriptor(strategyId);
    if (!item) return false;
    const QString normalizedMode = dataMode.trimmed().toLower();
    if (normalizedMode == QStringLiteral("raw_ticks")) return item->supportsRawTicks;
    if (normalizedMode == QStringLiteral("candles")) return item->supportsCandles;
    return false;
}

bool BacktestStrategyRegistry::normalizeParameters(const QString &strategyId,
                                                   const QVariantMap &parameters,
                                                   QVariantMap *normalized,
                                                   QString *error) const
{
    const auto item = descriptor(strategyId);
    if (!item) {
        if (error) *error = QStringLiteral("未知回测策略：%1").arg(strategyId);
        return false;
    }

    for (auto iterator = parameters.cbegin(); iterator != parameters.cend(); ++iterator) {
        bool known = false;
        for (const BacktestParameterSpec &spec : item->parameters) {
            if (spec.key == iterator.key()) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (error) *error = QStringLiteral("策略参数不存在：%1").arg(iterator.key());
            return false;
        }
    }

    QVariantMap result;
    for (const BacktestParameterSpec &spec : item->parameters) {
        const QVariant value = parameters.value(spec.key, spec.defaultValue);
        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (!ok || !std::isfinite(numeric)) {
            if (error) *error = QStringLiteral("策略参数不是有效数字：%1").arg(spec.displayName);
            return false;
        }
        const double minimum = spec.minimumValue.toDouble();
        const double maximum = spec.maximumValue.toDouble();
        if (numeric < minimum || numeric > maximum) {
            if (error) {
                *error = QStringLiteral("策略参数超出范围：%1，允许 %2–%3")
                    .arg(spec.displayName).arg(minimum).arg(maximum);
            }
            return false;
        }
        if (spec.type == QStringLiteral("integer")) {
            const int integer = value.toInt(&ok);
            if (!ok || std::abs(numeric - integer) > 1e-9) {
                if (error) *error = QStringLiteral("策略参数必须为整数：%1").arg(spec.displayName);
                return false;
            }
            result.insert(spec.key, integer);
        } else {
            result.insert(spec.key, numeric);
        }
    }

    if ((strategyId == QStringLiteral("sma_crossover")
         || strategyId == QStringLiteral("macd_trend"))
        && result.value(QStringLiteral("fastPeriod")).toInt()
            >= result.value(QStringLiteral("slowPeriod")).toInt()) {
        if (error) *error = QStringLiteral("快速周期必须小于慢速周期");
        return false;
    }
    if (strategyId == QStringLiteral("rsi_mean_reversion")
        && result.value(QStringLiteral("oversold")).toDouble()
            >= result.value(QStringLiteral("overbought")).toDouble()) {
        if (error) *error = QStringLiteral("超卖阈值必须小于超买阈值");
        return false;
    }
    if (strategyId == QStringLiteral("tick_mean_reversion")
        && result.value(QStringLiteral("exitDeviationPercent")).toDouble()
            <= -result.value(QStringLiteral("entryDeviationPercent")).toDouble()) {
        if (error) *error = QStringLiteral("退出偏离必须大于负的入场偏离");
        return false;
    }
    if (strategyId == QStringLiteral("tick_directional_imbalance")
        && result.value(QStringLiteral("exitImbalancePercent")).toDouble()
            >= result.value(QStringLiteral("entryImbalancePercent")).toDouble()) {
        if (error) *error = QStringLiteral("退出失衡阈值必须小于入场失衡阈值");
        return false;
    }

    if (normalized) *normalized = result;
    if (error) error->clear();
    return true;
}

std::unique_ptr<BacktestStrategy> BacktestStrategyRegistry::create(
    const QString &strategyId, const QVariantMap &parameters, QString *error) const
{
    QVariantMap values;
    if (!normalizeParameters(strategyId, parameters, &values, error)) return {};

    if (strategyId == QStringLiteral("sma_crossover")) {
        return std::make_unique<SmaCrossoverStrategy>(
            values.value(QStringLiteral("fastPeriod")).toInt(),
            values.value(QStringLiteral("slowPeriod")).toInt());
    }
    if (strategyId == QStringLiteral("rsi_mean_reversion")) {
        return std::make_unique<RsiMeanReversionStrategy>(
            values.value(QStringLiteral("period")).toInt(),
            values.value(QStringLiteral("oversold")).toDouble(),
            values.value(QStringLiteral("overbought")).toDouble());
    }
    if (strategyId == QStringLiteral("macd_trend")) {
        return std::make_unique<MacdTrendStrategy>(
            values.value(QStringLiteral("fastPeriod")).toInt(),
            values.value(QStringLiteral("slowPeriod")).toInt(),
            values.value(QStringLiteral("signalPeriod")).toInt());
    }
    if (strategyId == QStringLiteral("bollinger_mean_reversion")) {
        return std::make_unique<BollingerMeanReversionStrategy>(
            values.value(QStringLiteral("period")).toInt(),
            values.value(QStringLiteral("standardDeviations")).toDouble());
    }
    if (strategyId == QStringLiteral("tick_momentum")) {
        return std::make_unique<TickMomentumBacktestStrategy>(
            values.value(QStringLiteral("lookbackTicks")).toInt(),
            values.value(QStringLiteral("thresholdPercent")).toDouble(),
            values.value(QStringLiteral("cooldownSeconds")).toInt(),
            values.value(QStringLiteral("minimumHoldSeconds")).toInt());
    }
    if (strategyId == QStringLiteral("tick_mean_reversion")) {
        return std::make_unique<TickMeanReversionBacktestStrategy>(
            values.value(QStringLiteral("lookbackTicks")).toInt(),
            values.value(QStringLiteral("entryDeviationPercent")).toDouble(),
            values.value(QStringLiteral("exitDeviationPercent")).toDouble(),
            values.value(QStringLiteral("cooldownSeconds")).toInt(),
            values.value(QStringLiteral("minimumHoldSeconds")).toInt());
    }
    if (strategyId == QStringLiteral("tick_volume_breakout")) {
        return std::make_unique<TickVolumeBreakoutBacktestStrategy>(
            values.value(QStringLiteral("lookbackTicks")).toInt(),
            values.value(QStringLiteral("breakoutPercent")).toDouble(),
            values.value(QStringLiteral("breakdownPercent")).toDouble(),
            values.value(QStringLiteral("volumeMultiplier")).toDouble(),
            values.value(QStringLiteral("cooldownSeconds")).toInt(),
            values.value(QStringLiteral("minimumHoldSeconds")).toInt());
    }
    if (strategyId == QStringLiteral("tick_directional_imbalance")) {
        return std::make_unique<TickDirectionalImbalanceBacktestStrategy>(
            values.value(QStringLiteral("lookbackTicks")).toInt(),
            values.value(QStringLiteral("entryImbalancePercent")).toDouble(),
            values.value(QStringLiteral("exitImbalancePercent")).toDouble(),
            values.value(QStringLiteral("minimumDirectionalCoveragePercent")).toDouble(),
            values.value(QStringLiteral("cooldownSeconds")).toInt(),
            values.value(QStringLiteral("minimumHoldSeconds")).toInt());
    }
    if (strategyId == QStringLiteral("tick_second_board")
        || strategyId == QStringLiteral("tick_third_board")) {
        return std::make_unique<TickBoardContinuationBacktestStrategy>(
            strategyId == QStringLiteral("tick_second_board") ? 2 : 3,
            values.value(QStringLiteral("minimumEarlyReturnPercent")).toDouble(),
            values.value(QStringLiteral("maximumEarlyDrawdownPercent")).toDouble(),
            values.value(QStringLiteral("maximumPriorSealMinutes")).toInt(),
            values.value(QStringLiteral("entryDeadlineMinutesAfterOpen")).toInt(),
            values.value(QStringLiteral("participationPercent")).toDouble(),
            values.value(QStringLiteral("maximumPositionAmount")).toDouble(),
            values.value(QStringLiteral("excludeTouchedBeforeSignal")).toInt() != 0);
    }

    if (error) *error = QStringLiteral("未知回测策略：%1").arg(strategyId);
    return {};
}

} // namespace pp
