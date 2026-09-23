#include "services/CrossSectionalBacktestService.h"

#include "services/AlignedBarSeries.h"
#include "services/AShareTradingCalendar.h"
#include "services/CrossSectionalBacktestEngine.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CrossSectionalCustomStrategy.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QMetaObject>
#include <QPointer>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace pp {

CrossSectionalBacktestService::CrossSectionalBacktestService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<PortfolioResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderCrossSectionalBacktest"));
    m_thread.start();
}

CrossSectionalBacktestService::~CrossSectionalBacktestService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool CrossSectionalBacktestService::start(const QString &databasePath)
{
    if (m_ready) return true;
    if (databasePath.isEmpty() || !m_worker) return false;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_ready = initialized;
    m_running = false;
    return initialized;
}

void CrossSectionalBacktestService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_ready = false;
    m_running = false;
}

bool CrossSectionalBacktestService::request(
    quint64 requestId, const CustomStrategyDefinition &definition,
    const QVariantMap &runtimeParameters,
    const PortfolioDefinition &portfolioDefinition,
    int barCount, const QVector<Instrument> &knownInstruments)
{
    if (!m_ready || m_running || !m_worker || requestId == 0 || barCount < 2)
        return false;
    if (!validateIndustryConfiguration(
             portfolioDefinition.maximumIndustryWeight,
             portfolioDefinition.industryDatasetId)
             .value(QStringLiteral("valid")).toBool()) {
        return false;
    }
    if (!validateFactorExposureConfiguration(
             portfolioDefinition.maximumAbsoluteFactorExposure,
             portfolioDefinition.factorExposureAlias)
             .value(QStringLiteral("valid")).toBool()) {
        return false;
    }
    if (!validateFactorResearchConfiguration(
             portfolioDefinition.factorResearchAlias,
             portfolioDefinition.factorPreprocessing.lowerQuantile,
             portfolioDefinition.factorPreprocessing.upperQuantile,
             portfolioDefinition.factorPreprocessing.minimumPairs,
             portfolioDefinition.factorResearchCost.oneWayCostBps,
             portfolioDefinition.factorNeutralization.enabled(),
             portfolioDefinition.factorNeutralization.minimumGroupSize,
             portfolioDefinition.industryDatasetId)
             .value(QStringLiteral("valid")).toBool()) {
        return false;
    }
    m_cancelRequested.store(false, std::memory_order_release);
    m_running = true;
    QPointer<CrossSectionalBacktestService> guard(this);
    const bool invoked = QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, definition, runtimeParameters,
         portfolioDefinition, barCount, knownInstruments]() {
            if (!m_workerStore) {
                QMetaObject::invokeMethod(guard, [guard, requestId]() {
                    if (guard) {
                        guard->m_running = false;
                        emit guard->failed(requestId,
                                           QStringLiteral("多股回测数据库不可用"));
                    }
                }, Qt::QueuedConnection);
                return;
            }

            // 1. 编译 DSL v3。
            CustomStrategyCompiler compiler;
            QString error;
            const auto plan = compiler.compile(definition, runtimeParameters, &error);
            if (!plan) {
                QMetaObject::invokeMethod(guard, [guard, requestId, error]() {
                    if (guard) {
                        guard->m_running = false;
                        emit guard->failed(requestId, error);
                    }
                }, Qt::QueuedConnection);
                return;
            }

            // 2. 逐腿校验身份并读取 K 线。
            QHash<QString, QVector<Candle>> series;
            QStringList datasetVersions;
            QVector<BacktestDatasetIdentity> datasetIdentities;
            QDate firstDate;
            QDate lastDate;
            static const QTimeZone shanghai("Asia/Shanghai");
            QString failure;
            for (const QString &symbol : plan->universeSymbols) {
                if (guard && guard->cancelRequested()) {
                    failure = QStringLiteral("多股回测已取消");
                    break;
                }
                Instrument instrument;
                for (const Instrument &candidate : knownInstruments) {
                    if (candidate.symbol == symbol && candidate.market == Market::AShare) {
                        instrument = candidate;
                        break;
                    }
                }
                if (instrument.symbol.isEmpty()) {
                    instrument.symbol = symbol;
                    instrument.market = Market::AShare;
                }
                const auto identity = m_workerStore->loadBacktestDatasetIdentity(
                    instrument, portfolioDefinition.periodSeconds);
                if (!identity || identity->isEmpty()) {
                    failure = QStringLiteral("组合腿缺少数据集身份：%1").arg(symbol);
                    break;
                }
                if (identity->staleDayCount > 0) {
                    failure = QStringLiteral("组合腿派生缓存过期：%1").arg(symbol);
                    break;
                }
                QVector<Candle> candles = m_workerStore->loadRecentCandlesFromSource(
                    instrument, portfolioDefinition.periodSeconds, barCount,
                    QStringLiteral("raw_tick_cache_v1"));
                if (candles.size() < barCount) {
                    failure = QStringLiteral("组合腿 K 线不足：%1（%2/%3）")
                                  .arg(symbol).arg(candles.size()).arg(barCount);
                    break;
                }
                series.insert(symbol, candles);
                datasetVersions.append(identity->datasetVersion);
                datasetIdentities.append(*identity);
                for (const Candle &candle : candles) {
                    const QDate date = candle.startTime.toTimeZone(shanghai).date();
                    if (!firstDate.isValid() || date < firstDate) firstDate = date;
                    if (!lastDate.isValid() || date > lastDate) lastDate = date;
                }
            }
            if (!failure.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, failure]() {
                    if (guard) {
                        guard->m_running = false;
                        emit guard->failed(requestId, failure);
                    }
                }, Qt::QueuedConnection);
                return;
            }

            // 3. 对齐成帧。
            QString alignError;
            const AlignedBarSeries aligned = AlignedBarSeries::build(series, &alignError);
            if (aligned.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, alignError]() {
                    if (guard) {
                        guard->m_running = false;
                        emit guard->failed(requestId, alignError);
                    }
                }, Qt::QueuedConnection);
                return;
            }

            AShareIndustrySnapshot industry;
            const bool requiresIndustrySnapshot =
                portfolioDefinition.maximumIndustryWeight > 0.0
                || portfolioDefinition.factorNeutralization.enabled();
            if (requiresIndustrySnapshot) {
                const AlignedBarFrame *firstFrame = aligned.frameAt(0);
                const QDate industryAsOfDate = firstFrame
                    ? firstFrame->timestamp.toTimeZone(shanghai).date()
                    : QDate{};
                const auto resolved = industryAsOfDate.isValid()
                    ? m_workerStore->loadAShareIndustrySnapshot(
                          portfolioDefinition.industryDatasetId,
                          industryAsOfDate, plan->universeSymbols)
                    : std::nullopt;
                if (!resolved || !resolved->coverageComplete) {
                    const QString message = resolved
                        ? QStringLiteral(
                              "行业数据集在 %1 覆盖不完整：%2/%3")
                              .arg(industryAsOfDate.toString(Qt::ISODate))
                              .arg(resolved->resolvedSymbols)
                              .arg(resolved->expectedSymbols)
                        : QStringLiteral("行业数据集不存在或无法解析");
                    QMetaObject::invokeMethod(
                        guard, [guard, requestId, message]() {
                        if (guard) {
                            guard->m_running = false;
                            emit guard->failed(requestId, message);
                        }
                    }, Qt::QueuedConnection);
                    return;
                }
                industry = *resolved;
            }

            // 4. 运行多股引擎。
            CrossSectionalCustomStrategy strategy(*plan);
            PortfolioDefinition executionDefinition = portfolioDefinition;
            executionDefinition.legs.clear();
            for (const BacktestDatasetIdentity &identity : datasetIdentities)
                executionDefinition.legs.append({identity.instrument, 0.0});
            const QVector<AShareInstrumentDayRecord> instrumentDays =
                m_workerStore->loadAShareInstrumentDayRecords(
                    plan->universeSymbols, firstDate, lastDate);
            const AShareCalendarSnapshot calendar =
                AShareTradingCalendar::snapshot(
                    firstDate, lastDate, plan->universeSymbols,
                    instrumentDays);
            QString engineError;
            PortfolioResult result = CrossSectionalBacktestEngine::run(
                strategy, aligned, executionDefinition, &engineError,
                calendar, industry);
            if (result.equityCurve.isEmpty() || !engineError.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, engineError]() {
                    if (guard) {
                        guard->m_running = false;
                        emit guard->failed(
                            requestId, engineError.isEmpty()
                                ? QStringLiteral("多股回测无可用结果") : engineError);
                    }
                }, Qt::QueuedConnection);
                return;
            }
            result.datasetVersions = datasetVersions;
            result.resultKind = QStringLiteral("cross_sectional");
            result.datasetIdentities = datasetIdentities;
            const QJsonObject &document = definition.document;
            result.immutableIdentity = {
                {QStringLiteral("strategyId"), plan->strategyId},
                {QStringLiteral("strategyVersion"), plan->version},
                {QStringLiteral("normalizedParameters"),
                 plan->normalizedParameters},
                {QStringLiteral("ruleSnapshot"),
                 QVariantMap{
                     {QStringLiteral("indicators"),
                      document.value(QStringLiteral("indicators")).toArray().toVariantList()},
                     {QStringLiteral("crossIndicators"),
                      document.value(QStringLiteral("crossIndicators")).toArray().toVariantList()},
                     {QStringLiteral("entry"),
                      document.value(QStringLiteral("entry")).toObject().toVariantMap()},
                     {QStringLiteral("exit"),
                      document.value(QStringLiteral("exit")).toObject().toVariantMap()},
                     {QStringLiteral("position"),
                      document.value(QStringLiteral("position")).toObject().toVariantMap()},
                     {QStringLiteral("risk"),
                      document.value(QStringLiteral("risk")).toObject().toVariantMap()},
                     {QStringLiteral("signal"),
                      document.value(QStringLiteral("signal")).toObject().toVariantMap()},
                     {QStringLiteral("selection"),
                      document.value(QStringLiteral("selection")).toObject().toVariantMap()}}},
                {QStringLiteral("definitionJson"), definition.canonicalJson()},
                {QStringLiteral("requestedBars"), barCount},
                {QStringLiteral("actualBars"), result.equityCurve.size()},
                {QStringLiteral("periodSeconds"),
                 portfolioDefinition.periodSeconds},
                {QStringLiteral("universeSymbols"),
                 plan->universeSymbols},
                {QStringLiteral("portfolioRiskPolicy"),
                 QVariantMap{
                     {QStringLiteral("maximumLegWeight"),
                      portfolioDefinition.maximumLegWeight},
                     {QStringLiteral("maximumActiveLegs"),
                      portfolioDefinition.maximumActiveLegs},
                     {QStringLiteral("maximumConsecutiveMissingBars"),
                      portfolioDefinition.maximumConsecutiveMissingBars},
                     {QStringLiteral("maximumPortfolioLossPercent"),
                      portfolioDefinition.maximumPortfolioLossPercent},
                     {QStringLiteral("maximumPortfolioDrawdownPercent"),
                      portfolioDefinition.maximumPortfolioDrawdownPercent},
                     {QStringLiteral("maximumIndustryWeight"),
                      portfolioDefinition.maximumIndustryWeight},
                     {QStringLiteral("industryDatasetId"),
                      portfolioDefinition.industryDatasetId},
                     {QStringLiteral("factorExposureAlias"),
                      portfolioDefinition.factorExposureAlias},
                     {QStringLiteral("maximumAbsoluteFactorExposure"),
                      portfolioDefinition.maximumAbsoluteFactorExposure},
                     {QStringLiteral("factorResearchAlias"),
                      portfolioDefinition.factorResearchAlias},
                     {QStringLiteral("factorDefinitionId"),
                      portfolioDefinition.factorDefinitionId},
                     {QStringLiteral("factorDefinitionVersion"),
                      portfolioDefinition.factorDefinitionVersion},
                     {QStringLiteral("tPlusOne"),
                      portfolioDefinition.tPlusOne}}},
                {QStringLiteral("riskSummary"),
                 result.riskSummary.toVariantMap()}};
            result.immutableIdentity.insert(
                QStringLiteral("calendarSnapshot"),
                calendar.toVariantMap());
            if (requiresIndustrySnapshot) {
                result.immutableIdentity.insert(
                    QStringLiteral("industrySnapshot"),
                    industry.toVariantMap());
            }
            if (portfolioDefinition.maximumAbsoluteFactorExposure > 0.0) {
                QJsonObject factorDefinition;
                const QJsonArray crossIndicators = document
                    .value(QStringLiteral("crossIndicators")).toArray();
                for (const QJsonValue &value : crossIndicators) {
                    const QJsonObject candidate = value.toObject();
                    if (candidate.value(QStringLiteral("alias")).toString()
                        == portfolioDefinition.factorExposureAlias) {
                        factorDefinition = candidate;
                        break;
                    }
                }
                const QByteArray factorJson = QJsonDocument(factorDefinition)
                    .toJson(QJsonDocument::Compact);
                result.immutableIdentity.insert(
                    QStringLiteral("factorExposure"),
                    QVariantMap{
                        {QStringLiteral("alias"),
                         portfolioDefinition.factorExposureAlias},
                        {QStringLiteral("definition"),
                         factorDefinition.toVariantMap()},
                        {QStringLiteral("definitionSha256"),
                         QStringLiteral("sha256:%1").arg(QString::fromLatin1(
                             QCryptographicHash::hash(
                                 factorJson, QCryptographicHash::Sha256).toHex()))},
                        {QStringLiteral("standardization"),
                         QStringLiteral("population_zscore")},
                        {QStringLiteral("weighting"),
                         QStringLiteral("portfolio_equity_including_cash")},
                        {QStringLiteral("limitPolicy"),
                         QStringLiteral("proportional_de_risk_to_cash")}});
            }
            if (!portfolioDefinition.factorResearchAlias.isEmpty()) {
                result.immutableIdentity.insert(
                    QStringLiteral("factorResearch"),
                    QVariantMap{
                        {QStringLiteral("factorId"),
                         portfolioDefinition.factorDefinitionId},
                        {QStringLiteral("factorVersion"),
                         portfolioDefinition.factorDefinitionVersion},
                        {QStringLiteral("alias"),
                         portfolioDefinition.factorResearchAlias},
                        {QStringLiteral("definitionSha256"),
                         portfolioDefinition.factorDefinitionSha256},
                        {QStringLiteral("preprocessing"),
                         portfolioDefinition.factorPreprocessing.toVariantMap()},
                        {QStringLiteral("returnHorizon"),
                         QStringLiteral("next_aligned_frame")},
                        {QStringLiteral("returnCalculation"),
                         QStringLiteral("close_to_close")},
                        {QStringLiteral("frameAggregation"),
                         QStringLiteral("equal_weighted_mean")},
                        {QStringLiteral("researchMethodVersion"),
                         QStringLiteral("factor_research_v6")},
                        {QStringLiteral("neutralizationPolicy"),
                         portfolioDefinition.factorNeutralization.toVariantMap()},
                        {QStringLiteral("preprocessingOrder"),
                         QStringLiteral(
                             "pairwise_drop_then_global_winsorize_then_group_demean")},
                        {QStringLiteral("neutralizationDatasetId"),
                         portfolioDefinition.factorNeutralization.enabled()
                             ? industry.datasetId : QString{}},
                        {QStringLiteral("neutralizationSnapshotVersion"),
                         portfolioDefinition.factorNeutralization.enabled()
                             ? industry.snapshotVersion : QString{}},
                        {QStringLiteral("neutralizationAsOfDate"),
                         portfolioDefinition.factorNeutralization.enabled()
                             ? industry.asOfDate : QDate{}},
                        {QStringLiteral("icirConvention"),
                         QStringLiteral("mean_over_sample_sd_unannualized")},
                        {QStringLiteral("quantileCount"),
                         result.factorResearchSummary.quantileCount},
                        {QStringLiteral("quantileMethod"),
                         QStringLiteral("stable_factor_then_symbol_equal_count")},
                        {QStringLiteral("quantileAggregation"),
                         QStringLiteral("equal_weighted_within_frame_then_across_frames")},
                        {QStringLiteral("spreadDefinition"),
                         QStringLiteral("q5_minus_q1")},
                        {QStringLiteral("turnoverConvention"),
                         QStringLiteral("half_sum_absolute_weight_change")},
                        {QStringLiteral("initialFormation"),
                         QStringLiteral("excluded")},
                        {QStringLiteral("costFormula"),
                         QStringLiteral("absolute_weight_change_times_one_way_cost_rate")},
                        {QStringLiteral("costPolicy"),
                         portfolioDefinition.factorResearchCost.toVariantMap()},
                        {QStringLiteral("inferenceMethodVersion"),
                         QStringLiteral("newey_west_bartlett_v1")},
                        {QStringLiteral("inferenceMinimumFrames"), 20},
                        {QStringLiteral("confidenceLevel"), 0.95},
                        {QStringLiteral("hacLagRule"),
                         QStringLiteral(
                             "floor_4_times_n_over_100_power_2_over_9")},
                        {QStringLiteral("temporalSegmentCount"), 3},
                        {QStringLiteral("temporalSegmentMethod"),
                         QStringLiteral("contiguous_equal_count")},
                        {QStringLiteral("outOfSampleMethodVersion"),
                         QStringLiteral("expanding_oos_v1")},
                        {QStringLiteral("outOfSamplePolicy"),
                         result.factorResearchSummary.outOfSample.policy
                             .toVariantMap()},
                        {QStringLiteral("outOfSampleInferenceMethodVersion"),
                         QStringLiteral("newey_west_bartlett_v1")},
                        {QStringLiteral("summary"),
                         result.factorResearchSummary.toVariantMap()}});
            }
            QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
                if (guard) {
                    guard->m_running = false;
                    emit guard->completed(requestId, result);
                }
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    if (!invoked) m_running = false;
    return invoked;
}

void CrossSectionalBacktestService::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

bool CrossSectionalBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

bool CrossSectionalBacktestService::isReady() const { return m_ready; }
bool CrossSectionalBacktestService::isRunning() const { return m_running; }

QVariantMap CrossSectionalBacktestService::validateRiskConfiguration(
    double maximumLegWeight, int maximumActiveLegs,
    int maximumConsecutiveMissingBars)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), message}};
    };
    if (!std::isfinite(maximumLegWeight)
        || maximumLegWeight <= 0.0 || maximumLegWeight > 1.0) {
        return failure(QStringLiteral("单腿最大权重必须在 0–100% 之间"));
    }
    if (maximumActiveLegs < 0 || maximumActiveLegs > 5207) {
        return failure(QStringLiteral("最大活动腿数必须在 0–5207 之间，0 表示不限制"));
    }
    if (maximumConsecutiveMissingBars < 0
        || maximumConsecutiveMissingBars > 100000) {
        return failure(QStringLiteral("连续缺失阈值必须在 0–100000 Bar 之间，0 表示关闭"));
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("maximumLegWeight"), maximumLegWeight},
            {QStringLiteral("maximumActiveLegs"), maximumActiveLegs},
            {QStringLiteral("maximumConsecutiveMissingBars"),
             maximumConsecutiveMissingBars}};
}

QVariantMap CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
    double maximumPortfolioLossPercent,
    double maximumPortfolioDrawdownPercent)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), message}};
    };
    if (!std::isfinite(maximumPortfolioLossPercent)
        || maximumPortfolioLossPercent < 0.0
        || maximumPortfolioLossPercent > 100.0) {
        return failure(QStringLiteral(
            "组合累计亏损熔断必须在 0–100% 之间，0 表示关闭"));
    }
    if (!std::isfinite(maximumPortfolioDrawdownPercent)
        || maximumPortfolioDrawdownPercent < 0.0
        || maximumPortfolioDrawdownPercent > 100.0) {
        return failure(QStringLiteral(
            "组合峰值回撤熔断必须在 0–100% 之间，0 表示关闭"));
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("maximumPortfolioLossPercent"),
             maximumPortfolioLossPercent},
            {QStringLiteral("maximumPortfolioDrawdownPercent"),
             maximumPortfolioDrawdownPercent}};
}

QVariantMap CrossSectionalBacktestService::validateIndustryConfiguration(
    double maximumIndustryWeight, const QString &industryDatasetId)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), message}};
    };
    if (!std::isfinite(maximumIndustryWeight)
        || maximumIndustryWeight < 0.0
        || maximumIndustryWeight > 1.0) {
        return failure(QStringLiteral(
            "行业目标权重上限必须在 0–100% 之间，0 表示关闭"));
    }
    const QString normalizedDatasetId = industryDatasetId.trimmed();
    if (maximumIndustryWeight > 0.0 && normalizedDatasetId.isEmpty()) {
        return failure(QStringLiteral("开启行业约束必须选择不可变行业数据集"));
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("maximumIndustryWeight"),
             maximumIndustryWeight},
            {QStringLiteral("industryDatasetId"), normalizedDatasetId}};
}

QVariantMap CrossSectionalBacktestService::validateFactorExposureConfiguration(
    double maximumAbsoluteFactorExposure, const QString &factorExposureAlias)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), message}};
    };
    if (!std::isfinite(maximumAbsoluteFactorExposure)
        || maximumAbsoluteFactorExposure < 0.0
        || maximumAbsoluteFactorExposure > 10.0) {
        return failure(QStringLiteral(
            "因子绝对暴露上限必须在 0–10 Z 之间，0 表示关闭"));
    }
    const QString normalizedAlias = factorExposureAlias.trimmed();
    if (maximumAbsoluteFactorExposure > 0.0 && normalizedAlias.isEmpty()) {
        return failure(QStringLiteral("开启因子暴露约束必须选择横截面指标别名"));
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("maximumAbsoluteFactorExposure"),
             maximumAbsoluteFactorExposure},
            {QStringLiteral("factorExposureAlias"), normalizedAlias}};
}

QVariantMap CrossSectionalBacktestService::validateFactorResearchConfiguration(
    const QString &factorAlias, double lowerQuantile,
    double upperQuantile, int minimumPairs, double oneWayCostBps,
    bool industryNeutralization, int minimumGroupSize,
    const QString &industryDatasetId)
{
    const QString alias = factorAlias.trimmed();
    FactorPreprocessingPolicy policy;
    policy.winsorizationPolicy = alias.isEmpty()
        ? QStringLiteral("none") : QStringLiteral("percentile_clip");
    policy.lowerQuantile = lowerQuantile;
    policy.upperQuantile = upperQuantile;
    policy.minimumPairs = minimumPairs;
    QString error;
    if (!policy.isValid(&error)) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), error}};
    }
    FactorResearchCostPolicy costPolicy;
    costPolicy.oneWayCostBps = oneWayCostBps;
    if (!costPolicy.isValid(&error)) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), error}};
    }
    FactorNeutralizationPolicy neutralizationPolicy;
    neutralizationPolicy.method = industryNeutralization
        ? QStringLiteral("within_group_demean") : QStringLiteral("none");
    neutralizationPolicy.minimumGroupSize = minimumGroupSize;
    if (!neutralizationPolicy.isValid(&error)) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), error}};
    }
    const QString normalizedDatasetId = industryDatasetId.trimmed();
    if (industryNeutralization && alias.isEmpty()) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"),
                 QStringLiteral("行业中性化必须先启用因子研究")}};
    }
    if (industryNeutralization && normalizedDatasetId.isEmpty()) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"),
                 QStringLiteral("行业中性化必须选择不可变行业数据集")}};
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("factorResearchAlias"), alias},
            {QStringLiteral("preprocessing"), policy.toVariantMap()},
            {QStringLiteral("costPolicy"), costPolicy.toVariantMap()},
            {QStringLiteral("neutralizationPolicy"),
             neutralizationPolicy.toVariantMap()},
            {QStringLiteral("industryDatasetId"), normalizedDatasetId}};
}

QVariantMap CrossSectionalBacktestService::validateLocalDatasets(
    const CustomStrategyDefinition &definition, int periodSeconds,
    int barCount, const QVector<LocalHistoryDataset> &catalog)
{
    auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("valid"), false},
                           {QStringLiteral("error"), message}};
    };
    if (definition.schemaVersion != 3)
        return failure(QStringLiteral("多股回测只接受 DSL v3"));
    if (periodSeconds <= 0)
        return failure(QStringLiteral("请选择本地历史数据周期"));
    if (barCount < 2)
        return failure(QStringLiteral("回测根数必须大于 1"));

    const QJsonObject universe = definition.document
        .value(QStringLiteral("universe")).toObject();
    const QJsonArray symbols = universe.value(QStringLiteral("symbols")).toArray();
    if (symbols.size() < 2)
        return failure(QStringLiteral("多股回测至少需要 2 个本地标的"));

    QSet<QString> uniqueSymbols;
    int minimumCandleCount = std::numeric_limits<int>::max();
    for (const QJsonValue &value : symbols) {
        const QString symbol = value.toString();
        if (uniqueSymbols.contains(symbol))
            return failure(QStringLiteral("股票池包含重复代码：%1").arg(symbol));
        uniqueSymbols.insert(symbol);

        const LocalHistoryDataset *matched = nullptr;
        for (const LocalHistoryDataset &dataset : catalog) {
            if (dataset.instrument.symbol == symbol
                && dataset.periodSeconds == periodSeconds
                && dataset.isBacktestEligible()) {
                matched = &dataset;
                break;
            }
        }
        if (!matched)
            return failure(QStringLiteral("本地目录缺少同周期 Tick 数据：%1").arg(symbol));
        if (matched->datasetVersion.isEmpty())
            return failure(QStringLiteral("数据集身份尚未生成：%1").arg(symbol));
        if (matched->datasetStaleDayCount > 0)
            return failure(QStringLiteral("派生缓存已过期：%1").arg(symbol));
        if (matched->candleCount < barCount) {
            return failure(QStringLiteral("K 线不足：%1（%2/%3）")
                               .arg(symbol).arg(matched->candleCount).arg(barCount));
        }
        minimumCandleCount = std::min(minimumCandleCount, matched->candleCount);
    }

    const QJsonObject selection = definition.document
        .value(QStringLiteral("selection")).toObject();
    if (selection.value(QStringLiteral("count")).toInt() > symbols.size()) {
        return failure(QStringLiteral("Top-N 数量不能超过股票池标的数"));
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString{}},
            {QStringLiteral("symbolCount"), symbols.size()},
            {QStringLiteral("periodSeconds"), periodSeconds},
            {QStringLiteral("minimumCandleCount"), minimumCandleCount}};
}

} // namespace pp
