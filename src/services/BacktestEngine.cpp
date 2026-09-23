#include "services/BacktestEngine.h"

#include "services/BacktestDataVersion.h"
#include "services/BacktestExecutionPolicy.h"

#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

BacktestEngine::BacktestEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BacktestTrade>();
    qRegisterMetaType<EquityPoint>();
    qRegisterMetaType<PerformanceSummary>();

    m_timer.setInterval(0);
    connect(&m_timer, &QTimer::timeout, this, &BacktestEngine::processNextObservation);
}

BacktestEngine::~BacktestEngine() = default;

bool BacktestEngine::start(const BacktestConfig &config, QVector<Candle> candles,
                           std::unique_ptr<BacktestStrategy> providedStrategy)
{
    if (isRunning()) {
        cancel();
    }

    candles.erase(std::remove_if(candles.begin(), candles.end(), [&config](const Candle &candle) {
        return candle.instrument.symbol != config.instrument.symbol
            || candle.close <= 0.0
            || !candle.startTime.isValid();
    }), candles.end());
    std::sort(candles.begin(), candles.end(), [](const Candle &left, const Candle &right) {
        return left.startTime < right.startTime;
    });
    if (config.sampleFrom.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) {
                return candle.startTime < config.sampleFrom;
            }), candles.end());
    }
    if (config.sampleTo.isValid()) {
        candles.erase(std::remove_if(candles.begin(), candles.end(),
            [&config](const Candle &candle) {
                return candle.startTime > config.sampleTo;
            }), candles.end());
    }

    const bool hasProvidedStrategy = bool(providedStrategy);
    QString strategyError;
    std::unique_ptr<BacktestStrategy> strategy = std::move(providedStrategy);
    QVariantMap normalizedParameters;
    if (strategy) {
        if (!config.strategyId.startsWith(QStringLiteral("custom_"))
            || strategy->id() != config.strategyId
            || strategy->strategyVersion() != config.strategyVersion
            || strategy->normalizedParameters() != config.strategyParameters
            || strategy->ruleSnapshot() != config.ruleSnapshot
            || config.strategyVersion < 1 || config.ruleSnapshot.isEmpty()) {
            emit errorOccurred(QStringLiteral("自定义回测策略身份或规则快照无效"));
            return false;
        }
        normalizedParameters = config.strategyParameters;
    } else {
        strategy = m_registry.create(config.strategyId, config.strategyParameters, &strategyError);
        if (!strategy) {
            emit errorOccurred(strategyError);
            return false;
        }
        if (!m_registry.normalizeParameters(config.strategyId, config.strategyParameters,
                                            &normalizedParameters, &strategyError)) {
            emit errorOccurred(strategyError);
            return false;
        }
    }
    if (config.instrument.market != Market::AShare) {
        emit errorOccurred(QStringLiteral("当前内置回测策略仅支持 A 股"));
        return false;
    }
    if (!m_registry.supportsDataMode(config.strategyId, QStringLiteral("candles"))
        && !hasProvidedStrategy) {
        emit errorOccurred(QStringLiteral("所选策略不支持周期 K 线回测"));
        return false;
    }

    if (config.instrument.symbol.isEmpty() || config.initialCapital <= 0.0
        || candles.size() < strategy->minimumWarmupBars()) {
        emit errorOccurred(QStringLiteral("回测配置无效或历史 K 线不足"));
        return false;
    }

    m_result = {};
    m_streamedRawTicks = false;
    m_rawTickChunk.clear();
    m_rawTickChunkLoader = {};
    m_rawTickDataVersionBuilder.reset();
    m_rawTickChunkIndex = 0;
    m_rawTickProcessed = 0;
    m_rawTickExpected = 0;
    m_hasCurrentRawTick = false;
    m_hasLastRawTick = false;
    m_result.config = config;
    m_result.config.dataMode = BacktestDataMode::Candles;
    m_result.config.strategyParameters = normalizedParameters;
    m_result.config.dataVersion = computeBacktestDataVersion(
        candles, config.instrument.symbol, config.periodSeconds);
    m_result.config.actualBars = candles.size();
    m_result.config.from = candles.first().startTime;
    m_result.config.to = candles.last().startTime;
    m_candles = std::move(candles);
    m_position = {};
    m_currentSignal = {};
    m_hasCurrentSignal = false;
    m_index = 0;
    m_cash = config.initialCapital;
    m_peakEquity = config.initialCapital;
    m_totalCommission = 0.0;
    resetEquityTracking(m_candles.size(), false);
    if (m_strategy) disconnect(m_strategy.get(), nullptr, this, nullptr);
    m_strategy = std::move(strategy);
    m_strategy->reset();
    connect(m_strategy.get(), &Strategy::signalGenerated,
            this, &BacktestEngine::handleStrategySignal, Qt::DirectConnection);

    emit progressChanged(0.0);
    m_timer.start();
    return true;
}

bool BacktestEngine::start(const BacktestConfig &config, QVector<RawTradeTick> ticks,
                           std::unique_ptr<BacktestStrategy> providedStrategy)
{
    if (isRunning()) cancel();
    ticks.erase(std::remove_if(ticks.begin(), ticks.end(), [&config](const RawTradeTick &tick) {
        return tick.instrument.symbol != config.instrument.symbol
            || tick.price <= 0.0 || tick.volumeShares < 0.0
            || !tick.timestamp.isValid();
    }), ticks.end());
    std::sort(ticks.begin(), ticks.end(), [](const RawTradeTick &left,
                                             const RawTradeTick &right) {
        if (left.timestamp != right.timestamp) return left.timestamp < right.timestamp;
        return left.sourceLine < right.sourceLine;
    });
    if (config.sampleFrom.isValid()) {
        ticks.erase(std::remove_if(ticks.begin(), ticks.end(), [&config](const RawTradeTick &tick) {
            return tick.timestamp < config.sampleFrom;
        }), ticks.end());
    }
    if (config.sampleTo.isValid()) {
        ticks.erase(std::remove_if(ticks.begin(), ticks.end(), [&config](const RawTradeTick &tick) {
            return tick.timestamp > config.sampleTo;
        }), ticks.end());
    }

    if (providedStrategy) {
        emit errorOccurred(QStringLiteral("自定义策略暂不支持原始 Tick 回测"));
        return false;
    }
    if (!m_registry.supportsDataMode(config.strategyId, QStringLiteral("raw_ticks"))) {
        emit errorOccurred(QStringLiteral("所选策略不支持原始 Tick 回测"));
        return false;
    }
    QString strategyError;
    QVariantMap normalizedParameters;
    auto strategy = m_registry.create(config.strategyId, config.strategyParameters,
                                      &strategyError);
    if (!strategy
        || !m_registry.normalizeParameters(config.strategyId, config.strategyParameters,
                                           &normalizedParameters, &strategyError)) {
        emit errorOccurred(strategyError);
        return false;
    }
    if (config.instrument.market != Market::AShare) {
        emit errorOccurred(QStringLiteral("当前内置回测策略仅支持 A 股"));
        return false;
    }
    if (config.instrument.symbol.isEmpty() || config.initialCapital <= 0.0
        || ticks.size() < strategy->minimumWarmupBars()) {
        emit errorOccurred(QStringLiteral("回测配置无效或原始 Tick 不足"));
        return false;
    }

    m_result = {};
    m_streamedRawTicks = false;
    m_rawTickChunk.clear();
    m_rawTickChunkLoader = {};
    m_rawTickDataVersionBuilder.reset();
    m_rawTickChunkIndex = 0;
    m_rawTickProcessed = 0;
    m_rawTickExpected = 0;
    m_hasCurrentRawTick = false;
    m_hasLastRawTick = false;
    m_result.config = config;
    m_result.config.dataMode = BacktestDataMode::RawTicks;
    m_result.config.frequency = QStringLiteral("tick");
    m_result.config.periodSeconds = 0;
    m_result.config.strategyParameters = normalizedParameters;
    m_result.config.dataVersion = computeBacktestDataVersion(ticks, config.instrument.symbol);
    m_result.config.actualBars = ticks.size();
    m_result.config.from = ticks.first().timestamp;
    m_result.config.to = ticks.last().timestamp;
    m_result.rawTicks = std::move(ticks);
    m_candles.clear();
    m_position = {};
    m_currentSignal = {};
    m_hasCurrentSignal = false;
    m_index = 0;
    m_cash = config.initialCapital;
    m_peakEquity = config.initialCapital;
    m_totalCommission = 0.0;
    resetEquityTracking(m_result.rawTicks.size(), true);
    if (m_strategy) disconnect(m_strategy.get(), nullptr, this, nullptr);
    m_strategy = std::move(strategy);
    m_strategy->reset();
    connect(m_strategy.get(), &Strategy::signalGenerated,
            this, &BacktestEngine::handleStrategySignal, Qt::DirectConnection);
    emit progressChanged(0.0);
    m_timer.start();
    return true;
}

bool BacktestEngine::startStreamedRawTicks(const BacktestConfig &config,
                                           int expectedObservations,
                                           RawTickChunkLoader loader)
{
    if (isRunning()) cancel();
    if (!loader) {
        emit errorOccurred(QStringLiteral("原始 Tick 分段读取器不可用"));
        return false;
    }
    if (!m_registry.supportsDataMode(config.strategyId, QStringLiteral("raw_ticks"))) {
        emit errorOccurred(QStringLiteral("所选策略不支持原始 Tick 回测"));
        return false;
    }
    QString strategyError;
    QVariantMap normalizedParameters;
    auto strategy = m_registry.create(config.strategyId, config.strategyParameters,
                                      &strategyError);
    if (!strategy
        || !m_registry.normalizeParameters(config.strategyId, config.strategyParameters,
                                           &normalizedParameters, &strategyError)) {
        emit errorOccurred(strategyError);
        return false;
    }
    const QString datasetVersion = config.datasetIdentity.datasetVersion;
    if (config.instrument.market != Market::AShare) {
        emit errorOccurred(QStringLiteral("当前内置回测策略仅支持 A 股"));
        return false;
    }
    qint64 identityObservationCount = 0;
    for (const RawTickDayFingerprint &day : config.datasetIdentity.days) {
        identityObservationCount += day.rawCount;
    }
    if (config.instrument.symbol.isEmpty() || config.initialCapital <= 0.0
        || expectedObservations < strategy->minimumWarmupBars()
        || expectedObservations != identityObservationCount
        || !datasetVersion.startsWith(QStringLiteral("sha256:"))
        || datasetVersion.size() != 71) {
        emit errorOccurred(QStringLiteral("回测配置、原始 Tick 数量或数据集身份无效"));
        return false;
    }

    m_result = {};
    m_result.config = config;
    m_result.config.dataMode = BacktestDataMode::RawTicks;
    m_result.config.frequency = QStringLiteral("tick");
    m_result.config.periodSeconds = 0;
    m_result.config.strategyParameters = normalizedParameters;
    m_result.config.dataVersion.clear();
    m_result.config.actualBars = expectedObservations;
    m_result.rawTickSnapshotDeferred = true;
    m_candles.clear();
    m_rawTickChunk.clear();
    m_rawTickChunkLoader = std::move(loader);
    m_rawTickDataVersionBuilder =
        std::make_unique<RawTickBacktestDataVersionBuilder>(
            config.instrument.symbol);
    m_rawTickChunkIndex = 0;
    m_rawTickProcessed = 0;
    m_rawTickExpected = expectedObservations;
    m_streamedRawTicks = true;
    m_hasCurrentRawTick = false;
    m_hasLastRawTick = false;
    m_position = {};
    m_currentSignal = {};
    m_hasCurrentSignal = false;
    m_index = 0;
    m_cash = config.initialCapital;
    m_peakEquity = config.initialCapital;
    m_totalCommission = 0.0;
    resetEquityTracking(expectedObservations, true);
    if (m_strategy) disconnect(m_strategy.get(), nullptr, this, nullptr);
    m_strategy = std::move(strategy);
    m_strategy->reset();
    connect(m_strategy.get(), &Strategy::signalGenerated,
            this, &BacktestEngine::handleStrategySignal, Qt::DirectConnection);
    emit progressChanged(0.0);
    m_timer.start();
    return true;
}

void BacktestEngine::cancel()
{
    if (!m_timer.isActive()) {
        return;
    }
    m_timer.stop();
    if (m_streamedRawTicks) {
        m_rawTickChunk.clear();
        m_rawTickChunkLoader = {};
        m_rawTickDataVersionBuilder.reset();
        emit rawTickChunkResidencyChanged(0);
    }
    emit cancelled();
}

bool BacktestEngine::isRunning() const
{
    return m_timer.isActive();
}

double BacktestEngine::progress() const
{
    if (m_streamedRawTicks) {
        return m_rawTickExpected <= 0 ? 0.0
            : static_cast<double>(m_rawTickProcessed) / m_rawTickExpected;
    }
    const int total = m_result.config.dataMode == BacktestDataMode::RawTicks
        ? m_result.rawTicks.size() : m_candles.size();
    return total <= 0 ? 0.0 : static_cast<double>(m_index) / total;
}

const BacktestResult &BacktestEngine::result() const
{
    return m_result;
}

void BacktestEngine::processNextObservation()
{
    const bool rawMode = m_result.config.dataMode == BacktestDataMode::RawTicks;
    if (rawMode && m_streamedRawTicks) {
        constexpr int kRawTickObservationsPerTurn = 256;
        int processedThisTurn = 0;
        while (processedThisTurn < kRawTickObservationsPerTurn) {
            if (m_rawTickChunkIndex >= m_rawTickChunk.size()) {
                m_rawTickChunk.clear();
                m_rawTickChunkIndex = 0;
                emit rawTickChunkResidencyChanged(0);
                if (m_rawTickProcessed >= m_rawTickExpected) {
                    finishBacktest();
                    return;
                }
                if (!loadNextRawTickChunk()) return;
            }

            m_currentRawTick = m_rawTickChunk.at(m_rawTickChunkIndex++);
            m_hasCurrentRawTick = true;
            if (!m_rawTickDataVersionBuilder
                || !m_rawTickDataVersionBuilder->append(m_currentRawTick)) {
                failRawTickStream(QStringLiteral(
                    "原始 Tick 分段无法形成稳定执行数据版本"));
                return;
            }
            Tick tick;
            tick.instrument = m_currentRawTick.instrument;
            tick.timestamp = m_currentRawTick.timestamp;
            tick.price = m_currentRawTick.price;
            tick.volume = m_currentRawTick.volumeShares;
            tick.amount = m_currentRawTick.amount;
            tick.rawTradeDirection = m_currentRawTick.direction;
            tick.rawTradeDirectionKnown =
                isKnownRawTradeDirection(m_currentRawTick.direction);
            m_hasCurrentSignal = false;
            m_strategy->onTick(tick);
            appendEquityPoint(m_currentRawTick.timestamp, m_currentRawTick.price);
            m_lastRawTick = m_currentRawTick;
            m_hasLastRawTick = true;
            ++m_rawTickProcessed;
            ++m_index;
            ++processedThisTurn;
        }
        emit progressChanged(progress());
        if (m_rawTickProcessed >= m_rawTickExpected) finishBacktest();
        return;
    }
    const int total = rawMode ? m_result.rawTicks.size() : m_candles.size();
    if (m_index >= total) {
        finishBacktest();
        return;
    }

    if (rawMode) {
        constexpr int kRawTickObservationsPerTurn = 256;
        const int chunkEnd = std::min(
            total, m_index + kRawTickObservationsPerTurn);
        while (m_index < chunkEnd) {
            const RawTradeTick &raw = m_result.rawTicks.at(m_index);
            Tick tick;
            tick.instrument = raw.instrument;
            tick.timestamp = raw.timestamp;
            tick.price = raw.price;
            tick.volume = raw.volumeShares;
            tick.amount = raw.amount;
            tick.rawTradeDirection = raw.direction;
            tick.rawTradeDirectionKnown = isKnownRawTradeDirection(raw.direction);
            m_hasCurrentSignal = false;
            m_strategy->onTick(tick);
            appendEquityPoint(raw.timestamp, raw.price);
            ++m_index;
        }
        emit progressChanged(progress());
        if (m_index >= total) finishBacktest();
        return;
    }

    const Candle &candle = m_candles.at(m_index);
    m_hasCurrentSignal = false;
    m_strategy->onCandle(candle);
    appendEquityPoint(candle.startTime, candle.close);

    CandlePoint point;
    point.candle = candle;
    point.hasSignal = m_hasCurrentSignal;
    if (m_hasCurrentSignal) {
        point.signal = m_currentSignal;
        m_result.chartSignals.append(m_currentSignal);
    }
    emit pointReady(point);

    ++m_index;
    emit progressChanged(progress());
    if (m_index >= m_candles.size()) {
        finishBacktest();
    }
}

void BacktestEngine::handleStrategySignal(const TradeSignal &signal)
{
    const bool rawMode = m_result.config.dataMode == BacktestDataMode::RawTicks;
    if (rawMode && m_streamedRawTicks) {
        if (!m_hasCurrentRawTick) return;
        m_currentSignal = signal;
        m_hasCurrentSignal = true;
        m_result.chartSignals.append(signal);
        if (signal.side == SignalSide::Buy && m_position.quantity <= 0.0) {
            openPosition(signal, m_currentRawTick.price, m_currentRawTick.timestamp);
        } else if (signal.side == SignalSide::Sell && m_position.quantity > 0.0) {
            closePosition(signal, m_currentRawTick.price, m_currentRawTick.timestamp);
        }
        return;
    }
    const int total = rawMode ? m_result.rawTicks.size() : m_candles.size();
    if (m_index < 0 || m_index >= total) {
        return;
    }

    const std::optional<double> referencePrice = rawMode
        ? std::optional<double>(m_result.rawTicks.at(m_index).price)
        : candleExecutionPrice(m_candles.at(m_index), signal,
                               m_strategy->candleExecutionContext());
    if (!referencePrice) return;
    const double marketPrice = *referencePrice;
    const QDateTime timestamp = rawMode
        ? m_result.rawTicks.at(m_index).timestamp : m_candles.at(m_index).startTime;
    m_currentSignal = signal;
    m_hasCurrentSignal = true;
    if (rawMode) m_result.chartSignals.append(signal);
    if (signal.side == SignalSide::Buy && m_position.quantity <= 0.0) {
        openPosition(signal, marketPrice, timestamp);
    } else if (signal.side == SignalSide::Sell && m_position.quantity > 0.0) {
        closePosition(signal, marketPrice, timestamp);
    }
}

void BacktestEngine::openPosition(const TradeSignal &signal, double marketPrice,
                                  const QDateTime &timestamp)
{
    const BacktestConfig &config = m_result.config;
    const double fillPrice = marketPrice * (1.0 + std::max(0.0, config.slippageRate));
    const int lotSize = std::max(1, config.lotSize);
    const double fixedAmount = m_strategy->positionAmount();
    double budget = 0.0;
    if (fixedAmount > 0.0) {
        budget = std::min(fixedAmount, m_cash);
    } else {
        const double fraction = std::clamp(m_strategy->positionFraction(), 0.01, 1.0);
        budget = m_cash * fraction;
    }
    const double maxPercent = m_strategy->maxPositionPercent();
    if (maxPercent > 0.0) {
        budget = std::min(budget, m_cash * maxPercent / 100.0);
    }
    const double affordable = budget / (fillPrice * (1.0 + std::max(0.0, config.commissionRate)));
    double quantity = std::floor(affordable / lotSize) * lotSize;
    if (m_strategy->targetPositionShares() > 0) {
        quantity = std::min(quantity,
                            double(m_strategy->targetPositionShares()));
    }
    if (quantity <= 0.0) {
        return;
    }

    const double commission = fillPrice * quantity * std::max(0.0, config.commissionRate);
    m_cash -= fillPrice * quantity + commission;
    m_totalCommission += commission;
    m_position.quantity = quantity;
    m_position.averagePrice = fillPrice;
    m_position.entryCommission = commission;
    m_position.entryTime = timestamp;
    m_position.entryReason = signal.reason;
}

void BacktestEngine::closePosition(const TradeSignal &signal, double marketPrice,
                                   const QDateTime &timestamp)
{
    const BacktestConfig &config = m_result.config;
    if (!backtestPositionSellable(config.instrument.market, m_position.entryTime, timestamp))
        return;
    const double fillPrice = marketPrice * (1.0 - std::max(0.0, config.slippageRate));
    const double quantity = m_position.quantity;
    const double exitCommission = fillPrice * quantity * std::max(0.0, config.commissionRate);
    const double grossPnl = (fillPrice - m_position.averagePrice) * quantity;
    const double commission = m_position.entryCommission + exitCommission;

    m_cash += fillPrice * quantity - exitCommission;
    m_totalCommission += exitCommission;

    BacktestTrade trade;
    trade.instrument = config.instrument;
    trade.entryTime = m_position.entryTime;
    trade.exitTime = timestamp;
    trade.entryPrice = m_position.averagePrice;
    trade.exitPrice = fillPrice;
    trade.quantity = quantity;
    trade.grossPnl = grossPnl;
    trade.netPnl = grossPnl - commission;
    trade.commission = commission;
    trade.returnPercent = m_position.averagePrice > 0.0
        ? (fillPrice / m_position.averagePrice - 1.0) * 100.0 : 0.0;
    trade.entryReason = m_position.entryReason;
    trade.exitReason = signal.reason;
    m_result.trades.append(trade);
    emit tradeCompleted(trade);

    m_position = {};
}

void BacktestEngine::appendEquityPoint(const QDateTime &timestamp, double marketPrice)
{
    EquityPoint point;
    point.timestamp = timestamp;
    point.cash = m_cash;
    point.positionValue = m_position.quantity * marketPrice;
    point.equity = point.cash + point.positionValue;
    m_peakEquity = std::max(m_peakEquity, point.equity);
    point.drawdownPercent = m_peakEquity > 0.0
        ? (m_peakEquity - point.equity) / m_peakEquity * 100.0 : 0.0;

    const qint64 observationIndex = m_equityObservationCount++;
    m_result.equityObservationCount = m_equityObservationCount;
    m_lastEquityReturnValid = false;
    if (m_hasPreviousEquity && m_previousEquity > 0.0) {
        m_equityBeforeLast = m_previousEquity;
        m_lastEquityReturn = point.equity / m_previousEquity - 1.0;
        addEquityReturn(m_lastEquityReturn);
        m_lastEquityReturnValid = true;
    }
    m_previousEquity = point.equity;
    m_hasPreviousEquity = true;
    m_lastExactEquityPoint = point;
    if (m_maxDrawdownObservationIndex < 0
        || point.drawdownPercent > m_exactMaxDrawdownPercent) {
        m_exactMaxDrawdownPercent = point.drawdownPercent;
        m_maxDrawdownEquityPoint = point;
        m_maxDrawdownObservationIndex = observationIndex;
    }

    const bool keepForDisplay = !m_equityCurveSampled
        || observationIndex == 0
        || (observationIndex % m_equitySampleStride == 0
            && m_result.equityCurve.size()
                < RawTickDisplayEquityPointLimit - 2);
    if (keepForDisplay) {
        m_result.equityCurve.append(point);
        m_equityCurveObservationIndices.append(observationIndex);
        emit equityPointReady(point);
    }
}

void BacktestEngine::resetEquityTracking(int expectedObservations, bool rawMode)
{
    constexpr int kReservedKeyPoints = 2;
    m_equityCurveObservationIndices.clear();
    m_lastExactEquityPoint = {};
    m_maxDrawdownEquityPoint = {};
    m_equityObservationCount = 0;
    m_maxDrawdownObservationIndex = -1;
    m_equityCurveSampled = rawMode
        && expectedObservations > RawTickDisplayEquityPointLimit;
    m_result.equityObservationCount = 0;
    m_result.equityCurveSampled = m_equityCurveSampled;
    m_equitySampleStride = m_equityCurveSampled
        ? std::max(1, (expectedObservations + RawTickDisplayEquityPointLimit
                       - kReservedKeyPoints - 1)
                          / (RawTickDisplayEquityPointLimit - kReservedKeyPoints))
        : 1;
    m_hasPreviousEquity = false;
    m_lastEquityReturnValid = false;
    m_previousEquity = 0.0;
    m_equityBeforeLast = 0.0;
    m_lastEquityReturn = 0.0;
    m_equityReturnCount = 0;
    m_equityReturnMean = 0.0;
    m_equityReturnM2 = 0.0;
    m_exactMaxDrawdownPercent = 0.0;
    m_result.equityCurve.reserve(m_equityCurveSampled
        ? RawTickDisplayEquityPointLimit : std::max(0, expectedObservations));
}

void BacktestEngine::addEquityReturn(double value)
{
    ++m_equityReturnCount;
    const double delta = value - m_equityReturnMean;
    m_equityReturnMean += delta / static_cast<double>(m_equityReturnCount);
    const double deltaAfterMean = value - m_equityReturnMean;
    m_equityReturnM2 += delta * deltaAfterMean;
}

void BacktestEngine::removeEquityReturn(double value)
{
    if (m_equityReturnCount <= 1) {
        m_equityReturnCount = 0;
        m_equityReturnMean = 0.0;
        m_equityReturnM2 = 0.0;
        return;
    }
    const double count = static_cast<double>(m_equityReturnCount);
    const double reducedMean = (count * m_equityReturnMean - value) / (count - 1.0);
    m_equityReturnM2 -= (value - m_equityReturnMean) * (value - reducedMean);
    --m_equityReturnCount;
    m_equityReturnMean = reducedMean;
    if (m_equityReturnM2 < 0.0 && m_equityReturnM2 > -1e-18) {
        m_equityReturnM2 = 0.0;
    }
}

void BacktestEngine::reviseLastEquityPointAfterLiquidation()
{
    if (m_equityObservationCount <= 0) return;

    EquityPoint revised = m_lastExactEquityPoint;
    revised.cash = m_cash;
    revised.positionValue = 0.0;
    revised.equity = m_cash;
    revised.drawdownPercent = m_peakEquity > 0.0
        ? (m_peakEquity - revised.equity) / m_peakEquity * 100.0 : 0.0;
    if (m_lastEquityReturnValid) {
        removeEquityReturn(m_lastEquityReturn);
        m_lastEquityReturn = revised.equity / m_equityBeforeLast - 1.0;
        addEquityReturn(m_lastEquityReturn);
    }
    m_previousEquity = revised.equity;
    m_lastExactEquityPoint = revised;
    if (revised.drawdownPercent >= m_exactMaxDrawdownPercent) {
        m_exactMaxDrawdownPercent = revised.drawdownPercent;
        m_maxDrawdownEquityPoint = revised;
        m_maxDrawdownObservationIndex = m_equityObservationCount - 1;
    }

    const qint64 lastIndex = m_equityObservationCount - 1;
    const qsizetype storedIndex = m_equityCurveObservationIndices.indexOf(lastIndex);
    if (storedIndex >= 0) m_result.equityCurve[storedIndex] = revised;
}

void BacktestEngine::finalizeDisplayEquityCurve()
{
    if (!m_equityCurveSampled || m_equityObservationCount <= 0) return;

    auto insertOrReplace = [this](qint64 observationIndex,
                                  const EquityPoint &point) {
        const auto iterator = std::lower_bound(m_equityCurveObservationIndices.begin(),
                                               m_equityCurveObservationIndices.end(),
                                               observationIndex);
        const qsizetype position = iterator - m_equityCurveObservationIndices.begin();
        if (iterator != m_equityCurveObservationIndices.end()
            && *iterator == observationIndex) {
            m_result.equityCurve[position] = point;
            return;
        }
        m_equityCurveObservationIndices.insert(position, observationIndex);
        m_result.equityCurve.insert(position, point);
    };
    insertOrReplace(m_maxDrawdownObservationIndex, m_maxDrawdownEquityPoint);
    insertOrReplace(m_equityObservationCount - 1, m_lastExactEquityPoint);
}

void BacktestEngine::finishBacktest()
{
    const bool rawMode = m_result.config.dataMode == BacktestDataMode::RawTicks;
    const int total = rawMode
        ? (m_streamedRawTicks ? m_rawTickProcessed : m_result.rawTicks.size())
        : m_candles.size();
    if (!m_timer.isActive() && m_index < total) {
        return;
    }
    if (m_streamedRawTicks) {
        const QString dataVersion = m_rawTickDataVersionBuilder
            ? m_rawTickDataVersionBuilder->finish() : QString{};
        if (dataVersion.isEmpty()
            || !m_rawTickDataVersionBuilder
            || m_rawTickDataVersionBuilder->count() != m_rawTickExpected) {
            failRawTickStream(QStringLiteral(
                "原始 Tick 分段执行数据版本不完整"));
            return;
        }
        m_result.config.dataVersion = dataVersion;
    }
    m_timer.stop();

    if (m_position.quantity > 0.0 && total > 0) {
        const QDateTime lastTime = rawMode
            ? (m_streamedRawTicks ? m_lastRawTick.timestamp
                                  : m_result.rawTicks.last().timestamp)
            : m_candles.last().startTime;
        const double lastPrice = rawMode
            ? (m_streamedRawTicks ? m_lastRawTick.price
                                  : m_result.rawTicks.last().price)
            : m_candles.last().close;
        // 原始 Tick 与周期 Bar 使用相同的成交日门禁。
        const bool tPlusOneLocked = !backtestPositionSellable(
            m_result.config.instrument.market, m_position.entryTime, lastTime);
        if (tPlusOneLocked) {
            m_result.finalPosition = m_position;
        } else {
            TradeSignal signal;
            signal.instrument = m_result.config.instrument;
            signal.timestamp = lastTime;
            signal.side = SignalSide::Sell;
            signal.price = lastPrice;
            signal.reason = QStringLiteral("回测结束平仓");
            closePosition(signal, lastPrice, lastTime);

            reviseLastEquityPointAfterLiquidation();
        }
    }

    finalizeDisplayEquityCurve();
    if (m_strategy) m_result.strategyEvaluationFacts = m_strategy->evaluationFacts();
    m_result.summary = calculateSummary();
    if (!rawMode) m_result.candles = m_candles;
    if (m_streamedRawTicks) {
        m_rawTickChunk.clear();
        m_rawTickChunkLoader = {};
        m_rawTickDataVersionBuilder.reset();
        m_hasCurrentRawTick = false;
        emit rawTickChunkResidencyChanged(0);
    }
    emit progressChanged(1.0);
    emit finished(m_result.summary);
}

bool BacktestEngine::loadNextRawTickChunk()
{
    QVector<RawTradeTick> next;
    QString error;
    if (!m_rawTickChunkLoader || !m_rawTickChunkLoader(&next, &error)) {
        failRawTickStream(error.isEmpty()
            ? QStringLiteral("读取原始 Tick 分段失败") : error);
        return false;
    }
    if (next.isEmpty()) {
        failRawTickStream(QStringLiteral("原始 Tick 分段提前结束：已读取 %1，预期 %2")
                              .arg(m_rawTickProcessed).arg(m_rawTickExpected));
        return false;
    }
    if (m_rawTickProcessed + next.size() > m_rawTickExpected) {
        failRawTickStream(QStringLiteral("原始 Tick 分段数量超过不可变数据集身份"));
        return false;
    }

    QDateTime previousTime = m_hasLastRawTick ? m_lastRawTick.timestamp : QDateTime{};
    qint64 previousSourceLine = m_hasLastRawTick ? m_lastRawTick.sourceLine : -1;
    for (const RawTradeTick &tick : next) {
        const bool ordered = !previousTime.isValid() || tick.timestamp > previousTime
            || (tick.timestamp == previousTime && tick.sourceLine >= previousSourceLine);
        const bool inRange = (!m_result.config.sampleFrom.isValid()
                              || tick.timestamp >= m_result.config.sampleFrom)
            && (!m_result.config.sampleTo.isValid()
                || tick.timestamp <= m_result.config.sampleTo);
        if (tick.instrument.symbol != m_result.config.instrument.symbol
            || !tick.timestamp.isValid() || tick.price <= 0.0
            || tick.volumeShares < 0.0 || !ordered || !inRange) {
            failRawTickStream(QStringLiteral("原始 Tick 分段包含无效、越界或乱序事实"));
            return false;
        }
        previousTime = tick.timestamp;
        previousSourceLine = tick.sourceLine;
    }

    m_rawTickChunk = std::move(next);
    m_rawTickChunkIndex = 0;
    if (!m_result.config.from.isValid()) {
        m_result.config.from = m_rawTickChunk.first().timestamp;
    }
    m_result.config.to = m_rawTickChunk.last().timestamp;
    emit rawTickChunkResidencyChanged(m_rawTickChunk.size());
    return true;
}

void BacktestEngine::failRawTickStream(const QString &message)
{
    m_timer.stop();
    m_rawTickChunk.clear();
    m_rawTickChunkLoader = {};
    m_rawTickDataVersionBuilder.reset();
    m_hasCurrentRawTick = false;
    emit rawTickChunkResidencyChanged(0);
    emit errorOccurred(message);
}

PerformanceSummary BacktestEngine::calculateSummary() const
{
    PerformanceSummary summary;
    summary.initialCapital = m_result.config.initialCapital;
    summary.finalEquity = m_equityObservationCount <= 0
        ? m_cash : m_lastExactEquityPoint.equity;
    summary.totalReturnPercent = summary.initialCapital > 0.0
        ? (summary.finalEquity / summary.initialCapital - 1.0) * 100.0 : 0.0;
    summary.totalCommission = m_totalCommission;
    summary.totalTrades = m_result.trades.size();

    for (const BacktestTrade &trade : m_result.trades) {
        if (trade.netPnl > 0.0) {
            ++summary.winningTrades;
        } else if (trade.netPnl < 0.0) {
            ++summary.losingTrades;
        }
    }
    summary.winRatePercent = summary.totalTrades > 0
        ? static_cast<double>(summary.winningTrades) / summary.totalTrades * 100.0 : 0.0;

    if (m_equityReturnCount > 1) {
        const double deviation = std::sqrt(
            std::max(0.0, m_equityReturnM2
                / static_cast<double>(m_equityReturnCount - 1)));
        if (deviation > 0.0) {
            const double periodsPerYear = m_result.config.periodSeconds >= 86400
                ? 252.0 : 252.0 * 4.0 * 60.0 * 60.0 / std::max(1, m_result.config.periodSeconds);
            summary.sharpeRatio = m_equityReturnMean / deviation
                * std::sqrt(periodsPerYear);
        }
    }
    summary.maxDrawdownPercent = m_exactMaxDrawdownPercent;
    return summary;
}

} // namespace pp
