#include "app/TradingController.h"
#include "services/CompositeMarketDataProvider.h"

#include "app/CustomStrategyWorkspacePolicy.h"

#include "services/CompositeMarketDataProvider.h"
#include "services/CustomStrategyPreviewService.h"
#include "services/MarketSession.h"
#include "services/MootdxMarketDataProvider.h"
#include "services/StorageProfile.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/PairSpreadStrategy.h"
#include "strategy/TickMomentumStrategy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace pp {

namespace {

QVariantMap strategyExecutionConfigMap(const StrategyExecutionConfig &config)
{
    return {{QStringLiteral("strategyId"), config.strategyId},
            {QStringLiteral("enabled"), config.enabled},
            {QStringLiteral("executionMode"), static_cast<int>(config.mode)},
            {QStringLiteral("orderType"), static_cast<int>(config.orderType)},
            {QStringLiteral("timeInForce"), static_cast<int>(config.timeInForce)},
            {QStringLiteral("fixedOrderValue"), config.fixedOrderValue},
            {QStringLiteral("maxPositionPercent"), config.maxPositionPercent},
            {QStringLiteral("sellAll"), config.sellAll},
            {QStringLiteral("cooldownSeconds"), config.cooldownSeconds},
            {QStringLiteral("approvalTimeoutSeconds"), config.approvalTimeoutSeconds},
            {QStringLiteral("limitOffsetBps"), config.limitOffsetBps},
            {QStringLiteral("lotSize"), config.lotSize},
            {QStringLiteral("orderTimeoutSeconds"), config.orderTimeoutSeconds},
            {QStringLiteral("maxOrdersPerDay"), config.maxOrdersPerDay},
            {QStringLiteral("maxDailyOrderValue"), config.maxDailyOrderValue}};
}

QVariantMap backtestConfigMap(const BacktestConfig &config)
{
    return {{QStringLiteral("taskId"), config.taskId},
            {QStringLiteral("strategyId"), config.strategyId},
            {QStringLiteral("strategyVersion"), config.strategyVersion},
            {QStringLiteral("strategyParameters"), config.strategyParameters},
            {QStringLiteral("ruleSnapshot"), config.ruleSnapshot},
            {QStringLiteral("dataVersion"), config.dataVersion},
            {QStringLiteral("datasetIdentity"),
             config.datasetIdentity.isEmpty()
                 ? QVariantMap{} : config.datasetIdentity.toVariantMap()},
            {QStringLiteral("symbol"), config.instrument.symbol},
            {QStringLiteral("name"), config.instrument.name},
            {QStringLiteral("frequency"), config.frequency},
            {QStringLiteral("barCount"), config.requestedBars},
            {QStringLiteral("requestedBars"), config.requestedBars},
            {QStringLiteral("actualBars"), config.actualBars},
            {QStringLiteral("from"), config.from},
            {QStringLiteral("to"), config.to},
            {QStringLiteral("initialCapital"), config.initialCapital},
            {QStringLiteral("commissionRate"), config.commissionRate},
            {QStringLiteral("slippageRate"), config.slippageRate},
            {QStringLiteral("lotSize"), config.lotSize}};
}

QString instrumentTitle(const Instrument &instrument)
{
    return instrument.symbol.isEmpty()
        ? QString{}
        : QStringLiteral("%1  %2").arg(instrument.symbol, instrument.name);
}

QString periodText(int periodSeconds)
{
    if (periodSeconds % 86400 == 0) return QStringLiteral("%1d").arg(periodSeconds / 86400);
    if (periodSeconds % 3600 == 0) return QStringLiteral("%1h").arg(periodSeconds / 3600);
    if (periodSeconds % 60 == 0) return QStringLiteral("%1m").arg(periodSeconds / 60);
    return QStringLiteral("%1s").arg(periodSeconds);
}

QVector<EquityPoint> portfolioEquityPoints(const PortfolioResult &result)
{
    QVector<EquityPoint> points;
    double peak = result.definition.initialCapital;
    for (const PortfolioBar &bar : result.equityCurve) {
        EquityPoint point;
        point.timestamp = bar.timestamp;
        point.cash = bar.cash;
        point.positionValue = bar.equity - bar.cash;
        point.equity = bar.equity;
        peak = std::max(peak, bar.equity);
        point.drawdownPercent = peak > 0.0
            ? (peak - bar.equity) / peak * 100.0 : 0.0;
        points.append(point);
    }
    return points;
}

PortfolioDefinition portfolioDefinitionFromMap(const QVariantMap &map)
{
    PortfolioDefinition definition;
    definition.portfolioId =
        map.value(QStringLiteral("portfolioId")).toString();
    definition.name = map.value(QStringLiteral("name")).toString();
    definition.periodSeconds =
        map.value(QStringLiteral("periodSeconds")).toInt();
    definition.rebalanceEveryTradingDays =
        map.value(QStringLiteral("rebalanceEveryTradingDays")).toInt();
    definition.initialCapital =
        map.value(QStringLiteral("initialCapital")).toDouble();
    definition.commissionRate =
        map.value(QStringLiteral("commissionRate")).toDouble();
    definition.slippageRate =
        map.value(QStringLiteral("slippageRate")).toDouble();
    definition.lotSize = map.value(QStringLiteral("lotSize")).toInt();
    definition.tPlusOne = map.value(QStringLiteral("tPlusOne")).toBool();
    definition.maximumLegWeight =
        map.value(QStringLiteral("maximumLegWeight")).toDouble();
    definition.maximumActiveLegs =
        map.value(QStringLiteral("maximumActiveLegs")).toInt();
    definition.maximumConsecutiveMissingBars =
        map.value(QStringLiteral("maximumConsecutiveMissingBars")).toInt();
    definition.maximumPortfolioLossPercent =
        map.value(QStringLiteral("maximumPortfolioLossPercent")).toDouble();
    definition.maximumPortfolioDrawdownPercent = map
        .value(QStringLiteral("maximumPortfolioDrawdownPercent")).toDouble();
    definition.maximumIndustryWeight = map
        .value(QStringLiteral("maximumIndustryWeight")).toDouble();
    definition.industryDatasetId = map
        .value(QStringLiteral("industryDatasetId")).toString();
    definition.factorExposureAlias = map
        .value(QStringLiteral("factorExposureAlias")).toString();
    definition.maximumAbsoluteFactorExposure = map
        .value(QStringLiteral("maximumAbsoluteFactorExposure")).toDouble();
    definition.factorResearchAlias = map
        .value(QStringLiteral("factorResearchAlias")).toString();
    definition.factorDefinitionId = map
        .value(QStringLiteral("factorDefinitionId")).toString();
    definition.factorDefinitionVersion = map
        .value(QStringLiteral("factorDefinitionVersion")).toInt();
    definition.factorDefinitionSha256 = map
        .value(QStringLiteral("factorDefinitionSha256")).toString();
    definition.factorPreprocessing = FactorPreprocessingPolicy::fromVariantMap(
        map.value(QStringLiteral("factorPreprocessing")).toMap());
    definition.factorResearchCost = FactorResearchCostPolicy::fromVariantMap(
        map.value(QStringLiteral("factorResearchCost")).toMap());
    definition.factorNeutralization =
        FactorNeutralizationPolicy::fromVariantMap(
            map.value(QStringLiteral("factorNeutralization")).toMap());
    const QVariantList legs = map.value(QStringLiteral("legs")).toList();
    for (const QVariant &value : legs) {
        const QVariantMap legMap = value.toMap();
        PortfolioLeg leg;
        leg.instrument.symbol =
            legMap.value(QStringLiteral("symbol")).toString();
        leg.instrument.name =
            legMap.value(QStringLiteral("name")).toString();
        leg.targetWeight =
            legMap.value(QStringLiteral("targetWeight")).toDouble();
        definition.legs.append(leg);
    }
    return definition;
}

} // namespace

TradingController::TradingController(QObject *parent)
    : QObject(parent)
    , m_klineFetcher(KlineProviderBackend::Mootdx)
    , m_historicalDataService(&m_dataStore, &m_klineFetcher)
    , m_tushareHistoryFetcher(KlineProviderBackend::Tushare)
    , m_baostockHistoryFetcher(KlineProviderBackend::BaoStock)
    , m_historyDownloadFetcher(KlineProviderBackend::Mootdx)
    , m_historyDownloadService(&m_dataStore, &m_historyDownloadRouter)
    , m_tradingAccount(&m_broker)
    , m_strategyExecutionCoordinator(&m_tradingAccount, &m_positionSizer,
                                     &m_executionPolicy, &m_dataStore)
    , m_customStrategyRuntimeManager(&m_strategyEngine)
{
    initializePaperTrading();
    m_historyDownloadRouter.addProvider(&m_tushareHistoryFetcher);
    m_historyDownloadRouter.addProvider(&m_baostockHistoryFetcher);
    m_historyDownloadRouter.addProvider(&m_historyDownloadFetcher);
    m_strategyPluginManager.setPluginDirectory(
        QCoreApplication::applicationDirPath() + QStringLiteral("/strategies"));
    connect(&m_customStrategyRuntimeManager, &CustomStrategyRuntimeManager::pluginsChanged,
            this, &TradingController::strategyPluginStateChanged);
    const QString configuredLogPath = qEnvironmentVariable(
        "PPTRADER_LOG_PATH").trimmed();
    LogService::instance().configure(
        configuredLogPath.isEmpty()
            ? QCoreApplication::applicationDirPath()
                + QStringLiteral("/logs/pptrader.log")
            : configuredLogPath,
        5 * 1024 * 1024, 3);
    LogService::instance().info(QStringLiteral("app"), QStringLiteral("PPTrader 启动"));
    m_backtestStrategyRegistry.normalizeParameters(
        m_selectedBacktestStrategyId, {}, &m_selectedBacktestParameters, nullptr);
    MootdxMarketDataProvider::warmUpPython();
    m_marketData = new CompositeMarketDataProvider(this);

    connect(m_marketData, &MarketDataProvider::tickReceived, this, &TradingController::handleTick);
    connect(m_marketData, &MarketDataProvider::snapshotReceived,
            this, &TradingController::handleMarketSnapshot);
    connect(m_marketData, &MarketDataProvider::providerError, this, &TradingController::setStatusText);
    connect(m_marketData, &MarketDataProvider::providerStateChanged,
            this, &TradingController::handleProviderState);
    connect(m_marketData, &MarketDataProvider::providerRouteChanged,
            this, &TradingController::handleProviderRoute);
    connect(&m_realtimeMinuteService, &TencentMinuteBarService::resultReady,
            this, &TradingController::finishRealtimeMinuteRequest);
    connect(&m_realtimeChartWindowModel,
            &RealtimeChartWindowListModel::countChanged,
            this, &TradingController::realtimeChartWindowsChanged);
    m_realtimeMinuteRefreshTimer.setSingleShot(true);
    m_realtimeMinuteRefreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_realtimeMinuteRefreshTimer, &QTimer::timeout, this, [this]() {
        if (!m_realtimeLiveSessionState.minuteRefreshEnabled()
            || selectedLiveSymbol().isEmpty()) return;
        if (m_realtimeMinuteRequestGate.loading()) {
            scheduleRealtimeMinuteRefresh();
            return;
        }
        requestRealtimeMinuteSeries(selectedLiveSymbol());
    });
    connect(&m_asyncSnapshotQueryService, &AsyncMarketSnapshotQueryService::resultReady,
            this, &TradingController::finishSnapshotRequest);
    connect(&m_asyncLocalCandleQueryService,
            &AsyncLocalCandleSeriesQueryService::resultReady,
            this, &TradingController::finishLocalCandleSeriesRequest);
    connect(&m_asyncLocalTickQueryService,
            &AsyncLocalTickSeriesQueryService::resultReady,
            this, &TradingController::finishLocalTickSeriesRequest);
    connect(&m_asyncLiveDayCandleQueryService,
            &AsyncLocalCandleSeriesQueryService::resultReady,
            this, &TradingController::finishLiveDayCandleSeriesRequest);
    connect(&m_asyncCustomStrategyWorkspaceService,
            &AsyncCustomStrategyWorkspaceService::previewReady,
            this, &TradingController::finishCustomStrategyPreview);
    connect(&m_asyncCustomStrategyWorkspaceService,
            &AsyncCustomStrategyWorkspaceService::saveFinished,
            this, &TradingController::finishCustomStrategySave);
    connect(&m_asyncBacktestArchiveService,
            &AsyncBacktestArchiveService::finished,
            this, &TradingController::handleBatchBacktestArchiveFinished);
    connect(&m_asyncBacktestArchiveService,
            &AsyncBacktestArchiveService::loadFinished,
            this, &TradingController::finishBacktestArchiveLoad);
    connect(&m_asyncBacktestArchiveService,
            &AsyncBacktestArchiveService::rawTickPageReady,
            this, &TradingController::finishBacktestArchiveRawTickPage);
    connect(&m_asyncRawTickAvailabilityService,
            &AsyncRawTickAvailabilityService::resultReady,
            this, [this](quint64 requestId, const QVariantMap &result) {
        if (!m_rawTickAvailabilityRequestGate.finish(requestId)) return;
        m_rawTickAvailabilityResult = result;
        m_rawTickAvailabilityError =
            result.value(QStringLiteral("error")).toString();
        emit rawTickAvailabilityStateChanged();
    });
    connect(&m_backtestDatasetService, &BacktestDatasetService::prepared,
            this, &TradingController::handleBacktestDatasetPrepared);
    connect(&m_backtestDatasetService, &BacktestDatasetService::failed,
            this, &TradingController::handleBacktestDatasetFailed);
    connect(&m_nasBacktestPreparationService,
            &BacktestDatasetService::batchProgress,
            this, &TradingController::handleNasBacktestPreparationProgress);
    connect(&m_nasBacktestPreparationService,
            &BacktestDatasetService::batchFinished,
            this, &TradingController::handleNasBacktestPreparationFinished);
    connect(&m_dataStore, &SqliteLocalDataStore::writeFailed, this, &TradingController::setStatusText);
    connect(&m_historicalDataService, &HistoricalDataService::candlesReady,
            this, &TradingController::handleHistoryKline);
    connect(&m_historicalDataService, &HistoricalDataService::requestError,
            this, &TradingController::handleHistoryError);
    connect(&m_historyDownloadService, &HistoricalDataService::candlesReady,
            this, &TradingController::handleHistoryDownloadReady);
    connect(&m_historyDownloadService, &HistoricalDataService::requestError,
            this, &TradingController::handleHistoryDownloadError);
    connect(&m_historyDownloadRouter, &HistoricalDataProviderRouter::routeAttempted,
            this, [this](quint64 requestId, const QString &providerId, int attempt) {
        if (requestId != m_activeHistoryDownloadServiceRequestId) return;
        m_historyDownloadProvider = providerId;
        m_historyDownloadStatusText = QStringLiteral("正在请求 %1 · 第 %2 个候选源")
                                          .arg(providerId).arg(attempt);
        emit historyDownloadStateChanged();
    });
    connect(&m_historyDownloadRouter, &HistoricalDataProviderRouter::routeResolved,
            this, [this](quint64 requestId, const QString &providerId,
                         bool fallbackUsed) {
        if (requestId != m_activeHistoryDownloadServiceRequestId) return;
        m_historyDownloadProvider = providerId;
        m_historyDownloadFallbackUsed = fallbackUsed;
        emit historyDownloadStateChanged();
    });
    connect(&m_historyDownloadService, &HistoricalDataService::datasetIdentityReady,
            this, [this](quint64 requestId, const QString &source,
                         const QString &adjustment, const QString &dataVersion,
                         const QDateTime &downloadedAt, int, int) {
        if (requestId != m_activeHistoryDownloadServiceRequestId) return;
        m_historyDownloadProvider = source;
        m_historyDownloadAdjustment = adjustment;
        m_historyDownloadDataVersion = dataVersion;
        m_historyDownloadDownloadedAt = downloadedAt;
        emit historyDownloadStateChanged();
    });
    connect(&m_tickDataCsvImportService, &TickDataCsvImportService::finished,
            this, &TradingController::handleTickDataImportFinished);
    connect(&m_aShareInstrumentDayImportService,
            &AShareInstrumentDayImportService::finished,
            this, &TradingController::handleAShareInstrumentDayImportFinished);
    connect(&m_aShareIndustryImportService,
            &AShareIndustryImportService::finished,
            this, &TradingController::handleAShareIndustryImportFinished);
    connect(&m_authorizedFactorImportService,
            &AuthorizedFactorImportService::finished,
            this, &TradingController::handleAuthorizedFactorImportFinished);
    connect(&m_externalFactorResearchService,
            &ExternalFactorResearchService::finished,
            this, &TradingController::handleExternalFactorResearchFinished);
    connect(&m_externalFactorResearchService,
            &ExternalFactorResearchService::preflightFinished,
            this,
            &TradingController::handleExternalFactorResearchPreflightFinished);
    connect(&m_backtest, &BacktestEngine::pointReady, this, &TradingController::handleBacktestPoint);
    connect(&m_backtest, &BacktestEngine::tradeCompleted, this, &TradingController::handleBacktestTrade);
    connect(&m_backtest, &BacktestEngine::equityPointReady, this, &TradingController::handleEquityPoint);
    connect(&m_backtest, &BacktestEngine::progressChanged, this, [this](double progress) {
        m_backtestProgress = progress;
        emit backtestStateChanged();
    });
    connect(&m_backtest, &BacktestEngine::finished, this, &TradingController::handleBacktestFinished);
    connect(&m_backtest, &BacktestEngine::cancelled, this, &TradingController::handleBacktestCancelled);
    connect(&m_backtest, &BacktestEngine::errorOccurred, this, &TradingController::handleBacktestError);
    connect(&m_batchBacktestService, &BatchBacktestService::itemReady, this,
            [this](quint64 requestId, const BatchBacktestItemResult &result) {
                if (requestId != m_batchBacktestRequestId) return;
                BatchBacktestItemResult visibleResult = result;
                visibleResult.archivable =
                    m_batchRetainedResults.contains(result.instrument.symbol);
                m_batchBacktestModel.addResult(visibleResult);
                if (m_storageReady && !m_activeBatchBacktestRunId.isEmpty()) {
                    BatchBacktestRunItem item;
                    item.runId = m_activeBatchBacktestRunId;
                    item.instrument = result.instrument;
                    item.status = result.succeeded ? QStringLiteral("succeeded")
                                                   : QStringLiteral("failed");
                    item.error = result.error;
                    item.dataVersion = result.dataVersion;
                    item.datasetVersion = result.datasetVersion;
                    m_dataStore.upsertBatchBacktestRunItem(item);
                }
            });
    connect(&m_batchBacktestService, &BatchBacktestService::itemCompleted, this,
            [this](quint64 requestId, const QString &symbol,
                   const BacktestResult &result) {
                if (requestId != m_batchBacktestRequestId || symbol.isEmpty()) return;
                const QStringList evicted =
                    m_batchRetainedResults.insert(symbol, result);
                for (const QString &oldest : evicted) {
                    m_batchBacktestModel.updateArchivableState(oldest, false);
                }
                m_batchBacktestModel.updateArchivableState(symbol, true);
            });
    connect(&m_batchBacktestService, &BatchBacktestService::progressChanged, this,
            [this](quint64 requestId, int completed, int total,
                   int succeeded, int failed) {
                if (requestId != m_batchBacktestRequestId) return;
                m_batchBacktestCompletedCount = completed;
                m_batchBacktestRequestedCount = total;
                m_batchBacktestSucceededCount = succeeded;
                m_batchBacktestFailedCount = failed;
                if (m_storageReady && !m_activeBatchBacktestRunId.isEmpty()) {
                    m_activeBatchBacktestRun.completedSymbols = completed;
                    m_activeBatchBacktestRun.succeededSymbols = succeeded;
                    m_activeBatchBacktestRun.failedSymbols = failed;
                    m_dataStore.upsertBatchBacktestRun(m_activeBatchBacktestRun);
                }
                m_batchBacktestStatusText = m_batchBacktestSegmentedResearch
                    ? QStringLiteral("独立分段研究：%1/%2")
                          .arg(completed).arg(total)
                    : QStringLiteral("批量回测：%1/%2")
                          .arg(completed).arg(total);
                emit batchBacktestStateChanged();
            });
    connect(&m_batchBacktestService, &BatchBacktestService::finished, this,
            [this](quint64 requestId, bool cancelled, const QString &error) {
                if (requestId != m_batchBacktestRequestId) return;
                m_batchBacktestError = error;
                if (!error.isEmpty()) {
                    m_batchBacktestStatusText = QStringLiteral("批量回测失败");
                } else if (cancelled) {
                    m_batchBacktestStatusText = QStringLiteral("批量回测已取消");
                } else {
                    m_batchBacktestStatusText = m_batchBacktestSegmentedResearch
                        ? QStringLiteral("独立分段研究完成：成功 %1，失败 %2")
                              .arg(m_batchBacktestSucceededCount)
                              .arg(m_batchBacktestFailedCount)
                        : QStringLiteral("批量回测完成：成功 %1，失败 %2")
                              .arg(m_batchBacktestSucceededCount)
                              .arg(m_batchBacktestFailedCount);
                }
                if (m_storageReady && !m_activeBatchBacktestRunId.isEmpty()) {
                    m_activeBatchBacktestRun.completedAt =
                        QDateTime::currentDateTimeUtc();
                    m_activeBatchBacktestRun.status = !error.isEmpty()
                        ? QStringLiteral("failed")
                        : cancelled ? QStringLiteral("cancelled")
                                    : QStringLiteral("completed");
                    m_activeBatchBacktestRun.error = error;
                    m_dataStore.upsertBatchBacktestRun(m_activeBatchBacktestRun);
                    m_activeBatchBacktestRunId.clear();
                    reloadBatchBacktestRunHistory();
                }
                emit batchBacktestStateChanged();
            });
    connect(&m_portfolioBacktestService, &PortfolioBacktestService::completed,
            this, &TradingController::handlePortfolioBacktestCompleted);
    connect(&m_portfolioBacktestService, &PortfolioBacktestService::failed,
            this, [this](quint64 requestId, const QString &message) {
        if (requestId != m_portfolioBacktestRequestId) return;
        m_portfolioBacktestError = message;
        m_portfolioBacktestStatusText = QStringLiteral("组合回测失败");
        emit portfolioBacktestStateChanged();
    });
    connect(&m_crossSectionalBacktestService,
            &CrossSectionalBacktestService::completed,
            this, &TradingController::handleCrossSectionalBacktestCompleted);
    connect(&m_crossSectionalBacktestService, &CrossSectionalBacktestService::failed,
            this, &TradingController::handleCrossSectionalBacktestFailed);
    connect(&m_asyncFactorResearchReportService,
            &AsyncFactorResearchReportService::exportFinished,
            this, [this](quint64 requestId, const QVariantMap &result) {
        if (requestId != m_crossSectionalReportExportRequestId) return;
        m_crossSectionalReportExporting = false;
        m_crossSectionalReportExportPath =
            result.value(QStringLiteral("filePath")).toString();
        m_crossSectionalReportExportError =
            result.value(QStringLiteral("error")).toString();
        m_crossSectionalReportExportStatusText =
            result.value(QStringLiteral("success")).toBool()
                ? QStringLiteral("因子研究报告已保存")
                : QStringLiteral("因子研究报告保存失败");
        emit crossSectionalBacktestStateChanged();
    });
    connect(&m_strategyEngine, &StrategyEngine::strategySignalGenerated,
            this, &TradingController::handleStrategySignal);
    connect(&m_strategyEngine, &StrategyEngine::strategyCountChanged, this, &TradingController::strategyStateChanged);
    connect(&m_strategyEngine, &StrategyEngine::maxThreadCountChanged, this, &TradingController::strategyStateChanged);
    connect(&m_marketClockTimer, &QTimer::timeout, this, &TradingController::selectedMarketSessionChanged);
    m_customStrategyReplayTimer.setSingleShot(false);
    connect(&m_customStrategyReplayTimer, &QTimer::timeout,
            this, &TradingController::handleCustomStrategyReplayTick);
    connect(&m_parameterGridBacktestService, &ParameterGridBacktestService::completed,
            this, &TradingController::handleParameterGridCompleted);
    connect(&m_parameterGridBacktestService, &ParameterGridBacktestService::failed,
            this, &TradingController::handleParameterGridFailed);
    connect(&m_rawTickSensitivityService,
            &RawTickParameterSensitivityService::completed,
            this, &TradingController::handleRawTickSensitivityCompleted);
    connect(&m_rawTickSensitivityService,
            &RawTickParameterSensitivityService::failed,
            this, &TradingController::handleRawTickSensitivityFailed);
    m_tradingPersistenceTimer.setSingleShot(true);
    m_tradingPersistenceTimer.setInterval(1000);
    connect(&m_tradingPersistenceTimer, &QTimer::timeout,
            this, &TradingController::persistTradingState);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &TradingController::persistTradingState);
    connect(&m_broker, &BrokerGateway::loginStateChanged, this,
            [this](bool, const QString &message) {
                emit brokerStateChanged();
                setStatusText(message);
            });
    connect(&m_tradingAccount, &TradingAccountService::orderChanged,
            this, &TradingController::handleOrderChanged);
    connect(&m_tradingAccount, &TradingAccountService::executionAdded,
            this, &TradingController::handleExecutionAdded);
    connect(&m_tradingAccount, &TradingAccountService::positionChanged,
            this, &TradingController::handlePositionChanged);
    connect(&m_tradingAccount, &TradingAccountService::accountChanged,
            this, &TradingController::handleAccountChanged);
    connect(&m_tradingAccount, &TradingAccountService::accountReset, this, [this]() {
        m_orderModel.clear();
        m_executionModel.clear();
        m_positionModel.clear();
    });
    connect(&m_tradingAccount, &TradingAccountService::operationFailed,
            this, &TradingController::handleTradingError);
    m_notificationDispatcher.registerChannel(&m_desktopNotifications);
    m_notificationDispatcher.registerChannel(&m_webhookNotifications);
    restoreNotificationSettings();
    connect(&m_strategyExecutionCoordinator, &StrategyExecutionCoordinator::intentUpdated,
            this, [this](const StrategyOrderIntent &intent) {
                m_strategyIntentModel.upsertIntent(intent);
                emit strategyExecutionStateChanged();
            });
    connect(&m_strategyExecutionCoordinator, &StrategyExecutionCoordinator::orderSubmitted,
            this, [this](const StrategyOrderIntent &intent) {
                emit strategyExecutionStateChanged();
                setStatusText(QStringLiteral("策略订单已提交：%1").arg(intent.orderId));
                m_notificationDispatcher.notifyOrderSubmitted(intent);
            });
    connect(&m_strategyExecutionCoordinator, &StrategyExecutionCoordinator::approvalRequired,
            this, [this](const StrategyOrderIntent &intent) {
                emit strategyExecutionStateChanged();
                setStatusText(QStringLiteral("策略信号等待人工审批"));
                m_notificationDispatcher.notifyApprovalRequired(intent);
            });
    connect(&m_strategyExecutionCoordinator, &StrategyExecutionCoordinator::executionRejected,
            this, [this](const StrategyOrderIntent &intent) {
                emit strategyExecutionStateChanged();
                if (!intent.message.isEmpty()) setStatusText(intent.message);
                m_notificationDispatcher.notifyExecutionRejected(intent);
            });
    connect(&m_nasDataNodeClient, &NasDataNodeClient::catalogReady,
            this, [this](quint64 requestId, const NasCatalogResult &result) {
                if (requestId != m_nasActiveCatalogRequestId) return;
                m_nasDataNodeLoading = false;
                m_nasManifestElapsedMilliseconds = m_nasManifestTimer.isValid()
                    ? m_nasManifestTimer.elapsed() : 0;
                m_nasCatalogEntries = result.succeeded ? result.entries
                                                       : QVector<NasCatalogEntry>{};
                m_nasSyncQueue = NasTickSyncQueue{};
                clearNasSyncState();
                m_nasManifestRequestedSymbols = result.requestedSymbols;
                m_nasManifestAvailableSymbols = result.availableSymbols;
                m_nasManifestMissingSymbols = result.missingSymbols;
                m_nasManifestTotalBytes = result.totalBytes;
                m_nasDataNodeError = result.error;
                m_nasDataNodeStatusText = result.succeeded
                    ? (result.requestedSymbols > 1
                        ? QStringLiteral("NAS manifest 已加载：%1/%2 个代码，%3 个文件")
                              .arg(result.availableSymbols)
                              .arg(result.requestedSymbols)
                              .arg(result.entries.size())
                        : QStringLiteral("NAS catalog 已加载：%1 个交易日")
                              .arg(result.entries.size()))
                    : QStringLiteral("NAS catalog 查询失败");
                emit nasDataNodeStateChanged();
            });
    connect(&m_nasDataNodeClient, &NasDataNodeClient::changesReady,
            this, [this](quint64 requestId, const NasChangesResult &result) {
                if (requestId != m_nasDataNodeRequestVersion) return;
                m_nasDataNodeLoading = false;
                m_nasNodeChangesSummary = result.toVariantMap();
                m_nasDataNodeStatusText = result.succeeded
                    ? QStringLiteral("节点变更检查完成：%1 个目录 · 变更 %2 · 最近 %3")
                          .arg(result.totalDirectories)
                          .arg(result.changedDates.size())
                          .arg(QDateTime::fromMSecsSinceEpoch(
                                   result.latestDirectoryAt)
                                   .toString(Qt::ISODate))
                    : QStringLiteral("节点变更检查失败");
                m_nasDataNodeError = result.error;
                emit nasDataNodeStateChanged();
            });
    connect(&m_nasDataNodeClient, &NasDataNodeClient::downloadProgress,
            this, [this](quint64 requestId, qint64 received, qint64 total) {
                if (!m_nasDownloadRequestGate.indexFor(requestId).has_value()) return;
                m_nasDataNodeReceivedBytes = received;
                m_nasDataNodeExpectedBytes = total > 0 ? total : m_nasDataNodeExpectedBytes;
                emit nasDataNodeStateChanged();
            });
    connect(&m_nasDataNodeClient, &NasDataNodeClient::downloadReady,
            this, [this](quint64 requestId, const NasDownloadResult &result) {
                const std::optional<int> acceptedIndex =
                    m_nasDownloadRequestGate.finish(requestId);
                if (!acceptedIndex) return;
                const int syncIndex = *acceptedIndex;
                if (m_nasActiveDownloadRequestId == requestId) {
                    m_nasActiveDownloadRequestId = 0;
                }
                m_nasDownloadStartedAtMilliseconds.remove(syncIndex);
                if (!result.succeeded) {
                    m_nasSyncQueue.markFailed(syncIndex, result.error);
                    persistNasSyncState();
                    m_nasDataNodeStatusText = QStringLiteral("NAS 交易日下载失败，继续下一项");
                    emit nasDataNodeStateChanged();
                    QTimer::singleShot(0, this, &TradingController::startNextNasSyncItem);
                    return;
                }
                const Instrument instrument = findInstrument(result.entry.symbol);
                if (instrument.symbol.isEmpty()) {
                    m_nasSyncQueue.markFailed(
                        syncIndex, QStringLiteral("NAS 品种不在已加载的 A 股股票池中"));
                    persistNasSyncState();
                    emit nasDataNodeStateChanged();
                    QTimer::singleShot(0, this, &TradingController::startNextNasSyncItem);
                    return;
                }
                if (!m_nasSyncQueue.markImporting(syncIndex, 0, result.fromCache)) {
                    m_nasDataNodeError = QStringLiteral("NAS 多日同步状态异常");
                    m_nasSyncQueue.cancelActiveAndPending();
                    finishNasSyncRun(true);
                    return;
                }
                persistNasSyncState();
                m_nasImportQueue.append(syncIndex);
                startNextNasImport();
                emit nasDataNodeStateChanged();
            });
    const int configuredConcurrency = qEnvironmentVariableIntValue(
        "PPTRADER_NAS_DOWNLOAD_CONCURRENCY");
    if (configuredConcurrency > 0 && configuredConcurrency <= 16) {
        m_nasMaxConcurrentDownloads = configuredConcurrency;
    }
    const QString tokenFilePath =
        qEnvironmentVariable("PPTRADER_NAS_TOKEN_FILE");
    if (!tokenFilePath.isEmpty()) {
        m_nasDataNodeClient.setBearerTokenProvider(
            [this, tokenFilePath]() {
                const QFileInfo info(tokenFilePath);
                const qint64 modified = info.exists()
                    ? info.lastModified().toMSecsSinceEpoch() : 0;
                if (modified != m_nasTokenFileLastModified) {
                    m_nasTokenFileLastModified = modified;
                    QFile file(tokenFilePath);
                    if (file.open(QIODevice::ReadOnly)) {
                        m_nasBearerToken = file.readAll().trimmed();
                    }
                }
                if (!m_nasBearerToken.isEmpty()) return m_nasBearerToken;
                return qgetenv("PPTRADER_NAS_TOKEN");
            });
    } else {
        m_nasDataNodeClient.setBearerToken(qgetenv("PPTRADER_NAS_TOKEN"));
    }
    const QString savedNasEndpoint = QSettings().value(
        QStringLiteral("nas/endpoint")).toString();
    QString nasEndpointError;
    if (!savedNasEndpoint.isEmpty()
        && m_nasDataNodeClient.setEndpoint(QUrl(savedNasEndpoint), &nasEndpointError)) {
        m_nasDataNodeEndpoint = m_nasDataNodeClient.endpoint().toString();
        m_nasDataNodeStatusText = QStringLiteral("NAS 数据节点已配置；尚未请求网络");
    }
    const QString nasStateRoot = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    m_nasSyncStateStore.setFilePath(
        QDir(nasStateRoot).filePath(QStringLiteral("nas-sync-state-v1.json")));
    m_nasTickCacheManager.setRootDirectory(QDir(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath(QStringLiteral("nas-tick-staging")));
    NasPersistedSyncState persistedNasState;
    QString persistedNasError;
    if (m_nasSyncStateStore.load(&persistedNasState, &persistedNasError)
        && !persistedNasState.items.isEmpty()
        && m_nasSyncQueue.restore(persistedNasState.items)) {
        m_nasManifestSymbols = persistedNasState.symbols;
        m_nasDataNodePlanRecovered = true;
        const NasSyncSummary summary = m_nasSyncQueue.summary();
        m_nasDataNodeStatusText = QStringLiteral(
            "已恢复 NAS 同步计划：成功 %1，待重试 %2")
            .arg(summary.succeeded).arg(summary.pending + summary.failed);
    } else if (!persistedNasError.isEmpty()) {
        m_nasDataNodeError = persistedNasError;
        m_nasDataNodeStatusText = QStringLiteral("NAS 同步恢复状态不可用");
    }
    m_crossSectionalEngine.addIndicator(
        std::make_unique<AShareRelativeStrengthIndicator>());

    m_instrumentModel.addInstrument({QStringLiteral("600519"), QStringLiteral("贵州茅台"), Market::AShare, QStringLiteral("上交所")});
    m_instrumentModel.addInstrument({QStringLiteral("300750"), QStringLiteral("宁德时代"), Market::AShare, QStringLiteral("深交所")});
    reloadAShareUniverse();

    const QVector<Instrument> instruments = buildWatchlist();
    if (!instruments.isEmpty()) {
        m_workspaceInstrumentSelections.setResearchInstrument(instruments.first());
        m_workspaceInstrumentSelections.selectLiveInstrument(instruments.first().symbol);
    }
    const QString databasePath = StorageProfile::configuredDatabasePath();
    if (m_dataStore.initialize(databasePath)) {
        m_storageReady = true;
        m_tickDataCsvImportService.setDatabasePath(databasePath);
        m_aShareInstrumentDayImportService.setDatabasePath(databasePath);
        m_aShareIndustryImportService.setDatabasePath(databasePath);
        m_authorizedFactorImportService.setDatabasePath(databasePath);
        m_externalFactorResearchService.setDatabasePath(databasePath);
        m_batchBacktestService.setDatabasePath(databasePath);
        m_persistedTickCount = m_dataStore.tickCount();
        m_persistedSignalCount = m_dataStore.signalCount();
        const TradingState tradingState = m_dataStore.loadTradingState();
        if (tradingState.hasAccountSnapshot) {
            m_tradingAccount.restoreTradingState(tradingState);
        } else {
            m_dataStore.saveTradingState(m_tradingAccount.snapshot(), m_tradingAccount.positions());
        }
        m_strategyIntentModel.clear();
        m_strategyExecutionCoordinator.restore();
        m_strategyIntentModel.loadIntents(m_strategyExecutionCoordinator.intents());
        reloadCustomStrategies();
        refreshLocalBacktestDataCatalog();
        refreshBacktestArchiveList();
        reloadTickDataImportRunHistory();
        refreshAShareInstrumentDayImportHistory();
        refreshAShareIndustryImportHistory();
        recoverBatchBacktestRuns();
        reloadRawTickParameterSensitivityHistory();
        refreshPortfolioBacktestArchiveList();
        refreshCrossSectionalBacktestArchiveList();
        m_crossSectionalFactorDefinitionCatalog.clear();
        for (const FactorDefinitionVersion &factor :
             m_dataStore.loadFactorDefinitionVersions()) {
            m_crossSectionalFactorDefinitionCatalog.append(factor.toVariantMap());
        }
        refreshFactorCandidateCatalog();
        refreshExternalFactorResearchArchives();
        refreshExternalFactorDatasetCatalog();
        if (!m_asyncSnapshotQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步市场快照查询服务启动失败"));
        }
        if (!m_asyncLocalCandleQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步本地分钟序列查询服务启动失败"));
        }
        if (!m_asyncLocalTickQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步本地逐笔序列查询服务启动失败"));
        }
        if (!m_asyncLiveDayCandleQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步真实交易日 K 线查询服务启动失败"));
        }
        if (!m_asyncCustomStrategyWorkspaceService.start(databasePath)) {
            setStatusText(QStringLiteral("异步自定义策略工作区服务启动失败"));
        }
        if (!m_asyncBacktestArchiveService.start(databasePath)) {
            setStatusText(QStringLiteral("异步回测归档服务启动失败"));
        }
        if (!m_backtestDatasetService.start(databasePath)) {
            setStatusText(QStringLiteral("回测数据集准备服务启动失败"));
        }
        if (!m_nasBacktestPreparationService.start(databasePath)) {
            setStatusText(QStringLiteral("NAS 回测批量准备服务启动失败"));
        }
        if (!m_portfolioBacktestService.start(databasePath)) {
            setStatusText(QStringLiteral("组合回测服务启动失败"));
        }
        if (!m_crossSectionalBacktestService.start(databasePath)) {
            setStatusText(QStringLiteral("多股回测服务启动失败"));
        }
        if (!m_rawTickSensitivityService.start(databasePath)) {
            setStatusText(QStringLiteral("原始 Tick 参数敏感性服务启动失败"));
        }
        emit storageStateChanged();
    }
    initializeRealtimeStrategies();
    if (m_storageReady) {
        requestLatestMarketSnapshot();
        m_validationRuns = m_dataStore.loadValidationRuns(10);
        emit validationRunsChanged();
    }

    m_marketClockTimer.setInterval(1000);
    m_marketClockTimer.start();
    m_searchModel.loadFromJson({});
    setBacktestStatus(QStringLiteral("未运行"));
    m_accountSnapshot = m_tradingAccount.snapshot();
    setStatusText(QStringLiteral("就绪"));
    if (qEnvironmentVariableIntValue("PPTRADER_AUTOSTART_LIVE") > 0) {
        QTimer::singleShot(0, this, &TradingController::startLive);
    }
}

TradingController::~TradingController()
{
    shutdown();
}

void TradingController::shutdown()
{
    if (m_shutdownRequested) return;
    m_shutdownRequested = true;
    m_marketClockTimer.stop();
    m_tradingPersistenceTimer.stop();
    m_customStrategyReplayTimer.stop();
    m_realtimeMinuteRefreshTimer.stop();
    m_realtimeChartWindowModel.shutdown();
    stopAllPaperStrategies();

    // Cancel producers before draining their worker queues.
    m_backtest.cancel();
    m_batchBacktestService.cancel();
    m_portfolioBacktestService.cancel();
    m_crossSectionalBacktestService.cancel();
    m_parameterGridBacktestService.cancel();
    m_rawTickSensitivityService.cancel();
    m_tickDataCsvImportService.cancel();
    m_aShareInstrumentDayImportService.cancel();
    m_aShareIndustryImportService.cancel();
    m_authorizedFactorImportService.cancel();
    m_externalFactorResearchService.cancel();
    m_realtimeMinuteService.cancel();
    m_tickDataBatchCancelRequested = true;
    m_backtestArchiveLoadRequestGate.cancel();
    m_backtestArchiveRawTickPageRequestGate.cancel();
    m_rawTickAvailabilityRequestGate.cancel();
    m_asyncRawTickAvailabilityService.cancel();
    if (m_nasDataNodeRequestVersion != 0)
        m_nasDataNodeClient.cancel(m_nasDataNodeRequestVersion);
    if (auto *composite = dynamic_cast<CompositeMarketDataProvider *>(m_marketData)) {
        composite->shutdown();
    } else if (m_marketData) {
        m_marketData->stop();
    }

    // These stop methods use bounded worker-thread waits. Calling them while
    // aboutToQuit still owns a live event loop avoids teardown-time deadlocks.
    m_asyncSnapshotQueryService.stop();
    m_asyncLocalCandleQueryService.stop();
    m_asyncLocalTickQueryService.stop();
    m_asyncLiveDayCandleQueryService.stop();
    m_asyncCustomStrategyWorkspaceService.stop();
    m_asyncBacktestArchiveService.stop();
    m_backtestDatasetService.stop();
    m_nasBacktestPreparationService.stop();
    m_portfolioBacktestService.stop();
    m_crossSectionalBacktestService.stop();
    m_rawTickSensitivityService.stop();
}

QAbstractItemModel *TradingController::tickModel() { return &m_tickModel; }
QAbstractItemModel *TradingController::candleModel() { return &m_candleModel; }
QAbstractItemModel *TradingController::historyCandleModel() { return &m_historyCandleModel; }
QAbstractItemModel *TradingController::signalModel() { return &m_signalModel; }
QAbstractItemModel *TradingController::strategyModel() { return &m_strategyModel; }
QAbstractItemModel *TradingController::strategyIntentModel() { return &m_strategyIntentModel; }
QAbstractItemModel *TradingController::customStrategyModel() { return &m_customStrategyModel; }
QAbstractItemModel *TradingController::searchModel() { return &m_searchModel; }
QAbstractItemModel *TradingController::instrumentModel() { return &m_instrumentModel; }
QAbstractItemModel *TradingController::backtestTradeModel() { return &m_backtestTradeModel; }
QAbstractItemModel *TradingController::equityCurveModel() { return &m_equityCurveModel; }
QAbstractItemModel *TradingController::backtestArchiveModel() { return &m_backtestArchiveModel; }
QAbstractItemModel *TradingController::backtestArchiveRawTickModel()
{
    return &m_backtestArchiveRawTickModel;
}
QAbstractItemModel *TradingController::batchBacktestModel() { return &m_batchBacktestModel; }
QAbstractItemModel *TradingController::orderModel() { return &m_orderModel; }
QAbstractItemModel *TradingController::executionModel() { return &m_executionModel; }
QAbstractItemModel *TradingController::positionModel() { return &m_positionModel; }
QAbstractItemModel *TradingController::marketSnapshotModel() { return &m_marketSnapshotModel; }
QAbstractItemModel *TradingController::localCandleSeriesModel() { return &m_localCandleSeriesModel; }
QAbstractItemModel *TradingController::localTickSeriesModel() { return &m_localTickSeriesModel; }
QAbstractItemModel *TradingController::liveDayCandleSeriesModel() { return &m_liveDayCandleSeriesModel; }
QAbstractItemModel *TradingController::realtimeMinuteCandleModel() { return &m_realtimeMinuteCandleModel; }
QAbstractItemModel *TradingController::realtimeChartWindowModel()
{
    return &m_realtimeChartWindowModel;
}
int TradingController::realtimeChartWindowCount() const
{
    return m_realtimeChartWindowModel.count();
}
QString TradingController::realtimeChartWindowError() const
{
    return m_realtimeChartWindowError;
}
QVariantList TradingController::backtestStrategyCatalog() const
{
    return m_backtestStrategyRegistry.catalog();
}
QVariantMap TradingController::customStrategySchema() const
{
    return m_customStrategyValidator.schemaDescriptor();
}
int TradingController::customStrategyCount() const
{
    return m_customStrategyModel.count();
}
QString TradingController::customStrategyError() const
{
    return m_customStrategyError;
}
bool TradingController::customStrategyPreviewLoading() const { return m_customStrategyPreviewRequestGate.loading(); }
qulonglong TradingController::customStrategyPreviewRequestId() const { return m_customStrategyPreviewRequestGate.currentRequestId(); }
qulonglong TradingController::customStrategyPreviewResolvedRequestId() const { return m_customStrategyPreviewRequestGate.resolvedRequestId(); }
QVariantMap TradingController::customStrategyPreviewResult() const { return m_customStrategyPreviewResult; }
QString TradingController::customStrategyPreviewError() const { return m_customStrategyPreviewError; }
bool TradingController::customStrategySaveLoading() const { return m_customStrategySaveRequestGate.loading(); }
qulonglong TradingController::customStrategySaveRequestId() const { return m_customStrategySaveRequestGate.currentRequestId(); }
qulonglong TradingController::customStrategySaveResolvedRequestId() const { return m_customStrategySaveRequestGate.resolvedRequestId(); }
QVariantMap TradingController::customStrategySaveResult() const { return m_customStrategySaveResult; }
QString TradingController::customStrategySaveError() const { return m_customStrategySaveError; }

bool TradingController::liveRunning() const
{
    return m_realtimeLiveSessionState.requested();
}
bool TradingController::backtestRunning() const { return m_backtestPreparing || m_backtest.isRunning(); }
bool TradingController::marketIsOpen() const
{
    return MarketSession::isOpen(m_workspaceInstrumentSelections.researchInstrument(),
                                 QDateTime::currentDateTimeUtc());
}
double TradingController::backtestProgress() const { return m_backtestProgress; }
QString TradingController::backtestStatusText() const { return m_backtestStatusText; }
QString TradingController::backtestError() const { return m_backtestError; }
double TradingController::backtestInitialCapital() const { return m_backtestSummary.initialCapital; }
double TradingController::finalEquity() const { return m_backtestSummary.finalEquity; }
double TradingController::totalReturnPercent() const { return m_backtestSummary.totalReturnPercent; }
double TradingController::maxDrawdownPercent() const { return m_backtestSummary.maxDrawdownPercent; }
double TradingController::winRatePercent() const { return m_backtestSummary.winRatePercent; }
double TradingController::sharpeRatio() const { return m_backtestSummary.sharpeRatio; }
double TradingController::totalCommission() const { return m_backtestSummary.totalCommission; }
int TradingController::backtestTradeCount() const { return m_backtestSummary.totalTrades; }
QString TradingController::selectedBacktestStrategyId() const
{
    return m_selectedBacktestStrategyId;
}
QVariantMap TradingController::selectedBacktestStrategyDescriptor() const
{
    return backtestStrategyDescriptor(m_selectedBacktestStrategyId);
}
QVariantMap TradingController::selectedBacktestParameters() const
{
    return m_selectedBacktestParameters;
}
QString TradingController::backtestFrequency() const { return m_backtestFrequency; }
int TradingController::backtestBarCount() const { return m_backtestBarCount; }
double TradingController::backtestConfiguredInitialCapital() const
{
    return m_backtestConfiguredInitialCapital;
}
double TradingController::backtestCommissionRate() const { return m_backtestCommissionRate; }
double TradingController::backtestSlippageRate() const { return m_backtestSlippageRate; }
QString TradingController::backtestSampleFromDate() const
{
    return m_workspaceInstrumentSelections.backtestSampleFromDate();
}

QString TradingController::backtestSampleToDate() const
{
    return m_workspaceInstrumentSelections.backtestSampleToDate();
}
QString TradingController::backtestConfigurationError() const
{
    return m_backtestConfigurationError;
}
QVariantMap TradingController::backtestResultConfiguration() const
{
    if (!m_displayedBacktestResult.has_value()) return {};
    QVariantMap result = backtestConfigMap(m_displayedBacktestResult->config);
    const BacktestPosition &finalPosition = m_displayedBacktestResult->finalPosition;
    result.insert(QStringLiteral("hasOpenPosition"), finalPosition.quantity > 0.0);
    result.insert(QStringLiteral("openPositionQuantity"), finalPosition.quantity);
    result.insert(QStringLiteral("openPositionAveragePrice"), finalPosition.averagePrice);
    result.insert(QStringLiteral("openPositionEntryCommission"),
                  finalPosition.entryCommission);
    result.insert(QStringLiteral("openPositionEntryTime"), finalPosition.entryTime);
    result.insert(QStringLiteral("openPositionEntryReason"), finalPosition.entryReason);
    result.insert(QStringLiteral("equityObservationCount"),
                  m_displayedBacktestResult->equityObservationCount);
    result.insert(QStringLiteral("equityDisplayPointCount"),
                  m_displayedBacktestResult->equityCurve.size());
    result.insert(QStringLiteral("equityCurveSampled"),
                  m_displayedBacktestResult->equityCurveSampled);
    const bool rawTickSnapshotLoaded =
        m_displayedBacktestResult->rawTickSnapshotLoaded
        || !m_displayedBacktestResult->rawTicks.isEmpty();
    result.insert(QStringLiteral("rawTickSnapshotAvailable"),
                  m_displayedBacktestResult->rawTickSnapshotAvailable
                      || rawTickSnapshotLoaded);
    result.insert(QStringLiteral("rawTickSnapshotLoaded"),
                  rawTickSnapshotLoaded);
    result.insert(QStringLiteral("rawTickSnapshotDeferred"),
                  m_displayedBacktestResult->rawTickSnapshotDeferred);
    result.insert(QStringLiteral("rawTickLoadedCount"),
                  m_displayedBacktestResult->rawTicks.size());
    result.insert(QStringLiteral("resultId"), m_selectedBacktestArchiveId);
    result.insert(QStringLiteral("completedAt"), m_displayedBacktestCompletedAt);
    result.insert(QStringLiteral("archived"), m_backtestResultArchived);
    result.insert(QStringLiteral("historicalSnapshot"), m_backtestResultHistoricalSnapshot);
    return result;
}
qulonglong TradingController::backtestTaskId() const { return m_backtestTaskId; }
qulonglong TradingController::completedBacktestTaskId() const
{
    return m_completedBacktestTaskId;
}
QString TradingController::backtestResultStrategyId() const
{
    return m_displayedBacktestResult.has_value()
        ? m_displayedBacktestResult->config.strategyId : QString{};
}
QString TradingController::backtestResultSymbol() const
{
    return m_displayedBacktestResult.has_value()
        ? m_displayedBacktestResult->config.instrument.symbol : QString{};
}
QDateTime TradingController::backtestResultCompletedAt() const
{
    return m_displayedBacktestCompletedAt;
}
int TradingController::backtestArchiveCount() const { return m_backtestArchiveModel.count(); }
QString TradingController::selectedBacktestArchiveId() const
{
    return m_selectedBacktestArchiveId;
}
bool TradingController::backtestArchiveLoading() const
{
    return m_backtestArchiveLoadRequestGate.loading();
}
qulonglong TradingController::backtestArchiveRequestId() const
{
    return m_backtestArchiveLoadRequestGate.currentRequestId();
}
qulonglong TradingController::backtestArchiveResolvedRequestId() const
{
    return m_backtestArchiveLoadRequestGate.resolvedRequestId();
}
QString TradingController::backtestArchiveError() const { return m_backtestArchiveError; }
QString TradingController::backtestArchiveSaveError() const { return m_backtestArchiveSaveError; }
bool TradingController::backtestResultArchived() const { return m_backtestResultArchived; }
bool TradingController::backtestResultHistoricalSnapshot() const
{
    return m_backtestResultHistoricalSnapshot;
}
bool TradingController::backtestArchiveRawTickPageLoading() const
{
    return m_backtestArchiveRawTickPageRequestGate.loading();
}
qulonglong TradingController::backtestArchiveRawTickPageRequestId() const
{
    return m_backtestArchiveRawTickPageRequestGate.currentRequestId();
}
qulonglong TradingController::backtestArchiveRawTickPageResolvedRequestId() const
{
    return m_backtestArchiveRawTickPageRequestGate.resolvedRequestId();
}
QString TradingController::backtestArchiveRawTickPageResultId() const
{
    return m_backtestArchiveRawTickPageResultId;
}
int TradingController::backtestArchiveRawTickPageOffset() const
{
    return m_backtestArchiveRawTickPageOffset;
}
int TradingController::backtestArchiveRawTickPageSize() const
{
    return m_backtestArchiveRawTickPageSize;
}
int TradingController::backtestArchiveRawTickTotalCount() const
{
    return m_backtestArchiveRawTickTotalCount;
}
bool TradingController::backtestArchiveRawTickHasPrevious() const
{
    return m_backtestArchiveRawTickPageOffset > 0;
}
bool TradingController::backtestArchiveRawTickHasNext() const
{
    return m_backtestArchiveRawTickHasNext;
}
QString TradingController::backtestArchiveRawTickPageError() const
{
    return m_backtestArchiveRawTickPageError;
}
bool TradingController::batchBacktestRunning() const
{
    return m_batchBacktestService.isRunning();
}
qulonglong TradingController::batchBacktestRequestId() const
{
    return m_batchBacktestRequestId;
}
int TradingController::batchBacktestRequestedCount() const
{
    return m_batchBacktestRequestedCount;
}
int TradingController::batchBacktestCompletedCount() const
{
    return m_batchBacktestCompletedCount;
}
int TradingController::batchBacktestSucceededCount() const
{
    return m_batchBacktestSucceededCount;
}
int TradingController::batchBacktestFailedCount() const
{
    return m_batchBacktestFailedCount;
}
double TradingController::batchBacktestProgress() const
{
    return m_batchBacktestRequestedCount > 0
        ? static_cast<double>(m_batchBacktestCompletedCount)
              / static_cast<double>(m_batchBacktestRequestedCount)
        : 0.0;
}
QString TradingController::batchBacktestStatusText() const
{
    return m_batchBacktestStatusText;
}
QString TradingController::batchBacktestError() const
{
    return m_batchBacktestError;
}
bool TradingController::batchBacktestArchiveLoading() const
{
    return m_batchBacktestArchiveRequestGate.loading();
}
qulonglong TradingController::batchBacktestArchiveRequestId() const
{
    return m_batchBacktestArchiveRequestGate.currentRequestId();
}
qulonglong TradingController::batchBacktestArchiveResolvedRequestId() const
{
    return m_batchBacktestArchiveRequestGate.resolvedRequestId();
}
QString TradingController::batchBacktestArchiveSymbol() const
{
    return m_batchBacktestArchiveSymbol;
}
QString TradingController::batchBacktestArchiveResultId() const
{
    return m_batchBacktestArchiveResultId;
}
QString TradingController::batchBacktestArchiveError() const
{
    return m_batchBacktestArchiveError;
}
bool TradingController::rawTickAvailabilityLoading() const
{
    return m_rawTickAvailabilityRequestGate.loading();
}
qulonglong TradingController::rawTickAvailabilityRequestId() const
{
    return m_rawTickAvailabilityRequestGate.currentRequestId();
}
qulonglong TradingController::rawTickAvailabilityResolvedRequestId() const
{
    return m_rawTickAvailabilityRequestGate.resolvedRequestId();
}
QVariantMap TradingController::rawTickAvailabilityResult() const
{
    return m_rawTickAvailabilityResult;
}
QString TradingController::rawTickAvailabilityError() const
{
    return m_rawTickAvailabilityError;
}
QString TradingController::brokerName() const { return m_broker.brokerName(); }
bool TradingController::brokerLoggedIn() const { return m_broker.isLoggedIn(); }
QString TradingController::tradingError() const { return m_tradingError; }
double TradingController::accountInitialEquity() const { return m_accountSnapshot.initialEquity; }
double TradingController::accountCash() const { return m_accountSnapshot.cash; }
double TradingController::accountAvailableCash() const
{
    return m_accountSnapshot.cash - m_accountSnapshot.frozenCash;
}
double TradingController::accountFrozenCash() const { return m_accountSnapshot.frozenCash; }
double TradingController::accountMarketValue() const { return m_accountSnapshot.marketValue; }
double TradingController::accountTotalEquity() const { return m_accountSnapshot.totalEquity; }
double TradingController::accountRealizedPnl() const { return m_accountSnapshot.realizedPnl; }
double TradingController::accountUnrealizedPnl() const { return m_accountSnapshot.unrealizedPnl; }
double TradingController::accountTotalCommission() const { return m_accountSnapshot.totalCommission; }
double TradingController::maxOrderValue() const { return m_tradingAccount.riskLimits().maxOrderValue; }
double TradingController::maxPositionValue() const { return m_tradingAccount.riskLimits().maxPositionValue; }
double TradingController::maxDailyLoss() const { return m_tradingAccount.riskLimits().maxDailyLoss; }
int TradingController::maxActiveOrders() const { return m_tradingAccount.riskLimits().maxActiveOrders; }
QString TradingController::statusText() const { return m_statusText; }
QString TradingController::stockSearchText() const { return m_stockSearchText; }

void TradingController::setStockSearchText(const QString &text)
{
    if (m_stockSearchText == text) return;
    m_stockSearchText = text;
    m_searchModel.setFilterText(text);
    emit stockSearchTextChanged();
}

QString TradingController::selectedInstrumentTitle() const
{
    const Instrument &instrument = m_workspaceInstrumentSelections.researchInstrument();
    return instrument.symbol.isEmpty()
        ? QStringLiteral("未选择标的")
        : instrumentTitle(instrument);
}

QString TradingController::selectedSymbol() const
{
    return m_workspaceInstrumentSelections.researchInstrument().symbol;
}

QString TradingController::selectedPriceUnit() const
{
    switch (m_workspaceInstrumentSelections.researchInstrument().market) {
    case Market::AShare: return QStringLiteral("人民币");
    case Market::USStock: return QStringLiteral("美元");
    case Market::Crypto: return QStringLiteral("USDT");
    case Market::HongKong: return QStringLiteral("港币");
    }
    return QStringLiteral("价格");
}

QString TradingController::selectedMarketSessionText() const
{
    return MarketSession::statusText(m_workspaceInstrumentSelections.researchInstrument(),
                                     QDateTime::currentDateTimeUtc());
}

QString TradingController::selectedLiveSymbol() const
{
    return m_workspaceInstrumentSelections.liveInstrument().symbol;
}

QString TradingController::selectedLiveInstrumentTitle() const
{
    return instrumentTitle(m_workspaceInstrumentSelections.liveInstrument());
}

QString TradingController::selectedBacktestSymbol() const
{
    return m_workspaceInstrumentSelections.backtestInstrument().symbol;
}

QString TradingController::selectedBacktestInstrumentTitle() const
{
    return instrumentTitle(m_workspaceInstrumentSelections.backtestInstrument());
}

bool TradingController::selectedBacktestDataAvailable() const
{
    return m_workspaceInstrumentSelections.selectedBacktestDataset() != nullptr;
}

QString TradingController::selectedBacktestFrequency() const
{
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    return dataset ? dataset->frequency() : QString{};
}

int TradingController::selectedBacktestPeriodSeconds() const
{
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    return dataset ? dataset->periodSeconds : 0;
}

int TradingController::selectedBacktestCandleCount() const
{
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    return dataset ? dataset->candleCount : 0;
}

QDateTime TradingController::selectedBacktestFirstTime() const
{
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    return dataset ? dataset->firstTime : QDateTime{};
}

QDateTime TradingController::selectedBacktestLastTime() const
{
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    return dataset ? dataset->lastTime : QDateTime{};
}

QString TradingController::backtestInstrumentSelectionError() const
{
    return m_backtestInstrumentSelectionError;
}

QVariantList TradingController::localBacktestDataCatalog() const
{
    QVariantList result;
    const auto &catalog = m_workspaceInstrumentSelections.localHistoryCatalog();
    result.reserve(catalog.size());
    for (const LocalHistoryDataset &item : catalog) {
        if (item.isBacktestEligible()) result.append(item.toVariantMap());
    }
    return result;
}

QVariantList TradingController::localHistoryDataCatalog() const
{
    QVariantList result;
    const auto &catalog = m_workspaceInstrumentSelections.localHistoryCatalog();
    result.reserve(catalog.size());
    for (const LocalHistoryDataset &item : catalog) result.append(item.toVariantMap());
    return result;
}

bool TradingController::localCandleSeriesLoading() const { return m_localCandleSeriesRequestGate.loading(); }
qulonglong TradingController::localCandleSeriesRequestId() const { return m_localCandleSeriesRequestGate.currentRequestId(); }
qulonglong TradingController::localCandleSeriesResolvedRequestId() const { return m_localCandleSeriesRequestGate.resolvedRequestId(); }
QString TradingController::localCandleSeriesSymbol() const { return m_localCandleSeriesSymbol; }
QString TradingController::localCandleSeriesFrequency() const { return m_localCandleSeriesFrequency; }
int TradingController::localCandleSeriesPeriodSeconds() const { return m_localCandleSeriesPeriodSeconds; }
int TradingController::localCandleSeriesPage() const { return m_localCandleSeriesPage; }
int TradingController::localCandleSeriesPageCount() const { return m_localCandleSeriesPageCount; }
int TradingController::localCandleSeriesPageSize() const { return m_localCandleSeriesPageSize; }
bool TradingController::localCandleSeriesHasOlderData() const
{
    return m_localCandleSeriesPageCount > 0
        && m_localCandleSeriesPage + 1 < m_localCandleSeriesPageCount;
}
int TradingController::localCandleSeriesTotalBars() const { return m_localCandleSeriesTotalBars; }
int TradingController::localCandleSeriesRawBars() const { return m_localCandleSeriesRawBars; }
int TradingController::localCandleSeriesDisplayPoints() const { return m_localCandleSeriesModel.count(); }
int TradingController::localCandleSeriesBucketSize() const { return m_localCandleSeriesBucketSize; }
bool TradingController::localCandleSeriesDownsampled() const { return m_localCandleSeriesDownsampled; }
QDateTime TradingController::localCandleSeriesFirstTime() const { return m_localCandleSeriesFirstTime; }
QDateTime TradingController::localCandleSeriesLastTime() const { return m_localCandleSeriesLastTime; }
QString TradingController::localCandleSeriesStatusText() const { return m_localCandleSeriesStatusText; }
QString TradingController::localCandleSeriesError() const { return m_localCandleSeriesError; }
bool TradingController::localTickSeriesLoading() const { return m_localTickSeriesRequestGate.loading(); }
qulonglong TradingController::localTickSeriesRequestId() const { return m_localTickSeriesRequestGate.currentRequestId(); }
qulonglong TradingController::localTickSeriesResolvedRequestId() const { return m_localTickSeriesRequestGate.resolvedRequestId(); }
QString TradingController::localTickSeriesSymbol() const { return m_localTickSeriesSymbol; }
QDate TradingController::localTickSeriesTradingDate() const { return m_localTickSeriesTradingDate; }
qint64 TradingController::localTickSeriesRawCount() const { return m_localTickSeriesRawCount; }
int TradingController::localTickSeriesDisplayCount() const { return m_localTickSeriesModel.count(); }
bool TradingController::localTickSeriesDownsampled() const { return m_localTickSeriesDownsampled; }
QString TradingController::localTickSeriesStatusText() const { return m_localTickSeriesStatusText; }
QString TradingController::localTickSeriesError() const { return m_localTickSeriesError; }
bool TradingController::liveDayCandleSeriesLoading() const { return m_liveDayCandleSeriesRequestGate.loading(); }
QString TradingController::liveDayCandleSeriesStatusText() const { return m_liveDayCandleSeriesStatusText; }
QString TradingController::liveDayCandleSeriesError() const { return m_liveDayCandleSeriesError; }
int TradingController::liveDayCandleSeriesPeriodSeconds() const { return m_liveDayCandleSeriesPeriodSeconds; }
QDate TradingController::liveDayCandleSeriesTradingDate() const { return m_liveDayCandleSeriesTradingDate; }
int TradingController::liveDayCandleSeriesCount() const { return m_liveDayCandleSeriesModel.count(); }
bool TradingController::realtimeMinuteLoading() const { return m_realtimeMinuteRequestGate.loading(); }
QString TradingController::realtimeMinuteStatusText() const { return m_realtimeMinuteStatusText; }
QString TradingController::realtimeMinuteError() const { return m_realtimeMinuteError; }
QString TradingController::realtimeMinuteSymbol() const { return m_realtimeMinuteSymbol; }
QString TradingController::realtimeMinuteSource() const { return m_realtimeMinuteSource; }
int TradingController::realtimeMinutePeriodSeconds() const
{
    return m_realtimeMinutePeriodSeconds;
}
QVariantList TradingController::realtimeMinutePeriodOptions() const
{
    QVariantList result;
    for (const int seconds : {60, 300, 900, 1800, 3600}) {
        result.append(QVariantMap{
            {QStringLiteral("label"), localHistoryFrequency(seconds)},
            {QStringLiteral("periodSeconds"), seconds}});
    }
    return result;
}
int TradingController::realtimeMinuteCount() const { return m_realtimeMinuteCandleModel.count(); }
QDateTime TradingController::realtimeMinuteLastBarTime() const { return m_realtimeMinuteLastBarTime; }
bool TradingController::realtimeMinuteStale() const { return m_realtimeMinuteStale; }
qulonglong TradingController::realtimeMinuteRequestId() const
{
    return m_realtimeMinuteRequestGate.currentRequestId();
}
qulonglong TradingController::realtimeMinuteResolvedRequestId() const
{
    return m_realtimeMinuteRequestGate.resolvedRequestId();
}

QString TradingController::historyDownloadSymbol() const
{
    return m_historyDownloadInstrument.symbol;
}

QString TradingController::historyDownloadInstrumentTitle() const
{
    return instrumentTitle(m_historyDownloadInstrument);
}

QString TradingController::historyDownloadFrequency() const
{
    return m_historyDownloadFrequency;
}

int TradingController::historyDownloadTargetBars() const
{
    return m_historyDownloadTargetBars;
}

bool TradingController::historyDownloadRunning() const
{
    return m_historyDownloadRunning;
}

qulonglong TradingController::historyDownloadRequestVersion() const
{
    return m_historyDownloadRequestVersion;
}

QString TradingController::historyDownloadSource() const
{
    return m_historyDownloadSource;
}

QVariantList TradingController::historicalProviderCapabilities() const
{
    return m_historyDownloadRouter.providerCapabilities();
}

QString TradingController::historyDownloadProvider() const
{
    return m_historyDownloadProvider;
}

QString TradingController::historyDownloadAdjustment() const
{
    return m_historyDownloadAdjustment;
}

QString TradingController::historyDownloadDataVersion() const
{
    return m_historyDownloadDataVersion;
}

QDateTime TradingController::historyDownloadDownloadedAt() const
{
    return m_historyDownloadDownloadedAt;
}

bool TradingController::historyDownloadFallbackUsed() const
{
    return m_historyDownloadFallbackUsed;
}

int TradingController::historyDownloadStoredCount() const
{
    return m_historyDownloadStoredCount;
}

int TradingController::historyDownloadAvailableCount() const
{
    return m_historyDownloadAvailableCount;
}

QDateTime TradingController::historyDownloadFirstTime() const
{
    return m_historyDownloadFirstTime;
}

QDateTime TradingController::historyDownloadLastTime() const
{
    return m_historyDownloadLastTime;
}

QString TradingController::historyDownloadStatusText() const
{
    return m_historyDownloadStatusText;
}

QString TradingController::historyDownloadError() const
{
    return m_historyDownloadError;
}
QString TradingController::tickDataImportRootDirectory() const
{
    return m_tickDataImportRootDirectory;
}
QString TradingController::tickDataImportSymbol() const
{
    return m_tickDataImportInstrument.symbol;
}
QString TradingController::tickDataImportInstrumentTitle() const
{
    return instrumentTitle(m_tickDataImportInstrument);
}
QString TradingController::tickDataImportTradingDate() const
{
    return m_tickDataImportTradingDate;
}
int TradingController::tickDataImportFormat() const { return m_tickDataImportFormat; }
int TradingController::tickDataImportPeriodSeconds() const
{
    return m_tickDataImportPeriodSeconds;
}
int TradingController::tickDataImportFiveMinuteVolumeUnit() const
{
    return m_tickDataImportFiveMinuteVolumeUnit;
}
bool TradingController::tickDataImportRunning() const { return m_tickDataImportRunning; }
qulonglong TradingController::tickDataImportRequestId() const
{
    return m_tickDataImportRequestId;
}
QString TradingController::tickDataImportFilePath() const { return m_tickDataImportFilePath; }
QString TradingController::tickDataImportSource() const { return m_tickDataImportSource; }
qint64 TradingController::tickDataImportLinesRead() const { return m_tickDataImportLinesRead; }
qint64 TradingController::tickDataImportValidRecords() const
{
    return m_tickDataImportValidRecords;
}
qint64 TradingController::tickDataImportSkippedRecords() const
{
    return m_tickDataImportSkippedRecords;
}
int TradingController::tickDataImportWrittenBars() const { return m_tickDataImportWrittenBars; }
qint64 TradingController::tickDataImportRawTicksWritten() const { return m_tickDataImportRawTicksWritten; }
QDateTime TradingController::tickDataImportFirstTime() const
{
    return m_tickDataImportFirstTime;
}
QDateTime TradingController::tickDataImportLastTime() const
{
    return m_tickDataImportLastTime;
}
QVariantList TradingController::tickDataImportDiagnostics() const
{
    return m_tickDataImportDiagnostics;
}
QString TradingController::tickDataImportStatusText() const
{
    return m_tickDataImportStatusText;
}
QString TradingController::tickDataImportError() const { return m_tickDataImportError; }
QString TradingController::aShareInstrumentDayImportFilePath() const
{
    return m_aShareInstrumentDayImportFilePath;
}
QString TradingController::aShareInstrumentDayImportSource() const
{
    return m_aShareInstrumentDayImportSource;
}
QString TradingController::aShareInstrumentDayImportSourceVersion() const
{
    return m_aShareInstrumentDayImportSourceVersion;
}
bool TradingController::aShareInstrumentDayImportRunning() const
{
    return m_aShareInstrumentDayImportRunning;
}
qulonglong TradingController::aShareInstrumentDayImportRequestId() const
{
    return m_aShareInstrumentDayImportRequestId;
}
qint64 TradingController::aShareInstrumentDayImportLinesRead() const
{
    return m_aShareInstrumentDayImportLinesRead;
}
qint64 TradingController::aShareInstrumentDayImportValidRecords() const
{
    return m_aShareInstrumentDayImportValidRecords;
}
qint64 TradingController::aShareInstrumentDayImportWrittenRecords() const
{
    return m_aShareInstrumentDayImportWrittenRecords;
}
qint64 TradingController::aShareInstrumentDayImportNewRecords() const
{
    return m_aShareInstrumentDayImportNewRecords;
}
qint64 TradingController::aShareInstrumentDayImportUnchangedRecords() const
{
    return m_aShareInstrumentDayImportUnchangedRecords;
}
qint64 TradingController::aShareInstrumentDayImportCorrectedRecords() const
{
    return m_aShareInstrumentDayImportCorrectedRecords;
}
bool TradingController::aShareInstrumentDayImportPreviewReady() const
{
    return m_aShareInstrumentDayImportPreviewReady;
}
bool TradingController::aShareInstrumentDayImportReused() const
{
    return m_aShareInstrumentDayImportReused;
}
QString TradingController::aShareInstrumentDayImportContentSha256() const
{
    return m_aShareInstrumentDayImportContentSha256;
}
QString TradingController::aShareInstrumentDayImportDateRange() const
{
    if (!m_aShareInstrumentDayImportFirstDate.isValid()
        || !m_aShareInstrumentDayImportLastDate.isValid()) return {};
    return QStringLiteral("%1 至 %2")
        .arg(m_aShareInstrumentDayImportFirstDate.toString(Qt::ISODate),
             m_aShareInstrumentDayImportLastDate.toString(Qt::ISODate));
}
QVariantList TradingController::aShareInstrumentDayImportDiagnostics() const
{
    return m_aShareInstrumentDayImportDiagnostics;
}
QVariantList TradingController::aShareInstrumentDayImportHistory() const
{
    return m_aShareInstrumentDayImportHistory;
}
QString TradingController::aShareInstrumentDayImportStatusText() const
{
    return m_aShareInstrumentDayImportStatusText;
}
QString TradingController::aShareInstrumentDayImportError() const
{
    return m_aShareInstrumentDayImportError;
}
QVariantMap TradingController::aShareIndustryImportState() const
{
    return m_aShareIndustryImportState;
}
QVariantList TradingController::aShareIndustryImportHistory() const
{
    return m_aShareIndustryImportHistory;
}
QVariantList TradingController::tickDataBatchPlan() const
{
    QVariantList result;
    result.reserve(m_tickDataBatchPlan.size());
    for (const TickDataBatchPlanItem &item : m_tickDataBatchPlan) {
        result.append(item.toVariantMap());
    }
    return result;
}
QVariantMap TradingController::tickDataBatchSummary() const
{
    return m_tickDataBatchSummary.toVariantMap();
}
QVariantList TradingController::tickDataImportRunHistory() const
{
    return m_tickDataImportRunHistory;
}
bool TradingController::tickDataBatchPlanReady() const { return m_tickDataBatchPlanReady; }
bool TradingController::tickDataBatchRunning() const { return m_tickDataBatchRunning; }
double TradingController::tickDataBatchProgress() const
{
    return m_tickDataBatchSummary.totalFiles > 0
        ? static_cast<double>(m_tickDataBatchSummary.completedFiles)
            / m_tickDataBatchSummary.totalFiles
        : 0.0;
}
QString TradingController::tickDataBatchCurrentDate() const
{
    return m_tickDataBatchCurrentDate;
}
QString TradingController::tickDataBatchStatusText() const
{
    return m_tickDataBatchStatusText;
}
QString TradingController::tickDataBatchError() const { return m_tickDataBatchError; }

QString TradingController::storagePath() const { return m_dataStore.databasePath(); }
QString TradingController::databaseUuid() const { return m_dataStore.databaseUuid(); }
int TradingController::databaseSchemaVersion() const { return m_dataStore.schemaVersion(); }
bool TradingController::storageRelocationRunning() const
{
    return m_storageRelocationRunning;
}
QString TradingController::storageRelocationError() const
{
    return m_storageRelocationError;
}
QString TradingController::nasDataNodeEndpoint() const { return m_nasDataNodeEndpoint; }
bool TradingController::nasDataNodeLoading() const { return m_nasDataNodeLoading; }
bool TradingController::nasDataNodeSyncRunning() const { return m_nasDataNodeSyncRunning; }
qulonglong TradingController::nasDataNodeRequestVersion() const
{
    return m_nasDataNodeRequestVersion;
}
QVariantList TradingController::nasDataNodeCatalog() const
{
    QVariantList result;
    result.reserve(m_nasCatalogEntries.size());
    for (const NasCatalogEntry &entry : m_nasCatalogEntries)
        result.append(entry.toVariantMap());
    return result;
}
QVariantList TradingController::nasDataNodeSyncPlan() const
{
    QVariantList result;
    result.reserve(m_nasSyncQueue.items().size());
    for (const NasSyncPlanItem &item : m_nasSyncQueue.items())
        result.append(item.toVariantMap());
    return result;
}
QVariantMap TradingController::nasDataNodeSyncSummary() const
{
    QVariantMap result = m_nasSyncQueue.summary().toVariantMap();
    result.insert(QStringLiteral("elapsedMilliseconds"),
                  m_nasDataNodeSyncRunning && m_nasSyncTimer.isValid()
                      ? m_nasSyncTimer.elapsed() : m_nasSyncElapsedMilliseconds);
    return result;
}
QVariantMap TradingController::nasDataNodeManifestSummary() const
{
    return {{QStringLiteral("requestedSymbols"), m_nasManifestRequestedSymbols},
            {QStringLiteral("availableSymbols"), m_nasManifestAvailableSymbols},
            {QStringLiteral("missingSymbols"), m_nasManifestMissingSymbols},
            {QStringLiteral("entries"), m_nasCatalogEntries.size()},
            {QStringLiteral("totalBytes"), m_nasManifestTotalBytes},
            {QStringLiteral("elapsedMilliseconds"), m_nasManifestElapsedMilliseconds}};
}
QVariantMap TradingController::nasDataNodeCacheSummary() const
{
    return m_nasDataNodeCacheSummary;
}

QVariantMap TradingController::nasNodeChangesSummary() const
{
    return m_nasNodeChangesSummary;
}
bool TradingController::nasDataNodePlanRecovered() const
{
    return m_nasDataNodePlanRecovered;
}
double TradingController::nasDataNodeSyncProgress() const
{
    const NasSyncSummary summary = m_nasSyncQueue.summary();
    return summary.total > 0
        ? static_cast<double>(summary.completed) / summary.total : 0.0;
}
QString TradingController::nasDataNodeSyncCurrentDate() const
{
    const int index = m_nasSyncQueue.activeIndex();
    return index >= 0 && index < m_nasSyncQueue.items().size()
        ? m_nasSyncQueue.items().at(index).entry.tradingDate.toString(Qt::ISODate)
        : QString{};
}
QString TradingController::nasDataNodeSyncCurrentSymbol() const
{
    const int index = m_nasSyncQueue.activeIndex();
    return index >= 0 && index < m_nasSyncQueue.items().size()
        ? m_nasSyncQueue.items().at(index).entry.symbol : QString{};
}
bool TradingController::nasDataNodeSyncCanResume() const
{
    return m_nasSyncQueue.canResume();
}
qint64 TradingController::nasDataNodeReceivedBytes() const
{
    return m_nasDataNodeReceivedBytes;
}
qint64 TradingController::nasDataNodeExpectedBytes() const
{
    return m_nasDataNodeExpectedBytes;
}
QString TradingController::nasDataNodeStatusText() const { return m_nasDataNodeStatusText; }
QString TradingController::nasDataNodeError() const { return m_nasDataNodeError; }
QVariantMap TradingController::nasBacktestPreparationSummary() const
{
    const double progress = m_nasBacktestPreparationTotalSymbols > 0
        ? double(m_nasBacktestPreparationCompletedSymbols)
              / double(m_nasBacktestPreparationTotalSymbols)
        : 0.0;
    return {
        {QStringLiteral("running"), m_nasBacktestPreparationRunning},
        {QStringLiteral("requestId"),
         qulonglong(m_nasBacktestPreparationRequestId)},
        {QStringLiteral("periodSeconds"), m_nasBacktestPeriodSeconds},
        {QStringLiteral("minimumBars"), m_nasBacktestMinimumBars},
        {QStringLiteral("completedSymbols"),
         m_nasBacktestPreparationCompletedSymbols},
        {QStringLiteral("totalSymbols"), m_nasBacktestPreparationTotalSymbols},
        {QStringLiteral("preparedSymbols"), m_nasBacktestPreparedSymbols},
        {QStringLiteral("preparedCount"), m_nasBacktestPreparedSymbols.size()},
        {QStringLiteral("failedSymbols"), m_nasBacktestPreparationFailures},
        {QStringLiteral("failedCount"), m_nasBacktestPreparationFailures.size()},
        {QStringLiteral("currentSymbol"),
         m_nasBacktestPreparationCurrentSymbol},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("elapsedMilliseconds"),
         m_nasBacktestPreparationElapsedMilliseconds},
        {QStringLiteral("statusText"), m_nasBacktestPreparationStatusText}
    };
}

bool TradingController::configureNasDataNode(const QString &endpoint)
{
    if (m_nasDataNodeLoading || m_nasDataNodeSyncRunning
        || m_nasBacktestPreparationRunning) {
        m_nasDataNodeError = QStringLiteral("请先等待 NAS 查询结束或取消同步任务");
        emit nasDataNodeStateChanged();
        return false;
    }
    QString error;
    const QUrl url(endpoint.trimmed());
    if (!m_nasDataNodeClient.setEndpoint(url, &error)) {
        m_nasDataNodeError = error;
        m_nasDataNodeStatusText = QStringLiteral("NAS 数据节点配置失败");
        emit nasDataNodeStateChanged();
        return false;
    }
    m_nasDataNodeEndpoint = m_nasDataNodeClient.endpoint().toString();
    QSettings settings;
    settings.setValue(QStringLiteral("nas/endpoint"), m_nasDataNodeEndpoint);
    settings.sync();
    m_nasCatalogEntries.clear();
    m_nasManifestSymbols.clear();
    m_nasSyncQueue = NasTickSyncQueue{};
    clearNasSyncState();
    m_nasManifestRequestedSymbols = 0;
    m_nasManifestAvailableSymbols = 0;
    m_nasManifestMissingSymbols = 0;
    m_nasManifestTotalBytes = 0;
    m_nasManifestElapsedMilliseconds = 0;
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("NAS 数据节点已配置；尚未请求网络");
    emit nasDataNodeStateChanged();
    return true;
}

qulonglong TradingController::requestNasNodeChanges()
{
    if (m_nasDataNodeEndpoint.isEmpty()) {
        m_nasDataNodeError = QStringLiteral("尚未配置 NAS 数据节点");
        emit nasDataNodeStateChanged();
        return 0;
    }
    if (m_nasDataNodeLoading || m_nasDataNodeSyncRunning
        || m_nasBacktestPreparationRunning) {
        m_nasDataNodeError = QStringLiteral("请先等待 NAS 查询结束或取消同步任务");
        emit nasDataNodeStateChanged();
        return 0;
    }
    ++m_nasDataNodeRequestVersion;
    if (m_nasDataNodeRequestVersion == 0) ++m_nasDataNodeRequestVersion;
    m_nasDataNodeLoading = true;
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("正在检查 NAS 节点变更…");
    m_nasNodeChangesSummary.clear();
    if (!m_nasDataNodeClient.requestChanges(m_nasDataNodeRequestVersion)) {
        m_nasDataNodeLoading = false;
        m_nasDataNodeError = QStringLiteral("NAS 变更检查请求未能启动");
        m_nasDataNodeStatusText = QStringLiteral("NAS 变更检查失败");
        emit nasDataNodeStateChanged();
        return 0;
    }
    emit nasDataNodeStateChanged();
    return m_nasDataNodeRequestVersion;
}

qulonglong TradingController::requestNasTickCatalog(
    const QString &symbol, const QString &startDate, const QString &endDate)
{
    const Instrument instrument = findInstrument(symbol.trimmed());
    const QDate first = QDate::fromString(startDate, Qt::ISODate);
    const QDate last = QDate::fromString(endDate, Qt::ISODate);
    ++m_nasDataNodeRequestVersion;
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare
        || !first.isValid() || !last.isValid() || first > last
        || first.daysTo(last) > 366 || m_nasDataNodeEndpoint.isEmpty()
        || m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning) {
        m_nasDataNodeLoading = false;
        m_nasDataNodeError = QStringLiteral(
            "请选择股票池中的 A 股、有效起止日和已配置的 NAS 节点；范围最多 367 天");
        emit nasDataNodeStateChanged();
        return 0;
    }
    if (m_nasActiveCatalogRequestId != 0)
        m_nasDataNodeClient.cancel(m_nasActiveCatalogRequestId);
    m_nasActiveCatalogRequestId = m_nasDataNodeRequestVersion;
    m_nasManifestSymbols = {instrument.symbol};
    m_nasManifestRequestedSymbols = 1;
    m_nasManifestAvailableSymbols = 0;
    m_nasManifestMissingSymbols = 0;
    m_nasManifestTotalBytes = 0;
    m_nasManifestElapsedMilliseconds = 0;
    m_nasManifestTimer.restart();
    m_nasDataNodeLoading = true;
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("正在查询 NAS catalog");
    m_nasCatalogEntries.clear();
    if (!m_nasDataNodeClient.requestCatalog(
            m_nasActiveCatalogRequestId, instrument.symbol, first, last)) {
        m_nasDataNodeLoading = false;
        m_nasDataNodeError = QStringLiteral("NAS catalog 请求未能启动");
        emit nasDataNodeStateChanged();
        return 0;
    }
    emit nasDataNodeStateChanged();
    return m_nasDataNodeRequestVersion;
}

qulonglong TradingController::requestNasTickManifest(
    const QString &symbolsText, const QString &startDate, const QString &endDate)
{
    const QDate first = QDate::fromString(startDate, Qt::ISODate);
    const QDate last = QDate::fromString(endDate, Qt::ISODate);
    const QStringList candidates = symbolsText.split(
        QRegularExpression(QStringLiteral("[\\s,;，；]+")), Qt::SkipEmptyParts);
    QStringList symbols;
    QSet<QString> unique;
    int rejected = 0;
    for (const QString &candidate : candidates) {
        const QString symbol = candidate.trimmed();
        if (unique.contains(symbol)) continue;
        const Instrument instrument = findInstrument(symbol);
        if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
            ++rejected;
            continue;
        }
        unique.insert(symbol);
        symbols.append(symbol);
    }
    ++m_nasDataNodeRequestVersion;
    if (symbols.isEmpty() || symbols.size() > 6000 || rejected > 0
        || !first.isValid() || !last.isValid() || first > last
        || first.daysTo(last) > 30 || m_nasDataNodeEndpoint.isEmpty()
        || m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning) {
        m_nasDataNodeLoading = false;
        m_nasDataNodeError = rejected > 0
            ? QStringLiteral("代码清单含 %1 个不在当前 A 股股票池中的值").arg(rejected)
            : QStringLiteral(
                "请输入 1–6000 个股票池 A 股代码、有效日期和已配置节点；manifest 范围最多 31 天");
        emit nasDataNodeStateChanged();
        return 0;
    }
    if (m_nasActiveCatalogRequestId != 0)
        m_nasDataNodeClient.cancel(m_nasActiveCatalogRequestId);
    m_nasActiveCatalogRequestId = m_nasDataNodeRequestVersion;
    m_nasManifestSymbols = symbols;
    m_nasManifestRequestedSymbols = symbols.size();
    m_nasManifestAvailableSymbols = 0;
    m_nasManifestMissingSymbols = 0;
    m_nasManifestTotalBytes = 0;
    m_nasManifestElapsedMilliseconds = 0;
    m_nasManifestTimer.restart();
    m_nasDataNodeLoading = true;
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("正在查询 NAS 受控 manifest：%1 个代码")
                                  .arg(symbols.size());
    m_nasCatalogEntries.clear();
    m_nasSyncQueue = NasTickSyncQueue{};
    if (!m_nasDataNodeClient.requestManifest(
            m_nasActiveCatalogRequestId, symbols, first, last)) {
        m_nasDataNodeLoading = false;
        m_nasDataNodeError = QStringLiteral("NAS manifest 请求未能启动");
        emit nasDataNodeStateChanged();
        return 0;
    }
    emit nasDataNodeStateChanged();
    return m_nasDataNodeRequestVersion;
}

qulonglong TradingController::syncNasTickDay(
    const QString &symbol, const QString &tradingDate)
{
    const QDate date = QDate::fromString(tradingDate, Qt::ISODate);
    const auto it = std::find_if(m_nasCatalogEntries.cbegin(), m_nasCatalogEntries.cend(),
        [&symbol, &date](const NasCatalogEntry &entry) {
            return entry.symbol == symbol.trimmed() && entry.tradingDate == date;
        });
    ++m_nasDataNodeRequestVersion;
    if (it == m_nasCatalogEntries.cend()) {
        m_nasDataNodeError = QStringLiteral(
            "该交易日不在当前 catalog 中");
        emit nasDataNodeStateChanged();
        return 0;
    }
    return startNasSyncPlan(QVector<NasCatalogEntry>{*it}, symbol.trimmed())
        ? m_nasDataNodeRequestVersion : 0;
}

qulonglong TradingController::syncNasTickRange(const QString &symbol)
{
    ++m_nasDataNodeRequestVersion;
    return startNasSyncPlan(m_nasCatalogEntries, symbol.trimmed())
        ? m_nasDataNodeRequestVersion : 0;
}

qulonglong TradingController::syncNasTickManifest()
{
    ++m_nasDataNodeRequestVersion;
    if (m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning
        || m_tickDataImportRunning || m_tickDataBatchRunning
        || !m_storageReady || m_storageRelocationRunning
        || !m_nasSyncQueue.prepareManifest(m_nasCatalogEntries, m_nasManifestSymbols)
        || !m_nasSyncQueue.start()) {
        m_nasDataNodeError = QStringLiteral(
            "当前 manifest 没有可同步文件，或本地存储/导入任务正忙");
        emit nasDataNodeStateChanged();
        return 0;
    }
    m_nasDataNodeSyncRunning = true;
    m_nasSyncElapsedMilliseconds = 0;
    m_nasSyncTimer.restart();
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("NAS 多代码受控同步已启动：%1 个文件")
                                  .arg(m_nasSyncQueue.items().size());
    persistNasSyncState();
    emit nasDataNodeStateChanged();
    startNextNasSyncItem();
    return m_nasDataNodeRequestVersion;
}

bool TradingController::startNasSyncPlan(const QVector<NasCatalogEntry> &entries,
                                         const QString &symbol)
{
    if (m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning
        || m_tickDataImportRunning || m_tickDataBatchRunning
        || !m_storageReady || m_storageRelocationRunning
        || !m_nasSyncQueue.prepare(entries, symbol) || !m_nasSyncQueue.start()) {
        m_nasDataNodeError = QStringLiteral(
            "当前 catalog 没有该代码的交易日，或本地存储/导入任务正忙");
        emit nasDataNodeStateChanged();
        return false;
    }
    m_nasDataNodeSyncRunning = true;
    m_nasSyncElapsedMilliseconds = 0;
    m_nasSyncTimer.restart();
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("NAS 多日同步队列已启动");
    persistNasSyncState();
    emit nasDataNodeStateChanged();
    startNextNasSyncItem();
    return true;
}

void TradingController::startNextNasSyncItem()
{
    if (!m_nasDataNodeSyncRunning || !m_nasSyncQueue.running()) {
        finishNasSyncRun(false);
        return;
    }
    const int slotCount = std::max(1, m_nasMaxConcurrentDownloads)
        - m_nasSyncQueue.downloadingCount();
    const QVector<int> batch = m_nasSyncQueue.takeReadyBatch(slotCount);
    for (int index : batch) launchNasDownload(index);
    if (m_nasSyncQueue.downloadingCount() == 0
        && m_nasImportQueue.isEmpty() && m_nasImportingIndex < 0) {
        finishNasSyncRun(false);
    }
}

void TradingController::launchNasDownload(int index)
{
    const NasCatalogEntry entry = m_nasSyncQueue.items().at(index).entry;
    persistNasSyncState();
    quint64 requestId = ++m_nasDownloadRequestCounter;
    if (requestId == 0) requestId = ++m_nasDownloadRequestCounter;
    if (!m_nasDownloadRequestGate.begin(requestId, index)) {
        m_nasSyncQueue.markFailed(
            index, QStringLiteral("NAS 下载请求标识冲突，未启动下载"));
        persistNasSyncState();
        QTimer::singleShot(0, this, &TradingController::startNextNasSyncItem);
        return;
    }
    m_nasDownloadStartedAtMilliseconds.insert(
        index, QDateTime::currentMSecsSinceEpoch());
    m_nasActiveDownloadRequestId = requestId;
    m_nasDataNodeReceivedBytes = 0;
    m_nasDataNodeExpectedBytes = entry.size;
    m_nasDataNodeStatusText = QStringLiteral("正在同步 %1 · %2：下载并校验")
                                  .arg(entry.symbol,
                                       entry.tradingDate.toString(Qt::ISODate));
    m_nasPhaseTimer.restart();
    const QString localRoot = m_nasTickCacheManager.rootDirectory();
    if (!m_nasDataNodeClient.downloadTickFile(
            requestId, entry, localRoot)) {
        m_nasDownloadRequestGate.finish(requestId);
        m_nasDownloadStartedAtMilliseconds.remove(index);
        m_nasSyncQueue.markFailed(index, QStringLiteral("NAS Tick 下载请求未能启动"));
        persistNasSyncState();
        QTimer::singleShot(0, this, &TradingController::startNextNasSyncItem);
    }
    emit nasDataNodeStateChanged();
}

void TradingController::startNextNasImport()
{
    if (m_nasImportingIndex >= 0 || m_nasImportQueue.isEmpty()) {
        startNextNasSyncItem();
        return;
    }
    const int index = m_nasImportQueue.takeFirst();
    m_nasImportingIndex = index;
    const NasCatalogEntry &entry = m_nasSyncQueue.items().at(index).entry;
    const Instrument instrument = findInstrument(entry.symbol);
    if (instrument.symbol.isEmpty()) {
        m_nasSyncQueue.markFailed(
            index, QStringLiteral("NAS 品种不在已加载的 A 股股票池中"));
        m_nasImportingIndex = -1;
        persistNasSyncState();
        QTimer::singleShot(0, this, &TradingController::startNextNasImport);
        return;
    }
    TickDataImportRequest importRequest;
    importRequest.rootDirectory = m_nasTickCacheManager.rootDirectory();
    importRequest.instrument = instrument;
    importRequest.tradingDate = entry.tradingDate;
    importRequest.format = TickDataCsvFormat::Tick;
    importRequest.periodSeconds = 60;
    m_tickDataImportRootDirectory = importRequest.rootDirectory;
    m_tickDataImportInstrument = instrument;
    m_tickDataImportTradingDate = entry.tradingDate.toString(Qt::ISODate);
    m_tickDataImportFormat = static_cast<int>(TickDataCsvFormat::Tick);
    m_tickDataImportPeriodSeconds = 60;
    m_tickDataImportRunning = true;
    m_tickDataImportStatusText = QStringLiteral("NAS 文件校验通过，正在事务导入");
    m_tickDataImportRequestId = m_tickDataCsvImportService.start(importRequest);
    m_nasImportRequestId = m_tickDataImportRequestId;
    m_nasPhaseTimer.restart();
    if (m_tickDataImportRequestId == 0) {
        m_tickDataImportRunning = false;
        m_nasImportRequestId = 0;
        m_nasSyncQueue.markFailed(
            m_nasImportingIndex, QStringLiteral("本地 Tick 导入服务当前不可用"));
        m_nasImportingIndex = -1;
        persistNasSyncState();
        m_nasDataNodeStatusText = QStringLiteral("NAS 文件未能开始导入，继续下一项");
        QTimer::singleShot(0, this, &TradingController::startNextNasImport);
    } else {
        m_nasDataNodeStatusText = QStringLiteral("正在同步 %1：写入本地事实表")
            .arg(entry.tradingDate.toString(Qt::ISODate));
    }
    emit tickDataImportStateChanged();
}

void TradingController::finishNasSyncRun(bool cancelled)
{
    if (!m_nasDataNodeSyncRunning) return;
    m_nasDataNodeSyncRunning = false;
    m_nasSyncElapsedMilliseconds = m_nasSyncTimer.isValid()
        ? m_nasSyncTimer.elapsed() : 0;
    m_nasActiveDownloadRequestId = 0;
    m_nasImportRequestId = 0;
    m_nasDownloadRequestGate.cancelAll();
    m_nasDownloadStartedAtMilliseconds.clear();
    m_nasImportQueue.clear();
    m_nasImportingIndex = -1;
    const NasSyncSummary summary = m_nasSyncQueue.summary();
    if (cancelled) {
        m_nasDataNodeStatusText = QStringLiteral("NAS 多日同步已取消：完成 %1/%2")
                                      .arg(summary.completed).arg(summary.total);
        m_nasDataNodeError = QStringLiteral("用户取消；可恢复未完成日期");
    } else if (summary.failed > 0) {
        m_nasDataNodeStatusText = QStringLiteral("NAS 多日同步完成：成功 %1，失败 %2")
                                      .arg(summary.succeeded).arg(summary.failed);
        m_nasDataNodeError = QStringLiteral("部分交易日失败，可只重试失败日期");
    } else {
        m_nasDataNodeStatusText = QStringLiteral("NAS 多日同步完成：%1 个交易日")
                                      .arg(summary.succeeded);
        m_nasDataNodeError.clear();
    }
    persistNasSyncState();
    emit nasDataNodeStateChanged();
    if (summary.succeeded > 0) {
        if (cancelled) {
            refreshLocalBacktestDataCatalog();
            m_nasBacktestPreparationStatusText = QStringLiteral(
                "同步已取消；已完成数据可手动准备目标回测周期");
            emit nasDataNodeStateChanged();
        } else {
            QTimer::singleShot(
                0, this, &TradingController::prepareNasBacktestDatasets);
        }
    }
}

void TradingController::cancelNasTickSync()
{
    if (!m_nasDataNodeSyncRunning) return;
    const QList<quint64> downloadRequestIds =
        m_nasDownloadRequestGate.activeRequestIds();
    m_nasActiveDownloadRequestId = 0;
    for (quint64 requestId : downloadRequestIds) {
        m_nasDataNodeClient.cancel(requestId);
    }
    m_nasDownloadRequestGate.cancelAll();
    m_nasDownloadStartedAtMilliseconds.clear();
    m_nasImportQueue.clear();
    if (m_nasImportRequestId != 0) {
        m_nasImportRequestId = 0;
        m_tickDataCsvImportService.cancel();
    }
    m_nasImportingIndex = -1;
    m_nasSyncQueue.cancelActiveAndPending();
    persistNasSyncState();
    finishNasSyncRun(true);
}

bool TradingController::resumeNasTickSync()
{
    if (m_nasDataNodeSyncRunning || m_tickDataImportRunning || m_tickDataBatchRunning
        || m_nasBacktestPreparationRunning
        || !m_storageReady || m_storageRelocationRunning
        || m_nasDataNodeEndpoint.isEmpty() || !m_nasSyncQueue.resume()) {
        m_nasDataNodeError = QStringLiteral("当前没有可恢复日期，或本地导入任务正忙");
        emit nasDataNodeStateChanged();
        return false;
    }
    ++m_nasDataNodeRequestVersion;
    m_nasDataNodeSyncRunning = true;
    m_nasSyncElapsedMilliseconds = 0;
    m_nasSyncTimer.restart();
    m_nasDataNodeError.clear();
    m_nasDataNodeStatusText = QStringLiteral("正在重试未完成的 NAS 交易日");
    m_nasDataNodePlanRecovered = false;
    persistNasSyncState();
    emit nasDataNodeStateChanged();
    startNextNasSyncItem();
    return true;
}

bool TradingController::configureNasBacktestPreparation(
    int periodSeconds, int minimumBars)
{
    if (m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning) {
        m_nasDataNodeError = QStringLiteral(
            "NAS 同步或回测数据准备运行时不能修改目标周期");
        emit nasDataNodeStateChanged();
        return false;
    }
    if (periodSeconds < 1 || periodSeconds > 86400
        || minimumBars < 2 || minimumBars > 5000) {
        m_nasDataNodeError = QStringLiteral(
            "NAS 回测周期必须为 1–86400 秒，最低 K 线数必须为 2–5000");
        emit nasDataNodeStateChanged();
        return false;
    }
    m_nasBacktestPeriodSeconds = periodSeconds;
    m_nasBacktestMinimumBars = minimumBars;
    m_nasDataNodeError.clear();
    m_nasBacktestPreparationStatusText = QStringLiteral(
        "同步完成后将生成 %1 秒回测缓存，最低校验 %2 根")
        .arg(periodSeconds).arg(minimumBars);
    emit nasDataNodeStateChanged();
    return true;
}

qulonglong TradingController::prepareNasBacktestDatasets()
{
    if (m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning
        || !m_storageReady || !m_nasBacktestPreparationService.isRunning()) {
        m_nasDataNodeError = QStringLiteral(
            "NAS 同步仍在运行，或本地回测准备服务不可用");
        emit nasDataNodeStateChanged();
        return 0;
    }

    QStringList symbols;
    QSet<QString> seen;
    for (const NasSyncPlanItem &item : m_nasSyncQueue.items()) {
        if (item.status != NasSyncItemStatus::Succeeded
            || seen.contains(item.entry.symbol)) {
            continue;
        }
        seen.insert(item.entry.symbol);
        symbols.append(item.entry.symbol);
    }
    if (symbols.isEmpty()) {
        m_nasDataNodeError = QStringLiteral(
            "当前 NAS 同步计划没有已成功导入的品种");
        emit nasDataNodeStateChanged();
        return 0;
    }

    ++m_nasBacktestPreparationRequestId;
    if (m_nasBacktestPreparationRequestId == 0)
        ++m_nasBacktestPreparationRequestId;
    m_nasBacktestPreparationRunning = true;
    m_nasBacktestPreparationCompletedSymbols = 0;
    m_nasBacktestPreparationTotalSymbols = symbols.size();
    m_nasBacktestPreparationCurrentSymbol.clear();
    m_nasBacktestPreparedSymbols.clear();
    m_nasBacktestPreparationFailures.clear();
    m_nasBacktestPreparationElapsedMilliseconds = 0;
    m_nasBacktestPreparationStatusText = QStringLiteral(
        "正在从本地原始 Tick 生成 %1 秒回测缓存：0/%2")
        .arg(m_nasBacktestPeriodSeconds).arg(symbols.size());
    m_nasDataNodeError.clear();
    emit nasDataNodeStateChanged();

    if (!m_nasBacktestPreparationService.requestBatch(
            m_nasBacktestPreparationRequestId, symbols,
            m_nasBacktestPeriodSeconds, m_nasBacktestMinimumBars,
            m_aShareUniverse)) {
        m_nasBacktestPreparationRunning = false;
        m_nasDataNodeError = QStringLiteral("NAS 回测数据准备任务提交失败");
        m_nasBacktestPreparationStatusText = QStringLiteral("回测数据准备未启动");
        emit nasDataNodeStateChanged();
        return 0;
    }
    return m_nasBacktestPreparationRequestId;
}

void TradingController::cancelNasBacktestPreparation()
{
    if (!m_nasBacktestPreparationRunning) return;
    m_nasBacktestPreparationService.cancelBatch();
    m_nasBacktestPreparationStatusText = QStringLiteral(
        "正在取消 NAS 回测数据准备；已完成缓存保持可用");
    emit nasDataNodeStateChanged();
}

void TradingController::handleNasBacktestPreparationProgress(
    quint64 requestId, int completedSymbols, int totalSymbols,
    const QString &currentSymbol)
{
    if (requestId != m_nasBacktestPreparationRequestId
        || !m_nasBacktestPreparationRunning) return;
    m_nasBacktestPreparationCompletedSymbols = completedSymbols;
    m_nasBacktestPreparationTotalSymbols = totalSymbols;
    m_nasBacktestPreparationCurrentSymbol = currentSymbol;
    m_nasBacktestPreparationStatusText = QStringLiteral(
        "正在生成 %1 秒回测缓存：%2/%3 · %4")
        .arg(m_nasBacktestPeriodSeconds).arg(completedSymbols)
        .arg(totalSymbols).arg(currentSymbol);
    emit nasDataNodeStateChanged();
}

void TradingController::handleNasBacktestPreparationFinished(
    quint64 requestId, int periodSeconds,
    const QStringList &preparedSymbols, const QVariantMap &failedSymbols,
    qint64 elapsedMilliseconds, bool cancelled)
{
    if (requestId != m_nasBacktestPreparationRequestId) return;
    m_nasBacktestPreparationRunning = false;
    m_nasBacktestPreparedSymbols = preparedSymbols;
    m_nasBacktestPreparationFailures = failedSymbols;
    m_nasBacktestPreparationElapsedMilliseconds = elapsedMilliseconds;
    m_nasBacktestPreparationCompletedSymbols =
        preparedSymbols.size() + failedSymbols.size();
    m_nasBacktestPreparationCurrentSymbol.clear();
    refreshLocalBacktestDataCatalog();

    if (!cancelled && preparedSymbols.size() == 1) {
        QString selectionError;
        m_workspaceInstrumentSelections.selectBacktestInstrument(
            preparedSymbols.first(), periodSeconds,
            m_nasBacktestMinimumBars, &selectionError);
        m_backtestInstrumentSelectionError = selectionError;
        emit backtestInstrumentChanged();
    }

    if (cancelled) {
        m_nasBacktestPreparationStatusText = QStringLiteral(
            "NAS 回测数据准备已取消：完成 %1/%2，已生成缓存保持可用")
            .arg(m_nasBacktestPreparationCompletedSymbols)
            .arg(m_nasBacktestPreparationTotalSymbols);
        m_nasDataNodeError = QStringLiteral("用户取消回测数据准备");
    } else if (!failedSymbols.isEmpty()) {
        m_nasBacktestPreparationStatusText = QStringLiteral(
            "NAS 回测数据准备完成：可用 %1，失败 %2，耗时 %3 ms")
            .arg(preparedSymbols.size()).arg(failedSymbols.size())
            .arg(elapsedMilliseconds);
        m_nasDataNodeError = QStringLiteral(
            "部分品种没有足够的本地 Tick 或周期缓存生成失败");
    } else {
        m_nasBacktestPreparationStatusText = QStringLiteral(
            "NAS 回测数据准备完成：%1 个品种 · %2 秒 · %3 ms")
            .arg(preparedSymbols.size()).arg(periodSeconds)
            .arg(elapsedMilliseconds);
        m_nasDataNodeStatusText = m_nasBacktestPreparationStatusText;
        m_nasDataNodeError.clear();
    }
    emit nasDataNodeStateChanged();
}

void TradingController::refreshNasTickCacheSummary()
{
    if (m_nasDataNodeSyncRunning || m_nasBacktestPreparationRunning) {
        m_nasDataNodeError = QStringLiteral(
            "同步或回测数据准备运行时不能扫描 NAS 暂存缓存");
        emit nasDataNodeStateChanged();
        return;
    }
    m_nasDataNodeCacheSummary = m_nasTickCacheManager.scan().toVariantMap();
    m_nasDataNodeStatusText = QStringLiteral("NAS 暂存缓存统计已刷新：%1 个文件")
                                  .arg(m_nasDataNodeCacheSummary.value(
                                      QStringLiteral("files")).toInt());
    m_nasDataNodeError.clear();
    emit nasDataNodeStateChanged();
}

bool TradingController::pruneNasTickCache(int maximumAgeDays)
{
    if (m_nasDataNodeSyncRunning || m_nasDataNodeLoading
        || m_nasBacktestPreparationRunning) {
        m_nasDataNodeError = QStringLiteral(
            "查询、同步或回测数据准备运行时不能清理 NAS 暂存缓存");
        emit nasDataNodeStateChanged();
        return false;
    }
    NasTickCacheSummary summary;
    QString error;
    if (!m_nasTickCacheManager.pruneOlderThan(maximumAgeDays, &summary, &error)) {
        m_nasDataNodeError = error;
        emit nasDataNodeStateChanged();
        return false;
    }
    m_nasDataNodeCacheSummary = summary.toVariantMap();
    m_nasDataNodeStatusText = QStringLiteral("NAS 暂存清理完成：删除 %1 个文件，保留 %2 个")
                                  .arg(summary.removedFiles).arg(summary.files);
    m_nasDataNodeError.clear();
    emit nasDataNodeStateChanged();
    return true;
}

void TradingController::persistNasSyncState()
{
    if (m_nasSyncQueue.items().isEmpty()) {
        clearNasSyncState();
        return;
    }
    NasPersistedSyncState state;
    state.symbols = m_nasManifestSymbols;
    state.items = m_nasSyncQueue.items();
    QString error;
    if (!m_nasSyncStateStore.save(state, &error)) {
        m_nasDataNodeError = error;
    }
}

void TradingController::clearNasSyncState()
{
    QString error;
    if (!m_nasSyncStateStore.clear(&error) && m_nasDataNodeError.isEmpty())
        m_nasDataNodeError = error;
    m_nasDataNodePlanRecovered = false;
}

bool TradingController::relocateStorage(const QString &pathOrUrl)
{
    if (m_storageRelocationRunning || !m_storageReady) return false;
    if (m_realtimeLiveSessionState.requested() || backtestRunning()
        || batchBacktestRunning()
        || historyDownloadRunning()
        || m_tickDataImportRunning || m_tickDataBatchRunning
        || m_aShareInstrumentDayImportRunning
        || m_authorizedFactorImportState.value(
               QStringLiteral("running")).toBool()
        || m_externalFactorResearchState.value(
               QStringLiteral("running")).toBool()
        || m_externalFactorResearchPreflightState.value(
               QStringLiteral("running")).toBool()
        || m_nasDataNodeLoading || m_nasDataNodeSyncRunning
        || m_nasBacktestPreparationRunning
        || snapshotLoading()
        || localCandleSeriesLoading() || liveDayCandleSeriesLoading()
        || customStrategyPreviewLoading() || customStrategySaveLoading()
        || rawTickAvailabilityLoading()
        || m_asyncRawTickAvailabilityService.isBusy()
        || rawTickSensitivityRunning()) {
        m_storageRelocationError = QStringLiteral(
            "请先停止行情、回测、下载、导入和数据库查询，再搬迁本地数据库");
        emit storageStateChanged();
        return false;
    }

    const QUrl url(pathOrUrl);
    const QString targetPath = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    QString validationError;
    if (!StorageProfile::validateLocalDatabasePath(targetPath, &validationError)) {
        m_storageRelocationError = validationError;
        emit storageStateChanged();
        return false;
    }
    const QString sourcePath = m_dataStore.databasePath();
    if (QFileInfo(sourcePath).absoluteFilePath().compare(
            QFileInfo(targetPath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
        m_storageRelocationError = QStringLiteral("目标数据库路径与当前路径相同");
        emit storageStateChanged();
        return false;
    }

    persistTradingState();
    const QString uuid = m_dataStore.databaseUuid();
    m_backtestArchiveLoadRequestGate.cancel();
    clearBacktestArchiveRawTickPage();
    m_asyncSnapshotQueryService.stop();
    m_asyncLocalCandleQueryService.stop();
    m_asyncLocalTickQueryService.stop();
    m_asyncLiveDayCandleQueryService.stop();
    m_asyncCustomStrategyWorkspaceService.stop();
    m_asyncBacktestArchiveService.stop();
    m_backtestDatasetService.stop();
    m_nasBacktestPreparationService.stop();
    m_portfolioBacktestService.stop();
    m_crossSectionalBacktestService.stop();
    m_rawTickSensitivityService.stop();
    m_dataStore.close();
    m_storageReady = false;
    m_storageRelocationRunning = true;
    m_storageRelocationError.clear();
    emit storageStateChanged();

    QPointer<TradingController> guard(this);
    QThread *worker = QThread::create([guard, sourcePath, targetPath, uuid]() {
        const StorageRelocationResult result = DatabaseRelocationService::copyAndVerify(
            sourcePath, targetPath, uuid);
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, sourcePath, targetPath, result]() {
            if (guard) guard->finishStorageRelocation(
                sourcePath, targetPath, result.databaseUuid, result.error);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->setObjectName(QStringLiteral("PPTraderStorageRelocation"));
    worker->start();
    return true;
}

bool TradingController::restartStorageServices(const QString &databasePath)
{
    if (!m_dataStore.initialize(databasePath)) return false;
    m_storageReady = true;
    m_tickDataCsvImportService.setDatabasePath(databasePath);
    m_aShareInstrumentDayImportService.setDatabasePath(databasePath);
    m_aShareIndustryImportService.setDatabasePath(databasePath);
    m_authorizedFactorImportService.setDatabasePath(databasePath);
    m_externalFactorResearchService.setDatabasePath(databasePath);
    m_batchBacktestService.setDatabasePath(databasePath);
    m_persistedTickCount = m_dataStore.tickCount();
    m_persistedSignalCount = m_dataStore.signalCount();
    reloadCustomStrategies();
    refreshLocalBacktestDataCatalog();
    refreshBacktestArchiveList();
    reloadTickDataImportRunHistory();
    refreshAShareInstrumentDayImportHistory();
    refreshAShareIndustryImportHistory();
    m_crossSectionalFactorDefinitionCatalog.clear();
    for (const FactorDefinitionVersion &factor :
         m_dataStore.loadFactorDefinitionVersions()) {
        m_crossSectionalFactorDefinitionCatalog.append(factor.toVariantMap());
    }
    refreshFactorCandidateCatalog();
    refreshExternalFactorResearchArchives();
    reloadRawTickParameterSensitivityHistory();
    const bool snapshotReady = m_asyncSnapshotQueryService.start(databasePath);
    const bool localSeriesReady = m_asyncLocalCandleQueryService.start(databasePath);
    const bool localTickSeriesReady = m_asyncLocalTickQueryService.start(databasePath);
    const bool liveSeriesReady = m_asyncLiveDayCandleQueryService.start(databasePath);
    const bool customStrategyWorkspaceReady =
        m_asyncCustomStrategyWorkspaceService.start(databasePath);
    const bool backtestArchiveReady =
        m_asyncBacktestArchiveService.start(databasePath);
    const bool datasetReady = m_backtestDatasetService.start(databasePath);
    const bool nasDatasetReady =
        m_nasBacktestPreparationService.start(databasePath);
    const bool portfolioReady = m_portfolioBacktestService.start(databasePath);
    const bool crossSectionalReady = m_crossSectionalBacktestService.start(databasePath);
    const bool rawTickSensitivityReady =
        m_rawTickSensitivityService.start(databasePath);
    if (!snapshotReady || !localSeriesReady || !localTickSeriesReady
        || !liveSeriesReady || !customStrategyWorkspaceReady
        || !backtestArchiveReady || !datasetReady
        || !nasDatasetReady
        || !portfolioReady || !crossSectionalReady || !rawTickSensitivityReady) {
        setStatusText(QStringLiteral("数据库已切换，但部分后台存储服务启动失败"));
    }
    return true;
}

void TradingController::finishStorageRelocation(
    const QString &sourcePath, const QString &targetPath,
    const QString &databaseUuid, const QString &error)
{
    bool switched = error.isEmpty() && !databaseUuid.isEmpty()
        && restartStorageServices(targetPath);
    if (switched) {
        StorageProfile::saveDatabasePath(targetPath);
        setStatusText(QStringLiteral("本地数据库已复制并切换；原数据库保留为恢复副本"));
    } else {
        if (!m_storageReady && !restartStorageServices(sourcePath)) {
            m_storageRelocationError = QStringLiteral(
                "数据库搬迁失败，且无法重新打开原数据库：%1").arg(error);
        } else {
            m_storageRelocationError = error.isEmpty()
                ? QStringLiteral("目标数据库验证或重新打开失败，已恢复原数据库") : error;
        }
        setStatusText(m_storageRelocationError);
    }
    m_storageRelocationRunning = false;
    emit storageStateChanged();
    if (m_storageReady) requestLatestMarketSnapshot();
}
int TradingController::persistedTickCount() const { return m_persistedTickCount; }
int TradingController::persistedSignalCount() const { return m_persistedSignalCount; }
int TradingController::strategyCount() const { return m_strategyEngine.strategyCount(); }
int TradingController::strategyThreadCount() const { return m_strategyEngine.maxThreadCount(); }
int TradingController::pendingStrategyIntentCount() const
{
    return m_strategyIntentModel.pendingCount();
}
int TradingController::aShareUniverseCount() const { return m_aShareUniverse.size(); }
int TradingController::aShareUniverseSourceCount() const { return m_aShareUniverseSourceCount; }
int TradingController::aShareUniverseRejectedCount() const { return m_aShareUniverseRejectedCount; }
QString TradingController::aShareUniverseCachePath() const { return m_aShareUniverseCachePath; }
QString TradingController::aShareUniverseError() const { return m_aShareUniverseError; }
bool TradingController::snapshotLoading() const { return m_snapshotRequestGate.loading(); }
qulonglong TradingController::snapshotRequestId() const
{
    return m_snapshotRequestGate.currentRequestId();
}
qulonglong TradingController::resolvedSnapshotRequestId() const
{
    return m_snapshotRequestGate.resolvedRequestId();
}
QDateTime TradingController::requestedSnapshotTime() const { return m_requestedSnapshotTime; }
QDateTime TradingController::resolvedSnapshotTime() const { return m_resolvedSnapshotTime; }
QString TradingController::snapshotBatchId() const { return m_snapshotBatchId; }
int TradingController::snapshotExpectedCount() const { return m_snapshotExpectedCount; }
int TradingController::snapshotReceivedCount() const { return m_snapshotReceivedCount; }
double TradingController::snapshotCompleteness() const { return m_snapshotCompleteness; }
qint64 TradingController::snapshotLatencyMilliseconds() const
{
    return m_snapshotLatencyMilliseconds;
}
qint64 TradingController::snapshotStalenessMilliseconds() const
{
    return m_snapshotStalenessMilliseconds;
}
bool TradingController::snapshotStale() const { return m_snapshotHealth == SnapshotStale; }
int TradingController::snapshotHealth() const { return m_snapshotHealth; }
QString TradingController::snapshotHealthText() const
{
    return marketSnapshotHealthName(static_cast<MarketSnapshotHealth>(m_snapshotHealth));
}
QString TradingController::snapshotError() const { return m_snapshotError; }
int TradingController::providerHealth() const { return m_providerHealth; }
QString TradingController::providerHealthText() const
{
    return providerHealthName(static_cast<ProviderHealthState>(m_providerHealth));
}
QString TradingController::providerMessage() const { return m_providerMessage; }
QString TradingController::providerError() const { return m_providerError; }
int TradingController::providerExpectedCount() const { return m_providerExpectedCount; }
int TradingController::providerReceivedCount() const { return m_providerReceivedCount; }
double TradingController::providerCompleteness() const
{
    return m_providerExpectedCount > 0
        ? static_cast<double>(m_providerReceivedCount) / m_providerExpectedCount : 0.0;
}
QString TradingController::providerBatchId() const { return m_providerBatchId; }
QDateTime TradingController::providerUpdatedAt() const { return m_providerUpdatedAt; }
QString TradingController::activeRealtimeProvider() const { return m_activeRealtimeProvider; }
QString TradingController::activeRealtimeProviderName() const
{
    return m_activeRealtimeProviderName;
}
int TradingController::realtimeProviderScore() const { return m_realtimeProviderScore; }
bool TradingController::realtimeFallbackUsed() const { return m_realtimeFallbackUsed; }
int TradingController::realtimeFailoverCount() const { return m_realtimeFailoverCount; }
QString TradingController::realtimeRoutingMessage() const { return m_realtimeRoutingMessage; }
QVariantList TradingController::realtimeProviderCapabilities() const
{
    return m_realtimeProviderCapabilities;
}

void TradingController::startLive()
{
    if (m_paperReplayBook.hasRun()) {
        m_paperStrategyError = QStringLiteral("本工作台已绑定历史回放；安全时钟接管验收前禁止混入实时行情");
        emit paperStrategyStateChanged();
        return;
    }
    if (m_realtimeLiveSessionState.requested()) return;
    if (m_aShareUniverse.isEmpty() && !reloadAShareUniverse()) {
        degradeProvider(m_aShareUniverseError);
        return;
    }
    m_marketData->start(buildMarketUniverse());
    m_realtimeLiveSessionState.requestStart();
    emit liveRunningChanged();
    requestRealtimeMinuteSeries(selectedLiveSymbol());
    m_realtimeChartWindowModel.setRefreshEnabled(true, true);
    setStatusText(QStringLiteral("实时行情运行中"));
}

void TradingController::stopLive()
{
    if (!m_realtimeLiveSessionState.requested()) return;
    stopAllPaperStrategies();
    m_marketData->stop();
    m_realtimeMinuteRefreshTimer.stop();
    m_realtimeMinuteService.cancel();
    m_realtimeMinuteRequestGate.cancel();
    m_realtimeChartWindowModel.setRefreshEnabled(false);
    m_realtimeMinuteStatusText = QStringLiteral("实时分钟行情已停止；内存数据保留到下次选择或退出");
    emit realtimeMinuteStateChanged();
    m_realtimeLiveSessionState.requestStop();
    emit liveRunningChanged();
    setStatusText(QStringLiteral("实时行情已停止"));
}

void TradingController::runBacktest()
{
    runSelectedBacktest();
}

void TradingController::runHistoricalBacktest(const QString &symbol, const QString &frequency,
                                               int barCount, double initialCapital,
                                               double commissionRate, double slippageRate)
{
    runStrategyBacktest(m_selectedBacktestStrategyId, m_selectedBacktestParameters,
                        symbol, frequency, barCount,
                        initialCapital, commissionRate, slippageRate);
}

bool TradingController::validateBacktestConfiguration(
    const QString &frequency, int barCount, double initialCapital,
    double commissionRate, double slippageRate, QString *error) const
{
    if (HistoricalDataService::periodSecondsForFrequency(frequency) == 0) {
        if (error) *error = QStringLiteral("不支持的 K 线周期：%1").arg(frequency);
        return false;
    }
    if (barCount < 2 || barCount > 5000) {
        if (error) *error = QStringLiteral("K 线数量必须在 2–5000 之间");
        return false;
    }
    if (!std::isfinite(initialCapital) || initialCapital <= 0.0
        || initialCapital > 1.0e12) {
        if (error) *error = QStringLiteral("初始资金必须大于 0 且不超过 1 万亿元");
        return false;
    }
    if (!std::isfinite(commissionRate) || commissionRate < 0.0
        || commissionRate > 0.1) {
        if (error) *error = QStringLiteral("佣金率必须在 0%–10% 之间");
        return false;
    }
    if (!std::isfinite(slippageRate) || slippageRate < 0.0
        || slippageRate > 0.1) {
        if (error) *error = QStringLiteral("滑点率必须在 0%–10% 之间");
        return false;
    }
    if (error) error->clear();
    return true;
}

qulonglong TradingController::runStrategyBacktest(const QString &strategyId,
                                                  const QVariantMap &strategyParameters,
                                                  const QString &symbol,
                                                  const QString &frequency,
                                                  int barCount, double initialCapital,
                                                  double commissionRate,
                                                  double slippageRate)
{
    if (batchBacktestRunning()) {
        handleBacktestError(QStringLiteral("请先取消正在运行的多标的批量回测"));
        return 0;
    }
    const Instrument instrument = findInstrument(symbol.isEmpty()
        ? m_workspaceInstrumentSelections.researchInstrument().symbol : symbol);
    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(frequency);
    QVariantMap normalizedParameters;
    QString strategyError;
    if (!m_backtestStrategyRegistry.normalizeParameters(strategyId, strategyParameters,
                                                        &normalizedParameters, &strategyError)) {
        m_backtestConfigurationError = strategyError;
        emit backtestConfigurationChanged();
        handleBacktestError(strategyError);
        return 0;
    }
    QString configurationError;
    if (instrument.symbol.isEmpty()
        || !validateBacktestConfiguration(frequency, barCount, initialCapital,
                                          commissionRate, slippageRate,
                                          &configurationError)) {
        if (configurationError.isEmpty()) configurationError = QStringLiteral("请先选择回测品种");
        m_backtestConfigurationError = configurationError;
        emit backtestConfigurationChanged();
        handleBacktestError(configurationError);
        return 0;
    }

    const LocalHistoryDataset *dataset = nullptr;
    for (const LocalHistoryDataset &candidate
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (candidate.instrument.symbol == instrument.symbol
            && candidate.periodSeconds == periodSeconds
            && candidate.isBacktestEligible()) {
            dataset = &candidate;
            break;
        }
    }
    if (!dataset) {
        const QString error = QStringLiteral(
            "回测只允许使用 SQLite 中的历史 Tick 派生 K 线：%1 · %2")
                                  .arg(instrument.symbol,
                                       localHistoryFrequency(periodSeconds));
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }
    if (m_backtest.isRunning()) {
        m_backtest.cancel();
    }
    clearBacktestResult();
    m_pendingCustomBacktestPlan.reset();

    ++m_backtestTaskId;
    m_selectedBacktestStrategyId = strategyId;
    m_selectedBacktestParameters = normalizedParameters;
    m_backtestFrequency = frequency;
    m_backtestBarCount = barCount;
    m_backtestConfiguredInitialCapital = initialCapital;
    m_backtestCommissionRate = commissionRate;
    m_backtestSlippageRate = slippageRate;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();
    m_pendingBacktestConfig = {};
    m_pendingBacktestConfig.taskId = QString::number(m_backtestTaskId);
    m_pendingBacktestConfig.strategyId = strategyId;
    m_pendingBacktestConfig.strategyParameters = normalizedParameters;
    m_pendingBacktestConfig.instrument = dataset->instrument;
    m_pendingBacktestConfig.frequency = frequency;
    m_pendingBacktestConfig.periodSeconds = periodSeconds;
    m_pendingBacktestConfig.requestedBars = barCount;
    m_pendingBacktestConfig.initialCapital = initialCapital;
    m_pendingBacktestConfig.commissionRate = std::max(0.0, commissionRate);
    m_pendingBacktestConfig.slippageRate = std::max(0.0, slippageRate);
    m_pendingBacktestConfig.lotSize = 100;
    m_pendingBacktestConfig.sampleFrom = m_pendingSampleFrom;
    m_pendingBacktestConfig.sampleTo = m_pendingSampleTo;

    m_pendingBacktestHistoryRequestId = 0;
    m_backtestPreparing = true;
    m_pendingBacktestDatasetRequestId = ++m_pendingBacktestDatasetRequestId;
    if (m_pendingBacktestDatasetRequestId == 0) ++m_pendingBacktestDatasetRequestId;
    setBacktestStatus(QStringLiteral("正在准备回测数据集…"));
    setStatusText(QStringLiteral("严格本地回测：%1 · %2 · 准备数据集中")
                      .arg(dataset->instrument.symbol, strategyId));
    if (!m_backtestDatasetService.isRunning()
        || !m_backtestDatasetService.request(
            m_pendingBacktestDatasetRequestId, dataset->instrument.symbol,
            dataset->periodSeconds, barCount, m_aShareUniverse)) {
        m_backtestPreparing = false;
        m_pendingBacktestDatasetRequestId = 0;
        handleBacktestError(QStringLiteral("回测数据集准备服务不可用"));
        return 0;
    }
    emit backtestRunningChanged();
    return m_backtestTaskId;
}

qulonglong TradingController::runCustomStrategyBacktest(
    const QString &strategyId, int version, const QVariantMap &strategyParameters,
    const QString &symbol, const QString &frequency, int barCount,
    double initialCapital, double commissionRate, double slippageRate)
{
    if (batchBacktestRunning()) {
        handleBacktestError(QStringLiteral("请先取消正在运行的多标的批量回测"));
        return 0;
    }
    const CustomStrategyDefinition *definition = nullptr;
    for (const CustomStrategyDefinition &candidate : m_customStrategyDefinitions) {
        if (candidate.strategyId == strategyId
            && (version <= 0 || candidate.version == version)) {
            definition = &candidate;
            break;
        }
    }
    QString error;
    if (!definition) {
        error = QStringLiteral("未找到自定义策略版本：%1 v%2")
                    .arg(strategyId).arg(version);
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }

    const auto plan = m_customStrategyCompiler.compile(*definition, strategyParameters, &error);
    if (!plan) {
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }
    const Instrument instrument = findInstrument(
        symbol.isEmpty() ? m_workspaceInstrumentSelections.researchInstrument().symbol : symbol);
    if (instrument.symbol.isEmpty()
        || !validateBacktestConfiguration(frequency, barCount, initialCapital,
                                          commissionRate, slippageRate, &error)) {
        if (error.isEmpty()) error = QStringLiteral("请先选择回测品种");
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }

    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(frequency);
    const LocalHistoryDataset *dataset = nullptr;
    for (const LocalHistoryDataset &candidate
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (candidate.instrument.symbol == instrument.symbol
            && candidate.periodSeconds == periodSeconds
            && candidate.isBacktestEligible()) {
            dataset = &candidate;
            break;
        }
    }
    if (!dataset) {
        error = QStringLiteral(
            "自定义策略回测只允许使用历史 Tick 派生 K 线：%1 · %2")
                    .arg(instrument.symbol, localHistoryFrequency(periodSeconds));
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }
    if (m_backtest.isRunning()) m_backtest.cancel();
    clearBacktestResult();
    ++m_backtestTaskId;

    m_backtestFrequency = frequency;
    m_backtestBarCount = barCount;
    m_backtestConfiguredInitialCapital = initialCapital;
    m_backtestCommissionRate = commissionRate;
    m_backtestSlippageRate = slippageRate;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();

    m_pendingBacktestConfig = {};
    m_pendingBacktestConfig.taskId = QString::number(m_backtestTaskId);
    m_pendingBacktestConfig.strategyId = plan->strategyId;
    m_pendingBacktestConfig.strategyVersion = plan->version;
    m_pendingBacktestConfig.strategyParameters = plan->normalizedParameters;
    m_pendingBacktestConfig.ruleSnapshot = plan->rules.toVariantMap();
    m_pendingBacktestConfig.instrument = dataset->instrument;
    m_pendingBacktestConfig.frequency = frequency;
    m_pendingBacktestConfig.periodSeconds = periodSeconds;
    m_pendingBacktestConfig.requestedBars = barCount;
    m_pendingBacktestConfig.initialCapital = initialCapital;
    m_pendingBacktestConfig.commissionRate = std::max(0.0, commissionRate);
    m_pendingBacktestConfig.slippageRate = std::max(0.0, slippageRate);
    m_pendingBacktestConfig.lotSize = 100;
    m_pendingBacktestConfig.sampleFrom = m_pendingSampleFrom;
    m_pendingBacktestConfig.sampleTo = m_pendingSampleTo;
    m_pendingCustomBacktestPlan = *plan;

    m_pendingBacktestHistoryRequestId = 0;
    m_backtestPreparing = true;
    m_pendingBacktestDatasetRequestId = ++m_pendingBacktestDatasetRequestId;
    if (m_pendingBacktestDatasetRequestId == 0) ++m_pendingBacktestDatasetRequestId;
    setBacktestStatus(QStringLiteral("正在准备回测数据集…"));
    setStatusText(QStringLiteral("严格本地回测：%1 · %2 v%3 · 准备数据集中")
                      .arg(dataset->instrument.symbol, plan->strategyId)
                      .arg(plan->version));
    if (!m_backtestDatasetService.isRunning()
        || !m_backtestDatasetService.request(
            m_pendingBacktestDatasetRequestId, dataset->instrument.symbol,
            dataset->periodSeconds, barCount, m_aShareUniverse)) {
        m_backtestPreparing = false;
        m_pendingBacktestDatasetRequestId = 0;
        m_pendingCustomBacktestPlan.reset();
        handleBacktestError(QStringLiteral("回测数据集准备服务不可用"));
        emit backtestRunningChanged();
        return 0;
    }
    emit backtestRunningChanged();
    return m_backtestTaskId;
}

QVariantMap TradingController::customStrategyLocalRunAvailability(
    const QString &strategyId, int version,
    const QVariantMap &strategyParameters) const
{
    const CustomStrategyDefinition *definition = nullptr;
    for (const CustomStrategyDefinition &candidate : m_customStrategyDefinitions) {
        if (candidate.strategyId == strategyId && candidate.version == version) {
            definition = &candidate;
            break;
        }
    }

    QString error;
    const auto plan = definition
        ? m_customStrategyCompiler.compile(*definition, strategyParameters, &error)
        : std::optional<CompiledCustomStrategy>{};
    QVariantMap result = CustomStrategyWorkspacePolicy::localRunAvailability(
        definition != nullptr, plan.has_value(), strategyId, version,
        plan ? plan->minimumWarmupBars : 0,
        m_workspaceInstrumentSelections.selectedBacktestDataset(),
        m_backtestBarCount, backtestRunning());
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("available"), false);
        result.insert(QStringLiteral("error"), error);
    }
    if (plan) {
        result.insert(QStringLiteral("normalizedParameters"), plan->normalizedParameters);
        result.insert(QStringLiteral("ruleSnapshot"), plan->rules.toVariantMap());
    }
    return result;
}

QVariantMap TradingController::customStrategyPreview(
    const QString &json, const QString &symbol,
    int periodSeconds, int barCount)
{
    QVariantMap result;
    if (barCount <= 0) barCount = 300;
    if (barCount > 2000) barCount = 2000;
    if (periodSeconds <= 0) periodSeconds = 60;

    CustomStrategyValidator validator;
    CustomStrategyDefinition definition;
    QString error;
    if (!validator.parseAndValidate(json, &definition, &error)) {
        result.insert(QStringLiteral("error"), error);
        return result;
    }

    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        instrument.symbol = symbol.trimmed();
        instrument.name = symbol.trimmed();
        instrument.market = Market::AShare;
    }
    const auto preview = CustomStrategyPreviewService::buildLocalTickPreview(
        definition, {}, m_dataStore, instrument, periodSeconds, barCount, &error);
    if (!error.isEmpty()) {
        result.insert(QStringLiteral("error"), error);
        return result;
    }

    result.insert(QStringLiteral("symbol"), instrument.symbol);
    result.insert(QStringLiteral("source"), QStringLiteral("raw_tick_cache_v1"));
    result.insert(QStringLiteral("barCount"), preview.bars.size());
    QVariantList aliases;
    QVariantList labels;
    for (int index = 0; index < preview.seriesAliases.size(); ++index) {
        aliases.append(preview.seriesAliases.at(index));
        labels.append(preview.seriesLabels.at(index));
    }
    result.insert(QStringLiteral("aliases"), aliases);
    result.insert(QStringLiteral("labels"), labels);

    QVariantList bars;
    for (const auto &bar : preview.bars) {
        QVariantMap row;
        row.insert(QStringLiteral("t"), bar.time.toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("o"), bar.open);
        row.insert(QStringLiteral("h"), bar.high);
        row.insert(QStringLiteral("l"), bar.low);
        row.insert(QStringLiteral("c"), bar.close);
        row.insert(QStringLiteral("v"), bar.volume);
        QVariantList values;
        for (const auto &value : bar.seriesValues) {
            values.append(value ? QVariant(*value) : QVariant());
        }
        row.insert(QStringLiteral("s"), values);
        bars.append(row);
    }
    result.insert(QStringLiteral("bars"), bars);

    QVariantList signalList;
    for (const auto &signal : preview.signalEvents) {
        signalList.append(QVariantMap{
            {QStringLiteral("index"), signal.barIndex},
            {QStringLiteral("side"), signal.side},
            {QStringLiteral("price"), signal.price},
            {QStringLiteral("reason"), signal.reason}});
    }
    result.insert(QStringLiteral("signals"), signalList);
    return result;
}

qulonglong TradingController::requestCustomStrategyPreview(
    const QString &json, const QString &symbol,
    int periodSeconds, int barCount)
{
    periodSeconds = periodSeconds > 0 ? periodSeconds : 60;
    barCount = std::clamp(barCount > 0 ? barCount : 300, 1, 2000);
    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        instrument.symbol = symbol.trimmed();
        instrument.name = symbol.trimmed();
        instrument.market = Market::AShare;
    }
    const quint64 requestId = m_customStrategyPreviewRequestGate.begin();
    m_customStrategyPreviewResult.clear();
    m_customStrategyPreviewError.clear();
    emit customStrategyPreviewStateChanged();
    if (!m_asyncCustomStrategyWorkspaceService.requestPreview(
            requestId, json, instrument, periodSeconds, barCount)) {
        m_customStrategyPreviewRequestGate.finish(requestId);
        m_customStrategyPreviewError = QStringLiteral("异步自定义策略预览服务不可用");
        emit customStrategyPreviewStateChanged();
        return 0;
    }
    return requestId;
}

void TradingController::finishCustomStrategyPreview(
    quint64 requestId, const QVariantMap &result)
{
    if (!m_customStrategyPreviewRequestGate.finish(requestId)) return;
    m_customStrategyPreviewResult = result;
    m_customStrategyPreviewError = result.value(QStringLiteral("error")).toString();
    emit customStrategyPreviewStateChanged();
}

qulonglong TradingController::runSelectedLocalCustomStrategyBacktest(
    const QString &strategyId, int version,
    const QVariantMap &strategyParameters)
{
    const QVariantMap availability = customStrategyLocalRunAvailability(
        strategyId, version, strategyParameters);
    if (!availability.value(QStringLiteral("available")).toBool()) {
        const QString error = availability.value(QStringLiteral("error")).toString();
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        handleBacktestError(error);
        return 0;
    }

    const CustomStrategyDefinition *definition = nullptr;
    for (const CustomStrategyDefinition &candidate : m_customStrategyDefinitions) {
        if (candidate.strategyId == strategyId && candidate.version == version) {
            definition = &candidate;
            break;
        }
    }
    const LocalHistoryDataset *dataset =
        m_workspaceInstrumentSelections.selectedBacktestDataset();
    QString error;
    const auto plan = definition
        ? m_customStrategyCompiler.compile(*definition, strategyParameters, &error)
        : std::optional<CompiledCustomStrategy>{};
    if (!dataset || !plan) {
        handleBacktestError(error.isEmpty()
            ? QStringLiteral("自定义策略或本地数据集已失效") : error);
        return 0;
    }

    if (m_backtest.isRunning()) m_backtest.cancel();
    clearBacktestResult();
    ++m_backtestTaskId;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();

    m_pendingBacktestConfig = {};
    m_pendingBacktestConfig.taskId = QString::number(m_backtestTaskId);
    m_pendingBacktestConfig.strategyId = plan->strategyId;
    m_pendingBacktestConfig.strategyVersion = plan->version;
    m_pendingBacktestConfig.strategyParameters = plan->normalizedParameters;
    m_pendingBacktestConfig.ruleSnapshot = plan->rules.toVariantMap();
    m_pendingBacktestConfig.instrument = dataset->instrument;
    m_pendingBacktestConfig.frequency = dataset->frequency();
    m_pendingBacktestConfig.periodSeconds = dataset->periodSeconds;
    m_pendingBacktestConfig.requestedBars = m_backtestBarCount;
    m_pendingBacktestConfig.initialCapital = m_backtestConfiguredInitialCapital;
    m_pendingBacktestConfig.commissionRate = std::max(0.0, m_backtestCommissionRate);
    m_pendingBacktestConfig.slippageRate = std::max(0.0, m_backtestSlippageRate);
    m_pendingBacktestConfig.lotSize = 100;
    m_pendingBacktestConfig.sampleFrom = m_pendingSampleFrom;
    m_pendingBacktestConfig.sampleTo = m_pendingSampleTo;
    m_pendingCustomBacktestPlan = *plan;

    m_backtestPreparing = true;
    m_pendingBacktestDatasetRequestId = ++m_pendingBacktestDatasetRequestId;
    if (m_pendingBacktestDatasetRequestId == 0) ++m_pendingBacktestDatasetRequestId;
    setBacktestStatus(QStringLiteral("正在准备回测数据集…"));
    setStatusText(QStringLiteral("严格本地回测：%1 · %2 v%3 · 准备数据集中")
                      .arg(dataset->instrument.symbol, plan->strategyId)
                      .arg(plan->version));
    if (!m_backtestDatasetService.isRunning()
        || !m_backtestDatasetService.request(
            m_pendingBacktestDatasetRequestId, dataset->instrument.symbol,
            dataset->periodSeconds, m_backtestBarCount, m_aShareUniverse)) {
        m_backtestPreparing = false;
        m_pendingBacktestDatasetRequestId = 0;
        m_pendingCustomBacktestPlan.reset();
        handleBacktestError(QStringLiteral("回测数据集准备服务不可用"));
        emit backtestRunningChanged();
        return 0;
    }
    emit backtestRunningChanged();
    return m_backtestTaskId;
}

bool TradingController::backtestResultMatchesCustomStrategy(
    const QString &strategyId, int version) const
{
    return CustomStrategyWorkspacePolicy::resultMatches(
        backtestResultConfiguration(), strategyId, version);
}

qulonglong TradingController::runSelectedBacktest()
{
    return runStrategyBacktest(m_selectedBacktestStrategyId, m_selectedBacktestParameters,
                               selectedBacktestSymbol(), selectedBacktestFrequency(),
                               m_backtestBarCount, m_backtestConfiguredInitialCapital,
                               m_backtestCommissionRate, m_backtestSlippageRate);
}

bool TradingController::selectBacktestStrategy(const QString &strategyId)
{
    if (!m_backtestStrategyRegistry.descriptor(strategyId)) {
        setStatusText(QStringLiteral("未知回测策略：%1").arg(strategyId));
        return false;
    }
    if (m_selectedBacktestStrategyId == strategyId) return true;
    QVariantMap defaults;
    m_backtestStrategyRegistry.normalizeParameters(strategyId, {}, &defaults, nullptr);
    m_selectedBacktestStrategyId = strategyId;
    m_selectedBacktestParameters = defaults;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();
    return true;
}

bool TradingController::setSelectedBacktestParameter(const QString &key, const QVariant &value)
{
    QVariantMap candidate = m_selectedBacktestParameters;
    candidate.insert(key, value);
    QVariantMap normalized;
    QString error;
    if (!m_backtestStrategyRegistry.normalizeParameters(
            m_selectedBacktestStrategyId, candidate, &normalized, &error)) {
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        return false;
    }
    m_selectedBacktestParameters = normalized;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();
    return true;
}

void TradingController::resetSelectedBacktestParameters()
{
    m_backtestStrategyRegistry.normalizeParameters(
        m_selectedBacktestStrategyId, {}, &m_selectedBacktestParameters, nullptr);
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();
}

QVariant TradingController::backtestParameterValue(const QString &key) const
{
    return m_selectedBacktestParameters.value(key);
}

bool TradingController::setBacktestConfiguration(
    const QString &frequency, int barCount, double initialCapital,
    double commissionRate, double slippageRate)
{
    QString error;
    if (!validateBacktestConfiguration(frequency, barCount, initialCapital,
                                       commissionRate, slippageRate, &error)) {
        m_backtestConfigurationError = error;
        emit backtestConfigurationChanged();
        return false;
    }
    m_backtestFrequency = frequency;
    m_backtestBarCount = barCount;
    m_backtestConfiguredInitialCapital = initialCapital;
    m_backtestCommissionRate = commissionRate;
    m_backtestSlippageRate = slippageRate;
    m_backtestConfigurationError.clear();
    emit backtestConfigurationChanged();
    return true;
}

QVariantMap TradingController::backtestStrategyDescriptor(const QString &strategyId) const
{
    const auto descriptor = m_backtestStrategyRegistry.descriptor(strategyId);
    return descriptor ? descriptor->toVariantMap() : QVariantMap{};
}

QVariantMap TradingController::validateCustomStrategyJson(const QString &json) const
{
    CustomStrategyDefinition definition;
    QString error;
    const bool valid = m_customStrategyValidator.parseAndValidate(json, &definition, &error);
    return {{QStringLiteral("valid"), valid},
            {QStringLiteral("error"), error},
            {QStringLiteral("definition"), valid ? definition.toVariantMap() : QVariantMap{}}};
}

QVariantMap TradingController::analyzeCustomStrategyJson(
    const QString &json, const QVariantMap &runtimeParameters) const
{
    CustomStrategyDefinition definition;
    QString error;
    if (!m_customStrategyValidator.parseAndValidate(json, &definition, &error)) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("compilable"), false},
                {QStringLiteral("executable"), false},
                {QStringLiteral("error"), error}};
    }
    const auto plan = m_customStrategyCompiler.compile(definition, runtimeParameters, &error);
    if (!plan) {
        return {{QStringLiteral("valid"), true},
                {QStringLiteral("compilable"), false},
                {QStringLiteral("executable"), false},
                {QStringLiteral("error"), error}};
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("compilable"), true},
            {QStringLiteral("executable"), true},
            {QStringLiteral("stage"), QStringLiteral("backtest_ready")},
            {QStringLiteral("strategyId"), plan->strategyId},
            {QStringLiteral("version"), plan->version},
            {QStringLiteral("normalizedParameters"), plan->normalizedParameters},
            {QStringLiteral("ruleSnapshot"), plan->rules.toVariantMap()},
            {QStringLiteral("minimumWarmupBars"), plan->minimumWarmupBars},
            {QStringLiteral("indicatorCount"), plan->indicators.size()},
            {QStringLiteral("conditionCount"), plan->conditions.size()},
            {QStringLiteral("error"), QString{}}};
}

bool TradingController::saveCustomStrategyJson(const QString &json)
{
    CustomStrategyDefinition definition;
    QString error;
    if (!m_customStrategyValidator.parseAndValidate(json, &definition, &error)) {
        m_customStrategyError = error;
        emit customStrategyStateChanged();
        return false;
    }
    if (!m_storageReady) {
        m_customStrategyError = QStringLiteral("本地数据库不可用，无法保存自定义策略");
        emit customStrategyStateChanged();
        return false;
    }
    if (!m_dataStore.insertCustomStrategy(definition)) {
        m_customStrategyError = QStringLiteral("保存失败：同版本内容冲突或数据库写入错误");
        emit customStrategyStateChanged();
        return false;
    }
    m_customStrategyError.clear();
    reloadCustomStrategies();
    setStatusText(QStringLiteral("已保存自定义策略：%1 v%2")
                      .arg(definition.name).arg(definition.version));
    return true;
}

qulonglong TradingController::requestSaveCustomStrategyJson(const QString &json)
{
    const quint64 requestId = m_customStrategySaveRequestGate.begin();
    m_customStrategySaveResult.clear();
    m_customStrategySaveError.clear();
    emit customStrategySaveStateChanged();
    if (!m_asyncCustomStrategyWorkspaceService.requestSave(requestId, json)) {
        m_customStrategySaveRequestGate.finish(requestId);
        m_customStrategySaveError = QStringLiteral("异步自定义策略保存服务不可用");
        emit customStrategySaveStateChanged();
        return 0;
    }
    return requestId;
}

void TradingController::finishCustomStrategySave(
    quint64 requestId, const QVariantMap &result)
{
    if (!m_customStrategySaveRequestGate.finish(requestId)) return;
    m_customStrategySaveResult = result;
    m_customStrategySaveError = result.value(QStringLiteral("error")).toString();
    m_customStrategyError = m_customStrategySaveError;
    if (result.value(QStringLiteral("success")).toBool()) {
        reloadCustomStrategies();
        setStatusText(QStringLiteral("已保存自定义策略：%1 v%2")
                          .arg(result.value(QStringLiteral("name")).toString())
                          .arg(result.value(QStringLiteral("version")).toInt()));
    } else {
        emit customStrategyStateChanged();
    }
    emit customStrategySaveStateChanged();
}

QVariantMap TradingController::customStrategyDefinition(const QString &strategyId,
                                                        int version) const
{
    for (const CustomStrategyDefinition &definition : m_customStrategyDefinitions) {
        if (definition.strategyId == strategyId
            && (version <= 0 || definition.version == version)) {
            return definition.toVariantMap();
        }
    }
    return {};
}

QString TradingController::exportCustomStrategyJson(const QString &strategyId,
                                                    int version) const
{
    for (const CustomStrategyDefinition &definition : m_customStrategyDefinitions) {
        if (definition.strategyId == strategyId
            && (version <= 0 || definition.version == version)) {
            return definition.canonicalJson();
        }
    }
    return {};
}

void TradingController::reloadCustomStrategies()
{
    m_customStrategyDefinitions = m_dataStore.loadCustomStrategies();
    m_customStrategyModel.loadDefinitions(m_customStrategyDefinitions);
    emit customStrategyStateChanged();
    emit strategyPluginStateChanged();
}

void TradingController::cancelBacktest()
{
    if (m_backtest.isRunning()) {
        m_backtest.cancel();
    } else if (m_backtestPreparing) {
        m_backtestPreparing = false;
        m_pendingBacktestHistoryRequestId = 0;
        handleBacktestCancelled();
    }
}

qulonglong TradingController::runLocalBatchBacktest(
    const QString &strategyId, const QVariantMap &strategyParameters,
    const QString &frequency, int barCount, int maximumSymbols)
{
    Q_UNUSED(barCount);
    QDate startDate = QDate::fromString(backtestSampleFromDate(), Qt::ISODate);
    QDate endDate = QDate::fromString(backtestSampleToDate(), Qt::ISODate);
    if (!startDate.isValid() || !endDate.isValid()) {
        for (const LocalHistoryDataset &dataset
             : m_workspaceInstrumentSelections.localHistoryCatalog()) {
            const QDate first = dataset.firstTime.date();
            const QDate last = dataset.lastTime.date();
            if (first.isValid() && (!startDate.isValid() || first < startDate)) startDate = first;
            if (last.isValid() && (!endDate.isValid() || last > endDate)) endDate = last;
        }
    }
    return runLocalBatchBacktestRange(
        strategyId, strategyParameters, QStringLiteral("candles"), frequency,
        startDate.toString(Qt::ISODate), endDate.toString(Qt::ISODate), maximumSymbols);
}

qulonglong TradingController::runLocalBatchBacktestRange(
    const QString &strategyId, const QVariantMap &strategyParameters,
    const QString &dataMode, const QString &frequency,
    const QString &startDateText, const QString &endDateText, int maximumSymbols,
    int holdoutTradingDays, int walkForwardFoldCount, bool segmentedResearch)
{
    if (!m_storageReady) {
        m_batchBacktestError = QStringLiteral("本地 SQLite 数据库不可用");
        m_batchBacktestStatusText = QStringLiteral("批量回测未启动");
        emit batchBacktestStateChanged();
        return 0;
    }
    if (batchBacktestRunning() || backtestRunning()) {
        m_batchBacktestError = QStringLiteral("已有单品种或批量回测正在运行");
        m_batchBacktestStatusText = QStringLiteral("批量回测未启动");
        emit batchBacktestStateChanged();
        return 0;
    }

    const QString normalizedDataMode = dataMode.trimmed().toLower();
    if (normalizedDataMode != QStringLiteral("raw_ticks")
        && normalizedDataMode != QStringLiteral("candles")) {
        m_batchBacktestError = QStringLiteral("未知批量回测数据模式：%1").arg(dataMode);
        m_batchBacktestStatusText = QStringLiteral("批量回测参数无效");
        emit batchBacktestStateChanged();
        return 0;
    }
    const BacktestDataMode mode = normalizedDataMode == QStringLiteral("candles")
        ? BacktestDataMode::Candles : BacktestDataMode::RawTicks;
    if (holdoutTradingDays > 0 && walkForwardFoldCount == 0) {
        walkForwardFoldCount = 1;
    }
    if (holdoutTradingDays < 0 || walkForwardFoldCount < 0
        || walkForwardFoldCount > 20
        || (holdoutTradingDays == 0 && walkForwardFoldCount > 0)
        || (holdoutTradingDays > 0
            && (mode != BacktestDataMode::RawTicks || holdoutTradingDays < 2
                || walkForwardFoldCount < 1))
        || (segmentedResearch
            && (mode != BacktestDataMode::RawTicks
                || holdoutTradingDays > 0 || walkForwardFoldCount > 0))) {
        m_batchBacktestError = mode == BacktestDataMode::RawTicks
            ? segmentedResearch
                ? QStringLiteral("独立分段研究不能与样本外验证同时启用")
                : QStringLiteral("样本外验证需至少 2 个交易日，walk-forward 折数为 1–20")
            : QStringLiteral("样本外验证目前只支持原始 Tick 模式");
        m_batchBacktestStatusText = QStringLiteral("批量回测参数无效");
        emit batchBacktestStateChanged();
        return 0;
    }
    const QString modeCode = backtestDataModeCode(mode);
    const QDate startDate = QDate::fromString(startDateText.trimmed(), Qt::ISODate);
    const QDate endDate = QDate::fromString(endDateText.trimmed(), Qt::ISODate);
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        m_batchBacktestError = QStringLiteral("请输入有效日期范围（YYYY-MM-DD）");
        m_batchBacktestStatusText = QStringLiteral("批量回测参数无效");
        emit batchBacktestStateChanged();
        return 0;
    }

    QVariantMap normalizedParameters;
    QString error;
    if (!m_backtestStrategyRegistry.normalizeParameters(
            strategyId, strategyParameters, &normalizedParameters, &error)
        || !m_backtestStrategyRegistry.supportsDataMode(strategyId, modeCode)
        || !validateBacktestConfiguration(
            mode == BacktestDataMode::Candles ? frequency : QStringLiteral("day"), 2,
            m_backtestConfiguredInitialCapital,
            m_backtestCommissionRate, m_backtestSlippageRate, &error)) {
        if (error.isEmpty()) {
            error = mode == BacktestDataMode::RawTicks
                ? QStringLiteral("所选策略不支持原始 Tick 回测")
                : QStringLiteral("所选策略不支持周期 K 线回测");
        }
        m_batchBacktestError = error;
        m_batchBacktestStatusText = QStringLiteral("批量回测参数无效");
        emit batchBacktestStateChanged();
        return 0;
    }

    const int periodSeconds = mode == BacktestDataMode::RawTicks ? 0
        : HistoricalDataService::periodSecondsForFrequency(frequency);
    QVector<LocalHistoryDataset> datasets;
    const int limit = std::clamp(maximumSymbols, 1, 10000);
    if (mode == BacktestDataMode::RawTicks) {
        for (const Instrument &instrument
             : m_dataStore.rawBacktestInstruments(limit)) {
            LocalHistoryDataset dataset;
            dataset.instrument = instrument;
            dataset.periodSeconds = 0;
            dataset.source = QStringLiteral("raw_trade_ticks_v1");
            datasets.append(dataset);
            if (datasets.size() >= limit) break;
        }
    } else {
        for (const LocalHistoryDataset &dataset
             : m_workspaceInstrumentSelections.localHistoryCatalog()) {
            if (dataset.periodSeconds != periodSeconds || !dataset.isBacktestEligible()) continue;
            datasets.append(dataset);
            if (datasets.size() >= limit) break;
        }
    }
    std::sort(datasets.begin(), datasets.end(),
              [](const LocalHistoryDataset &left, const LocalHistoryDataset &right) {
                  return left.instrument.symbol < right.instrument.symbol;
              });
    if (datasets.isEmpty()) {
        m_batchBacktestError = mode == BacktestDataMode::RawTicks
            ? QStringLiteral("本地目录中没有可用于回测的原始 Tick")
            : QStringLiteral("本地目录中没有可用于 %1 回测的 Tick 派生 K 线")
                  .arg(frequency);
        m_batchBacktestStatusText = QStringLiteral("批量回测未启动");
        emit batchBacktestStateChanged();
        return 0;
    }

    ++m_batchBacktestRequestId;
    if (m_batchBacktestRequestId == 0) ++m_batchBacktestRequestId;
    BatchBacktestRequest request;
    request.requestId = m_batchBacktestRequestId;
    request.datasets = std::move(datasets);
    request.strategyId = strategyId;
    request.strategyParameters = normalizedParameters;
    request.dataMode = mode;
    request.startDate = startDate;
    request.endDate = endDate;
    request.periodSeconds = periodSeconds;
    request.barCount = 0;
    request.initialCapital = m_backtestConfiguredInitialCapital;
    request.commissionRate = m_backtestCommissionRate;
    request.slippageRate = m_backtestSlippageRate;
    request.lotSize = 100;
    request.maximumActiveEngines = 20;
    request.holdoutTradingDays = holdoutTradingDays;
    request.walkForwardFoldCount = walkForwardFoldCount;
    request.segmentedResearch = segmentedResearch;

    // 独立分段研究没有连续权益身份，也不进入可恢复/可归档批次历史。
    const QString runId = segmentedResearch
        ? QString{} : QUuid::createUuid().toString(QUuid::WithoutBraces);
    return launchBatchBacktest(request, runId);
}

qulonglong TradingController::analyzeLocalRawTickAvailability(
    const QString &startDateText, const QString &endDateText,
    int maximumSymbols)
{
    // 新一代请求（包括参数无效的请求）都必须先取消旧扫描；旧 worker
    // 真正排空前 isBusy() 仍为 true，数据库搬迁因此不会与只读快照并发。
    m_asyncRawTickAvailabilityService.cancel();
    const quint64 requestId = m_rawTickAvailabilityRequestGate.begin();
    m_rawTickAvailabilityResult.clear();
    m_rawTickAvailabilityError.clear();
    emit rawTickAvailabilityStateChanged();

    const QDate startDate = QDate::fromString(
        startDateText.trimmed(), Qt::ISODate);
    const QDate endDate = QDate::fromString(
        endDateText.trimmed(), Qt::ISODate);
    if (!m_storageReady || m_storageRelocationRunning) {
        m_rawTickAvailabilityRequestGate.finish(requestId);
        m_rawTickAvailabilityError = QStringLiteral("本地 SQLite 数据库不可用");
        emit rawTickAvailabilityStateChanged();
        return requestId;
    }
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        m_rawTickAvailabilityRequestGate.finish(requestId);
        m_rawTickAvailabilityError =
            QStringLiteral("请输入有效日期范围（YYYY-MM-DD）");
        emit rawTickAvailabilityStateChanged();
        return requestId;
    }

    const int limit = std::clamp(maximumSymbols, 1, 10000);
    if (!m_asyncRawTickAvailabilityService.request(
            m_dataStore.databasePath(), requestId, startDate, endDate,
            limit)) {
        m_rawTickAvailabilityRequestGate.finish(requestId);
        m_rawTickAvailabilityError =
            QStringLiteral("原始 Tick 可用区间分析未能启动");
        emit rawTickAvailabilityStateChanged();
    }
    return requestId;
}

qulonglong TradingController::launchBatchBacktest(
    const BatchBacktestRequest &request, const QString &runId)
{
    m_batchBacktestModel.clear();
    m_batchRetainedResults.clear();
    m_batchBacktestRequestedCount = request.datasets.size();
    m_batchBacktestCompletedCount = 0;
    m_batchBacktestSucceededCount = 0;
    m_batchBacktestFailedCount = 0;
    m_batchBacktestSegmentedResearch = request.segmentedResearch;
    m_batchBacktestError.clear();
    m_batchBacktestStatusText = request.segmentedResearch
        ? QStringLiteral("独立分段研究准备中：0/%1")
              .arg(m_batchBacktestRequestedCount)
        : QStringLiteral("批量回测准备中：0/%1")
              .arg(m_batchBacktestRequestedCount);

    m_activeBatchBacktestRun = {};
    if (m_storageReady && !runId.isEmpty()) {
        m_activeBatchBacktestRun.runId = runId;
        m_activeBatchBacktestRun.createdAt = QDateTime::currentDateTimeUtc();
        m_activeBatchBacktestRun.strategyId = request.strategyId;
        m_activeBatchBacktestRun.strategyParameters = request.strategyParameters;
        m_activeBatchBacktestRun.dataMode = request.dataMode;
        m_activeBatchBacktestRun.startDate = request.startDate;
        m_activeBatchBacktestRun.endDate = request.endDate;
        m_activeBatchBacktestRun.frequency = request.dataMode == BacktestDataMode::RawTicks
            ? QStringLiteral("tick") : localHistoryFrequency(request.periodSeconds);
        m_activeBatchBacktestRun.periodSeconds = request.periodSeconds;
        m_activeBatchBacktestRun.barCount = request.barCount;
        m_activeBatchBacktestRun.initialCapital = request.initialCapital;
        m_activeBatchBacktestRun.commissionRate = request.commissionRate;
        m_activeBatchBacktestRun.slippageRate = request.slippageRate;
        m_activeBatchBacktestRun.lotSize = request.lotSize;
        m_activeBatchBacktestRun.holdoutTradingDays = request.holdoutTradingDays;
        m_activeBatchBacktestRun.walkForwardFoldCount =
            request.walkForwardFoldCount;
        m_activeBatchBacktestRun.totalSymbols = request.datasets.size();
        m_activeBatchBacktestRun.status = QStringLiteral("running");
        m_dataStore.upsertBatchBacktestRun(m_activeBatchBacktestRun);
        for (const LocalHistoryDataset &dataset : request.datasets) {
            BatchBacktestRunItem item;
            item.runId = runId;
            item.instrument = dataset.instrument;
            item.status = QStringLiteral("ready");
            m_dataStore.upsertBatchBacktestRunItem(item);
        }
    }
    m_activeBatchBacktestRunId = m_activeBatchBacktestRun.runId;

    if (!m_batchBacktestService.start(request)) {
        m_batchBacktestError = QStringLiteral("批量回测后台线程未能启动");
        m_batchBacktestStatusText = QStringLiteral("批量回测未启动");
        m_activeBatchBacktestRunId.clear();
        m_activeBatchBacktestRun = {};
        emit batchBacktestStateChanged();
        return 0;
    }
    reloadBatchBacktestRunHistory();
    emit batchBacktestStateChanged();
    return m_batchBacktestRequestId;
}

void TradingController::cancelLocalBatchBacktest()
{
    m_batchBacktestService.cancel();
}

qulonglong TradingController::archiveBatchBacktestItem(const QString &symbol)
{
    const QString normalized = symbol.trimmed();
    m_batchBacktestArchiveError.clear();
    m_batchBacktestArchiveResultId.clear();
    emit batchBacktestStateChanged();
    if (m_batchBacktestArchiveRequestGate.loading()) {
        m_batchBacktestArchiveError = QStringLiteral(
            "已有回测归档任务正在后台执行，请等待完成");
        emit batchBacktestStateChanged();
        return 0;
    }
    if (m_batchBacktestService.isRunning()) {
        m_batchBacktestArchiveError = QStringLiteral(
            "请先等待批量回测完成再归档单项结果");
        emit batchBacktestStateChanged();
        return 0;
    }
    if (!m_storageReady || !m_asyncBacktestArchiveService.isRunning()) {
        m_batchBacktestArchiveError = QStringLiteral("异步回测归档服务不可用");
        emit batchBacktestStateChanged();
        return 0;
    }
    const std::shared_ptr<const BacktestResult> retained =
        m_batchRetainedResults.sharedResult(normalized);
    if (!retained) {
        m_batchBacktestArchiveError = QStringLiteral(
            "结果已释放或不存在：%1；请重新运行批量回测后再归档").arg(normalized);
        emit batchBacktestStateChanged();
        return 0;
    }
    if (retained->config.datasetIdentity.isEmpty()) {
        m_batchBacktestArchiveError = QStringLiteral(
            "批量结果缺少数据集身份，拒绝归档：%1").arg(normalized);
        emit batchBacktestStateChanged();
        return 0;
    }

    const quint64 requestId = m_batchBacktestArchiveRequestGate.begin();
    const QString resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_batchBacktestArchiveSymbol = normalized;
    if (!m_asyncBacktestArchiveService.request(
            requestId, resultId, QDateTime::currentDateTimeUtc(), retained)) {
        m_batchBacktestArchiveRequestGate.finish(requestId);
        m_batchBacktestArchiveError = QStringLiteral(
            "异步回测归档任务提交失败：%1").arg(normalized);
        emit batchBacktestStateChanged();
        return 0;
    }
    setStatusText(QStringLiteral("正在后台归档：%1").arg(normalized));
    emit batchBacktestStateChanged();
    return requestId;
}

void TradingController::handleBatchBacktestArchiveFinished(
    quint64 requestId, const QString &symbol,
    const QString &resultId, const QString &error)
{
    if (!m_batchBacktestArchiveRequestGate.finish(requestId)) return;
    m_batchBacktestArchiveSymbol = symbol;
    m_batchBacktestArchiveResultId = resultId;
    m_batchBacktestArchiveError = error;
    if (error.isEmpty() && !resultId.isEmpty()) {
        m_batchBacktestModel.updateArchiveState(symbol, resultId);
        refreshBacktestArchiveList();
        setStatusText(QStringLiteral("批量单项已归档：%1 · %2")
                          .arg(symbol, resultId));
    } else {
        m_batchBacktestArchiveResultId.clear();
        if (m_batchBacktestArchiveError.isEmpty()) {
            m_batchBacktestArchiveError = QStringLiteral(
                "回测归档失败：%1").arg(symbol);
        }
        setStatusText(m_batchBacktestArchiveError);
    }
    emit batchBacktestStateChanged();
}

QString TradingController::resumeBatchBacktest(const QString &runId)
{
    m_batchBacktestError.clear();
    emit batchBacktestStateChanged();
    if (!m_storageReady) {
        m_batchBacktestError = QStringLiteral("本地 SQLite 数据库不可用");
        emit batchBacktestStateChanged();
        return {};
    }
    if (batchBacktestRunning() || backtestRunning()) {
        m_batchBacktestError = QStringLiteral("已有单品种或批量回测正在运行");
        emit batchBacktestStateChanged();
        return {};
    }

    const QString normalized = runId.trimmed();
    const QVector<BatchBacktestRunSummary> runs =
        m_dataStore.loadBatchBacktestRuns(1000);
    const BatchBacktestRunSummary *source = nullptr;
    for (const BatchBacktestRunSummary &run : runs) {
        if (run.runId == normalized) {
            source = &run;
            break;
        }
    }
    if (!source) {
        m_batchBacktestError = QStringLiteral("未找到批量回测运行：%1")
                                   .arg(normalized);
        emit batchBacktestStateChanged();
        return {};
    }
    if (source->status == QStringLiteral("running")) {
        m_batchBacktestError = QStringLiteral("该批量任务仍未完成，不能恢复");
        emit batchBacktestStateChanged();
        return {};
    }
    if (!source->startDate.isValid() || !source->endDate.isValid()) {
        m_batchBacktestError = QStringLiteral(
            "该运行来自旧版统一根数任务，缺少日期范围，不能自动恢复；请按日期重新运行");
        emit batchBacktestStateChanged();
        return {};
    }

    QVector<LocalHistoryDataset> datasets;
    const QVector<BatchBacktestRunItem> items =
        m_dataStore.loadBatchBacktestRunItems(normalized);
    const QVector<Instrument> rawInstruments = source->dataMode == BacktestDataMode::RawTicks
        ? m_dataStore.rawBacktestInstruments() : QVector<Instrument>{};
    for (const BatchBacktestRunItem &item : items) {
        if (item.status == QStringLiteral("succeeded")) continue;
        if (source->dataMode == BacktestDataMode::RawTicks) {
            for (const Instrument &instrument : rawInstruments) {
                if (instrument.symbol != item.instrument.symbol) continue;
                LocalHistoryDataset dataset;
                dataset.instrument = instrument;
                dataset.periodSeconds = 0;
                dataset.source = QStringLiteral("raw_trade_ticks_v1");
                datasets.append(dataset);
                break;
            }
        } else {
            for (const LocalHistoryDataset &candidate
                 : m_workspaceInstrumentSelections.localHistoryCatalog()) {
                if (candidate.instrument.symbol == item.instrument.symbol
                    && candidate.periodSeconds == source->periodSeconds
                    && candidate.isBacktestEligible()) {
                    datasets.append(candidate);
                    break;
                }
            }
        }
    }
    if (datasets.isEmpty()) {
        m_batchBacktestError = source->dataMode == BacktestDataMode::RawTicks
            ? QStringLiteral("没有可恢复的本地原始 Tick 数据集；请先同步或导入数据")
            : QStringLiteral("没有可恢复的本地 Tick 派生 K 线；请先同步、导入或生成周期缓存");
        emit batchBacktestStateChanged();
        return {};
    }

    ++m_batchBacktestRequestId;
    if (m_batchBacktestRequestId == 0) ++m_batchBacktestRequestId;
    BatchBacktestRequest request;
    request.requestId = m_batchBacktestRequestId;
    request.datasets = std::move(datasets);
    request.strategyId = source->strategyId;
    request.strategyParameters = source->strategyParameters;
    request.dataMode = source->dataMode;
    request.startDate = source->startDate;
    request.endDate = source->endDate;
    request.periodSeconds = source->periodSeconds;
    request.barCount = source->barCount;
    request.initialCapital = source->initialCapital;
    request.commissionRate = source->commissionRate;
    request.slippageRate = source->slippageRate;
    request.lotSize = source->lotSize;
    request.maximumActiveEngines = 20;
    request.holdoutTradingDays = source->holdoutTradingDays;
    request.walkForwardFoldCount = source->walkForwardFoldCount;

    const QString newRunId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (launchBatchBacktest(request, newRunId) == 0) return {};
    return newRunId;
}

void TradingController::reloadBatchBacktestRunHistory()
{
    m_batchBacktestRunHistory.clear();
    if (!m_storageReady) return;
    const QVector<BatchBacktestRunSummary> runs =
        m_dataStore.loadBatchBacktestRuns(20);
    m_batchBacktestRunHistory.reserve(runs.size());
    for (const BatchBacktestRunSummary &run : runs) {
        m_batchBacktestRunHistory.append(run.toVariantMap());
    }
}

void TradingController::recoverBatchBacktestRuns()
{
    if (!m_storageReady) return;
    const QVector<BatchBacktestRunSummary> runs =
        m_dataStore.loadBatchBacktestRuns(1000);
    bool changed = false;
    for (const BatchBacktestRunSummary &run : runs) {
        if (run.status != QStringLiteral("running")) continue;
        const QVector<BatchBacktestRunItem> items =
            m_dataStore.loadBatchBacktestRunItems(run.runId);
        for (const BatchBacktestRunItem &item : items) {
            if (item.status != QStringLiteral("ready")
                && item.status != QStringLiteral("running")) {
                continue;
            }
            BatchBacktestRunItem updated = item;
            updated.status = QStringLiteral("cancelled");
            updated.error = QStringLiteral("应用启动恢复时终止未完成批量任务");
            m_dataStore.upsertBatchBacktestRunItem(updated);
            changed = true;
        }
        BatchBacktestRunSummary updatedRun = run;
        updatedRun.completedAt = QDateTime::currentDateTimeUtc();
        updatedRun.status = QStringLiteral("cancelled");
        updatedRun.error = QStringLiteral("应用启动恢复时终止未完成批量任务");
        m_dataStore.upsertBatchBacktestRun(updatedRun);
        changed = true;
    }
    if (changed) reloadBatchBacktestRunHistory();
}

QVariantList TradingController::batchBacktestRunHistory() const
{
    return m_batchBacktestRunHistory;
}

qulonglong TradingController::runPortfolioBacktest(
    const QString &portfolioJson, int barCount)
{
    m_portfolioBacktestError.clear();
    emit portfolioBacktestStateChanged();
    if (!m_storageReady) {
        m_portfolioBacktestError = QStringLiteral("本地 SQLite 数据库不可用");
        m_portfolioBacktestStatusText = QStringLiteral("组合回测未启动");
        emit portfolioBacktestStateChanged();
        return 0;
    }
    if (m_portfolioBacktestService.isRunning() || batchBacktestRunning()
        || backtestRunning()) {
        m_portfolioBacktestError = QStringLiteral("已有回测任务正在运行");
        m_portfolioBacktestStatusText = QStringLiteral("组合回测未启动");
        emit portfolioBacktestStateChanged();
        return 0;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        portfolioJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_portfolioBacktestError = QStringLiteral("组合定义 JSON 无效");
        m_portfolioBacktestStatusText = QStringLiteral("组合回测未启动");
        emit portfolioBacktestStateChanged();
        return 0;
    }
    const PortfolioDefinition definition = portfolioDefinitionFromMap(
        document.object().toVariantMap());
    QString validationError;
    if (!definition.isValid(&validationError) || barCount < 2) {
        m_portfolioBacktestError = validationError.isEmpty()
            ? QStringLiteral("回测根数必须大于 1") : validationError;
        m_portfolioBacktestStatusText = QStringLiteral("组合回测未启动");
        emit portfolioBacktestStateChanged();
        return 0;
    }

    ++m_portfolioBacktestRequestId;
    if (m_portfolioBacktestRequestId == 0) ++m_portfolioBacktestRequestId;
    m_displayedPortfolioResult.reset();
    m_selectedPortfolioArchiveId.clear();
    m_portfolioEquityCurveModel.clear();
    m_portfolioTradeModel.clear();
    m_portfolioBacktestStatusText = QStringLiteral(
        "组合回测准备中：%1 腿 · %2").arg(definition.legs.size()).arg(definition.name);
    if (!m_portfolioBacktestService.request(
            m_portfolioBacktestRequestId, definition, barCount,
            m_aShareUniverse)) {
        m_portfolioBacktestError = QStringLiteral("组合回测服务不可用");
        m_portfolioBacktestStatusText = QStringLiteral("组合回测未启动");
        emit portfolioBacktestStateChanged();
        return 0;
    }
    emit portfolioBacktestStateChanged();
    return m_portfolioBacktestRequestId;
}

void TradingController::cancelPortfolioBacktest()
{
    if (!m_portfolioBacktestService.isRunning()) return;
    m_portfolioBacktestService.cancel();
    m_portfolioBacktestStatusText = QStringLiteral("正在取消组合回测…");
    emit portfolioBacktestStateChanged();
}

qulonglong TradingController::runCrossSectionalBacktest(
    const QString &strategyJson, int barCount)
{
    const int periodSeconds = selectedBacktestPeriodSeconds() > 0
        ? selectedBacktestPeriodSeconds() : 60;
    return runCrossSectionalBacktestForPeriod(strategyJson, barCount, periodSeconds);
}

QVariantMap TradingController::validateCrossSectionalBacktestRequest(
    const QString &strategyJson, int barCount, int periodSeconds) const
{
    if (!m_storageReady) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), QStringLiteral("本地 SQLite 数据库不可用")}};
    }
    CustomStrategyDefinition definition;
    QString error;
    if (!m_customStrategyValidator.parseAndValidate(strategyJson, &definition, &error)) {
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), error}};
    }
    return CrossSectionalBacktestService::validateLocalDatasets(
        definition, periodSeconds, barCount,
        m_workspaceInstrumentSelections.localHistoryCatalog());
}

qulonglong TradingController::runCrossSectionalBacktestForPeriod(
    const QString &strategyJson, int barCount, int periodSeconds)
{
    m_crossSectionalBacktestError.clear();
    emit crossSectionalBacktestStateChanged();
    if (!m_storageReady) {
        m_crossSectionalBacktestError = QStringLiteral("本地 SQLite 数据库不可用");
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    if (m_crossSectionalBacktestService.isRunning() || batchBacktestRunning()
        || backtestRunning() || portfolioBacktestRunning()) {
        m_crossSectionalBacktestError = QStringLiteral("已有回测任务正在运行");
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    CustomStrategyDefinition definition;
    QString validationError;
    if (!m_customStrategyValidator.parseAndValidate(strategyJson, &definition,
                                                    &validationError)) {
        m_crossSectionalBacktestError = validationError;
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    const QVariantMap availability = CrossSectionalBacktestService::validateLocalDatasets(
        definition, periodSeconds, barCount,
        m_workspaceInstrumentSelections.localHistoryCatalog());
    if (!availability.value(QStringLiteral("valid")).toBool()) {
        m_crossSectionalBacktestError = availability.value(QStringLiteral("error")).toString();
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }

    FactorDefinitionVersion registeredFactor;
    if (!m_crossSectionalFactorResearchAlias.isEmpty()) {
        QJsonObject factorObject;
        const QJsonArray indicators = definition.document
            .value(QStringLiteral("crossIndicators")).toArray();
        for (const QJsonValue &value : indicators) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("alias")).toString()
                == m_crossSectionalFactorResearchAlias) {
                factorObject = candidate;
                break;
            }
        }
        if (factorObject.isEmpty()) {
            m_crossSectionalBacktestError = QStringLiteral(
                "策略不存在因子研究别名：%1")
                .arg(m_crossSectionalFactorResearchAlias);
            m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
            emit crossSectionalBacktestStateChanged();
            return 0;
        }
        registeredFactor.factorId = QStringLiteral("%1.%2")
            .arg(definition.strategyId, m_crossSectionalFactorResearchAlias);
        registeredFactor.name = QStringLiteral("%1 / %2")
            .arg(definition.name, m_crossSectionalFactorResearchAlias);
        registeredFactor.alias = m_crossSectionalFactorResearchAlias;
        registeredFactor.sourceStrategyId = definition.strategyId;
        registeredFactor.sourceStrategyVersion = definition.version;
        registeredFactor.definition = factorObject.toVariantMap();
        registeredFactor.preprocessing = m_crossSectionalFactorPreprocessing;
        bool reused = false;
        QString registryError;
        if (!m_dataStore.registerFactorDefinitionVersion(
                registeredFactor, &reused, &registryError)) {
            m_crossSectionalBacktestError = QStringLiteral(
                "因子定义版本注册失败：%1").arg(registryError);
            m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
            emit crossSectionalBacktestStateChanged();
            return 0;
        }
        m_crossSectionalFactorDefinitionCatalog.clear();
        for (const FactorDefinitionVersion &factor :
             m_dataStore.loadFactorDefinitionVersions()) {
            m_crossSectionalFactorDefinitionCatalog.append(factor.toVariantMap());
        }
    }

    PortfolioDefinition portfolioDefinition;
    portfolioDefinition.portfolioId = QStringLiteral("cross_sectional_ui");
    portfolioDefinition.name = definition.name;
    portfolioDefinition.periodSeconds = periodSeconds;
    portfolioDefinition.initialCapital = m_backtestConfiguredInitialCapital;
    portfolioDefinition.commissionRate = m_backtestCommissionRate;
    portfolioDefinition.slippageRate = m_backtestSlippageRate;
    portfolioDefinition.lotSize = 100;
    portfolioDefinition.tPlusOne = true;
    portfolioDefinition.maximumLegWeight =
        m_crossSectionalMaximumLegWeight;
    portfolioDefinition.maximumActiveLegs =
        m_crossSectionalMaximumActiveLegs;
    portfolioDefinition.maximumConsecutiveMissingBars =
        m_crossSectionalMaximumConsecutiveMissingBars;
    portfolioDefinition.maximumPortfolioLossPercent =
        m_crossSectionalMaximumPortfolioLossPercent;
    portfolioDefinition.maximumPortfolioDrawdownPercent =
        m_crossSectionalMaximumPortfolioDrawdownPercent;
    portfolioDefinition.maximumIndustryWeight =
        m_crossSectionalMaximumIndustryWeight;
    portfolioDefinition.industryDatasetId =
        m_crossSectionalIndustryDatasetId;
    portfolioDefinition.factorExposureAlias =
        m_crossSectionalFactorExposureAlias;
    portfolioDefinition.maximumAbsoluteFactorExposure =
        m_crossSectionalMaximumAbsoluteFactorExposure;
    portfolioDefinition.factorResearchAlias =
        m_crossSectionalFactorResearchAlias;
    if (!m_crossSectionalFactorResearchAlias.isEmpty()) {
        portfolioDefinition.factorDefinitionId = registeredFactor.factorId;
        portfolioDefinition.factorDefinitionVersion = registeredFactor.version;
        portfolioDefinition.factorDefinitionSha256 =
            registeredFactor.contentSha256;
        portfolioDefinition.factorPreprocessing =
            registeredFactor.preprocessing;
        portfolioDefinition.factorResearchCost =
            m_crossSectionalFactorResearchCost;
        portfolioDefinition.factorNeutralization =
            m_crossSectionalFactorNeutralization;
    }

    ++m_crossSectionalBacktestRequestId;
    if (m_crossSectionalBacktestRequestId == 0) ++m_crossSectionalBacktestRequestId;
    m_displayedCrossSectionalResult.reset();
    m_displayedCrossSectionalCompletedAt = {};
    m_selectedCrossSectionalArchiveId.clear();
    m_crossSectionalResultHistoricalSnapshot = false;
    m_crossSectionalEquityCurveModel.clear();
    m_crossSectionalTradeModel.clear();
    const int symbolCount = definition.document.value(QStringLiteral("universe"))
        .toObject().value(QStringLiteral("symbols")).toArray().size();
    m_crossSectionalBacktestStatusText = QStringLiteral(
        "多股回测准备中：%1 标的").arg(symbolCount);
    if (!m_crossSectionalBacktestService.request(
            m_crossSectionalBacktestRequestId, definition, {}, portfolioDefinition,
            barCount, m_aShareUniverse)) {
        m_crossSectionalBacktestError = QStringLiteral("多股回测服务不可用");
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    emit crossSectionalBacktestStateChanged();
    return m_crossSectionalBacktestRequestId;
}

void TradingController::cancelCrossSectionalBacktest()
{
    if (!m_crossSectionalBacktestService.isRunning()) return;
    m_crossSectionalBacktestService.cancel();
    m_crossSectionalBacktestStatusText = QStringLiteral("正在取消多股回测…");
    emit crossSectionalBacktestStateChanged();
}

void TradingController::handleCrossSectionalBacktestCompleted(
    quint64 requestId, const PortfolioResult &result)
{
    if (requestId != m_crossSectionalBacktestRequestId) return;
    m_displayedCrossSectionalResult = result;
    m_displayedCrossSectionalCompletedAt = QDateTime::currentDateTimeUtc();
    m_selectedCrossSectionalArchiveId.clear();
    m_crossSectionalResultHistoricalSnapshot = false;
    m_crossSectionalEquityCurveModel.loadPoints(portfolioEquityPoints(result));
    m_crossSectionalTradeModel.loadTrades(result.trades);
    m_crossSectionalBacktestError.clear();
    m_crossSectionalBacktestStatusText = QStringLiteral(
        "多股回测完成：收益 %1%，%2 笔调仓")
        .arg(result.summary.totalReturnPercent, 0, 'f', 2)
        .arg(result.summary.totalTrades);
    if (m_storageReady) {
        PortfolioArchiveRecord archive;
        archive.resultId = QStringLiteral("cross-sectional-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        archive.completedAt = m_displayedCrossSectionalCompletedAt;
        archive.result = result;
        QString archiveError;
        if (m_dataStore.savePortfolioBacktestArchive(archive, &archiveError)) {
            m_selectedCrossSectionalArchiveId = archive.resultId;
            refreshCrossSectionalBacktestArchiveList();
        } else {
            m_crossSectionalBacktestError = QStringLiteral("多股结果未保存：%1")
                .arg(archiveError.isEmpty()
                         ? QStringLiteral("SQLite 归档写入失败") : archiveError);
        }
    }
    emit crossSectionalBacktestStateChanged();
}

void TradingController::handleCrossSectionalBacktestFailed(
    quint64 requestId, const QString &message)
{
    if (requestId != m_crossSectionalBacktestRequestId) return;
    m_crossSectionalBacktestError = message;
    m_crossSectionalBacktestStatusText = QStringLiteral("多股回测失败");
    emit crossSectionalBacktestStateChanged();
}

void TradingController::handlePortfolioBacktestCompleted(
    quint64 requestId, const PortfolioResult &result)
{
    if (requestId != m_portfolioBacktestRequestId) return;
    m_displayedPortfolioResult = result;
    m_displayedPortfolioCompletedAt = QDateTime::currentDateTimeUtc();
    m_selectedPortfolioArchiveId.clear();
    m_portfolioEquityCurveModel.loadPoints(portfolioEquityPoints(result));
    m_portfolioTradeModel.loadTrades(result.trades);
    m_portfolioBacktestError.clear();
    m_portfolioBacktestStatusText = QStringLiteral(
        "组合回测完成：收益 %1%，%2 笔调仓")
        .arg(result.summary.totalReturnPercent, 0, 'f', 2)
        .arg(result.summary.totalTrades);
    if (m_storageReady) {
        PortfolioArchiveRecord archive;
        archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        archive.completedAt = m_displayedPortfolioCompletedAt;
        archive.result = result;
        if (m_dataStore.savePortfolioBacktestArchive(archive)) {
            m_selectedPortfolioArchiveId = archive.resultId;
            refreshPortfolioBacktestArchiveList();
        } else {
            m_portfolioBacktestError = QStringLiteral(
                "组合结果未保存：SQLite 归档写入失败");
        }
    }
    emit portfolioBacktestStateChanged();
}

bool TradingController::loadPortfolioBacktestArchive(const QString &resultId)
{
    m_portfolioArchiveError.clear();
    m_portfolioArchiveLoading = true;
    emit portfolioBacktestStateChanged();
    const auto record = m_dataStore.loadPortfolioBacktestArchive(resultId);
    m_portfolioArchiveLoading = false;
    if (!record) {
        m_portfolioArchiveError = QStringLiteral(
            "组合归档加载失败或不存在：%1").arg(resultId);
        emit portfolioBacktestStateChanged();
        return false;
    }
    if (record->result.resultKind != QStringLiteral("fixed_portfolio")) {
        m_portfolioArchiveError = QStringLiteral("所选归档不是固定组合回测结果");
        emit portfolioBacktestStateChanged();
        return false;
    }
    m_displayedPortfolioResult = record->result;
    m_displayedPortfolioCompletedAt = record->completedAt;
    m_selectedPortfolioArchiveId = record->resultId;
    m_portfolioEquityCurveModel.loadPoints(
        portfolioEquityPoints(record->result));
    m_portfolioTradeModel.loadTrades(record->result.trades);
    emit portfolioBacktestStateChanged();
    return true;
}

void TradingController::refreshPortfolioBacktestArchiveList()
{
    QVector<PortfolioArchiveSummary> filtered;
    if (m_storageReady) {
        const QVector<PortfolioArchiveSummary> summaries =
            m_dataStore.loadPortfolioBacktestArchiveSummaries(100);
        for (const PortfolioArchiveSummary &summary : summaries) {
            if (summary.resultKind == QStringLiteral("fixed_portfolio"))
                filtered.append(summary);
        }
        if (filtered.size() > 50) filtered.resize(50);
    }
    m_portfolioArchiveModel.loadSummaries(filtered);
    emit portfolioBacktestStateChanged();
}

bool TradingController::portfolioBacktestRunning() const
{
    return m_portfolioBacktestService.isRunning();
}

QString TradingController::portfolioBacktestStatusText() const
{
    return m_portfolioBacktestStatusText;
}

QString TradingController::portfolioBacktestError() const
{
    return m_portfolioBacktestError;
}

QVariantMap TradingController::portfolioResultConfiguration() const
{
    if (!m_displayedPortfolioResult) return {};
    const PortfolioResult &result = *m_displayedPortfolioResult;
    QVariantMap map = result.definition.toVariantMap();
    map.insert(QStringLiteral("completedAt"), m_displayedPortfolioCompletedAt);
    map.insert(QStringLiteral("barCount"), result.equityCurve.size());
    int staleValuationPointCount = 0;
    for (const PortfolioBar &bar : result.equityCurve) {
        if (std::any_of(bar.positions.cbegin(), bar.positions.cend(),
                        [](const PortfolioPosition &position) {
            return position.priceStale;
        })) {
            ++staleValuationPointCount;
        }
    }
    map.insert(QStringLiteral("staleValuationPointCount"),
               staleValuationPointCount);
    map.insert(QStringLiteral("valuationCalendar"),
               result.riskSummary.marketCalendarCoverageComplete
                   ? QStringLiteral("authoritative_market_calendar")
                   : QStringLiteral("observed_union"));
    map.insert(QStringLiteral("datasetVersions"),
               QVariant(result.datasetVersions));
    map.insert(QStringLiteral("finalEquity"), result.summary.finalEquity);
    map.insert(QStringLiteral("totalReturnPercent"),
               result.summary.totalReturnPercent);
    map.insert(QStringLiteral("maxDrawdownPercent"),
               result.summary.maxDrawdownPercent);
    map.insert(QStringLiteral("sharpeRatio"), result.summary.sharpeRatio);
    map.insert(QStringLiteral("totalCommission"),
               result.summary.totalCommission);
    map.insert(QStringLiteral("totalTrades"), result.summary.totalTrades);
    map.insert(QStringLiteral("riskSummary"),
               result.riskSummary.toVariantMap());
    map.insert(QStringLiteral("archived"),
               !m_selectedPortfolioArchiveId.isEmpty());
    map.insert(QStringLiteral("resultId"), m_selectedPortfolioArchiveId);
    return map;
}

QAbstractItemModel *TradingController::portfolioEquityCurveModel()
{
    return &m_portfolioEquityCurveModel;
}

QAbstractItemModel *TradingController::portfolioTradeModel()
{
    return &m_portfolioTradeModel;
}

QAbstractItemModel *TradingController::portfolioArchiveModel()
{
    return &m_portfolioArchiveModel;
}

int TradingController::portfolioArchiveCount() const
{
    return m_portfolioArchiveModel.count();
}

bool TradingController::portfolioArchiveLoading() const
{
    return m_portfolioArchiveLoading;
}

QString TradingController::portfolioArchiveError() const
{
    return m_portfolioArchiveError;
}

bool TradingController::crossSectionalBacktestRunning() const
{
    return m_crossSectionalBacktestService.isRunning();
}

QString TradingController::crossSectionalBacktestStatusText() const
{
    return m_crossSectionalBacktestStatusText;
}

QString TradingController::crossSectionalBacktestError() const
{
    return m_crossSectionalBacktestError;
}

QVariantMap TradingController::crossSectionalRiskConfiguration() const
{
    return {{QStringLiteral("maximumLegWeight"),
             m_crossSectionalMaximumLegWeight},
            {QStringLiteral("maximumActiveLegs"),
             m_crossSectionalMaximumActiveLegs},
            {QStringLiteral("maximumConsecutiveMissingBars"),
             m_crossSectionalMaximumConsecutiveMissingBars},
            {QStringLiteral("maximumPortfolioLossPercent"),
             m_crossSectionalMaximumPortfolioLossPercent},
            {QStringLiteral("maximumPortfolioDrawdownPercent"),
             m_crossSectionalMaximumPortfolioDrawdownPercent},
            {QStringLiteral("maximumIndustryWeight"),
             m_crossSectionalMaximumIndustryWeight},
            {QStringLiteral("industryDatasetId"),
             m_crossSectionalIndustryDatasetId},
            {QStringLiteral("factorExposureAlias"),
             m_crossSectionalFactorExposureAlias},
            {QStringLiteral("maximumAbsoluteFactorExposure"),
             m_crossSectionalMaximumAbsoluteFactorExposure},
            {QStringLiteral("factorResearchAlias"),
             m_crossSectionalFactorResearchAlias},
            {QStringLiteral("factorPreprocessing"),
             m_crossSectionalFactorPreprocessing.toVariantMap()},
            {QStringLiteral("factorResearchCost"),
             m_crossSectionalFactorResearchCost.toVariantMap()},
            {QStringLiteral("factorNeutralization"),
             m_crossSectionalFactorNeutralization.toVariantMap()},
            {QStringLiteral("tPlusOne"), true}};
}

QString TradingController::crossSectionalRiskConfigurationError() const
{
    return m_crossSectionalRiskConfigurationError;
}

QVariantList TradingController::crossSectionalFactorDefinitionCatalog() const
{
    return m_crossSectionalFactorDefinitionCatalog;
}

bool TradingController::setCrossSectionalRiskConfiguration(
    double maximumLegWeight, int maximumActiveLegs,
    int maximumConsecutiveMissingBars)
{
    const QVariantMap validation =
        CrossSectionalBacktestService::validateRiskConfiguration(
            maximumLegWeight, maximumActiveLegs,
            maximumConsecutiveMissingBars);
    m_crossSectionalRiskConfigurationError =
        validation.value(QStringLiteral("error")).toString();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_crossSectionalMaximumLegWeight = maximumLegWeight;
    m_crossSectionalMaximumActiveLegs = maximumActiveLegs;
    m_crossSectionalMaximumConsecutiveMissingBars =
        maximumConsecutiveMissingBars;
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::setCrossSectionalCircuitBreakerConfiguration(
    double maximumPortfolioLossPercent,
    double maximumPortfolioDrawdownPercent)
{
    const QVariantMap validation =
        CrossSectionalBacktestService::validateCircuitBreakerConfiguration(
            maximumPortfolioLossPercent,
            maximumPortfolioDrawdownPercent);
    m_crossSectionalRiskConfigurationError =
        validation.value(QStringLiteral("error")).toString();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_crossSectionalMaximumPortfolioLossPercent =
        maximumPortfolioLossPercent;
    m_crossSectionalMaximumPortfolioDrawdownPercent =
        maximumPortfolioDrawdownPercent;
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::setCrossSectionalIndustryConfiguration(
    double maximumIndustryWeight, const QString &industryDatasetId)
{
    const QVariantMap validation =
        CrossSectionalBacktestService::validateIndustryConfiguration(
            maximumIndustryWeight, industryDatasetId);
    m_crossSectionalRiskConfigurationError =
        validation.value(QStringLiteral("error")).toString();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_crossSectionalMaximumIndustryWeight = maximumIndustryWeight;
    m_crossSectionalIndustryDatasetId =
        validation.value(QStringLiteral("industryDatasetId")).toString();
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::setCrossSectionalFactorExposureConfiguration(
    double maximumAbsoluteFactorExposure, const QString &factorExposureAlias)
{
    const QVariantMap validation =
        CrossSectionalBacktestService::validateFactorExposureConfiguration(
            maximumAbsoluteFactorExposure, factorExposureAlias);
    m_crossSectionalRiskConfigurationError =
        validation.value(QStringLiteral("error")).toString();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_crossSectionalMaximumAbsoluteFactorExposure =
        maximumAbsoluteFactorExposure;
    m_crossSectionalFactorExposureAlias = maximumAbsoluteFactorExposure > 0.0
        ? validation.value(QStringLiteral("factorExposureAlias")).toString()
        : QString{};
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::setCrossSectionalFactorResearchConfiguration(
    const QString &factorAlias, double lowerPercentile,
    double upperPercentile, int minimumPairs, double oneWayCostBps,
    bool industryNeutralization, int minimumIndustryGroupSize)
{
    const QVariantMap validation =
        CrossSectionalBacktestService::validateFactorResearchConfiguration(
            factorAlias, lowerPercentile / 100.0,
            upperPercentile / 100.0, minimumPairs, oneWayCostBps,
            industryNeutralization, minimumIndustryGroupSize,
            m_crossSectionalIndustryDatasetId);
    m_crossSectionalRiskConfigurationError =
        validation.value(QStringLiteral("error")).toString();
    if (!validation.value(QStringLiteral("valid")).toBool()) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_crossSectionalFactorResearchAlias = validation
        .value(QStringLiteral("factorResearchAlias")).toString();
    m_crossSectionalFactorPreprocessing =
        FactorPreprocessingPolicy::fromVariantMap(
            validation.value(QStringLiteral("preprocessing")).toMap());
    m_crossSectionalFactorResearchCost =
        FactorResearchCostPolicy::fromVariantMap(
            validation.value(QStringLiteral("costPolicy")).toMap());
    m_crossSectionalFactorNeutralization =
        FactorNeutralizationPolicy::fromVariantMap(
            validation.value(QStringLiteral("neutralizationPolicy")).toMap());
    emit crossSectionalBacktestStateChanged();
    return true;
}

QVariantMap TradingController::crossSectionalResultConfiguration() const
{
    QVariantMap map;
    if (!m_displayedCrossSectionalResult) return map;
    map = m_displayedCrossSectionalResult->immutableIdentity;
    map.insert(QStringLiteral("resultKind"),
               m_displayedCrossSectionalResult->resultKind);
    map.insert(QStringLiteral("completedAt"),
               m_displayedCrossSectionalCompletedAt);
    map.insert(QStringLiteral("finalEquity"),
               m_displayedCrossSectionalResult->summary.finalEquity);
    map.insert(QStringLiteral("totalReturnPercent"),
               m_displayedCrossSectionalResult->summary.totalReturnPercent);
    map.insert(QStringLiteral("maxDrawdownPercent"),
               m_displayedCrossSectionalResult->summary.maxDrawdownPercent);
    map.insert(QStringLiteral("sharpeRatio"),
               m_displayedCrossSectionalResult->summary.sharpeRatio);
    map.insert(QStringLiteral("totalCommission"),
               m_displayedCrossSectionalResult->summary.totalCommission);
    map.insert(QStringLiteral("totalTrades"),
               m_displayedCrossSectionalResult->summary.totalTrades);
    map.insert(QStringLiteral("datasetVersions"),
               QVariant(m_displayedCrossSectionalResult->datasetVersions));
    QVariantList datasetIdentities;
    for (const BacktestDatasetIdentity &identity :
         m_displayedCrossSectionalResult->datasetIdentities) {
        datasetIdentities.append(identity.toVariantMap());
    }
    map.insert(QStringLiteral("datasetIdentities"), datasetIdentities);
    map.insert(QStringLiteral("resultId"), m_selectedCrossSectionalArchiveId);
    map.insert(QStringLiteral("archived"),
               !m_selectedCrossSectionalArchiveId.isEmpty());
    map.insert(QStringLiteral("historicalSnapshot"),
               m_crossSectionalResultHistoricalSnapshot);
    return map;
}

QAbstractItemModel *TradingController::crossSectionalEquityCurveModel()
{
    return &m_crossSectionalEquityCurveModel;
}

QAbstractItemModel *TradingController::crossSectionalTradeModel()
{
    return &m_crossSectionalTradeModel;
}

QAbstractItemModel *TradingController::crossSectionalArchiveModel()
{
    return &m_crossSectionalArchiveModel;
}

int TradingController::crossSectionalArchiveCount() const
{
    return m_crossSectionalArchiveModel.count();
}

bool TradingController::crossSectionalArchiveLoading() const
{
    return m_crossSectionalArchiveLoading;
}

QString TradingController::crossSectionalArchiveError() const
{
    return m_crossSectionalArchiveError;
}

QAbstractItemModel *TradingController::crossSectionalFactorComparisonModel()
{
    return &m_crossSectionalFactorComparisonModel;
}

int TradingController::crossSectionalFactorComparisonCount() const
{
    return m_crossSectionalFactorComparisonModel.count();
}

int TradingController::crossSectionalMultipleTestingCandidateCount() const
{
    return m_crossSectionalFactorComparisonModel
        .multipleTestingCandidateCount();
}

QString TradingController::
crossSectionalMultipleTestingCandidateSetSha256() const
{
    return m_crossSectionalFactorComparisonModel
        .multipleTestingCandidateSetSha256();
}

QString TradingController::crossSectionalMultipleTestingStatusText() const
{
    return m_crossSectionalFactorComparisonModel.multipleTestingStatusText();
}

QString TradingController::crossSectionalFactorComparisonError() const
{
    return m_crossSectionalFactorComparisonError;
}

QVariantList TradingController::factorCandidateCatalog() const
{
    return m_factorCandidateCatalog;
}

QVariantList TradingController::factorCandidateEvaluationCatalog() const
{
    return m_factorCandidateEvaluationCatalog;
}

QString TradingController::selectedFactorCandidateId() const
{
    return m_selectedFactorCandidateId;
}

int TradingController::selectedFactorCandidateVersion() const
{
    return m_selectedFactorCandidateVersion;
}

QString TradingController::factorCandidateGovernanceStatusText() const
{
    return m_factorCandidateGovernanceStatusText;
}

QString TradingController::factorCandidateGovernanceError() const
{
    return m_factorCandidateGovernanceError;
}

QVariantMap TradingController::authorizedFactorImportState() const
{
    return m_authorizedFactorImportState;
}

QVariantList TradingController::externalFactorDatasetCatalog() const
{
    return m_externalFactorDatasetCatalog;
}

QVariantList TradingController::selectedFactorCandidateDatasetBindings() const
{
    return m_selectedFactorCandidateDatasetBindings;
}

QVariantMap TradingController::externalFactorResearchState() const
{
    return m_externalFactorResearchState;
}

QVariantMap TradingController::externalFactorResearchPreflightState() const
{
    return m_externalFactorResearchPreflightState;
}

QVariantList TradingController::externalFactorResearchArchiveCatalog() const
{
    return m_externalFactorResearchArchiveCatalog;
}

QVariantMap TradingController::selectedExternalFactorResearchArchive() const
{
    return m_selectedExternalFactorResearchArchive;
}

QString TradingController::selectedCrossSectionalArchiveId() const
{
    return m_selectedCrossSectionalArchiveId;
}

bool TradingController::crossSectionalResultHistoricalSnapshot() const
{
    return m_crossSectionalResultHistoricalSnapshot;
}

bool TradingController::crossSectionalReportExporting() const
{
    return m_crossSectionalReportExporting;
}

qulonglong TradingController::crossSectionalReportExportRequestId() const
{
    return m_crossSectionalReportExportRequestId;
}

QString TradingController::crossSectionalReportExportStatusText() const
{
    return m_crossSectionalReportExportStatusText;
}

QString TradingController::crossSectionalReportExportError() const
{
    return m_crossSectionalReportExportError;
}

QString TradingController::crossSectionalReportExportPath() const
{
    return m_crossSectionalReportExportPath;
}

qulonglong TradingController::exportCrossSectionalFactorReport(
    const QString &pathOrUrl)
{
    if (m_crossSectionalReportExporting) return 0;
    m_crossSectionalReportExportError.clear();
    if (!m_displayedCrossSectionalResult
        || m_selectedCrossSectionalArchiveId.trimmed().isEmpty()
        || !m_displayedCrossSectionalCompletedAt.isValid()) {
        m_crossSectionalReportExportStatusText = QStringLiteral("无法导出报告");
        m_crossSectionalReportExportError = QStringLiteral(
            "请先加载一个已归档且包含因子研究身份的多股回测结果");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    const QUrl url(pathOrUrl.trimmed());
    QString filePath = url.isLocalFile() ? url.toLocalFile()
                                         : pathOrUrl.trimmed();
    if (filePath.isEmpty()) {
        m_crossSectionalReportExportStatusText = QStringLiteral("无法导出报告");
        m_crossSectionalReportExportError = QStringLiteral("请选择本地报告保存路径");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    if (QFileInfo(filePath).suffix().isEmpty()) filePath += QStringLiteral(".md");

    PortfolioArchiveRecord record;
    record.resultId = m_selectedCrossSectionalArchiveId;
    record.completedAt = m_displayedCrossSectionalCompletedAt;
    record.result = *m_displayedCrossSectionalResult;
    ++m_crossSectionalReportExportRequestId;
    if (m_crossSectionalReportExportRequestId == 0)
        ++m_crossSectionalReportExportRequestId;
    m_crossSectionalReportExporting = true;
    m_crossSectionalReportExportPath = QDir::cleanPath(filePath);
    m_crossSectionalReportExportStatusText = QStringLiteral("正在保存因子研究报告…");
    emit crossSectionalBacktestStateChanged();
    if (m_asyncFactorResearchReportService.requestExport(
            m_crossSectionalReportExportRequestId, record,
            m_crossSectionalReportExportPath)) {
        return m_crossSectionalReportExportRequestId;
    }
    m_crossSectionalReportExporting = false;
    m_crossSectionalReportExportStatusText = QStringLiteral("无法导出报告");
    m_crossSectionalReportExportError = QStringLiteral("报告导出服务当前不可用");
    emit crossSectionalBacktestStateChanged();
    return 0;
}

bool TradingController::loadCrossSectionalBacktestArchive(
    const QString &resultId)
{
    m_crossSectionalArchiveError.clear();
    m_crossSectionalArchiveLoading = true;
    emit crossSectionalBacktestStateChanged();
    const auto record = m_dataStore.loadPortfolioBacktestArchive(resultId);
    m_crossSectionalArchiveLoading = false;
    if (!record || record->result.resultKind != QStringLiteral("cross_sectional")) {
        m_crossSectionalArchiveError = QStringLiteral(
            "多股归档加载失败、已损坏或类型不匹配：%1").arg(resultId);
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_displayedCrossSectionalResult = record->result;
    m_displayedCrossSectionalCompletedAt = record->completedAt;
    m_selectedCrossSectionalArchiveId = record->resultId;
    m_crossSectionalResultHistoricalSnapshot = true;
    m_crossSectionalEquityCurveModel.loadPoints(
        portfolioEquityPoints(record->result));
    m_crossSectionalTradeModel.loadTrades(record->result.trades);
    m_crossSectionalBacktestError.clear();
    m_crossSectionalBacktestStatusText = QStringLiteral(
        "已恢复多股历史归档：仅使用 SQLite 快照，未重新运行");
    emit crossSectionalBacktestStateChanged();
    return true;
}

void TradingController::refreshCrossSectionalBacktestArchiveList()
{
    QVector<PortfolioArchiveSummary> filtered;
    if (m_storageReady) {
        const QVector<PortfolioArchiveSummary> summaries =
            m_dataStore.loadPortfolioBacktestArchiveSummaries(100);
        for (const PortfolioArchiveSummary &summary : summaries) {
            if (summary.resultKind == QStringLiteral("cross_sectional"))
                filtered.append(summary);
        }
        if (filtered.size() > 50) filtered.resize(50);
    }
    m_crossSectionalArchiveModel.loadSummaries(filtered);
    emit crossSectionalBacktestStateChanged();
}

bool TradingController::toggleCrossSectionalFactorComparison(
    const QString &resultId)
{
    m_crossSectionalFactorComparisonError.clear();
    if (m_crossSectionalFactorComparisonModel.contains(resultId)) {
        m_crossSectionalFactorComparisonModel.removeArchive(resultId);
        emit crossSectionalBacktestStateChanged();
        return true;
    }
    if (!m_storageReady) {
        m_crossSectionalFactorComparisonError =
            QStringLiteral("本地 SQLite 尚未就绪，无法读取因子研究归档");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    const auto record = m_dataStore.loadPortfolioBacktestArchive(resultId);
    if (!record || record->result.resultKind != QStringLiteral("cross_sectional")) {
        m_crossSectionalFactorComparisonError = QStringLiteral(
            "因子研究对比归档加载失败、已损坏或类型不匹配：%1").arg(resultId);
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    if (!m_crossSectionalFactorComparisonModel.addArchive(
            *record, &m_crossSectionalFactorComparisonError)) {
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::isCrossSectionalFactorComparisonSelected(
    const QString &resultId) const
{
    return m_crossSectionalFactorComparisonModel.contains(resultId);
}

void TradingController::clearCrossSectionalFactorComparison()
{
    m_crossSectionalFactorComparisonError.clear();
    m_crossSectionalFactorComparisonModel.clear();
    emit crossSectionalBacktestStateChanged();
}

void TradingController::refreshFactorCandidateCatalog()
{
    m_factorCandidateCatalog.clear();
    m_factorCandidateEvaluationCatalog.clear();
    m_selectedFactorCandidateDatasetBindings.clear();
    if (!m_storageReady) {
        m_factorCandidateGovernanceStatusText = QStringLiteral("本地 SQLite 尚未就绪");
        emit crossSectionalBacktestStateChanged();
        return;
    }
    const QVector<FactorCandidateVersion> candidates =
        m_dataStore.loadFactorCandidateVersions();
    bool selectionFound = false;
    for (const FactorCandidateVersion &candidate : candidates) {
        m_factorCandidateCatalog.append(candidate.toVariantMap());
        if (candidate.candidateId == m_selectedFactorCandidateId
            && candidate.version == m_selectedFactorCandidateVersion) {
            selectionFound = true;
        }
    }
    if (!selectionFound) {
        m_selectedFactorCandidateId.clear();
        m_selectedFactorCandidateVersion = 0;
    }
    if (selectionFound) {
        const QVector<FactorCandidateEvaluation> evaluations =
            m_dataStore.loadFactorCandidateEvaluations(
                m_selectedFactorCandidateId, m_selectedFactorCandidateVersion);
        for (const FactorCandidateEvaluation &evaluation : evaluations)
            m_factorCandidateEvaluationCatalog.append(evaluation.toVariantMap());
        for (const ExternalFactorDatasetBinding &binding :
             m_dataStore.loadExternalFactorDatasetBindings(
                 m_selectedFactorCandidateId,
                 m_selectedFactorCandidateVersion)) {
            m_selectedFactorCandidateDatasetBindings.append(
                binding.toVariantMap());
        }
    }
    m_factorCandidateGovernanceStatusText = candidates.isEmpty()
        ? QStringLiteral("尚未登记候选因子")
        : QStringLiteral("已登记 %1 个不可变候选版本；不会自动运行研究")
              .arg(candidates.size());
    emit crossSectionalBacktestStateChanged();
}

bool TradingController::setAuthorizedFactorImportFile(
    const QString &pathOrUrl)
{
    if (m_authorizedFactorImportState.value(QStringLiteral("running")).toBool())
        return false;
    const QUrl url(pathOrUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (path.isEmpty() || !QDir::isAbsolutePath(path)
        || path.startsWith(QStringLiteral("//"))) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("error"),
            QStringLiteral("请选择本地磁盘或已挂载盘符下的绝对 CSV 路径"));
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("文件未选择"));
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_authorizedFactorImportState.insert(
        QStringLiteral("filePath"), QDir::toNativeSeparators(path));
    m_authorizedFactorImportState.insert(QStringLiteral("previewReady"), false);
    m_authorizedFactorImportState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_authorizedFactorImportState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("文件已选择；尚未读取，请先异步预览"));
    m_authorizedFactorPreviewContentSha256.clear();
    emit crossSectionalBacktestStateChanged();
    return true;
}

qulonglong TradingController::previewAuthorizedFactorImport(
    const QString &factorKey, const QString &sourceVersion,
    const QString &sourceReference)
{
    if (m_authorizedFactorImportState.value(QStringLiteral("running")).toBool())
        return 0;
    m_authorizedFactorImportState.insert(QStringLiteral("previewReady"), false);
    m_authorizedFactorImportState.insert(QStringLiteral("reused"), false);
    m_authorizedFactorImportState.insert(QStringLiteral("linesRead"), qint64(0));
    m_authorizedFactorImportState.insert(QStringLiteral("validRecords"), qint64(0));
    m_authorizedFactorImportState.insert(QStringLiteral("duplicateRecords"), qint64(0));
    m_authorizedFactorImportState.insert(QStringLiteral("writtenRecords"), qint64(0));
    m_authorizedFactorImportState.insert(QStringLiteral("diagnostics"), QVariantList{});
    m_authorizedFactorImportState.insert(QStringLiteral("error"), QStringLiteral(""));

    QVariantMap selectedCandidate;
    for (const QVariant &value : m_factorCandidateCatalog) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("candidateId")).toString()
                == m_selectedFactorCandidateId
            && item.value(QStringLiteral("version")).toInt()
                == m_selectedFactorCandidateVersion) {
            selectedCandidate = item;
            break;
        }
    }
    QString error;
    const QString filePath = m_authorizedFactorImportState
        .value(QStringLiteral("filePath")).toString();
    if (!m_storageReady) error = QStringLiteral("本地 SQLite 尚未就绪");
    else if (filePath.isEmpty() || !QFileInfo::exists(filePath))
        error = QStringLiteral("请先显式选择存在的授权因子 CSV");
    else if (selectedCandidate.isEmpty())
        error = QStringLiteral("请先选择候选因子版本");
    else if (!selectedCandidate.value(QStringLiteral("frozen")).toBool())
        error = QStringLiteral("候选因子必须先冻结再导入验证数据");
    else if (factorKey.trimmed().isEmpty()
             || sourceVersion.trimmed().isEmpty()
             || sourceReference.trimmed().isEmpty())
        error = QStringLiteral("因子键、来源版本和授权来源引用不能为空");
    if (!error.isEmpty()) {
        m_authorizedFactorImportState.insert(QStringLiteral("error"), error);
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("预览未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }

    FactorCandidateVersion identity;
    identity.factorDefinition = selectedCandidate
        .value(QStringLiteral("factorDefinition")).toMap();
    AuthorizedFactorImportRequest request;
    request.filePath = filePath;
    request.factorKey = factorKey.trimmed();
    request.factorDefinitionSha256 = identity.computedFactorDefinitionSha256();
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = sourceVersion.trimmed();
    request.sourceReference = sourceReference.trimmed();
    const quint64 requestId = m_authorizedFactorImportService.preview(request);
    if (requestId == 0) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("error"), QStringLiteral("已有授权因子导入任务正在结束"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    m_authorizedFactorImportState.insert(QStringLiteral("requestId"), requestId);
    m_authorizedFactorImportState.insert(QStringLiteral("running"), true);
    m_authorizedFactorImportState.insert(QStringLiteral("factorKey"), request.factorKey);
    m_authorizedFactorImportState.insert(QStringLiteral("sourceVersion"), request.sourceVersion);
    m_authorizedFactorImportState.insert(QStringLiteral("sourceReference"), request.sourceReference);
    m_authorizedFactorImportState.insert(
        QStringLiteral("statusText"), QStringLiteral("正在异步预览授权因子 CSV"));
    emit crossSectionalBacktestStateChanged();
    return requestId;
}

qulonglong TradingController::commitAuthorizedFactorImport(
    const QString &factorKey, const QString &sourceVersion,
    const QString &sourceReference)
{
    if (m_authorizedFactorImportState.value(QStringLiteral("running")).toBool())
        return 0;
    const QString filePath = m_authorizedFactorImportState
        .value(QStringLiteral("filePath")).toString();
    if (!m_authorizedFactorImportState.value(QStringLiteral("previewReady")).toBool()
        || filePath != m_authorizedFactorPreviewFilePath
        || factorKey.trimmed() != m_authorizedFactorPreviewFactorKey
        || sourceVersion.trimmed() != m_authorizedFactorPreviewSourceVersion
        || sourceReference.trimmed() != m_authorizedFactorPreviewSourceReference
        || m_authorizedFactorPreviewContentSha256.isEmpty()) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("error"),
            QStringLiteral("文件或元数据已变化；请重新预览再确认导入"));
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("确认导入未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    AuthorizedFactorImportRequest request;
    request.filePath = filePath;
    request.factorKey = m_authorizedFactorPreviewFactorKey;
    request.factorDefinitionSha256 = m_authorizedFactorPreviewDefinitionSha256;
    request.source = QStringLiteral("authorized_csv");
    request.sourceVersion = m_authorizedFactorPreviewSourceVersion;
    request.sourceReference = m_authorizedFactorPreviewSourceReference;
    request.expectedContentSha256 = m_authorizedFactorPreviewContentSha256;
    const quint64 requestId = m_authorizedFactorImportService.start(request);
    if (requestId == 0) return 0;
    m_authorizedFactorImportState.insert(QStringLiteral("requestId"), requestId);
    m_authorizedFactorImportState.insert(QStringLiteral("running"), true);
    m_authorizedFactorImportState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_authorizedFactorImportState.insert(
        QStringLiteral("statusText"), QStringLiteral("正在异步确认导入并事务写入 SQLite"));
    emit crossSectionalBacktestStateChanged();
    return requestId;
}

void TradingController::cancelAuthorizedFactorImport()
{
    if (!m_authorizedFactorImportState.value(QStringLiteral("running")).toBool())
        return;
    m_authorizedFactorImportService.cancel();
    m_authorizedFactorImportState.insert(
        QStringLiteral("statusText"), QStringLiteral("正在取消授权因子导入"));
    emit crossSectionalBacktestStateChanged();
}

void TradingController::refreshExternalFactorDatasetCatalog()
{
    m_externalFactorDatasetCatalog.clear();
    if (m_storageReady) {
        for (const ExternalFactorDataset &dataset :
             m_dataStore.loadExternalFactorDatasets()) {
            m_externalFactorDatasetCatalog.append(dataset.toVariantMap());
        }
    }
    emit crossSectionalBacktestStateChanged();
}

bool TradingController::bindSelectedExternalFactorDataset(
    const QString &datasetId)
{
    m_factorCandidateGovernanceError.clear();
    ExternalFactorDatasetBinding binding;
    bool reused = false;
    if (!m_storageReady || m_selectedFactorCandidateId.isEmpty()
        || m_selectedFactorCandidateVersion < 1
        || !m_dataStore.bindExternalFactorDataset(
            m_selectedFactorCandidateId, m_selectedFactorCandidateVersion,
            datasetId, binding, &reused, &m_factorCandidateGovernanceError)) {
        if (m_factorCandidateGovernanceError.isEmpty())
            m_factorCandidateGovernanceError = QStringLiteral("请选择候选和授权数据集");
        m_factorCandidateGovernanceStatusText = QStringLiteral("外部数据集绑定失败");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    refreshFactorCandidateCatalog();
    m_factorCandidateGovernanceStatusText = reused
        ? QStringLiteral("该授权数据集绑定已存在，未重复写入")
        : QStringLiteral("已绑定不可变授权因子数据集；尚未运行独立研究");
    emit crossSectionalBacktestStateChanged();
    return true;
}

qulonglong TradingController::preflightExternalFactorResearch(
    const QString &datasetId, int periodSeconds,
    int minimumPairs, double oneWayCostBps)
{
    if (!m_storageReady
        || m_externalFactorResearchPreflightState.value(
               QStringLiteral("running")).toBool()
        || m_externalFactorResearchState.value(
               QStringLiteral("running")).toBool()) {
        return 0;
    }
    if (m_selectedFactorCandidateId.isEmpty()
        || m_selectedFactorCandidateVersion < 1
        || datasetId.trimmed().isEmpty() || periodSeconds <= 0
        || minimumPairs < 2 || minimumPairs > 10000
        || !std::isfinite(oneWayCostBps)
        || oneWayCostBps < 0.0 || oneWayCostBps > 1000.0) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("ready"), false);
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("error"),
            QStringLiteral("请选择已冻结候选及其绑定数据集，并填写有效预检参数"));
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"), QStringLiteral("预检未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    bool bound = false;
    for (const QVariant &value : m_selectedFactorCandidateDatasetBindings) {
        if (value.toMap().value(QStringLiteral("datasetId")).toString()
            == datasetId) {
            bound = true;
            break;
        }
    }
    if (!bound) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("ready"), false);
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("error"), QStringLiteral("所选数据集尚未绑定当前候选"));
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"), QStringLiteral("预检未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }

    ExternalFactorResearchRequest request;
    request.candidateId = m_selectedFactorCandidateId;
    request.candidateVersion = m_selectedFactorCandidateVersion;
    request.datasetId = datasetId;
    request.periodSeconds = periodSeconds;
    request.preprocessing.minimumPairs = minimumPairs;
    request.costPolicy.oneWayCostBps = oneWayCostBps;
    const quint64 requestId = m_externalFactorResearchService.preflight(request);
    if (requestId == 0) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("ready"), false);
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("error"), QStringLiteral("异步预检服务尚未就绪"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    m_externalFactorResearchPreflightRequestGate.activate(requestId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("running"), true);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("ready"), false);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("requestId"), requestId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("candidateId"), m_selectedFactorCandidateId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("candidateVersion"), m_selectedFactorCandidateVersion);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("datasetId"), datasetId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("periodSeconds"), periodSeconds);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("minimumPairs"), minimumPairs);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("oneWayCostBps"), oneWayCostBps);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("error"), QStringLiteral(""));
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("正在核对授权观测、全部标的和本地 Tick 派生完成 Bar"));
    emit crossSectionalBacktestStateChanged();
    return requestId;
}

qulonglong TradingController::startExternalFactorResearch(
    const QString &datasetId, int periodSeconds,
    int minimumPairs, double oneWayCostBps)
{
    if (!m_storageReady
        || m_externalFactorResearchState.value(
               QStringLiteral("running")).toBool()) {
        return 0;
    }
    if (m_selectedFactorCandidateId.isEmpty()
        || m_selectedFactorCandidateVersion < 1
        || datasetId.trimmed().isEmpty() || periodSeconds <= 0
        || minimumPairs < 2 || minimumPairs > 10000
        || !std::isfinite(oneWayCostBps)
        || oneWayCostBps < 0.0 || oneWayCostBps > 1000.0) {
        m_externalFactorResearchState.insert(
            QStringLiteral("error"),
            QStringLiteral("请选择已冻结候选及其绑定数据集，并填写有效研究参数"));
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"), QStringLiteral("研究未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    bool bound = false;
    for (const QVariant &value : m_selectedFactorCandidateDatasetBindings) {
        if (value.toMap().value(QStringLiteral("datasetId")).toString()
            == datasetId) {
            bound = true;
            break;
        }
    }
    if (!bound) {
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), QStringLiteral("所选数据集尚未绑定当前候选"));
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"), QStringLiteral("研究未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    const bool preflightMatches =
        m_externalFactorResearchPreflightState.value(
            QStringLiteral("ready")).toBool()
        && m_externalFactorResearchPreflightState.value(
            QStringLiteral("candidateId")).toString()
            == m_selectedFactorCandidateId
        && m_externalFactorResearchPreflightState.value(
            QStringLiteral("candidateVersion")).toInt()
            == m_selectedFactorCandidateVersion
        && m_externalFactorResearchPreflightState.value(
            QStringLiteral("datasetId")).toString() == datasetId
        && m_externalFactorResearchPreflightState.value(
            QStringLiteral("periodSeconds")).toInt() == periodSeconds
        && m_externalFactorResearchPreflightState.value(
            QStringLiteral("minimumPairs")).toInt() == minimumPairs
        && qFuzzyCompare(
            m_externalFactorResearchPreflightState.value(
                QStringLiteral("oneWayCostBps")).toDouble() + 1.0,
            oneWayCostBps + 1.0);
    if (!preflightMatches) {
        m_externalFactorResearchState.insert(
            QStringLiteral("error"),
            QStringLiteral("请先对当前候选、数据集和收益周期完成异步预检"));
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"), QStringLiteral("研究未启动"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }

    ExternalFactorResearchRequest request;
    request.candidateId = m_selectedFactorCandidateId;
    request.candidateVersion = m_selectedFactorCandidateVersion;
    request.datasetId = datasetId;
    request.periodSeconds = periodSeconds;
    request.preprocessing.minimumPairs = minimumPairs;
    request.costPolicy.oneWayCostBps = oneWayCostBps;
    const quint64 requestId = m_externalFactorResearchService.start(request);
    if (requestId == 0) {
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), QStringLiteral("异步研究服务尚未就绪"));
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    m_externalFactorResearchState.insert(QStringLiteral("running"), true);
    m_externalFactorResearchRequestGate.activate(requestId);
    m_externalFactorResearchState.insert(QStringLiteral("requestId"), requestId);
    m_externalFactorResearchState.insert(QStringLiteral("datasetId"), datasetId);
    m_externalFactorResearchState.insert(
        QStringLiteral("periodSeconds"), periodSeconds);
    m_externalFactorResearchState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_externalFactorResearchState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("正在异步读取本地 Tick 派生 K 线并计算 IC/HAC/OOS"));
    emit crossSectionalBacktestStateChanged();
    return requestId;
}

void TradingController::cancelExternalFactorResearch()
{
    const bool researchRunning = m_externalFactorResearchState.value(
        QStringLiteral("running")).toBool();
    const bool preflightRunning = m_externalFactorResearchPreflightState.value(
        QStringLiteral("running")).toBool();
    if (!researchRunning && !preflightRunning) return;
    m_externalFactorResearchService.cancel();
    if (researchRunning) {
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"), QStringLiteral("正在取消外部因子研究"));
    }
    if (preflightRunning) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"), QStringLiteral("正在取消外部因子研究预检"));
    }
    emit crossSectionalBacktestStateChanged();
}

void TradingController::refreshExternalFactorResearchArchives()
{
    m_externalFactorResearchArchiveCatalog.clear();
    if (m_storageReady) {
        for (const ExternalFactorResearchArchiveSummary &summary :
             m_dataStore.loadExternalFactorResearchArchiveSummaries()) {
            m_externalFactorResearchArchiveCatalog.append(summary.toVariantMap());
        }
    }
    emit crossSectionalBacktestStateChanged();
}

bool TradingController::loadExternalFactorResearchArchive(
    const QString &resultId)
{
    if (!m_storageReady
        || m_externalFactorResearchState.value(
               QStringLiteral("running")).toBool()) return false;
    const auto archive = m_dataStore.loadExternalFactorResearchArchive(resultId);
    if (!archive) {
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), QStringLiteral("外部因子研究归档不存在或已损坏"));
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"), QStringLiteral("归档加载失败"));
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_selectedExternalFactorResearchArchive = archive->toVariantMap();
    m_externalFactorResearchState.insert(
        QStringLiteral("resultId"), archive->resultId);
    m_externalFactorResearchState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_externalFactorResearchState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("已从本地 SQLite 恢复历史研究快照；未联网、未重算"));
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::registerFactorCandidate(
    const QString &candidateId, const QString &name,
    const QString &hypothesis, const QString &sourceKind,
    const QString &sourceReference, const QVariantMap &factorDefinition)
{
    m_factorCandidateGovernanceError.clear();
    if (!m_storageReady) {
        m_factorCandidateGovernanceError = QStringLiteral("本地 SQLite 尚未就绪");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    FactorCandidateVersion candidate;
    candidate.candidateId = candidateId;
    candidate.name = name;
    candidate.hypothesis = hypothesis;
    candidate.sourceKind = sourceKind.trimmed().isEmpty()
        ? QStringLiteral("manual") : sourceKind;
    candidate.sourceReference = sourceReference;
    candidate.factorDefinition = factorDefinition;
    candidate.proposedAt = QDateTime::currentDateTimeUtc();
    bool reused = false;
    if (!m_dataStore.registerFactorCandidateVersion(
            candidate, &reused, &m_factorCandidateGovernanceError)) {
        m_factorCandidateGovernanceStatusText = QStringLiteral("候选因子登记失败");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_selectedFactorCandidateId = candidate.candidateId;
    m_selectedFactorCandidateVersion = candidate.version;
    refreshFactorCandidateCatalog();
    m_factorCandidateGovernanceStatusText = reused
        ? QStringLiteral("候选内容未变化，已复用 %1 v%2")
              .arg(candidate.candidateId).arg(candidate.version)
        : QStringLiteral("已登记 %1 v%2；冻结前不能关联验证")
              .arg(candidate.candidateId).arg(candidate.version);
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::selectFactorCandidate(const QString &candidateId,
                                               int version)
{
    m_factorCandidateGovernanceError.clear();
    bool found = false;
    for (const QVariant &value : m_factorCandidateCatalog) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("candidateId")).toString() == candidateId
            && item.value(QStringLiteral("version")).toInt() == version) {
            found = true;
            break;
        }
    }
    if (!found) {
        m_factorCandidateGovernanceError = QStringLiteral("候选因子版本不存在");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_selectedFactorCandidateId = candidateId;
    m_selectedFactorCandidateVersion = version;
    m_factorCandidateEvaluationCatalog.clear();
    m_selectedFactorCandidateDatasetBindings.clear();
    for (const FactorCandidateEvaluation &evaluation :
         m_dataStore.loadFactorCandidateEvaluations(candidateId, version)) {
        m_factorCandidateEvaluationCatalog.append(evaluation.toVariantMap());
    }
    for (const ExternalFactorDatasetBinding &binding :
         m_dataStore.loadExternalFactorDatasetBindings(candidateId, version)) {
        m_selectedFactorCandidateDatasetBindings.append(binding.toVariantMap());
    }
    m_factorCandidateGovernanceStatusText = QStringLiteral("已选择 %1 v%2")
        .arg(candidateId).arg(version);
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::freezeFactorCandidate(const QString &candidateId,
                                               int version)
{
    m_factorCandidateGovernanceError.clear();
    if (!m_storageReady || !m_dataStore.freezeFactorCandidateVersion(
            candidateId, version, QDateTime::currentDateTimeUtc(),
            &m_factorCandidateGovernanceError)) {
        m_factorCandidateGovernanceStatusText = QStringLiteral("候选因子冻结失败");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    m_selectedFactorCandidateId = candidateId;
    m_selectedFactorCandidateVersion = version;
    refreshFactorCandidateCatalog();
    m_factorCandidateGovernanceStatusText = QStringLiteral(
        "候选版本已冻结；后续修改必须登记新版本");
    emit crossSectionalBacktestStateChanged();
    return true;
}

bool TradingController::linkSelectedArchiveToFactorCandidate(bool prospective)
{
    m_factorCandidateGovernanceError.clear();
    if (!m_storageReady || m_selectedFactorCandidateId.isEmpty()
        || m_selectedFactorCandidateVersion < 1
        || m_selectedCrossSectionalArchiveId.isEmpty()) {
        m_factorCandidateGovernanceError = QStringLiteral(
            "请先选择候选版本，并加载一个多股因子研究归档");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    const auto archive = m_dataStore.loadPortfolioBacktestArchive(
        m_selectedCrossSectionalArchiveId);
    if (!archive) {
        m_factorCandidateGovernanceError = QStringLiteral("所选归档不存在或已损坏");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    FactorCandidateEvaluation evaluation;
    bool reused = false;
    const FactorCandidateEvaluationRole role = prospective
        ? FactorCandidateEvaluationRole::ProspectiveHoldout
        : FactorCandidateEvaluationRole::RetrospectiveReference;
    if (!m_dataStore.linkFactorCandidateEvaluation(
            m_selectedFactorCandidateId, m_selectedFactorCandidateVersion,
            *archive, role, evaluation, &reused,
            &m_factorCandidateGovernanceError)) {
        m_factorCandidateGovernanceStatusText = QStringLiteral("验证关联被拒绝");
        emit crossSectionalBacktestStateChanged();
        return false;
    }
    selectFactorCandidate(m_selectedFactorCandidateId,
                          m_selectedFactorCandidateVersion);
    m_factorCandidateGovernanceStatusText = reused
        ? QStringLiteral("该验证关联已存在，未重复写入")
        : QStringLiteral("已关联为%1；%2")
              .arg(factorCandidateEvaluationRoleLabel(role),
                   evaluation.preregistered
                       ? QStringLiteral("计入预注册证据")
                       : QStringLiteral("不计入预注册证据"));
    emit crossSectionalBacktestStateChanged();
    return true;
}

QVariantMap TradingController::replayBacktestBar(int index) const
{
    QVariantMap map;
    if (!m_displayedBacktestResult) return map;
    const QVector<Candle> &candles = m_displayedBacktestResult->candles;
    if (index < 0 || index >= candles.size()) return map;
    const Candle &candle = candles.at(index);
    map.insert(QStringLiteral("timestamp"), candle.startTime);
    map.insert(QStringLiteral("open"), candle.open);
    map.insert(QStringLiteral("high"), candle.high);
    map.insert(QStringLiteral("low"), candle.low);
    map.insert(QStringLiteral("close"), candle.close);
    map.insert(QStringLiteral("volume"), candle.volume);

    QString side;
    QString reason;
    double price = 0.0;
    for (const BacktestTrade &trade : m_displayedBacktestResult->trades) {
        if (trade.entryTime == candle.startTime) {
            side = QStringLiteral("买入");
            reason = trade.entryReason;
            price = trade.entryPrice;
            break;
        }
        if (trade.exitTime == candle.startTime) {
            side = QStringLiteral("卖出");
            reason = trade.exitReason;
            price = trade.exitPrice;
            break;
        }
    }
    map.insert(QStringLiteral("hasSignal"), !side.isEmpty());
    map.insert(QStringLiteral("signalSide"), side);
    map.insert(QStringLiteral("signalReason"), reason);
    map.insert(QStringLiteral("signalPrice"), price);
    return map;
}

QVariantList TradingController::validationRuns() const
{
    QVariantList list;
    for (const ValidationRun &run : m_validationRuns) {
        list.append(run.toVariantMap());
    }
    return list;
}

bool TradingController::saveValidationRun(const QString &name)
{
    if (!m_displayedBacktestResult || !m_storageReady) {
        setStatusText(QStringLiteral("没有已完成结果或本地存储未就绪，验证记录未保存"));
        return false;
    }
    if (!m_displayedBacktestResult->config.sampleWindowKnown) {
        setStatusText(QStringLiteral("旧归档未记录请求日期范围，请明确选择范围并重新回测后保存验证"));
        return false;
    }
    ValidationRun run;
    run.name = name.trimmed().isEmpty() ? QStringLiteral("未命名验证") : name.trimmed();
    run.strategyId = m_displayedBacktestResult->config.strategyId;
    run.strategyVersion = m_displayedBacktestResult->config.strategyVersion;
    run.instrumentSymbol = m_displayedBacktestResult->config.instrument.symbol;
    run.periodSeconds = m_displayedBacktestResult->config.periodSeconds;
    run.sampleFrom = m_displayedBacktestResult->config.sampleFrom;
    run.sampleTo = m_displayedBacktestResult->config.sampleTo;
    run.requestedBars = m_displayedBacktestResult->config.requestedBars;
    run.totalReturnPercent = m_displayedBacktestResult->summary.totalReturnPercent;
    run.maxDrawdownPercent = m_displayedBacktestResult->summary.maxDrawdownPercent;
    run.sharpeRatio = m_displayedBacktestResult->summary.sharpeRatio;
    run.totalTrades = m_displayedBacktestResult->summary.totalTrades;
    run.completedAt = QDateTime::currentDateTimeUtc();
    QString saveError;
    const auto errorConnection = connect(&m_dataStore, &SqliteLocalDataStore::writeFailed,
        this, [&saveError](const QString &error) { saveError = error; });
    const bool saved = m_dataStore.saveValidationRun(run);
    disconnect(errorConnection);
    if (!saved) {
        setStatusText(saveError.isEmpty() ? QStringLiteral("验证记录未保存，请检查本地存储") : saveError);
        return false;
    }
    m_validationRuns.append(run);
    std::sort(m_validationRuns.begin(), m_validationRuns.end(),
              [](const ValidationRun &left, const ValidationRun &right) {
        return left.totalReturnPercent > right.totalReturnPercent;
    });
    while (m_validationRuns.size() > 10) {
        m_validationRuns.removeLast();
    }
    emit validationRunsChanged();
    setStatusText(QStringLiteral("验证记录已保存"));
    return true;
}

bool TradingController::loadValidationRun(int index)
{
    if (index < 0 || index >= m_validationRuns.size()) return false;
    const ValidationRun &run = m_validationRuns.at(index);
    m_workspaceInstrumentSelections.setBacktestSampleWindow(run.sampleFrom, run.sampleTo);
    m_pendingSampleFrom = m_workspaceInstrumentSelections.backtestSampleFrom();
    m_pendingSampleTo = m_workspaceInstrumentSelections.backtestSampleTo();
    emit backtestSampleWindowChanged();
    emit backtestConfigurationChanged();
    emit validationRunsChanged();
    return true;
}

void TradingController::setBacktestSampleWindow(const QDateTime &from, const QDateTime &to)
{
    m_workspaceInstrumentSelections.setBacktestSampleWindow(from, to);
    m_pendingSampleFrom = m_workspaceInstrumentSelections.backtestSampleFrom();
    m_pendingSampleTo = m_workspaceInstrumentSelections.backtestSampleTo();
    emit backtestSampleWindowChanged();
    emit backtestConfigurationChanged();
}

bool TradingController::setBacktestSampleDateRange(const QString &from, const QString &to)
{
    QString error;
    if (!m_workspaceInstrumentSelections.setBacktestSampleDateRange(from, to, &error))
        return false;
    m_pendingSampleFrom = m_workspaceInstrumentSelections.backtestSampleFrom();
    m_pendingSampleTo = m_workspaceInstrumentSelections.backtestSampleTo();
    emit backtestSampleWindowChanged();
    emit backtestConfigurationChanged();
    return true;
}

void TradingController::clearBacktestSampleWindow()
{
    m_workspaceInstrumentSelections.clearBacktestSampleWindow();
    m_pendingSampleFrom = m_workspaceInstrumentSelections.backtestSampleFrom();
    m_pendingSampleTo = m_workspaceInstrumentSelections.backtestSampleTo();
    emit backtestSampleWindowChanged();
    emit backtestConfigurationChanged();
}

void TradingController::clearBacktestResult()
{
    m_backtestArchiveLoadRequestGate.cancel();
    clearBacktestArchiveRawTickPage();
    m_candleModel.clear();
    m_signalModel.clear();
    m_backtestTradeModel.clear();
    m_equityCurveModel.clear();
    m_backtestSummary = {};
    m_displayedBacktestResult.reset();
    m_displayedBacktestCompletedAt = {};
    m_completedBacktestTaskId = 0;
    m_backtestProgress = 0.0;
    m_backtestError.clear();
    m_selectedBacktestArchiveId.clear();
    m_backtestArchiveError.clear();
    m_backtestArchiveSaveError.clear();
    m_backtestResultArchived = false;
    m_backtestResultHistoricalSnapshot = false;
    emit backtestSummaryChanged();
    emit backtestArchiveStateChanged();
    emit backtestStateChanged();
}

void TradingController::clearBacktestArchiveRawTickPage()
{
    m_backtestArchiveRawTickPageRequestGate.cancel();
    m_backtestArchiveRawTickModel.clear();
    m_backtestArchiveRawTickPageResultId.clear();
    m_backtestArchiveRawTickPageOffset = 0;
    m_backtestArchiveRawTickPageSize = 256;
    m_backtestArchiveRawTickTotalCount = 0;
    m_backtestArchiveRawTickHasNext = false;
    m_backtestArchiveRawTickPageError.clear();
    emit backtestArchiveRawTickPageStateChanged();
}

void TradingController::refreshBacktestArchiveList()
{
    if (!m_storageReady) return;
    const QString previousSelection = m_selectedBacktestArchiveId;
    m_backtestArchiveModel.loadSummaries(m_dataStore.loadBacktestArchiveSummaries());
    if (!previousSelection.isEmpty()
        && m_backtestArchiveModel.indexOfResultId(previousSelection) < 0) {
        m_selectedBacktestArchiveId.clear();
        m_backtestResultArchived = false;
        m_backtestResultHistoricalSnapshot = false;
    }
    emit backtestArchiveStateChanged();
}

bool TradingController::loadBacktestArchive(const QString &resultId)
{
    if (!m_storageReady || resultId.trimmed().isEmpty() || m_backtest.isRunning()
        || m_backtestPreparing || !m_asyncBacktestArchiveService.isRunning()) {
        m_backtestArchiveError = m_storageReady
            && m_asyncBacktestArchiveService.isRunning()
            ? QStringLiteral("回测运行或准备期间不能加载归档")
            : QStringLiteral("异步回测归档服务尚未就绪");
        emit backtestArchiveStateChanged();
        return false;
    }
    clearBacktestArchiveRawTickPage();
    m_backtestArchiveError.clear();
    const quint64 requestId = m_backtestArchiveLoadRequestGate.begin();
    if (!m_asyncBacktestArchiveService.requestLoad(requestId, resultId.trimmed())) {
        m_backtestArchiveLoadRequestGate.finish(requestId);
        m_backtestArchiveError = QStringLiteral("异步归档加载任务提交失败");
        emit backtestArchiveStateChanged();
        return false;
    }
    setStatusText(QStringLiteral("正在后台读取回测归档：%1")
                      .arg(resultId.trimmed()));
    emit backtestArchiveStateChanged();
    return true;
}

void TradingController::finishBacktestArchiveLoad(
    quint64 requestId, const BacktestArchiveRecord &record,
    const QString &error)
{
    if (!m_backtestArchiveLoadRequestGate.finish(requestId)) return;
    if (!error.isEmpty() || record.resultId.isEmpty()) {
        m_backtestArchiveError = error.isEmpty()
            ? QStringLiteral("归档不存在、已损坏或无法读取") : error;
        emit backtestArchiveStateChanged();
        return;
    }

    m_backtestArchiveError.clear();
    m_displayedBacktestResult = record.result;
    m_displayedBacktestCompletedAt = record.completedAt;
    m_backtestSummary = record.result.summary;
    m_candleModel.loadBacktestResult(record.result);
    m_signalModel.loadSignals(record.result.chartSignals);
    m_backtestTradeModel.loadTrades(record.result.trades);
    m_equityCurveModel.loadPoints(record.result.equityCurve);
    bool taskIdOk = false;
    const qulonglong restoredTaskId = record.result.config.taskId.toULongLong(&taskIdOk);
    m_completedBacktestTaskId = taskIdOk && restoredTaskId > 0 ? restoredTaskId : 1;
    m_selectedBacktestArchiveId = record.resultId;
    m_backtestResultArchived = true;
    m_backtestResultHistoricalSnapshot = true;
    m_backtestArchiveSaveError.clear();
    setBacktestStatus(QStringLiteral("已加载归档回测"));
    if (record.result.config.dataMode == BacktestDataMode::RawTicks
        && record.result.rawTickSnapshotAvailable) {
        setStatusText(QStringLiteral(
            "已从 SQLite 恢复原始 Tick 回测：%1 · %2；完整输入已归档，当前按需加载")
                          .arg(record.result.config.instrument.symbol,
                               record.result.config.strategyId));
    } else if (record.result.candles.isEmpty()) {
        setStatusText(QStringLiteral(
            "已从 SQLite 恢复旧回测：%1 · %2；旧归档不含 K 线快照")
                          .arg(record.result.config.instrument.symbol,
                               record.result.config.strategyId));
    } else {
        setStatusText(QStringLiteral("已从 SQLite 恢复回测：%1 · %2 · %3 根 K 线")
                          .arg(record.result.config.instrument.symbol,
                               record.result.config.strategyId)
                          .arg(record.result.candles.size()));
    }
    emit backtestSummaryChanged();
    emit backtestArchiveStateChanged();
    emit backtestStateChanged();
}

qulonglong TradingController::requestBacktestArchiveRawTickPage(
    const QString &resultId, int offset, int pageSize)
{
    const QString normalizedResultId = resultId.trimmed();
    m_backtestArchiveRawTickPageError.clear();
    if (!m_storageReady || !m_asyncBacktestArchiveService.isRunning()
        || normalizedResultId.isEmpty() || offset < 0
        || pageSize <= 0 || pageSize > 4096) {
        m_backtestArchiveRawTickPageError = QStringLiteral(
            "原始 Tick 分页参数无效或异步存储服务不可用（每页最多 4096 条）");
        emit backtestArchiveRawTickPageStateChanged();
        return 0;
    }
    if (!m_displayedBacktestResult.has_value()
        || m_selectedBacktestArchiveId != normalizedResultId
        || m_displayedBacktestResult->config.dataMode != BacktestDataMode::RawTicks
        || !m_displayedBacktestResult->rawTickSnapshotAvailable) {
        m_backtestArchiveRawTickPageError = QStringLiteral(
            "请先加载包含完整原始 Tick 快照的归档结果");
        emit backtestArchiveRawTickPageStateChanged();
        return 0;
    }
    if (m_backtestArchiveRawTickTotalCount > 0
        && offset >= m_backtestArchiveRawTickTotalCount) {
        m_backtestArchiveRawTickPageError = QStringLiteral("请求页已超出归档范围");
        emit backtestArchiveRawTickPageStateChanged();
        return 0;
    }

    const quint64 requestId = m_backtestArchiveRawTickPageRequestGate.begin();
    m_backtestArchiveRawTickPageSize = pageSize;
    if (!m_asyncBacktestArchiveService.requestRawTickPage(
            requestId, normalizedResultId, offset, pageSize)) {
        m_backtestArchiveRawTickPageRequestGate.finish(requestId);
        m_backtestArchiveRawTickPageError = QStringLiteral(
            "异步原始 Tick 分页任务提交失败");
        emit backtestArchiveRawTickPageStateChanged();
        return 0;
    }
    emit backtestArchiveRawTickPageStateChanged();
    return requestId;
}

void TradingController::finishBacktestArchiveRawTickPage(
    quint64 requestId, const BacktestRawTickPage &page,
    const QString &error)
{
    if (!m_backtestArchiveRawTickPageRequestGate.finish(requestId)) return;
    if (!error.isEmpty() || page.resultId.isEmpty()
        || page.resultId != m_selectedBacktestArchiveId) {
        m_backtestArchiveRawTickPageError = error.isEmpty()
            ? QStringLiteral("原始 Tick 分页响应与当前归档不一致") : error;
        emit backtestArchiveRawTickPageStateChanged();
        return;
    }

    QVector<LocalTickSeriesPoint> points;
    points.reserve(page.ticks.size());
    for (int index = 0; index < page.ticks.size(); ++index) {
        LocalTickSeriesPoint point;
        point.tick = page.ticks.at(index);
        point.eventIndex = static_cast<qint64>(page.offset) + index;
        point.representedCount = 1;
        points.append(std::move(point));
    }
    m_backtestArchiveRawTickModel.setPoints(points);
    m_backtestArchiveRawTickPageResultId = page.resultId;
    m_backtestArchiveRawTickPageOffset = page.offset;
    m_backtestArchiveRawTickTotalCount = page.totalCount;
    m_backtestArchiveRawTickHasNext = page.hasMore;
    m_backtestArchiveRawTickPageError.clear();
    emit backtestArchiveRawTickPageStateChanged();
}

void TradingController::clear()
{
    m_tickModel.clear();
    clearBacktestResult();
    setStatusText(QStringLiteral("已清空"));
}

void TradingController::selectInstrument(const QString &symbol)
{
    const Instrument instrument = findInstrument(symbol);
    if (instrument.symbol.isEmpty()) {
        setStatusText(QStringLiteral("未找到标的：%1").arg(symbol));
        return;
    }
    if (m_workspaceInstrumentSelections.researchInstrument().symbol == instrument.symbol) return;
    m_workspaceInstrumentSelections.setResearchInstrument(instrument);
    m_activeHistoryRequestId = 0;
    m_historyCandleModel.clear();
    emit selectedInstrumentChanged();
    emit selectedMarketSessionChanged();
    setStatusText(QStringLiteral("已选择 %1 %2").arg(instrument.symbol, instrument.name));
}

bool TradingController::selectLiveInstrument(const QString &symbol)
{
    const QString previous = selectedLiveSymbol();
    QString error;
    if (!m_workspaceInstrumentSelections.selectLiveInstrument(symbol, &error)) {
        setStatusText(error);
        return false;
    }
    if (previous != selectedLiveSymbol()) {
        emit liveInstrumentChanged();
        m_realtimeMinuteRefreshTimer.stop();
        m_realtimeMinuteService.cancel();
        m_realtimeMinuteRequestGate.cancel();
        m_realtimeMinuteCandleModel.clear();
        m_realtimeMinuteSymbol = selectedLiveSymbol();
        m_realtimeMinuteLastBarTime = {};
        m_realtimeMinuteStale = true;
        m_realtimeMinuteError.clear();
        m_realtimeMinuteStatusText = m_realtimeLiveSessionState.requested()
            ? QStringLiteral("正在切换实时 1 分钟行情…")
            : QStringLiteral("已选择实时品种；启动行情后读取 1 分钟数据");
        emit realtimeMinuteStateChanged();
        if (m_realtimeLiveSessionState.requested())
            requestRealtimeMinuteSeries(selectedLiveSymbol());
    }
    setStatusText(QStringLiteral("主页实时品种已选择：%1")
                      .arg(selectedLiveInstrumentTitle()));
    return true;
}

bool TradingController::selectLocalBacktestInstrument(
    const QString &symbol, const QString &frequency, int minimumBars)
{
    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(frequency);
    QString error;
    if (periodSeconds <= 0) {
        error = QStringLiteral("不支持的本地回测周期：%1").arg(frequency);
    } else {
        m_workspaceInstrumentSelections.selectBacktestInstrument(
            symbol, periodSeconds, minimumBars, &error);
    }
    if (!error.isEmpty()) {
        m_backtestInstrumentSelectionError = error;
        emit backtestInstrumentChanged();
        setStatusText(error);
        return false;
    }
    m_backtestInstrumentSelectionError.clear();
    emit backtestInstrumentChanged();
    setStatusText(QStringLiteral("主页本地回测品种已选择：%1 · %2 · %3 根")
                      .arg(selectedBacktestInstrumentTitle(), selectedBacktestFrequency())
                      .arg(selectedBacktestCandleCount()));
    return true;
}

QVariantList TradingController::refreshLocalBacktestDataCatalog()
{
    // 任一本地目录刷新都可能来自 Tick/审计导入或 NAS 显式同步。旧的
    // “完整区间”只代表分析时的一致读快照，不能继续显示为当前建议。
    const bool hadAvailabilityState = rawTickAvailabilityLoading()
        || m_asyncRawTickAvailabilityService.isBusy()
        || !m_rawTickAvailabilityResult.isEmpty()
        || !m_rawTickAvailabilityError.isEmpty();
    if (hadAvailabilityState) {
        m_asyncRawTickAvailabilityService.cancel();
        m_rawTickAvailabilityRequestGate.cancel();
        m_rawTickAvailabilityResult.clear();
        m_rawTickAvailabilityError.clear();
        emit rawTickAvailabilityStateChanged();
    }

    const QString previousSymbol = selectedBacktestSymbol();
    const int previousPeriod = selectedBacktestPeriodSeconds();
    QVector<LocalHistoryDataset> catalog = m_storageReady
        ? m_dataStore.localHistoryCatalog() : QVector<LocalHistoryDataset>{};
    if (m_storageReady) {
        const QVector<BacktestDatasetIdentity> identities =
            m_dataStore.loadBacktestDatasetIdentities();
        for (LocalHistoryDataset &item : catalog) {
            if (!item.isBacktestEligible()) continue;
            for (const BacktestDatasetIdentity &identity : identities) {
                if (identity.instrument.symbol == item.instrument.symbol
                    && identity.periodSeconds == item.periodSeconds) {
                    item.datasetVersion = identity.datasetVersion;
                    item.datasetFirstDate = identity.firstTradingDate;
                    item.datasetLastDate = identity.lastTradingDate;
                    item.datasetDayCount = identity.days.size();
                    item.datasetRawTickCount = identity.rawTickTotal();
                    item.datasetGapDayCount = identity.gapDayCount;
                    item.datasetStaleDayCount = identity.staleDayCount;
                    break;
                }
            }
        }
    }
    m_workspaceInstrumentSelections.setLocalHistoryCatalog(catalog);

    if (!m_workspaceInstrumentSelections.selectedBacktestDataset()) {
        for (const LocalHistoryDataset &item
             : m_workspaceInstrumentSelections.localHistoryCatalog()) {
            if (item.isBacktestEligible() && item.candleCount >= 2) {
                QString ignored;
                m_workspaceInstrumentSelections.selectBacktestInstrument(
                    item.instrument.symbol, item.periodSeconds, 2, &ignored);
                break;
            }
        }
    }

    if (m_workspaceInstrumentSelections.selectedBacktestDataset()) {
        m_backtestInstrumentSelectionError.clear();
    } else if (!m_workspaceInstrumentSelections.backtestInstrument().symbol.isEmpty()) {
        m_backtestInstrumentSelectionError =
            QStringLiteral("已选回测品种的本地 K 线缓存已不存在");
    } else {
        m_backtestInstrumentSelectionError =
            QStringLiteral("本地历史数据库没有可用的历史 Tick 派生 K 线");
    }
    emit localBacktestDataCatalogChanged();
    if (previousSymbol != selectedBacktestSymbol()
        || previousPeriod != selectedBacktestPeriodSeconds()
        || !m_backtestInstrumentSelectionError.isEmpty()) {
        emit backtestInstrumentChanged();
    }
    return localBacktestDataCatalog();
}

qulonglong TradingController::requestLocalCandleSeries(
    const QString &symbol, const QString &frequency, int page,
    int pageSize, int maximumPoints)
{
    return requestLocalCandleSeriesPeriod(
        symbol, HistoricalDataService::periodSecondsForFrequency(frequency),
        page, pageSize, maximumPoints);
}

qulonglong TradingController::requestLocalCandleSeriesPeriod(
    const QString &symbol, int periodSeconds, int page,
    int pageSize, int maximumPoints)
{
    m_localCandleSeriesPrependRequestId = 0;
    m_prepareLocalBacktestPeriodRequestId = 0;
    const quint64 requestId = m_localCandleSeriesRequestGate.begin();
    m_localCandleSeriesError.clear();
    m_localCandleSeriesStatusText = QStringLiteral("正在异步读取本地分钟序列…");
    emit localCandleSeriesStateChanged();

    LocalCandleSeriesQuery query;
    query.periodSeconds = periodSeconds;
    query.page = page;
    query.pageSize = pageSize;
    query.maximumPoints = maximumPoints;
    const QString normalizedSymbol = symbol.trimmed();
    for (const LocalHistoryDataset &dataset
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (dataset.instrument.symbol == normalizedSymbol) {
            query.instrument = dataset.instrument;
            break;
        }
    }
    if (query.instrument.symbol.isEmpty()) query.instrument = findInstrument(normalizedSymbol);

    if (query.instrument.symbol.isEmpty()) {
        LocalCandleSeriesResult result;
        result.query = query;
        result.query.instrument.symbol = normalizedSymbol;
        result.error = QStringLiteral("本地目录中不存在品种 %1").arg(normalizedSymbol);
        finishLocalCandleSeriesRequest(requestId, result);
        return requestId;
    }
    if (!m_storageReady || !m_asyncLocalCandleQueryService.isRunning()
        || !m_asyncLocalCandleQueryService.request(requestId, query)) {
        LocalCandleSeriesResult result;
        result.query = query;
        result.error = QStringLiteral("本地分钟序列查询任务提交失败");
        finishLocalCandleSeriesRequest(requestId, result);
    }
    return requestId;
}

qulonglong TradingController::requestOlderLocalCandleSeries()
{
    if (localCandleSeriesLoading() || !localCandleSeriesHasOlderData()
        || m_localCandleSeriesSymbol.isEmpty()
        || m_localCandleSeriesPeriodSeconds <= 0) return 0;
    const quint64 requestId = requestLocalCandleSeriesPeriod(
        m_localCandleSeriesSymbol, m_localCandleSeriesPeriodSeconds,
        m_localCandleSeriesPage + 1, m_localCandleSeriesPageSize,
        m_localCandleSeriesMaximumPoints);
    if (m_localCandleSeriesRequestGate.loading())
        m_localCandleSeriesPrependRequestId = requestId;
    return requestId;
}

qulonglong TradingController::prepareLocalBacktestPeriod(
    const QString &symbol, int periodSeconds)
{
    if (periodSeconds < 1 || periodSeconds > 86400) {
        m_backtestInstrumentSelectionError = QStringLiteral(
            "回测 K 线周期必须在 1–86400 秒之间");
        emit backtestInstrumentChanged();
        return 0;
    }
    const quint64 requestId = requestLocalCandleSeriesPeriod(
        symbol, periodSeconds, 0, 5000, 800);
    if (m_localCandleSeriesRequestGate.loading())
        m_prepareLocalBacktestPeriodRequestId = requestId;
    return requestId;
}

qulonglong TradingController::requestLocalTickSeries(
    const QString &symbol, const QString &tradingDate, int maximumPoints)
{
    const quint64 requestId = m_localTickSeriesRequestGate.begin();
    m_localTickSeriesError.clear();
    m_localTickSeriesStatusText = QStringLiteral("正在异步读取逐笔事件序列…");
    emit localTickSeriesStateChanged();

    LocalTickSeriesQuery query;
    const QString normalizedSymbol = symbol.trimmed();
    for (const LocalHistoryDataset &dataset
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (dataset.instrument.symbol == normalizedSymbol) {
            query.instrument = dataset.instrument;
            break;
        }
    }
    if (query.instrument.symbol.isEmpty()) query.instrument = findInstrument(normalizedSymbol);
    query.tradingDate = QDate::fromString(tradingDate.trimmed(), Qt::ISODate);
    query.maximumPoints = maximumPoints;

    if (query.instrument.symbol.isEmpty()) {
        LocalTickSeriesResult result;
        result.query = query;
        result.query.instrument.symbol = normalizedSymbol;
        result.error = QStringLiteral("本地目录中不存在品种 %1").arg(normalizedSymbol);
        finishLocalTickSeriesRequest(requestId, result);
        return requestId;
    }
    if (!tradingDate.trimmed().isEmpty() && !query.tradingDate.isValid()) {
        LocalTickSeriesResult result;
        result.query = query;
        result.error = QStringLiteral("交易日格式必须为 yyyy-MM-dd；留空读取最新交易日");
        finishLocalTickSeriesRequest(requestId, result);
        return requestId;
    }
    if (!m_storageReady || !m_asyncLocalTickQueryService.isRunning()
        || !m_asyncLocalTickQueryService.request(requestId, query)) {
        LocalTickSeriesResult result;
        result.query = query;
        result.error = QStringLiteral("本地逐笔序列查询任务提交失败");
        finishLocalTickSeriesRequest(requestId, result);
    }
    return requestId;
}

qulonglong TradingController::requestLiveDayCandleSeries(
    const QString &symbol, int periodSeconds, int maximumPoints)
{
    const quint64 requestId = m_liveDayCandleSeriesRequestGate.begin();
    m_liveDayCandleSeriesError.clear();
    m_liveDayCandleSeriesStatusText = QStringLiteral("正在从原始逐笔生成真实交易日 K 线…");
    emit liveDayCandleSeriesStateChanged();
    LocalCandleSeriesQuery query;
    query.instrument = findInstrument(symbol.trimmed());
    if (query.instrument.symbol.isEmpty()) {
        for (const LocalHistoryDataset &dataset
             : m_workspaceInstrumentSelections.localHistoryCatalog()) {
            if (dataset.instrument.symbol == symbol.trimmed()) {
                query.instrument = dataset.instrument;
                break;
            }
        }
    }
    query.periodSeconds = periodSeconds;
    query.page = 0;
    query.pageSize = 5000;
    query.maximumPoints = maximumPoints;
    query.latestTradingDayOnly = true;
    if (query.instrument.symbol.isEmpty() || !m_storageReady
        || !m_asyncLiveDayCandleQueryService.isRunning()
        || !m_asyncLiveDayCandleQueryService.request(requestId, query)) {
        LocalCandleSeriesResult result;
        result.query = query;
        result.error = query.instrument.symbol.isEmpty()
            ? QStringLiteral("未找到真实交易日品种")
            : QStringLiteral("真实交易日 K 线查询任务提交失败");
        finishLiveDayCandleSeriesRequest(requestId, result);
    }
    return requestId;
}

bool TradingController::setRealtimeMinutePeriod(int periodSeconds)
{
    if (!TencentMinuteBarService::supportsPeriod(periodSeconds)) {
        m_realtimeMinuteError = QStringLiteral(
            "实时行情周期只支持 1m、5m、10m、15m、30m、60m");
        emit realtimeMinuteStateChanged();
        return false;
    }
    if (m_realtimeMinutePeriodSeconds == periodSeconds) return true;
    m_realtimeMinutePeriodSeconds = periodSeconds;
    m_realtimeMinuteError.clear();
    rebuildRealtimeMinuteModel();
    emit realtimeMinuteStateChanged();
    return true;
}

QString TradingController::openRealtimeChartWindow(const QString &symbol)
{
    if (m_realtimeChartWindowModel.count() >= 8) {
        m_realtimeChartWindowError = QStringLiteral("实时行情窗口最多同时打开 8 个");
        emit realtimeChartWindowsChanged();
        return {};
    }
    if (m_aShareUniverse.isEmpty() && !reloadAShareUniverse()) {
        m_realtimeChartWindowError = m_aShareUniverseError;
        emit realtimeChartWindowsChanged();
        return {};
    }
    const QString requestedSymbol = symbol.trimmed().isEmpty()
        ? selectedLiveSymbol() : symbol.trimmed();
    const Instrument instrument = findInstrument(requestedSymbol);
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
        m_realtimeChartWindowError = QStringLiteral(
            "实时窗口品种不在已加载的沪深 A 股股票池中：%1")
            .arg(requestedSymbol);
        emit realtimeChartWindowsChanged();
        return {};
    }
    const QString windowId = QStringLiteral("realtime-window-%1")
                                 .arg(++m_realtimeChartWindowSequence);
    const int refreshOffsetMilliseconds =
        (m_realtimeChartWindowModel.count() % 4) * 220;
    RealtimeChartSession *session = m_realtimeChartWindowModel.appendSession(
        windowId, instrument, refreshOffsetMilliseconds);
    session->setRefreshEnabled(m_realtimeLiveSessionState.requested());
    session->requestRefresh();
    m_realtimeChartWindowError.clear();
    emit realtimeChartWindowsChanged();
    setStatusText(QStringLiteral("已打开实时行情窗口：%1")
                      .arg(session->instrumentTitle()));
    return windowId;
}

bool TradingController::closeRealtimeChartWindow(const QString &windowId)
{
    if (!m_realtimeChartWindowModel.removeSession(windowId)) {
        m_realtimeChartWindowError = QStringLiteral("实时行情窗口不存在：%1")
                                         .arg(windowId);
        emit realtimeChartWindowsChanged();
        return false;
    }
    m_realtimeChartWindowError.clear();
    emit realtimeChartWindowsChanged();
    return true;
}

bool TradingController::setRealtimeChartWindowInstrument(
    const QString &windowId, const QString &symbol)
{
    RealtimeChartSession *session =
        m_realtimeChartWindowModel.sessionById(windowId);
    if (!session) {
        m_realtimeChartWindowError = QStringLiteral("实时行情窗口不存在：%1")
                                         .arg(windowId);
        emit realtimeChartWindowsChanged();
        return false;
    }
    const Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
        m_realtimeChartWindowError = QStringLiteral(
            "切换失败，品种不在已加载的沪深 A 股股票池中：%1")
            .arg(symbol.trimmed());
        emit realtimeChartWindowsChanged();
        return false;
    }
    session->setInstrument(instrument, true);
    m_realtimeChartWindowError.clear();
    emit realtimeChartWindowsChanged();
    setStatusText(QStringLiteral("实时窗口已切换：%1")
                      .arg(session->instrumentTitle()));
    return true;
}

QVariantList TradingController::searchRealtimeChartWindowInstruments(
    const QString &windowId, const QString &query, int limit) const
{
    if (!m_realtimeChartWindowModel.sessionById(windowId)) return {};
    const QString normalized = query.trimmed();
    if (normalized.isEmpty()) return {};
    limit = std::clamp(limit, 1, 100);
    QVariantList result;
    result.reserve(limit);
    for (const Instrument &instrument : m_aShareUniverse) {
        if (!instrument.symbol.contains(normalized, Qt::CaseInsensitive)
            && !instrument.name.contains(normalized, Qt::CaseInsensitive)) {
            continue;
        }
        result.append(QVariantMap {
            {QStringLiteral("code"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("venue"), instrument.venue}
        });
        if (result.size() >= limit) break;
    }
    return result;
}

QString TradingController::addRealtimeChart(
    const QString &windowId, int periodSeconds)
{
    RealtimeChartSession *session =
        m_realtimeChartWindowModel.sessionById(windowId);
    if (!session) return {};
    return session->addChart(periodSeconds);
}

bool TradingController::removeRealtimeChart(
    const QString &windowId, const QString &chartId)
{
    RealtimeChartSession *session =
        m_realtimeChartWindowModel.sessionById(windowId);
    return session && session->removeChart(chartId);
}

bool TradingController::setRealtimeChartPeriod(
    const QString &windowId, const QString &chartId, int periodSeconds)
{
    RealtimeChartSession *session =
        m_realtimeChartWindowModel.sessionById(windowId);
    return session && session->setChartPeriod(chartId, periodSeconds);
}

qulonglong TradingController::refreshRealtimeChartWindow(
    const QString &windowId)
{
    RealtimeChartSession *session =
        m_realtimeChartWindowModel.sessionById(windowId);
    if (!session) {
        m_realtimeChartWindowError = QStringLiteral("实时行情窗口不存在：%1")
                                         .arg(windowId);
        emit realtimeChartWindowsChanged();
        return 0;
    }
    m_realtimeChartWindowError.clear();
    emit realtimeChartWindowsChanged();
    return session->requestRefresh();
}

qulonglong TradingController::requestRealtimeMinuteSeries(const QString &symbol)
{
    return requestRealtimeMinuteSeriesPeriod(symbol, m_realtimeMinutePeriodSeconds);
}

qulonglong TradingController::requestRealtimeMinuteSeriesPeriod(
    const QString &symbol, int periodSeconds)
{
    if (periodSeconds > 0 && !setRealtimeMinutePeriod(periodSeconds)) return 0;
    m_realtimeMinuteRefreshTimer.stop();
    const quint64 requestId = m_realtimeMinuteRequestGate.begin();
    const QString requestedSymbol = symbol.trimmed().isEmpty()
        ? selectedLiveSymbol() : symbol.trimmed();
    const Instrument instrument = findInstrument(requestedSymbol);
    if (m_realtimeMinuteSymbol != requestedSymbol) {
        m_realtimeMinuteCandleModel.clear();
        m_realtimeMinuteRawCandles.clear();
        m_realtimeMinuteLastBarTime = {};
    }
    m_realtimeMinuteSymbol = requestedSymbol;
    m_realtimeMinuteError.clear();
    m_realtimeMinuteStale = true;
    m_realtimeMinuteStatusText = QStringLiteral(
        "正在读取腾讯 1 分钟基础行情并生成 %1；仅保存在内存中…")
        .arg(periodText(m_realtimeMinutePeriodSeconds));
    emit realtimeMinuteStateChanged();
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare
        || !m_realtimeMinuteService.request(requestId, instrument, 320)) {
        RealtimeMinuteSeriesResult result;
        result.instrument = instrument;
        result.requestedAt = QDateTime::currentDateTimeUtc();
        result.fetchedAt = result.requestedAt;
        result.providerId = QStringLiteral("tencent_public_m1");
        result.error = instrument.symbol.isEmpty()
            ? QStringLiteral("实时品种不在已加载的沪深 A 股股票池中：%1")
                  .arg(requestedSymbol)
            : QStringLiteral("腾讯 1 分钟行情请求提交失败");
        finishRealtimeMinuteRequest(requestId, result);
    }
    return requestId;
}

void TradingController::finishLocalCandleSeriesRequest(
    quint64 requestId, const LocalCandleSeriesResult &result)
{
    const bool prepend = requestId == m_localCandleSeriesPrependRequestId;
    const bool prepareBacktestPeriod =
        requestId == m_prepareLocalBacktestPeriodRequestId;
    if (!m_localCandleSeriesRequestGate.finish(requestId)) return;
    m_localCandleSeriesPrependRequestId = 0;
    m_prepareLocalBacktestPeriodRequestId = 0;

    if (prepend && result.succeeded
        && !m_localCandleSeriesModel.prependPoints(result.points)) {
        m_localCandleSeriesError = QStringLiteral(
            "较早分页与当前图表的数据身份或时间边界不一致，已保留当前视图");
        m_localCandleSeriesStatusText = QStringLiteral("较早分页加载失败");
        emit localCandleSeriesStateChanged();
        return;
    }
    m_localCandleSeriesSymbol = result.query.instrument.symbol;
    m_localCandleSeriesFrequency = periodText(result.query.periodSeconds);
    m_localCandleSeriesPeriodSeconds = result.query.periodSeconds;
    m_localCandleSeriesPage = result.query.page;
    m_localCandleSeriesPageSize = result.query.pageSize;
    m_localCandleSeriesMaximumPoints = result.query.maximumPoints;
    m_localCandleSeriesPageCount = result.pageCount;
    m_localCandleSeriesTotalBars = result.totalBars;
    m_localCandleSeriesRawBars = prepend
        ? m_localCandleSeriesRawBars + result.rawBarCount : result.rawBarCount;
    m_localCandleSeriesBucketSize = prepend
        ? std::max(m_localCandleSeriesBucketSize, result.bucketSize)
        : result.bucketSize;
    m_localCandleSeriesDownsampled = prepend
        ? m_localCandleSeriesDownsampled || result.downsampled
        : result.downsampled;
    m_localCandleSeriesError = result.error;
    if (result.succeeded) {
        if (!prepend) m_localCandleSeriesModel.setPoints(result.points);
        const QVector<LocalCandleSeriesPoint> &loaded =
            m_localCandleSeriesModel.points();
        m_localCandleSeriesFirstTime = loaded.isEmpty()
            ? QDateTime{} : loaded.first().candle.startTime;
        m_localCandleSeriesLastTime = loaded.isEmpty()
            ? QDateTime{} : loaded.last().endTime;
        if (loaded.isEmpty()) {
            m_localCandleSeriesStatusText = QStringLiteral("该本地分钟数据集为空");
        } else if (prepend) {
            m_localCandleSeriesStatusText = QStringLiteral(
                "已向左追加第 %1/%2 页 · 累计 %3 根原始 K 线 · %4 个显示点")
                .arg(result.query.page + 1).arg(result.pageCount)
                .arg(m_localCandleSeriesRawBars).arg(loaded.size());
        } else {
            const QString cacheText = result.regeneratedCacheDays > 0
                ? QStringLiteral(" · 重建 %1 个交易日缓存").arg(result.regeneratedCacheDays)
                : QString{};
            m_localCandleSeriesStatusText = (result.downsampled
                ? QStringLiteral("已加载 %1 根原始 K 线，按每桶最多 %2 根聚合为 %3 点")
                      .arg(result.rawBarCount).arg(result.bucketSize).arg(result.points.size())
                : QStringLiteral("已加载 %1 根本地 K 线").arg(result.rawBarCount)) + cacheText;
        }
        if (prepareBacktestPeriod && !loaded.isEmpty()) {
            refreshLocalBacktestDataCatalog();
            QString selectionError;
            m_workspaceInstrumentSelections.selectBacktestInstrument(
                result.query.instrument.symbol, result.query.periodSeconds,
                2, &selectionError);
            m_backtestInstrumentSelectionError = selectionError;
            emit backtestInstrumentChanged();
            if (selectionError.isEmpty()) {
                m_localCandleSeriesStatusText += QStringLiteral(
                    " · 已生成并选中本地回测周期 %1")
                    .arg(localHistoryFrequency(result.query.periodSeconds));
            }
        }
    } else {
        if (!prepend) {
            m_localCandleSeriesModel.clear();
            m_localCandleSeriesFirstTime = {};
            m_localCandleSeriesLastTime = {};
        }
        m_localCandleSeriesStatusText = QStringLiteral("本地分钟序列查询失败");
    }
    emit localCandleSeriesStateChanged();
}

void TradingController::finishLocalTickSeriesRequest(
    quint64 requestId, const LocalTickSeriesResult &result)
{
    if (!m_localTickSeriesRequestGate.finish(requestId)) return;
    m_localTickSeriesSymbol = result.query.instrument.symbol;
    m_localTickSeriesTradingDate = result.resolvedTradingDate;
    m_localTickSeriesRawCount = result.rawTickCount;
    m_localTickSeriesDownsampled = result.downsampled;
    m_localTickSeriesError = result.error;
    if (result.succeeded) {
        m_localTickSeriesModel.setPoints(result.points);
        if (result.points.isEmpty()) {
            m_localTickSeriesStatusText = QStringLiteral("该品种没有可用的原始逐笔成交");
        } else {
            m_localTickSeriesStatusText = result.downsampled
                ? QStringLiteral("%1 · 原始 %2 笔 · 按事件序号抽样显示 %3 点（保留首尾）")
                      .arg(result.resolvedTradingDate.toString(Qt::ISODate))
                      .arg(result.rawTickCount).arg(result.points.size())
                : QStringLiteral("%1 · 已加载全部 %2 笔逐笔成交")
                      .arg(result.resolvedTradingDate.toString(Qt::ISODate))
                      .arg(result.rawTickCount);
        }
    } else {
        m_localTickSeriesModel.clear();
        m_localTickSeriesStatusText = QStringLiteral("本地逐笔事件序列查询失败");
    }
    emit localTickSeriesStateChanged();
}

void TradingController::finishLiveDayCandleSeriesRequest(
    quint64 requestId, const LocalCandleSeriesResult &result)
{
    if (!m_liveDayCandleSeriesRequestGate.finish(requestId)) return;
    m_liveDayCandleSeriesPeriodSeconds = result.query.periodSeconds;
    m_liveDayCandleSeriesTradingDate = result.resolvedTradingDate;
    m_liveDayCandleSeriesError = result.error;
    if (result.succeeded) {
        m_liveDayCandleSeriesModel.setPoints(result.points);
        if (result.points.isEmpty()) {
            m_liveDayCandleSeriesStatusText = QStringLiteral(
                "该品种没有可用的原始逐笔交易日数据");
        } else {
            m_liveDayCandleSeriesStatusText = QStringLiteral(
                "%1 · %2 · %3 根真实 K 线%4")
                .arg(result.query.instrument.symbol,
                     result.resolvedTradingDate.toString(Qt::ISODate),
                     QString::number(result.rawBarCount),
                     result.regeneratedCacheDays > 0
                         ? QStringLiteral(" · 周期缓存已重建") : QString{});
        }
    } else {
        m_liveDayCandleSeriesModel.clear();
        m_liveDayCandleSeriesStatusText = QStringLiteral("真实交易日 K 线查询失败");
    }
    emit liveDayCandleSeriesStateChanged();
}

void TradingController::finishRealtimeMinuteRequest(
    quint64 requestId, const RealtimeMinuteSeriesResult &result)
{
    if (!m_realtimeMinuteRequestGate.finish(requestId)) return;
    m_realtimeMinuteRefreshTimer.stop();
    const QDateTime previousLastBarTime = m_realtimeMinuteLastBarTime;
    m_realtimeMinuteSymbol = result.instrument.symbol;
    m_realtimeMinuteSource = result.providerId;
    m_realtimeMinuteError = result.error;
    m_realtimeMinuteLastBarTime = result.lastBarTime;
    const qint64 ageSeconds = result.lastBarTime.isValid()
        ? result.lastBarTime.secsTo(QDateTime::currentDateTimeUtc())
        : std::numeric_limits<qint64>::max();
    m_realtimeMinuteStale = ageSeconds > 120;
    if (result.succeeded) {
        m_realtimeMinuteRawCandles = result.candles;
        rebuildRealtimeMinuteModel();
    } else {
        m_realtimeMinuteRawCandles.clear();
        m_realtimeMinuteCandleModel.clear();
        m_realtimeMinuteStatusText = QStringLiteral("实时 1 分钟行情不可用");
    }
    LogService::instance().info(
        QStringLiteral("realtime-minute"),
        QStringLiteral("request=%1 symbol=%2 source=%3 bars=%4 succeeded=%5 "
                       "requestedUtc=%6 fetchedUtc=%7 previousLastBarUtc=%8 "
                       "lastBarUtc=%9 refreshEnabled=%10 error=%11")
            .arg(requestId)
            .arg(result.instrument.symbol, result.providerId)
            .arg(result.candles.size())
            .arg(result.succeeded ? 1 : 0)
            .arg(result.requestedAt.toUTC().toString(Qt::ISODateWithMs),
                 result.fetchedAt.toUTC().toString(Qt::ISODateWithMs),
                 previousLastBarTime.isValid()
                     ? previousLastBarTime.toUTC().toString(Qt::ISODateWithMs)
                     : QStringLiteral("-"),
                 result.lastBarTime.isValid()
                     ? result.lastBarTime.toUTC().toString(Qt::ISODateWithMs)
                     : QStringLiteral("-"),
                 m_realtimeLiveSessionState.minuteRefreshEnabled()
                     ? QStringLiteral("1") : QStringLiteral("0"),
                 result.error.isEmpty() ? QStringLiteral("-") : result.error));
    emit realtimeMinuteStateChanged();
    scheduleRealtimeMinuteRefresh();
}

void TradingController::scheduleRealtimeMinuteRefresh()
{
    m_realtimeMinuteRefreshTimer.stop();
    if (m_shutdownRequested
        || !m_realtimeLiveSessionState.minuteRefreshEnabled()
        || selectedLiveSymbol().isEmpty()) return;
    m_realtimeMinuteRefreshTimer.start(
        TencentMinuteBarService::millisecondsUntilNextMinuteRefresh(
            QDateTime::currentDateTimeUtc()));
}

void TradingController::rebuildRealtimeMinuteModel()
{
    const QVector<Candle> candles = TencentMinuteBarService::aggregateCandles(
        m_realtimeMinuteRawCandles, m_realtimeMinutePeriodSeconds);
    QVector<LocalCandleSeriesPoint> points;
    points.reserve(candles.size());
    for (const Candle &candle : candles) {
        LocalCandleSeriesPoint point;
        point.candle = candle;
        point.endTime = candle.startTime.addSecs(candle.periodSeconds);
        point.sourceCount = std::max(1, candle.periodSeconds / 60);
        points.append(point);
    }
    m_realtimeMinuteCandleModel.setPoints(points);
    if (m_realtimeMinuteRawCandles.isEmpty()) {
        m_realtimeMinuteStatusText = QStringLiteral("实时分钟行情尚未加载");
        return;
    }
    m_realtimeMinuteStatusText = QStringLiteral(
        "%1 · 腾讯 1m 基础行情派生 %2 · %3 根 · %4 · 仅内存，不进入历史回测库")
        .arg(m_realtimeMinuteSymbol, periodText(m_realtimeMinutePeriodSeconds))
        .arg(points.size())
        .arg(m_realtimeMinuteStale ? QStringLiteral("休市或行情陈旧")
                                   : QStringLiteral("行情已更新"));
}

bool TradingController::selectHistoryDownloadInstrument(const QString &symbol)
{
    const Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
        m_historyDownloadError = QStringLiteral("下载品种不在已加载的 A 股股票池中：%1")
                                     .arg(symbol.trimmed());
        emit historyDownloadStateChanged();
        return false;
    }
    m_historyDownloadInstrument = instrument;
    m_historyDownloadError.clear();
    m_historyDownloadStatusText = QStringLiteral("已选择下载品种；尚未请求网络");
    m_historyDownloadAvailableCount = 0;
    m_historyDownloadFirstTime = {};
    m_historyDownloadLastTime = {};
    const int periodSeconds =
        HistoricalDataService::periodSecondsForFrequency(m_historyDownloadFrequency);
    for (const LocalHistoryDataset &item
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (item.instrument.symbol == instrument.symbol
            && item.periodSeconds == periodSeconds) {
            m_historyDownloadAvailableCount = item.candleCount;
            m_historyDownloadFirstTime = item.firstTime;
            m_historyDownloadLastTime = item.lastTime;
            break;
        }
    }
    emit historyDownloadStateChanged();
    return true;
}

qulonglong TradingController::downloadAndStoreHistory(const QString &frequency,
                                                       int targetBars)
{
    return downloadAndStoreHistoryAdjusted(frequency, targetBars,
                                           QStringLiteral("none"));
}

qulonglong TradingController::downloadAndStoreHistoryAdjusted(
    const QString &frequency, int targetBars, const QString &adjustment)
{
    ++m_historyDownloadRequestVersion;
    m_historyDownloadFrequency = frequency;
    m_historyDownloadTargetBars = targetBars;
    m_historyDownloadSource.clear();
    m_historyDownloadStoredCount = 0;
    m_historyDownloadProvider.clear();
    m_historyDownloadAdjustment = adjustment.isEmpty()
        ? QStringLiteral("none") : adjustment;
    m_historyDownloadDataVersion.clear();
    m_historyDownloadDownloadedAt = {};
    m_historyDownloadFallbackUsed = false;
    m_historyDownloadError.clear();

    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(frequency);
    if (m_historyDownloadInstrument.symbol.isEmpty()) {
        m_historyDownloadStatusText = QStringLiteral("请求未启动");
        m_historyDownloadError = QStringLiteral("请先从本地 A 股股票池选择下载品种");
    } else if (periodSeconds <= 0) {
        m_historyDownloadStatusText = QStringLiteral("请求未启动");
        m_historyDownloadError = QStringLiteral("不支持的历史数据周期：%1").arg(frequency);
    } else if (!QStringList{QStringLiteral("none"), QStringLiteral("qfq"),
                            QStringLiteral("hfq")}.contains(
                                m_historyDownloadAdjustment)) {
        m_historyDownloadStatusText = QStringLiteral("请求未启动");
        m_historyDownloadError = QStringLiteral("复权方式必须为 none/qfq/hfq");
    } else if (targetBars < 2 || targetBars > 5000) {
        m_historyDownloadStatusText = QStringLiteral("请求未启动");
        m_historyDownloadError = QStringLiteral("目标 K 线根数必须在 2–5000 之间");
    } else if (!m_storageReady) {
        m_historyDownloadStatusText = QStringLiteral("请求未启动");
        m_historyDownloadError = QStringLiteral("SQLite 本地数据库不可用");
    }
    if (!m_historyDownloadError.isEmpty()) {
        m_historyDownloadRunning = false;
        emit historyDownloadStateChanged();
        return 0;
    }

    m_historyDownloadRunning = true;
    m_historyDownloadStatusText = QStringLiteral("正在检查缓存并准备显式下载");
    emit historyDownloadStateChanged();
    m_activeHistoryDownloadServiceRequestId = m_historyDownloadService.request(
        m_historyDownloadInstrument, frequency, targetBars, false,
        m_historyDownloadAdjustment);
    return m_historyDownloadRequestVersion;
}

bool TradingController::setTickDataImportRootDirectory(const QString &pathOrUrl)
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning) return false;
    const QUrl url(pathOrUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (path.isEmpty() || !QDir::isAbsolutePath(path)
        || path.startsWith(QStringLiteral("//"))) {
        m_tickDataImportError = QStringLiteral("请选择本地磁盘或已挂载盘符下的绝对目录");
        m_tickDataImportStatusText = QStringLiteral("目录未选择");
        emit tickDataImportStateChanged();
        return false;
    }
    m_tickDataImportRootDirectory = QDir::toNativeSeparators(path);
    m_tickDataImportFilePath.clear();
    m_tickDataImportError.clear();
    m_tickDataImportStatusText = QStringLiteral("目录已选择；尚未读取任何 CSV 文件");
    emit tickDataImportStateChanged();
    return true;
}

bool TradingController::selectTickDataImportInstrument(const QString &symbol)
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning) return false;
    const Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
        m_tickDataImportError = QStringLiteral("导入代码不在已加载的 A 股股票池中：%1")
                                    .arg(symbol.trimmed());
        emit tickDataImportStateChanged();
        return false;
    }
    m_tickDataImportInstrument = instrument;
    m_tickDataImportError.clear();
    m_tickDataImportStatusText = QStringLiteral("已选择导入品种；尚未读取文件");
    emit tickDataImportStateChanged();
    return true;
}

qulonglong TradingController::startTickDataImport(const QString &tradingDate,
                                                   int format, int periodSeconds,
                                                   int fiveMinuteVolumeUnit)
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning) return 0;
    m_tickDataImportTradingDate = tradingDate.trimmed();
    m_tickDataImportFormat = format;
    m_tickDataImportPeriodSeconds = periodSeconds;
    m_tickDataImportFiveMinuteVolumeUnit = fiveMinuteVolumeUnit;
    m_tickDataImportLinesRead = 0;
    m_tickDataImportValidRecords = 0;
    m_tickDataImportSkippedRecords = 0;
    m_tickDataImportWrittenBars = 0;
    m_tickDataImportRawTicksWritten = 0;
    m_tickDataImportFirstTime = {};
    m_tickDataImportLastTime = {};
    m_tickDataImportDiagnostics.clear();
    m_tickDataImportSource.clear();
    m_tickDataImportError.clear();

    QDate date = QDate::fromString(m_tickDataImportTradingDate, QStringLiteral("yyyy-MM-dd"));
    if (!date.isValid()) {
        date = QDate::fromString(m_tickDataImportTradingDate, QStringLiteral("yyyyMMdd"));
    }
    const bool formatValid = format == static_cast<int>(TickDataCsvFormat::Tick)
        || format == static_cast<int>(TickDataCsvFormat::FiveMinute);
    const bool volumeUnitValid = fiveMinuteVolumeUnit >= static_cast<int>(FiveMinuteVolumeUnit::Unknown)
        && fiveMinuteVolumeUnit <= static_cast<int>(FiveMinuteVolumeUnit::Lots);
    if (!m_storageReady) m_tickDataImportError = QStringLiteral("SQLite 本地数据库不可用");
    else if (m_tickDataImportRootDirectory.isEmpty())
        m_tickDataImportError = QStringLiteral("请先显式选择 TickData 根目录");
    else if (m_tickDataImportInstrument.symbol.isEmpty())
        m_tickDataImportError = QStringLiteral("请先选择单个 A 股代码");
    else if (!date.isValid()) m_tickDataImportError = QStringLiteral("交易日必须是 yyyy-MM-dd");
    else if (!formatValid || !volumeUnitValid)
        m_tickDataImportError = QStringLiteral("CSV 格式或 volume 单位配置无效");
    else if (format == static_cast<int>(TickDataCsvFormat::Tick)
             && periodSeconds != 60 && periodSeconds != 300)
        m_tickDataImportError = QStringLiteral("逐笔文件仅支持聚合为 1min 或 5min");
    else if (format == static_cast<int>(TickDataCsvFormat::FiveMinute)
             && periodSeconds != 300)
        m_tickDataImportError = QStringLiteral("5min 文件只能导入 5min K 线");
    else if (format == static_cast<int>(TickDataCsvFormat::FiveMinute)
             && fiveMinuteVolumeUnit == static_cast<int>(FiveMinuteVolumeUnit::Unknown))
        m_tickDataImportError = QStringLiteral("5min volume 单位未证实；请显式选择股或手");

    if (!m_tickDataImportError.isEmpty()) {
        m_tickDataImportStatusText = QStringLiteral("导入未启动");
        emit tickDataImportStateChanged();
        return 0;
    }

    TickDataImportRequest request;
    request.rootDirectory = m_tickDataImportRootDirectory;
    request.instrument = m_tickDataImportInstrument;
    request.tradingDate = date;
    request.format = static_cast<TickDataCsvFormat>(format);
    request.periodSeconds = periodSeconds;
    request.fiveMinuteVolumeUnit = static_cast<FiveMinuteVolumeUnit>(fiveMinuteVolumeUnit);
    m_tickDataImportFilePath = TickDataCsvImporter::expectedFilePath(request);
    const quint64 requestId = m_tickDataCsvImportService.start(request);
    if (requestId == 0) {
        m_tickDataImportError = QStringLiteral("已有本地 CSV 导入任务正在运行");
        m_tickDataImportStatusText = QStringLiteral("导入未启动");
        emit tickDataImportStateChanged();
        return 0;
    }
    m_tickDataImportRequestId = requestId;
    m_tickDataImportRunning = true;
    m_tickDataImportStatusText = QStringLiteral("正在逐行解析本地 CSV；不会访问网络");
    emit tickDataImportStateChanged();
    return requestId;
}

void TradingController::cancelTickDataImport()
{
    if (!m_tickDataImportRunning) return;
    m_tickDataCsvImportService.cancel();
    m_tickDataImportStatusText = QStringLiteral("正在取消本地 CSV 导入");
    emit tickDataImportStateChanged();
}

bool TradingController::setAShareInstrumentDayImportFile(
    const QString &pathOrUrl)
{
    if (m_aShareInstrumentDayImportRunning) return false;
    const QUrl url(pathOrUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (path.isEmpty() || !QDir::isAbsolutePath(path)
        || path.startsWith(QStringLiteral("//"))) {
        m_aShareInstrumentDayImportError = QStringLiteral(
            "请选择本地磁盘或已挂载盘符下的绝对 CSV 路径");
        m_aShareInstrumentDayImportStatusText = QStringLiteral("文件未选择");
        emit aShareInstrumentDayImportStateChanged();
        return false;
    }
    m_aShareInstrumentDayImportFilePath = QDir::toNativeSeparators(path);
    m_aShareInstrumentDayImportPreviewReady = false;
    m_aShareInstrumentDayImportPreviewContentSha256.clear();
    m_aShareInstrumentDayImportError.clear();
    m_aShareInstrumentDayImportStatusText = QStringLiteral(
        "文件已选择；尚未读取，请先预览差异再确认导入");
    emit aShareInstrumentDayImportStateChanged();
    return true;
}

qulonglong TradingController::startAShareInstrumentDayImport(
    const QString &source, const QString &sourceVersion)
{
    return previewAShareInstrumentDayImport(source, sourceVersion);
}

qulonglong TradingController::previewAShareInstrumentDayImport(
    const QString &source, const QString &sourceVersion)
{
    if (m_aShareInstrumentDayImportRunning) return 0;
    m_aShareInstrumentDayImportSource = source.trimmed().toUpper();
    m_aShareInstrumentDayImportSourceVersion = sourceVersion.trimmed();
    m_aShareInstrumentDayImportLinesRead = 0;
    m_aShareInstrumentDayImportValidRecords = 0;
    m_aShareInstrumentDayImportWrittenRecords = 0;
    m_aShareInstrumentDayImportNewRecords = 0;
    m_aShareInstrumentDayImportUnchangedRecords = 0;
    m_aShareInstrumentDayImportCorrectedRecords = 0;
    m_aShareInstrumentDayImportPreviewReady = false;
    m_aShareInstrumentDayImportPreviewContentSha256.clear();
    m_aShareInstrumentDayImportReused = false;
    m_aShareInstrumentDayImportContentSha256.clear();
    m_aShareInstrumentDayImportFirstDate = {};
    m_aShareInstrumentDayImportLastDate = {};
    m_aShareInstrumentDayImportDiagnostics.clear();
    m_aShareInstrumentDayImportError.clear();

    const QFileInfo fileInfo(m_aShareInstrumentDayImportFilePath);
    if (!m_storageReady) {
        m_aShareInstrumentDayImportError = QStringLiteral("SQLite 本地数据库不可用");
    } else if (m_aShareInstrumentDayImportFilePath.isEmpty()) {
        m_aShareInstrumentDayImportError = QStringLiteral("请先显式选择事实 CSV 文件");
    } else if (!fileInfo.exists() || !fileInfo.isFile()) {
        m_aShareInstrumentDayImportError = QStringLiteral("选择的 CSV 文件不存在");
    } else if (!QStringList{QStringLiteral("SSE"), QStringLiteral("SZSE")}
                    .contains(m_aShareInstrumentDayImportSource)) {
        m_aShareInstrumentDayImportError = QStringLiteral("来源只允许 SSE 或 SZSE");
    } else if (m_aShareInstrumentDayImportSourceVersion.isEmpty()) {
        m_aShareInstrumentDayImportError = QStringLiteral(
            "必须填写官方公告号、导出日期或数据版本");
    }
    if (!m_aShareInstrumentDayImportError.isEmpty()) {
        m_aShareInstrumentDayImportStatusText = QStringLiteral("导入未启动");
        emit aShareInstrumentDayImportStateChanged();
        return 0;
    }

    AShareInstrumentDayImportRequest request;
    request.filePath = m_aShareInstrumentDayImportFilePath;
    request.source = m_aShareInstrumentDayImportSource;
    request.sourceVersion = m_aShareInstrumentDayImportSourceVersion;
    const quint64 requestId = m_aShareInstrumentDayImportService.preview(request);
    if (requestId == 0) {
        m_aShareInstrumentDayImportError = QStringLiteral("已有事实导入任务正在结束");
        m_aShareInstrumentDayImportStatusText = QStringLiteral("导入未启动");
        emit aShareInstrumentDayImportStateChanged();
        return 0;
    }
    m_aShareInstrumentDayImportRequestId = requestId;
    m_aShareInstrumentDayImportRunning = true;
    m_aShareInstrumentDayImportStatusText = QStringLiteral(
        "正在后台逐行校验并与 SQLite 当前事实比较；本次不会写入");
    emit aShareInstrumentDayImportStateChanged();
    return requestId;
}

qulonglong TradingController::commitAShareInstrumentDayImport(
    const QString &source, const QString &sourceVersion,
    const QString &revisionReason)
{
    if (m_aShareInstrumentDayImportRunning) return 0;
    const QString normalizedSource = source.trimmed().toUpper();
    const QString normalizedVersion = sourceVersion.trimmed();
    if (!m_aShareInstrumentDayImportPreviewReady
        || m_aShareInstrumentDayImportPreviewFilePath
            != m_aShareInstrumentDayImportFilePath
        || m_aShareInstrumentDayImportPreviewSource != normalizedSource
        || m_aShareInstrumentDayImportPreviewSourceVersion != normalizedVersion
        || m_aShareInstrumentDayImportPreviewContentSha256.isEmpty()) {
        m_aShareInstrumentDayImportError = QStringLiteral(
            "文件、来源或版本与最近预览不一致；请重新预览");
        m_aShareInstrumentDayImportStatusText = QStringLiteral("确认导入未启动");
        emit aShareInstrumentDayImportStateChanged();
        return 0;
    }
    if (m_aShareInstrumentDayImportCorrectedRecords > 0
        && revisionReason.trimmed().isEmpty()) {
        m_aShareInstrumentDayImportError = QStringLiteral(
            "预览检测到既有事实变化；必须填写修订原因");
        m_aShareInstrumentDayImportStatusText = QStringLiteral("确认导入未启动");
        emit aShareInstrumentDayImportStateChanged();
        return 0;
    }

    AShareInstrumentDayImportRequest request;
    request.filePath = m_aShareInstrumentDayImportFilePath;
    request.source = normalizedSource;
    request.sourceVersion = normalizedVersion;
    request.revisionReason = revisionReason.trimmed();
    request.expectedContentSha256 = m_aShareInstrumentDayImportPreviewContentSha256;
    const quint64 requestId = m_aShareInstrumentDayImportService.start(request);
    if (requestId == 0) {
        m_aShareInstrumentDayImportError = QStringLiteral("已有事实导入任务正在结束");
        emit aShareInstrumentDayImportStateChanged();
        return 0;
    }
    m_aShareInstrumentDayImportRequestId = requestId;
    m_aShareInstrumentDayImportRunning = true;
    m_aShareInstrumentDayImportError.clear();
    m_aShareInstrumentDayImportStatusText = QStringLiteral(
        "正在重新校验预览哈希并事务写入不可变批次明细与当前事实");
    emit aShareInstrumentDayImportStateChanged();
    return requestId;
}

void TradingController::cancelAShareInstrumentDayImport()
{
    if (!m_aShareInstrumentDayImportRunning) return;
    m_aShareInstrumentDayImportService.cancel();
    m_aShareInstrumentDayImportStatusText = QStringLiteral("正在取消事实导入");
    emit aShareInstrumentDayImportStateChanged();
}

void TradingController::refreshAShareInstrumentDayImportHistory()
{
    m_aShareInstrumentDayImportHistory.clear();
    if (m_storageReady) {
        const auto runs = m_dataStore.loadAShareInstrumentDayImportRuns(20);
        m_aShareInstrumentDayImportHistory.reserve(runs.size());
        for (const auto &run : runs)
            m_aShareInstrumentDayImportHistory.append(run.toVariantMap());
    }
    emit aShareInstrumentDayImportStateChanged();
}

bool TradingController::setAShareIndustryImportFile(const QString &pathOrUrl)
{
    if (m_aShareIndustryImportState.value(QStringLiteral("running")).toBool())
        return false;
    const QUrl url(pathOrUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl.trimmed();
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (path.isEmpty() || !QDir::isAbsolutePath(path)
        || path.startsWith(QStringLiteral("//"))) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"),
            QStringLiteral("请选择本地磁盘或已挂载盘符下的绝对 CSV 路径"));
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("文件未选择"));
        emit aShareIndustryImportStateChanged();
        return false;
    }
    m_aShareIndustryImportState.insert(
        QStringLiteral("filePath"), QDir::toNativeSeparators(path));
    m_aShareIndustryImportState.insert(QStringLiteral("previewReady"), false);
    m_aShareIndustryImportState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_aShareIndustryImportState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("文件已选择；尚未读取，请先预览再确认导入"));
    m_aShareIndustryPreviewFileSha256.clear();
    emit aShareIndustryImportStateChanged();
    return true;
}

qulonglong TradingController::previewAShareIndustryImport(
    const QString &classification, const QString &source,
    const QString &sourceVersion)
{
    if (m_aShareIndustryImportState.value(QStringLiteral("running")).toBool())
        return 0;
    const QString filePath = m_aShareIndustryImportState
        .value(QStringLiteral("filePath")).toString();
    m_aShareIndustryImportState.insert(
        QStringLiteral("classification"), classification.trimmed());
    m_aShareIndustryImportState.insert(
        QStringLiteral("source"), source.trimmed());
    m_aShareIndustryImportState.insert(
        QStringLiteral("sourceVersion"), sourceVersion.trimmed());
    m_aShareIndustryImportState.insert(QStringLiteral("previewReady"), false);
    m_aShareIndustryImportState.insert(QStringLiteral("reused"), false);
    m_aShareIndustryImportState.insert(
        QStringLiteral("requiresRevisionReason"), false);
    m_aShareIndustryImportState.insert(QStringLiteral("linesRead"), qint64(0));
    m_aShareIndustryImportState.insert(QStringLiteral("validRecords"), qint64(0));
    m_aShareIndustryImportState.insert(QStringLiteral("writtenRecords"), qint64(0));
    m_aShareIndustryImportState.insert(
        QStringLiteral("fileContentSha256"), QStringLiteral(""));
    m_aShareIndustryImportState.insert(
        QStringLiteral("datasetContentSha256"), QStringLiteral(""));
    m_aShareIndustryImportState.insert(
        QStringLiteral("datasetId"), QStringLiteral(""));
    m_aShareIndustryImportState.insert(
        QStringLiteral("diagnostics"), QVariantList{});
    m_aShareIndustryImportState.insert(QStringLiteral("error"), QStringLiteral(""));
    if (!m_storageReady) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"), QStringLiteral("SQLite 本地数据库不可用"));
    } else if (filePath.isEmpty() || !QFileInfo(filePath).isFile()) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"), QStringLiteral("请先选择存在的行业分类 CSV"));
    }
    if (!m_aShareIndustryImportState.value(QStringLiteral("error"))
             .toString().isEmpty()) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("预览未启动"));
        emit aShareIndustryImportStateChanged();
        return 0;
    }
    AShareIndustryImportRequest request;
    request.filePath = filePath;
    request.classification = classification;
    request.source = source;
    request.sourceVersion = sourceVersion;
    const quint64 requestId = m_aShareIndustryImportService.preview(request);
    if (requestId == 0) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"), QStringLiteral("已有行业导入任务正在结束"));
        emit aShareIndustryImportStateChanged();
        return 0;
    }
    m_aShareIndustryImportState.insert(QStringLiteral("requestId"), requestId);
    m_aShareIndustryImportState.insert(QStringLiteral("running"), true);
    m_aShareIndustryImportState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("正在后台逐行校验行业 CSV；预览不会写库"));
    emit aShareIndustryImportStateChanged();
    return requestId;
}

qulonglong TradingController::commitAShareIndustryImport(
    const QString &classification, const QString &source,
    const QString &sourceVersion, const QString &revisionReason)
{
    if (m_aShareIndustryImportState.value(QStringLiteral("running")).toBool())
        return 0;
    const QString filePath = m_aShareIndustryImportState
        .value(QStringLiteral("filePath")).toString();
    const QString normalizedClassification = classification.trimmed();
    const QString normalizedSource = source.trimmed();
    const QString normalizedVersion = sourceVersion.trimmed();
    if (!m_aShareIndustryImportState.value(QStringLiteral("previewReady")).toBool()
        || m_aShareIndustryPreviewFilePath != filePath
        || m_aShareIndustryPreviewClassification != normalizedClassification
        || m_aShareIndustryPreviewSource != normalizedSource
        || m_aShareIndustryPreviewSourceVersion != normalizedVersion
        || m_aShareIndustryPreviewFileSha256.isEmpty()) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"),
            QStringLiteral("文件、分类体系、来源或版本与最近预览不一致"));
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"), QStringLiteral("确认导入未启动"));
        emit aShareIndustryImportStateChanged();
        return 0;
    }
    if (m_aShareIndustryImportState
            .value(QStringLiteral("requiresRevisionReason")).toBool()
        && revisionReason.trimmed().isEmpty()) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"),
            QStringLiteral("同一来源版本存在不同内容，必须填写修订原因"));
        emit aShareIndustryImportStateChanged();
        return 0;
    }
    AShareIndustryImportRequest request;
    request.filePath = filePath;
    request.classification = normalizedClassification;
    request.source = normalizedSource;
    request.sourceVersion = normalizedVersion;
    request.revisionReason = revisionReason.trimmed();
    request.expectedFileContentSha256 = m_aShareIndustryPreviewFileSha256;
    const quint64 requestId = m_aShareIndustryImportService.start(request);
    if (requestId == 0) return 0;
    m_aShareIndustryImportState.insert(QStringLiteral("requestId"), requestId);
    m_aShareIndustryImportState.insert(QStringLiteral("running"), true);
    m_aShareIndustryImportState.insert(QStringLiteral("error"), QStringLiteral(""));
    m_aShareIndustryImportState.insert(
        QStringLiteral("statusText"),
        QStringLiteral("正在重新校验文件哈希并事务写入不可变行业数据集"));
    emit aShareIndustryImportStateChanged();
    return requestId;
}

void TradingController::cancelAShareIndustryImport()
{
    if (!m_aShareIndustryImportState.value(QStringLiteral("running")).toBool())
        return;
    m_aShareIndustryImportService.cancel();
    m_aShareIndustryImportState.insert(
        QStringLiteral("statusText"), QStringLiteral("正在取消行业导入"));
    emit aShareIndustryImportStateChanged();
}

void TradingController::refreshAShareIndustryImportHistory()
{
    m_aShareIndustryImportHistory.clear();
    if (m_storageReady) {
        const auto datasets = m_dataStore.loadAShareIndustryDatasets(20);
        for (const auto &dataset : datasets)
            m_aShareIndustryImportHistory.append(dataset.toVariantMap());
    }
    emit aShareIndustryImportStateChanged();
}

bool TradingController::prepareTickDataBatchImport(
    const QString &startDate, const QString &endDate, int format,
    int periodSeconds, int fiveMinuteVolumeUnit, bool skipCompleteExisting)
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning) return false;
    m_tickDataBatchError.clear();
    const QDate from = QDate::fromString(startDate.trimmed(), QStringLiteral("yyyy-MM-dd"));
    const QDate to = QDate::fromString(endDate.trimmed(), QStringLiteral("yyyy-MM-dd"));
    const bool formatValid = format == static_cast<int>(TickDataCsvFormat::Tick)
        || format == static_cast<int>(TickDataCsvFormat::FiveMinute);
    const bool volumeUnitValid = fiveMinuteVolumeUnit >= static_cast<int>(FiveMinuteVolumeUnit::Unknown)
        && fiveMinuteVolumeUnit <= static_cast<int>(FiveMinuteVolumeUnit::Lots);
    if (!m_storageReady) m_tickDataBatchError = QStringLiteral("SQLite 本地数据库不可用");
    else if (m_tickDataImportRootDirectory.isEmpty())
        m_tickDataBatchError = QStringLiteral("请先显式选择 TickData 根目录");
    else if (m_tickDataImportInstrument.symbol.isEmpty())
        m_tickDataBatchError = QStringLiteral("请先选择单个 A 股代码");
    else if (!formatValid || !volumeUnitValid)
        m_tickDataBatchError = QStringLiteral("批量导入格式或 volume 单位无效");
    if (!m_tickDataBatchError.isEmpty()) {
        m_tickDataBatchStatusText = QStringLiteral("计划未生成");
        emit tickDataBatchStateChanged();
        return false;
    }

    const QHash<QDate, int> existingBars = m_dataStore.candleCountsByTradingDate(
        m_tickDataImportInstrument, periodSeconds, from, to);
    const TickDataBatchPlanResult plan = TickDataBatchPlanner::create(
        m_tickDataImportRootDirectory, m_tickDataImportInstrument, from, to,
        static_cast<TickDataCsvFormat>(format), periodSeconds,
        static_cast<FiveMinuteVolumeUnit>(fiveMinuteVolumeUnit), existingBars,
        skipCompleteExisting);
    if (!plan.succeeded) {
        m_tickDataBatchPlan.clear();
        m_tickDataBatchPlanReady = false;
        m_tickDataBatchError = plan.error;
        m_tickDataBatchStatusText = QStringLiteral("计划未生成");
        emit tickDataBatchStateChanged();
        return false;
    }

    m_tickDataBatchPlan = plan.items;
    m_tickDataBatchSummary = {};
    m_tickDataBatchSummary.instrument = m_tickDataImportInstrument;
    m_tickDataBatchSummary.startDate = from;
    m_tickDataBatchSummary.endDate = to;
    m_tickDataBatchSummary.format = static_cast<TickDataCsvFormat>(format);
    m_tickDataBatchSummary.periodSeconds = periodSeconds;
    m_tickDataBatchSummary.fiveMinuteVolumeUnit =
        static_cast<FiveMinuteVolumeUnit>(fiveMinuteVolumeUnit);
    m_tickDataBatchSummary.excludedHolidayDays = plan.excludedHolidayDays;
    m_tickDataBatchSummary.calendarFallbackDays = plan.calendarFallbackDays;
    m_tickDataBatchSummary.calendarSource = plan.calendarSource;
    m_tickDataBatchSummary.status = QStringLiteral("planned");
    m_tickDataBatchPlanReady = true;
    m_tickDataBatchCancelRequested = false;
    m_tickDataBatchAnyWrite = false;
    m_tickDataBatchCurrentDate.clear();
    recomputeTickDataBatchSummary();
    m_tickDataBatchStatusText = QStringLiteral(
        "计划已生成：%1 个交易日，排除周末 %2、休市日 %3；日历 %4；尚未解析 CSV")
        .arg(m_tickDataBatchSummary.totalFiles).arg(plan.excludedWeekendDays)
        .arg(plan.excludedHolidayDays).arg(plan.calendarSource);
    emit tickDataBatchStateChanged();
    return true;
}

bool TradingController::startTickDataBatchImport()
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning
        || !m_tickDataBatchPlanReady) return false;
    const bool hasReady = std::any_of(
        m_tickDataBatchPlan.cbegin(), m_tickDataBatchPlan.cend(),
        [](const TickDataBatchPlanItem &item) {
            return item.status == TickDataBatchItemStatus::Ready;
        });
    if (!hasReady) {
        m_tickDataBatchError = QStringLiteral("计划中没有可读取的非空文件");
        m_tickDataBatchStatusText = QStringLiteral("批量导入未启动");
        emit tickDataBatchStateChanged();
        return false;
    }
    const bool restoredRun = m_tickDataBatchSummary.status == QStringLiteral("restored")
        && !m_tickDataBatchSummary.runId.isEmpty();
    if (!restoredRun) {
        m_tickDataBatchSummary.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_tickDataBatchSummary.createdAt = QDateTime::currentDateTimeUtc();
    }
    m_tickDataBatchSummary.completedAt = {};
    m_tickDataBatchSummary.status = QStringLiteral("running");
    m_tickDataBatchSummary.error.clear();
    m_tickDataBatchRunning = true;
    m_tickDataBatchCancelRequested = false;
    m_tickDataBatchAnyWrite = false;
    m_tickDataBatchActiveIndex = -1;
    m_tickDataBatchActiveRequestId = 0;
    m_tickDataBatchError.clear();
    m_tickDataBatchStatusText = restoredRun
        ? QStringLiteral("继续历史任务：成功日期保留，按交易日串行重试")
        : QStringLiteral("批量导入已启动；按交易日串行逐行解析");
    recomputeTickDataBatchSummary();
    persistTickDataBatchSummary();
    emit tickDataBatchStateChanged();
    QTimer::singleShot(0, this, &TradingController::startNextTickDataBatchFile);
    return true;
}

void TradingController::cancelTickDataBatchImport()
{
    if (!m_tickDataBatchRunning) return;
    m_tickDataBatchCancelRequested = true;
    m_tickDataBatchStatusText = QStringLiteral("正在取消当前文件；未开始文件保留为可继续状态");
    if (m_tickDataBatchActiveRequestId > 0) m_tickDataCsvImportService.cancel();
    emit tickDataBatchStateChanged();
}

bool TradingController::resumeTickDataBatchImport()
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning
        || !m_tickDataBatchPlanReady) return false;
    const int readyCount = TickDataBatchPlanner::resetRetryableFiles(&m_tickDataBatchPlan);
    if (readyCount <= 0) {
        m_tickDataBatchError = QStringLiteral("没有可继续的失败、取消或新补齐文件");
        emit tickDataBatchStateChanged();
        return false;
    }
    if (m_tickDataBatchSummary.runId.isEmpty()) {
        return startTickDataBatchImport();
    }
    m_tickDataBatchSummary.completedAt = {};
    m_tickDataBatchSummary.status = QStringLiteral("running");
    m_tickDataBatchSummary.error.clear();
    m_tickDataBatchRunning = true;
    m_tickDataBatchCancelRequested = false;
    m_tickDataBatchActiveIndex = -1;
    m_tickDataBatchActiveRequestId = 0;
    m_tickDataBatchError.clear();
    m_tickDataBatchStatusText = QStringLiteral("继续批量导入：已成功文件不会重复解析");
    recomputeTickDataBatchSummary();
    persistTickDataBatchSummary();
    emit tickDataBatchStateChanged();
    QTimer::singleShot(0, this, &TradingController::startNextTickDataBatchFile);
    return true;
}

bool TradingController::restoreTickDataBatchRetryPlan(
    const QString &runId, bool skipCompleteExisting)
{
    if (m_tickDataImportRunning || m_tickDataBatchRunning || !m_storageReady) return false;
    m_tickDataBatchError.clear();
    if (m_tickDataImportRootDirectory.isEmpty()) {
        m_tickDataBatchError = QStringLiteral("请先重新确认 TickData 根目录；历史摘要不会保存路径");
        emit tickDataBatchStateChanged();
        return false;
    }
    const std::optional<TickDataImportRunSummary> stored =
        m_dataStore.loadTickDataImportRun(runId.trimmed());
    if (!stored.has_value()) {
        m_tickDataBatchError = QStringLiteral("未找到所选批次摘要");
        emit tickDataBatchStateChanged();
        return false;
    }
    const QVector<TickDataBatchPlanItem> storedItems =
        m_dataStore.loadTickDataImportRunItems(stored->runId);
    if (storedItems.isEmpty()) {
        m_tickDataBatchError = QStringLiteral("该任务来自旧版摘要或没有逐日明细，无法恢复失败日期");
        emit tickDataBatchStateChanged();
        return false;
    }
    const Instrument instrument = findInstrument(stored->instrument.symbol);
    if (instrument.symbol.isEmpty() || instrument.market != Market::AShare) {
        m_tickDataBatchError = QStringLiteral("历史任务代码不在当前 A 股股票池中：%1")
                                   .arg(stored->instrument.symbol);
        emit tickDataBatchStateChanged();
        return false;
    }
    const QHash<QDate, int> existingBars = m_dataStore.candleCountsByTradingDate(
        instrument, stored->periodSeconds, stored->startDate, stored->endDate);
    TickDataBatchPlanResult fresh = TickDataBatchPlanner::create(
        m_tickDataImportRootDirectory, instrument, stored->startDate, stored->endDate,
        stored->format, stored->periodSeconds, stored->fiveMinuteVolumeUnit,
        existingBars, skipCompleteExisting);
    if (!fresh.succeeded) {
        m_tickDataBatchError = fresh.error;
        emit tickDataBatchStateChanged();
        return false;
    }
    TickDataBatchPlanner::restoreCompletedItems(&fresh.items, storedItems);
    m_tickDataImportInstrument = instrument;
    m_tickDataBatchPlan = fresh.items;
    m_tickDataBatchSummary = *stored;
    m_tickDataBatchSummary.status = QStringLiteral("restored");
    m_tickDataBatchSummary.excludedHolidayDays = fresh.excludedHolidayDays;
    m_tickDataBatchSummary.calendarFallbackDays = fresh.calendarFallbackDays;
    m_tickDataBatchSummary.calendarSource = fresh.calendarSource;
    m_tickDataBatchPlanReady = true;
    m_tickDataBatchCancelRequested = false;
    m_tickDataBatchAnyWrite = false;
    m_tickDataBatchCurrentDate.clear();
    recomputeTickDataBatchSummary();
    m_tickDataBatchStatusText = QStringLiteral(
        "已恢复任务 %1：成功日期保留，仅失败/取消/缺失日期可继续；请核对覆盖预检")
        .arg(stored->runId.left(8));
    emit tickDataImportStateChanged();
    emit tickDataBatchStateChanged();
    return true;
}

bool TradingController::refreshTickDataImportRunHistory(
    const QString &symbolFilter, const QString &statusFilter)
{
    if (!m_storageReady) return false;
    const QString symbol = symbolFilter.trimmed();
    const QString status = statusFilter.trimmed();
    static const QSet<QString> allowedStatuses{
        QStringLiteral("completed"), QStringLiteral("completed_with_errors"),
        QStringLiteral("cancelled"), QStringLiteral("running"),
        QStringLiteral("restored")};
    static const QRegularExpression symbolPattern(QStringLiteral("^[0-9]{6}$"));
    if ((!symbol.isEmpty() && !symbolPattern.match(symbol).hasMatch())
        || (!status.isEmpty() && !allowedStatuses.contains(status))) {
        m_tickDataBatchError = QStringLiteral("历史筛选条件无效");
        emit tickDataBatchStateChanged();
        return false;
    }
    m_tickDataImportRunSymbolFilter = symbol;
    m_tickDataImportRunStatusFilter = status;
    reloadTickDataImportRunHistory();
    m_tickDataBatchError.clear();
    emit tickDataBatchStateChanged();
    return true;
}

void TradingController::searchStocks(const QString &text) { setStockSearchText(text); }

bool TradingController::openInstrumentResearch(const QString &symbol)
{
    const QString normalized = symbol.trimmed();
    const Instrument instrument = findInstrument(normalized);
    if (instrument.symbol.isEmpty()) {
        setStatusText(QStringLiteral("未找到标的：%1").arg(normalized));
        return false;
    }
    selectInstrument(instrument.symbol);
    emit instrumentResearchRequested(instrument.symbol);
    return true;
}

void TradingController::addInstrument(const QString &code)
{
    if (code.isEmpty()) return;
    for (const Instrument &instrument : buildWatchlist()) {
        if (instrument.symbol == code) {
            selectInstrument(code);
            setStatusText(QStringLiteral("标的已在列表中：%1").arg(code));
            return;
        }
    }

    QString name;
    QString market;
    for (int index = 0; index < m_searchModel.rowCount(); ++index) {
        const QVariantMap info = m_searchModel.get(index);
        if (info.value(QStringLiteral("code")).toString() == code) {
            name = info.value(QStringLiteral("name")).toString();
            market = info.value(QStringLiteral("market")).toString();
            break;
        }
    }

    Instrument instrument;
    instrument.symbol = code;
    instrument.name = name.isEmpty() ? code : name;
    instrument.market = Market::AShare;
    instrument.venue = market.isEmpty() ? QStringLiteral("沪深") : market;
    m_instrumentModel.addInstrument(instrument);

    auto *strategy = new TickMomentumStrategy(instrument, 0.0025, 20);
    const QString strategyId = strategy->id();
    m_strategyEngine.addStrategy(strategy);
    m_strategyModel.addStrategy({strategyId, instrument.symbol, strategy->displayName(),
        QStringLiteral("阈值 %1%，冷却 %2 秒").arg(strategy->thresholdPercent(), 0, 'f', 2)
            .arg(strategy->cooldownSeconds()),
        strategy->thresholdPercent(), strategy->cooldownSeconds(), true});
    emit strategyStateChanged();
    setStatusText(QStringLiteral("已添加 %1 %2").arg(code, instrument.name));
    restartLiveIfRunning();
    selectInstrument(code);
}

void TradingController::fetchHistory(const QString &frequency, int count)
{
    const Instrument &researchInstrument = m_workspaceInstrumentSelections.researchInstrument();
    if (researchInstrument.symbol.isEmpty()) {
        setStatusText(QStringLiteral("请先选择标的"));
        return;
    }
    if (m_backtestPreparing) {
        m_backtestPreparing = false;
        handleBacktestCancelled();
    }
    m_historyCandleModel.clear();
    setStatusText(QStringLiteral("正在获取 %1 %2 K线").arg(researchInstrument.symbol, frequency));
    m_activeHistoryRequestId = m_historicalDataService.request(
        researchInstrument, frequency, count);
}

void TradingController::loginBroker()
{
    m_tradingError.clear();
    emit tradingStateChanged();
    m_broker.login();
}

void TradingController::logoutBroker()
{
    m_broker.logout();
}

QString TradingController::placeOrder(const QString &symbol, const QString &side,
                                      const QString &orderType, double quantity, double limitPrice)
{
    if (m_paperStrategyBook.hasRun() || m_paperReplayBook.hasRun()) {
        handleTradingError(QStringLiteral("账户已绑定策略，不能混入手动委托"));
        return {};
    }
    const Instrument instrument = findInstrument(symbol.isEmpty()
        ? m_workspaceInstrumentSelections.researchInstrument().symbol : symbol);
    if (instrument.symbol.isEmpty()) {
        handleTradingError(QStringLiteral("未找到交易标的：%1").arg(symbol));
        return {};
    }

    const QString normalizedSide = side.trimmed().toLower();
    const QString normalizedType = orderType.trimmed().toLower();
    OrderRequest request;
    request.clientOrderId = QString::number(QDateTime::currentMSecsSinceEpoch());
    request.instrument = instrument;
    request.quantity = quantity;
    request.createdAt = QDateTime::currentDateTimeUtc();
    if (normalizedSide == QStringLiteral("buy") || normalizedSide == QStringLiteral("买入")) {
        request.side = OrderSide::Buy;
    } else if (normalizedSide == QStringLiteral("sell") || normalizedSide == QStringLiteral("卖出")) {
        request.side = OrderSide::Sell;
    } else {
        handleTradingError(QStringLiteral("不支持的买卖方向：%1").arg(side));
        return {};
    }

    if (normalizedType == QStringLiteral("market") || normalizedType == QStringLiteral("市价")) {
        request.type = OrderType::Market;
    } else if (normalizedType == QStringLiteral("limit") || normalizedType == QStringLiteral("限价")) {
        request.type = OrderType::Limit;
        request.limitPrice = limitPrice;
    } else {
        handleTradingError(QStringLiteral("不支持的订单类型：%1").arg(orderType));
        return {};
    }

    const Tick latestTick = m_latestTicks.value(instrument.symbol);
    request.referencePrice = request.side == OrderSide::Buy
        ? (latestTick.ask > 0.0 ? latestTick.ask : latestTick.price)
        : (latestTick.bid > 0.0 ? latestTick.bid : latestTick.price);
    if (request.type == OrderType::Limit && request.referencePrice <= 0.0) {
        request.referencePrice = request.limitPrice;
    }

    const QString orderId = m_tradingAccount.submitOrder(request);
    if (!orderId.isEmpty()) {
        m_tradingError.clear();
        emit tradingStateChanged();
        setStatusText(QStringLiteral("订单已提交：%1").arg(orderId));
    }
    return orderId;
}

void TradingController::cancelOrder(const QString &orderId)
{
    m_tradingAccount.cancelOrder(orderId);
}

void TradingController::resetPaperAccount(double initialCapital)
{
    if (m_paperStrategyBook.hasRun() || m_paperReplayBook.hasRun()) {
        handleTradingError(QStringLiteral("已有策略运行记录，禁止重置资金和持仓"));
        return;
    }
    if (initialCapital <= 0.0) {
        handleTradingError(QStringLiteral("初始资金必须大于 0"));
        return;
    }
    m_tradingAccount.reset(initialCapital);
    m_tradingError.clear();
    emit tradingStateChanged();
    setStatusText(QStringLiteral("模拟账户已重置"));
}

void TradingController::configureRiskLimits(double maxOrderValue, double maxPositionValue,
                                            double maxDailyLoss, int maxActiveOrders)
{
    if (maxOrderValue < 0.0 || maxPositionValue < 0.0
        || maxDailyLoss < 0.0 || maxActiveOrders < 0) {
        handleTradingError(QStringLiteral("风控限制不能为负数"));
        return;
    }
    RiskLimits limits;
    limits.maxOrderValue = maxOrderValue;
    limits.maxPositionValue = maxPositionValue;
    limits.maxDailyLoss = maxDailyLoss;
    limits.maxActiveOrders = maxActiveOrders;
    m_tradingAccount.setRiskLimits(limits);
    m_tradingError.clear();
    emit tradingStateChanged();
    emit riskLimitsChanged();
    setStatusText(QStringLiteral("交易前风控限制已更新"));
}

bool TradingController::approveStrategyIntent(const QString &intentId)
{
    if (m_paperStrategyBook.hasRun() || m_paperReplayBook.hasRun()) return false;
    if (intentId.trimmed().isEmpty()) return false;
    return m_strategyExecutionCoordinator.approveIntent(intentId);
}

bool TradingController::rejectStrategyIntent(const QString &intentId, const QString &reason)
{
    if (intentId.trimmed().isEmpty()) return false;
    return m_strategyExecutionCoordinator.rejectIntent(intentId, reason.trimmed());
}

bool TradingController::configureStrategyExecution(const QString &strategyId, int executionMode,
                                                    int timeInForce, double fixedOrderValue,
                                                    double maxPositionPercent,
                                                    int approvalTimeoutSeconds, int orderTimeoutSeconds,
                                                    int maxOrdersPerDay, double maxDailyOrderValue)
{
    const auto existing = m_strategyExecutionCoordinator.configuration(strategyId);
    if (!existing.has_value()) {
        setStatusText(QStringLiteral("未找到策略执行配置：%1").arg(strategyId));
        return false;
    }
    if (executionMode < static_cast<int>(StrategyExecutionMode::Disabled)
        || executionMode > static_cast<int>(StrategyExecutionMode::Automatic)
        || timeInForce < static_cast<int>(TimeInForce::Day)
        || timeInForce > static_cast<int>(TimeInForce::FillOrKill)
        || !std::isfinite(fixedOrderValue) || fixedOrderValue < 0.0
        || !std::isfinite(maxPositionPercent) || maxPositionPercent < 0.0
        || approvalTimeoutSeconds < 0 || orderTimeoutSeconds < 0
        || maxOrdersPerDay < 0 || !std::isfinite(maxDailyOrderValue)
        || maxDailyOrderValue < 0.0) {
        setStatusText(QStringLiteral("策略执行参数无效"));
        return false;
    }

    StrategyExecutionConfig config = *existing;
    config.mode = static_cast<StrategyExecutionMode>(executionMode);
    config.enabled = config.mode != StrategyExecutionMode::Disabled;
    config.timeInForce = static_cast<TimeInForce>(timeInForce);
    config.fixedOrderValue = fixedOrderValue;
    config.maxPositionPercent = maxPositionPercent;
    config.approvalTimeoutSeconds = approvalTimeoutSeconds;
    config.orderTimeoutSeconds = orderTimeoutSeconds;
    config.maxOrdersPerDay = maxOrdersPerDay;
    config.maxDailyOrderValue = maxDailyOrderValue;
    if (!m_strategyExecutionCoordinator.setConfiguration(config)) {
        setStatusText(QStringLiteral("策略执行配置持久化失败：%1").arg(strategyId));
        return false;
    }
    emit strategyExecutionConfigChanged(strategyId);
    emit strategyExecutionStateChanged();
    setStatusText(QStringLiteral("策略执行配置已更新：%1").arg(strategyId));
    return true;
}

bool TradingController::configureStrategyOrderPolicy(const QString &strategyId, int orderType,
                                                       bool sellAll, double limitOffsetBps,
                                                       int lotSize)
{
    const auto existing = m_strategyExecutionCoordinator.configuration(strategyId);
    if (!existing.has_value()) {
        setStatusText(QStringLiteral("未找到策略执行配置：%1").arg(strategyId));
        return false;
    }
    if (orderType < static_cast<int>(OrderType::Market)
        || orderType > static_cast<int>(OrderType::Limit)
        || !std::isfinite(limitOffsetBps) || limitOffsetBps < 0.0 || lotSize < 0) {
        setStatusText(QStringLiteral("策略订单参数无效"));
        return false;
    }

    StrategyExecutionConfig config = *existing;
    config.orderType = static_cast<OrderType>(orderType);
    config.sellAll = sellAll;
    config.limitOffsetBps = limitOffsetBps;
    config.lotSize = lotSize;
    if (!m_strategyExecutionCoordinator.setConfiguration(config)) {
        setStatusText(QStringLiteral("策略订单配置持久化失败：%1").arg(strategyId));
        return false;
    }
    emit strategyExecutionConfigChanged(strategyId);
    emit strategyExecutionStateChanged();
    setStatusText(QStringLiteral("策略订单配置已更新：%1").arg(strategyId));
    return true;
}

QVariantMap TradingController::strategyExecutionConfiguration(const QString &strategyId) const
{
    const auto configuration = m_strategyExecutionCoordinator.configuration(strategyId);
    return configuration.has_value() ? strategyExecutionConfigMap(*configuration) : QVariantMap{};
}

QVariantList TradingController::strategyExecutionConfigurations() const
{
    QVariantList result;
    const QVector<StrategyExecutionConfig> configurations =
        m_strategyExecutionCoordinator.configurations();
    result.reserve(configurations.size());
    for (const StrategyExecutionConfig &config : configurations) {
        result.append(strategyExecutionConfigMap(config));
    }
    return result;
}

QVariantMap TradingController::notificationSettings() const
{
    const NotificationSettings settings = m_notificationDispatcher.settings();
    QVariantMap map;
    map.insert(QStringLiteral("desktopEnabled"), settings.desktopEnabled);
    map.insert(QStringLiteral("webhookEnabled"), settings.webhookEnabled);
    map.insert(QStringLiteral("webhookUrl"), settings.webhookUrl);
    map.insert(QStringLiteral("notifySignal"), settings.notifySignal);
    map.insert(QStringLiteral("notifyApproval"), settings.notifyApproval);
    map.insert(QStringLiteral("notifyOrderSubmitted"), settings.notifyOrderSubmitted);
    map.insert(QStringLiteral("notifyExecutionRejected"), settings.notifyExecutionRejected);
    map.insert(QStringLiteral("notifyIntentExpired"), settings.notifyIntentExpired);
    return map;
}

bool TradingController::configureNotifications(bool desktopEnabled, bool webhookEnabled,
                                               const QString &webhookUrl, bool notifySignal,
                                               bool notifyApproval, bool notifyOrderSubmitted,
                                               bool notifyExecutionRejected, bool notifyIntentExpired)
{
    NotificationSettings settings;
    settings.desktopEnabled = desktopEnabled;
    settings.webhookEnabled = webhookEnabled;
    settings.webhookUrl = webhookUrl.trimmed();
    settings.notifySignal = notifySignal;
    settings.notifyApproval = notifyApproval;
    settings.notifyOrderSubmitted = notifyOrderSubmitted;
    settings.notifyExecutionRejected = notifyExecutionRejected;
    settings.notifyIntentExpired = notifyIntentExpired;

    m_notificationDispatcher.setSettings(settings);
    m_webhookNotifications.setWebhookUrl(settings.webhookUrl);
    persistNotificationSettings();
    emit notificationSettingsChanged();
    setStatusText(QStringLiteral("通知设置已更新（桌面 %1，Webhook %2）")
        .arg(settings.desktopEnabled ? QStringLiteral("开") : QStringLiteral("关"),
             settings.webhookEnabled ? QStringLiteral("开") : QStringLiteral("关")));
    return true;
}

void TradingController::restoreNotificationSettings()
{
    NotificationSettings settings;
    QSettings store;
    settings.desktopEnabled = store.value(QStringLiteral("notifications/desktopEnabled"), false).toBool();
    settings.webhookEnabled = store.value(QStringLiteral("notifications/webhookEnabled"), false).toBool();
    settings.webhookUrl = store.value(QStringLiteral("notifications/webhookUrl"), QString{}).toString();
    settings.notifySignal = store.value(QStringLiteral("notifications/notifySignal"), true).toBool();
    settings.notifyApproval = store.value(QStringLiteral("notifications/notifyApproval"), true).toBool();
    settings.notifyOrderSubmitted = store.value(QStringLiteral("notifications/notifyOrderSubmitted"), true).toBool();
    settings.notifyExecutionRejected = store.value(QStringLiteral("notifications/notifyExecutionRejected"), true).toBool();
    settings.notifyIntentExpired = store.value(QStringLiteral("notifications/notifyIntentExpired"), true).toBool();
    m_notificationDispatcher.setSettings(settings);
    m_webhookNotifications.setWebhookUrl(settings.webhookUrl);
}

void TradingController::persistNotificationSettings() const
{
    const NotificationSettings settings = m_notificationDispatcher.settings();
    QSettings store;
    store.setValue(QStringLiteral("notifications/desktopEnabled"), settings.desktopEnabled);
    store.setValue(QStringLiteral("notifications/webhookEnabled"), settings.webhookEnabled);
    store.setValue(QStringLiteral("notifications/webhookUrl"), settings.webhookUrl);
    store.setValue(QStringLiteral("notifications/notifySignal"), settings.notifySignal);
    store.setValue(QStringLiteral("notifications/notifyApproval"), settings.notifyApproval);
    store.setValue(QStringLiteral("notifications/notifyOrderSubmitted"), settings.notifyOrderSubmitted);
    store.setValue(QStringLiteral("notifications/notifyExecutionRejected"), settings.notifyExecutionRejected);
    store.setValue(QStringLiteral("notifications/notifyIntentExpired"), settings.notifyIntentExpired);
    store.sync();
}

bool TradingController::enableCustomStrategyExecution(const QString &strategyId, int version,
                                                      const QString &symbol)
{
    if (m_customStrategyRuntimeManager.state(strategyId) == CustomStrategyRuntimeState::Active) {
        return true;
    }

    const CustomStrategyDefinition *definition = nullptr;
    for (const CustomStrategyDefinition &candidate : m_customStrategyDefinitions) {
        if (candidate.strategyId == strategyId && candidate.version == version) {
            definition = &candidate;
            break;
        }
    }
    if (!definition) {
        setStatusText(QStringLiteral("未找到自定义策略：%1 v%2").arg(strategyId).arg(version));
        return false;
    }
    if (definition->executionScope != QStringLiteral("mock_execution")) {
        setStatusText(QStringLiteral("策略 %1 未声明 mock_execution，无法进入模拟执行").arg(strategyId));
        return false;
    }

    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        instrument.symbol = symbol.trimmed();
        instrument.name = symbol.trimmed();
        instrument.market = Market::AShare;
    }

    QString error;
    if (!m_customStrategyRuntimeManager.load(*definition, instrument, &error)) {
        setStatusText(QStringLiteral("策略加载失败：%1").arg(error));
        return false;
    }
    if (!m_customStrategyRuntimeManager.activate(strategyId)) {
        setStatusText(QStringLiteral("策略激活失败：%1").arg(strategyId));
        return false;
    }

    const CustomStrategyPlugin *plugin = m_customStrategyRuntimeManager.plugin(strategyId);
    ensureStrategyExecutionConfiguration(strategyId, 0);
    m_strategyModel.addStrategy({strategyId, instrument.symbol,
        plugin ? plugin->plan.name : strategyId,
        QStringLiteral("自定义策略模拟执行（%1 v%2）")
            .arg(plugin ? plugin->plan.name : strategyId)
            .arg(plugin ? plugin->plan.version : version),
        0.0, 0, true});
    emit strategyStateChanged();
    setStatusText(QStringLiteral("自定义策略已进入模拟执行：%1 · %2")
        .arg(strategyId, instrument.symbol));
    return true;
}

bool TradingController::disableCustomStrategyExecution(const QString &strategyId)
{
    const CustomStrategyRuntimeState previousState =
        m_customStrategyRuntimeManager.state(strategyId);
    if (previousState == CustomStrategyRuntimeState::Unloaded) {
        setStatusText(QStringLiteral("自定义策略未加载：%1").arg(strategyId));
        return false;
    }
    m_customStrategyRuntimeManager.unload(strategyId);
    m_strategyModel.removeStrategy(strategyId);
    m_strategyExecutionCoordinator.removeConfiguration(strategyId);
    emit strategyStateChanged();
    setStatusText(QStringLiteral("自定义策略已卸载：%1").arg(strategyId));
    return true;
}

bool TradingController::isCustomStrategyExecuting(const QString &strategyId) const
{
    return m_customStrategyRuntimeManager.state(strategyId) == CustomStrategyRuntimeState::Active;
}

QVariantList TradingController::customStrategyExecutionCatalog() const
{
    QHash<QString, const CustomStrategyDefinition *> latestDefinitions;
    for (const CustomStrategyDefinition &definition : m_customStrategyDefinitions) {
        const auto iterator = latestDefinitions.constFind(definition.strategyId);
        if (iterator == latestDefinitions.constEnd()
            || (*iterator)->version < definition.version) {
            latestDefinitions.insert(definition.strategyId, &definition);
        }
    }

    QStringList strategyIds = latestDefinitions.keys();
    strategyIds.sort(Qt::CaseInsensitive);
    QVariantList result;
    result.reserve(strategyIds.size());
    for (const QString &strategyId : strategyIds) {
        const CustomStrategyDefinition &definition = *latestDefinitions.value(strategyId);
        const CustomStrategyPlugin *runtime = m_customStrategyRuntimeManager.plugin(strategyId);
        const CustomStrategyRuntimeState state = runtime
            ? runtime->state : CustomStrategyRuntimeState::Unloaded;
        const bool runtimeEligible =
            definition.executionScope == QStringLiteral("mock_execution");

        QVariantMap map = definition.toVariantMap();
        map.insert(QStringLiteral("latest"), true);
        map.insert(QStringLiteral("state"), static_cast<int>(state));
        map.insert(QStringLiteral("stateName"), customStrategyRuntimeStateName(state));
        map.insert(QStringLiteral("symbol"), runtime ? runtime->instrument.symbol : QString{});
        map.insert(QStringLiteral("runtimeEligible"), runtimeEligible);
        map.insert(QStringLiteral("unavailableReason"), runtimeEligible
            ? QString{} : QStringLiteral("策略执行范围为仅历史回测"));
        map.insert(QStringLiteral("error"), runtime ? runtime->error : QString{});
        result.append(map);
    }
    return result;
}

QVariantList TradingController::customStrategyRuntimePlugins() const
{
    QVariantList result;
    const QVector<CustomStrategyPlugin> plugins = m_customStrategyRuntimeManager.plugins();
    result.reserve(plugins.size());
    for (const CustomStrategyPlugin &plugin : plugins) {
        result.append(plugin.toVariantMap());
    }
    return result;
}

QVariantList TradingController::strategyPluginScan()
{
    QVariantList result;
    const auto scanResults = m_strategyPluginManager.scan();
    result.reserve(scanResults.size());
    for (const auto &entry : scanResults) {
        QVariantMap map;
        map.insert(QStringLiteral("fileName"), entry.fileName);
        map.insert(QStringLiteral("sha256"), entry.sha256);
        map.insert(QStringLiteral("manifestOk"), entry.manifestOk);
        map.insert(QStringLiteral("error"), entry.error);
        result.append(map);
    }
    return result;
}

bool TradingController::loadStrategyPlugin(const QString &fileName)
{
    QString error;
    StrategyPlugin *plugin = m_strategyPluginManager.load(fileName, &error);
    if (!plugin) {
        setStatusText(QStringLiteral("插件加载失败：%1").arg(error));
        return false;
    }
    setStatusText(QStringLiteral("插件已加载：%1（%2 v%3）")
        .arg(plugin->pluginId(), plugin->pluginName()).arg(plugin->pluginVersion()));
    emit strategyPluginStateChanged();
    return true;
}

bool TradingController::unloadStrategyPlugin(const QString &pluginId)
{
    // 先停用该插件创建的所有策略（其 vtable 位于插件 DLL，卸载后调用即悬空崩溃）。
    const QStringList strategyIds = m_pluginStrategyIds.value(pluginId);
    for (const QString &strategyId : strategyIds) {
        m_strategyEngine.removeStrategy(strategyId);
        m_strategyModel.removeStrategy(strategyId);
        m_strategyExecutionCoordinator.removeConfiguration(strategyId);
    }
    m_pluginStrategyIds.remove(pluginId);
    if (!strategyIds.isEmpty()) emit strategyStateChanged();

    QString error;
    if (!m_strategyPluginManager.unload(pluginId, &error)) {
        setStatusText(QStringLiteral("插件卸载失败：%1").arg(error));
        return false;
    }
    setStatusText(QStringLiteral("插件已卸载：%1").arg(pluginId));
    emit strategyPluginStateChanged();
    return true;
}

QVariantList TradingController::loadedStrategyPlugins() const
{
    QVariantList result;
    const QVector<StrategyPluginRecord> records = m_strategyPluginManager.loadedPlugins();
    result.reserve(records.size());
    for (const StrategyPluginRecord &record : records) {
        QVariantMap map;
        map.insert(QStringLiteral("pluginId"), record.pluginId);
        map.insert(QStringLiteral("pluginName"), record.pluginName);
        map.insert(QStringLiteral("pluginVersion"), record.pluginVersion);
        map.insert(QStringLiteral("pluginDescription"), record.pluginDescription);
        map.insert(QStringLiteral("apiVersion"), record.apiVersion);
        map.insert(QStringLiteral("fileName"), record.fileName);
        result.append(map);
    }
    return result;
}

QVariantList TradingController::strategyPluginParameterSchema(const QString &pluginId) const
{
    QVariantList result;
    StrategyPlugin *plugin = m_strategyPluginManager.plugin(pluginId);
    if (!plugin) return result;
    const QVector<PluginParameterSpec> specs = plugin->parameterSchema();
    result.reserve(specs.size());
    for (const PluginParameterSpec &spec : specs) {
        result.append(spec.toVariantMap());
    }
    return result;
}

bool TradingController::activateStrategyPluginStrategy(const QString &pluginId,
                                                       const QVariantMap &config)
{
    StrategyPlugin *plugin = m_strategyPluginManager.plugin(pluginId);
    if (!plugin) {
        setStatusText(QStringLiteral("插件未加载：%1").arg(pluginId));
        return false;
    }
    const QString strategyId = config.value(QStringLiteral("strategyId")).toString();
    if (strategyId.isEmpty()) {
        setStatusText(QStringLiteral("缺少 strategyId 配置"));
        return false;
    }
    if (m_strategyEngine.strategyById(strategyId) != nullptr) {
        setStatusText(QStringLiteral("策略 ID 已存在：%1").arg(strategyId));
        return false;
    }
    Strategy *strategy = plugin->createStrategy(config);
    if (!strategy) {
        setStatusText(QStringLiteral("插件创建策略失败：%1").arg(pluginId));
        return false;
    }
    m_strategyEngine.addStrategy(strategy);
    m_pluginStrategyIds[pluginId].append(strategyId);
    ensureStrategyExecutionConfiguration(strategyId, 0);
    m_strategyModel.addStrategy({strategyId, strategy->subscribedSymbols().value(0),
        strategy->displayName(), QStringLiteral("第三方插件策略 %1").arg(pluginId),
        0.0, 0, true});
    emit strategyStateChanged();
    emit strategyPluginStateChanged();
    setStatusText(QStringLiteral("插件策略已激活：%1").arg(strategyId));
    return true;
}

void TradingController::setStrategyPluginDirectory(const QString &path)
{
    m_strategyPluginManager.setPluginDirectory(path);
    emit strategyPluginStateChanged();
    setStatusText(QStringLiteral("策略插件目录已设置：%1").arg(path));
}

QString TradingController::strategyPluginDirectory() const
{
    return m_strategyPluginManager.pluginDirectory();
}

bool TradingController::parameterGridRunning() const
{
    return m_parameterGridBacktestService.isBusy();
}

QString TradingController::parameterGridError() const
{
    return m_parameterGridError;
}

QVariantList TradingController::parameterGridResults() const
{
    QVariantList result;
    result.reserve(m_parameterGridResults.size());
    for (const ParameterGridResult &row : m_parameterGridResults) {
        result.append(row.toVariantMap());
    }
    return result;
}

qulonglong TradingController::runParameterGridBacktest(const QString &strategyId, int version,
                                                       const QString &symbol, int periodSeconds,
                                                       int barCount, const QVariantList &axes)
{
    if (m_parameterGridBacktestService.isBusy()) {
        setStatusText(QStringLiteral("参数网格扫描正在运行"));
        return 0;
    }

    const CustomStrategyDefinition *definition = nullptr;
    for (const CustomStrategyDefinition &candidate : m_customStrategyDefinitions) {
        if (candidate.strategyId == strategyId && candidate.version == version) {
            definition = &candidate;
            break;
        }
    }
    if (!definition) {
        m_parameterGridError = QStringLiteral("未找到自定义策略：%1 v%2").arg(strategyId).arg(version);
        emit parameterGridStateChanged();
        setStatusText(m_parameterGridError);
        return 0;
    }

    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        instrument.symbol = symbol.trimmed();
        instrument.name = symbol.trimmed();
        instrument.market = Market::AShare;
    }
    if (periodSeconds <= 0) periodSeconds = 60;
    if (barCount <= 0) barCount = 300;
    if (barCount > 2000) barCount = 2000;

    const QVector<Candle> candles = m_dataStore.loadRecentCandlesFromSource(
        instrument, periodSeconds, barCount, QStringLiteral("raw_tick_cache_v1"));
    if (candles.isEmpty()) {
        m_parameterGridError = QStringLiteral("没有可用于扫描的 raw_tick_cache_v1 本地 K 线：%1").arg(symbol);
        emit parameterGridStateChanged();
        setStatusText(m_parameterGridError);
        return 0;
    }

    QVector<ParameterGridAxis> gridAxes;
    for (const QVariant &entry : axes) {
        const QVariantMap map = entry.toMap();
        ParameterGridAxis axis;
        axis.key = map.value(QStringLiteral("key")).toString();
        const QVariantList values = map.value(QStringLiteral("values")).toList();
        for (const QVariant &value : values) {
            axis.values.append(value.toDouble());
        }
        if (!axis.key.isEmpty() && !axis.values.isEmpty()) {
            gridAxes.append(axis);
        }
    }

    BacktestConfig config;
    config.instrument = instrument;
    config.periodSeconds = periodSeconds;
    config.initialCapital = m_backtestConfiguredInitialCapital;
    config.commissionRate = std::max(0.0, m_backtestCommissionRate);
    config.slippageRate = std::max(0.0, m_backtestSlippageRate);
    config.lotSize = 100;

    m_parameterGridError.clear();
    m_parameterGridResults.clear();
    quint64 requestId = ++m_parameterGridRequestId;
    if (requestId == 0) requestId = ++m_parameterGridRequestId;
    emit parameterGridStateChanged();
    if (!m_parameterGridBacktestService.request(requestId, *definition, gridAxes, candles, config)) {
        m_parameterGridError = QStringLiteral("参数网格扫描服务不可用");
        emit parameterGridStateChanged();
        setStatusText(m_parameterGridError);
        return 0;
    }
    setStatusText(QStringLiteral("参数网格扫描开始：%1 · %2 根 · 组合 %3")
        .arg(symbol).arg(candles.size())
        .arg(ParameterGridScanner::expandGrid(gridAxes).size()));
    return requestId;
}

void TradingController::cancelParameterGridBacktest()
{
    m_parameterGridBacktestService.cancel();
    setStatusText(QStringLiteral("参数网格扫描已请求取消"));
}

void TradingController::handleParameterGridCompleted(quint64 requestId,
                                                     const QVector<ParameterGridResult> &results)
{
    if (requestId != m_parameterGridRequestId) return;
    m_parameterGridResults = results;
    m_parameterGridError.clear();
    emit parameterGridStateChanged();
    setStatusText(QStringLiteral("参数网格扫描完成：%1 组结果").arg(results.size()));
}

void TradingController::handleParameterGridFailed(quint64 requestId, const QString &error)
{
    if (requestId != m_parameterGridRequestId) return;
    m_parameterGridError = error;
    m_parameterGridResults.clear();
    emit parameterGridStateChanged();
    setStatusText(QStringLiteral("参数网格扫描失败：%1").arg(error));
}

bool TradingController::rawTickSensitivityRunning() const
{
    return m_rawTickSensitivityService.isBusy();
}

qulonglong TradingController::rawTickSensitivityRequestId() const
{
    return m_rawTickSensitivityRequestId;
}

QString TradingController::rawTickSensitivityError() const
{
    return m_rawTickSensitivityError;
}

QVariantMap TradingController::rawTickSensitivityResult() const
{
    return m_rawTickSensitivityResult
        ? m_rawTickSensitivityResult->toVariantMap() : QVariantMap{};
}

QVariantList TradingController::rawTickSensitivityHistory() const
{
    QVariantList result;
    result.reserve(m_rawTickSensitivityHistory.size());
    for (const RawTickParameterSensitivityResult &record :
         m_rawTickSensitivityHistory) {
        result.append(record.toVariantMap());
    }
    return result;
}

qulonglong TradingController::runRawTickParameterSensitivity(
    const QString &strategyId, const QVariantMap &strategyParameters,
    const QString &symbol, const QString &startDateText,
    const QString &endDateText, int validationTradingDays,
    const QVariantList &axes)
{
    return runRawTickParameterSensitivity(
        strategyId, strategyParameters, symbol, startDateText, endDateText,
        validationTradingDays, 1, axes);
}

qulonglong TradingController::runRawTickParameterSensitivity(
    const QString &strategyId, const QVariantMap &strategyParameters,
    const QString &symbol, const QString &startDateText,
    const QString &endDateText, int validationTradingDays,
    int walkForwardFoldCount, const QVariantList &axes)
{
    if (m_rawTickSensitivityService.isBusy()) {
        setStatusText(QStringLiteral("原始 Tick 参数敏感性正在运行"));
        return 0;
    }
    const QDate startDate = QDate::fromString(startDateText.trimmed(), Qt::ISODate);
    const QDate endDate = QDate::fromString(endDateText.trimmed(), Qt::ISODate);
    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        for (const Instrument &candidate : m_dataStore.rawBacktestInstruments()) {
            if (candidate.symbol == symbol.trimmed()) {
                instrument = candidate;
                break;
            }
        }
    }

    QVariantMap normalizedBaseParameters;
    QString error;
    if (!startDate.isValid() || !endDate.isValid() || startDate > endDate) {
        error = QStringLiteral("请输入有效日期范围（YYYY-MM-DD）");
    } else if (instrument.symbol.isEmpty()
               || instrument.market != Market::AShare) {
        error = QStringLiteral("所选品种不是本地可用的 A 股原始 Tick 数据集");
    } else if (validationTradingDays < 2 || validationTradingDays > 20) {
        error = QStringLiteral("每折验证窗口必须为 2–20 个交易日");
    } else if (walkForwardFoldCount < 1 || walkForwardFoldCount > 20) {
        error = QStringLiteral("walk-forward 折数必须为 1–20");
    } else if (!m_backtestStrategyRegistry.supportsDataMode(
                   strategyId, QStringLiteral("raw_ticks"))) {
        error = QStringLiteral("所选策略不支持原始 Tick 回测");
    } else if (!m_backtestStrategyRegistry.normalizeParameters(
                   strategyId, strategyParameters,
                   &normalizedBaseParameters, &error)) {
        if (error.isEmpty()) error = QStringLiteral("基础策略参数无效");
    }

    QVector<ParameterGridAxis> gridAxes;
    qint64 combinationCount = 1;
    if (error.isEmpty()) {
        for (const QVariant &entry : axes) {
            const QVariantMap map = entry.toMap();
            ParameterGridAxis axis;
            axis.key = map.value(QStringLiteral("key")).toString().trimmed();
            const QVariantList values = map.value(QStringLiteral("values")).toList();
            for (const QVariant &value : values) {
                bool ok = false;
                const double number = value.toDouble(&ok);
                if (ok && std::isfinite(number)) axis.values.append(number);
            }
            if (!axis.key.isEmpty() && !axis.values.isEmpty()) {
                gridAxes.append(axis);
                combinationCount *= axis.values.size();
            }
        }
        if (gridAxes.isEmpty() || combinationCount < 2) {
            error = QStringLiteral("参数敏感性至少需要一个含 2 个候选值的参数轴");
        } else if (combinationCount > 64) {
            error = QStringLiteral("参数组合过多：%1（上限 64）")
                        .arg(combinationCount);
        }
    }

    if (!error.isEmpty()) {
        m_rawTickSensitivityError = error;
        emit rawTickSensitivityStateChanged();
        setStatusText(QStringLiteral("原始 Tick 参数敏感性无法开始：%1").arg(error));
        return 0;
    }

    BacktestConfig config;
    config.instrument = instrument;
    config.strategyId = strategyId;
    config.strategyParameters = normalizedBaseParameters;
    config.dataMode = BacktestDataMode::RawTicks;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.initialCapital = m_backtestConfiguredInitialCapital;
    config.commissionRate = std::max(0.0, m_backtestCommissionRate);
    config.slippageRate = std::max(0.0, m_backtestSlippageRate);
    config.lotSize = 100;

    RawTickParameterSensitivityRequest request;
    request.strategyId = strategyId;
    request.baseParameters = normalizedBaseParameters;
    request.axes = gridAxes;
    request.baseConfig = config;
    request.validationTradingDays = validationTradingDays;
    request.walkForwardFoldCount = walkForwardFoldCount;
    request.maximumCombinations = 64;

    quint64 requestId = ++m_rawTickSensitivityRequestId;
    if (requestId == 0) requestId = ++m_rawTickSensitivityRequestId;
    m_rawTickSensitivityError.clear();
    m_rawTickSensitivityResult.reset();
    emit rawTickSensitivityStateChanged();
    if (!m_rawTickSensitivityService.request(requestId, request,
                                              startDate, endDate)) {
        m_rawTickSensitivityError = QStringLiteral(
            "原始 Tick 参数敏感性异步服务不可用");
        emit rawTickSensitivityStateChanged();
        setStatusText(m_rawTickSensitivityError);
        return 0;
    }
    setStatusText(walkForwardFoldCount > 1
        ? QStringLiteral(
            "原始 Tick 嵌套 walk-forward 开始：%1 · %2 组 · %3 折×%4 日")
              .arg(instrument.symbol)
              .arg(combinationCount)
              .arg(walkForwardFoldCount)
              .arg(validationTradingDays)
        : QStringLiteral(
            "原始 Tick 参数敏感性开始：%1 · %2 组 · 最终留出 %3 日")
              .arg(instrument.symbol)
              .arg(combinationCount)
              .arg(validationTradingDays));
    return requestId;
}

void TradingController::cancelRawTickParameterSensitivity()
{
    m_rawTickSensitivityService.cancel();
    setStatusText(QStringLiteral("原始 Tick 参数敏感性已请求取消"));
}

bool TradingController::loadRawTickParameterSensitivityResult(
    const QString &resultId)
{
    const QString normalizedId = resultId.trimmed();
    if (normalizedId.isEmpty()) return false;
    if (m_rawTickSensitivityHistory.isEmpty()) {
        reloadRawTickParameterSensitivityHistory();
    }
    for (const RawTickParameterSensitivityResult &record :
         m_rawTickSensitivityHistory) {
        if (record.resultId != normalizedId) continue;
        m_rawTickSensitivityResult = record;
        m_rawTickSensitivityError.clear();
        emit rawTickSensitivityStateChanged();
        setStatusText(QStringLiteral("已恢复原始 Tick 参数敏感性结果：%1")
                          .arg(normalizedId));
        return true;
    }
    m_rawTickSensitivityError = QStringLiteral("未找到参数敏感性归档：%1")
                                    .arg(normalizedId);
    emit rawTickSensitivityStateChanged();
    setStatusText(m_rawTickSensitivityError);
    return false;
}

void TradingController::handleRawTickSensitivityCompleted(
    quint64 requestId, const RawTickParameterSensitivityResult &result)
{
    if (requestId != m_rawTickSensitivityRequestId) return;
    m_rawTickSensitivityResult = result;
    m_rawTickSensitivityError.clear();
    reloadRawTickParameterSensitivityHistory();
    emit rawTickSensitivityStateChanged();
    setStatusText(result.analysisMode == QStringLiteral("nested_walk_forward")
        ? QStringLiteral(
            "原始 Tick 嵌套 walk-forward 完成：%1 折，验证收益均值 %2%")
              .arg(result.walkForwardFoldCount)
              .arg(result.meanValidationReturnPercent, 0, 'f', 2)
        : QStringLiteral(
            "原始 Tick 参数敏感性完成：训练 %1 日至 %2 日，最终验证收益 %3%")
              .arg(result.trainingStartDate.toString(Qt::ISODate),
                   result.trainingEndDate.toString(Qt::ISODate))
              .arg(result.validationSummary.totalReturnPercent, 0, 'f', 2));
}

void TradingController::handleRawTickSensitivityFailed(
    quint64 requestId, const QString &error)
{
    if (requestId != m_rawTickSensitivityRequestId) return;
    m_rawTickSensitivityError = error;
    emit rawTickSensitivityStateChanged();
    setStatusText(QStringLiteral("原始 Tick 参数敏感性失败：%1").arg(error));
}

void TradingController::reloadRawTickParameterSensitivityHistory()
{
    m_rawTickSensitivityHistory =
        m_dataStore.loadRawTickParameterSensitivityResults(50);
    emit rawTickSensitivityStateChanged();
}

bool TradingController::customStrategyReplayRunning() const
{
    return m_customStrategyReplayTimer.isActive();
}

int TradingController::customStrategyReplayedBars() const
{
    return m_customStrategyReplayIndex;
}

int TradingController::customStrategyReplayTotalBars() const
{
    return m_customStrategyReplayCandles.size();
}

bool TradingController::startCustomStrategyReplay(const QString &strategyId,
                                                 const QString &symbol,
                                                 int periodSeconds, int barCount,
                                                 int intervalMs)
{
    if (m_customStrategyReplayTimer.isActive()) {
        stopCustomStrategyReplay();
    }
    if (m_customStrategyRuntimeManager.state(strategyId) != CustomStrategyRuntimeState::Active) {
        setStatusText(QStringLiteral("策略未处于模拟执行状态，请先启用模拟执行：%1").arg(strategyId));
        return false;
    }
    if (barCount <= 0) barCount = 300;
    if (barCount > 2000) barCount = 2000;
    if (periodSeconds <= 0) periodSeconds = 60;
    if (intervalMs < 20) intervalMs = 20;

    Instrument instrument = findInstrument(symbol.trimmed());
    if (instrument.symbol.isEmpty()) {
        instrument.symbol = symbol.trimmed();
        instrument.name = symbol.trimmed();
        instrument.market = Market::AShare;
    }

    const QVector<Candle> candles = m_dataStore.loadRecentCandlesFromSource(
        instrument, periodSeconds, barCount, QStringLiteral("raw_tick_cache_v1"));
    if (candles.isEmpty()) {
        setStatusText(QStringLiteral("没有可用于回放的 raw_tick_cache_v1 本地 K 线：%1").arg(symbol));
        return false;
    }

    m_customStrategyReplayCandles = candles;
    m_customStrategyReplayIndex = 0;
    m_customStrategyReplaySymbol = instrument.symbol;
    m_customStrategyReplayTimer.setInterval(intervalMs);
    m_customStrategyReplayTimer.start();
    emit customStrategyReplayStateChanged();
    setStatusText(QStringLiteral("模拟回放开始：%1 · %2 根 · 间隔 %3 ms")
        .arg(instrument.symbol).arg(candles.size()).arg(intervalMs));
    return true;
}

void TradingController::stopCustomStrategyReplay()
{
    if (m_customStrategyReplayTimer.isActive()) {
        m_customStrategyReplayTimer.stop();
    }
    m_customStrategyReplayCandles.clear();
    m_customStrategyReplayIndex = 0;
    emit customStrategyReplayStateChanged();
    setStatusText(QStringLiteral("模拟回放已停止"));
}

void TradingController::handleCustomStrategyReplayTick()
{
    if (m_customStrategyReplayIndex >= m_customStrategyReplayCandles.size()) {
        const int total = m_customStrategyReplayCandles.size();
        stopCustomStrategyReplay();
        setStatusText(QStringLiteral("模拟回放完成：共 %1 根").arg(total));
        return;
    }
    const Candle candle = m_customStrategyReplayCandles.at(m_customStrategyReplayIndex);
    ++m_customStrategyReplayIndex;
    m_strategyEngine.dispatchCandle(candle);
    emit customStrategyReplayStateChanged();
}

bool TradingController::requestMarketSnapshot(const QDateTime &requestedTime)
{
    m_followLatestSnapshot = false;
    const quint64 requestId = beginSnapshotRequest(requestedTime);
    if (!requestedTime.isValid()) {
        MarketSnapshotQueryResult result;
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("请求时刻无效");
        finishSnapshotRequest(requestId, result);
        return false;
    }
    return submitSnapshotRequest(requestId, requestedTime);
}

bool TradingController::requestMarketSnapshotAt(const QString &marketTime)
{
    m_followLatestSnapshot = false;
    const quint64 requestId = beginSnapshotRequest({});
    QString error;
    const auto requestedTime = MarketSnapshotQueryService::parseMarketTime(marketTime, &error);
    if (!requestedTime.has_value()) {
        MarketSnapshotQueryResult result;
        result.health = MarketSnapshotHealth::Error;
        result.error = error;
        finishSnapshotRequest(requestId, result);
        return false;
    }
    m_requestedSnapshotTime = requestedTime->toUTC();
    emit marketSnapshotChanged();
    return submitSnapshotRequest(requestId, *requestedTime);
}

bool TradingController::requestLatestMarketSnapshot()
{
    m_followLatestSnapshot = true;
    const QDateTime requestedTime = QDateTime::currentDateTimeUtc();
    const quint64 requestId = beginSnapshotRequest(requestedTime);
    if (m_latestLiveMarketSnapshot.timestamp.isValid()) {
        finishSnapshotRequest(
            requestId,
            MarketSnapshotQueryService::evaluate(
                m_latestLiveMarketSnapshot, requestedTime, 10000));
    } else {
        MarketSnapshotQueryResult result;
        result.requestedTime = requestedTime;
        result.health = MarketSnapshotHealth::Empty;
        result.error = QStringLiteral(
            "等待实时行情；实时行情仅驻留内存，历史数据需显式下载或导入");
        finishSnapshotRequest(requestId, result);
    }
    return true;
}

quint64 TradingController::beginSnapshotRequest(const QDateTime &requestedTime)
{
    const quint64 requestId = m_snapshotRequestGate.begin();
    m_requestedSnapshotTime = requestedTime.isValid() ? requestedTime.toUTC() : QDateTime{};
    m_snapshotError.clear();
    emit snapshotQueryStateChanged();
    emit marketSnapshotChanged();
    return requestId;
}

bool TradingController::submitSnapshotRequest(quint64 requestId,
                                               const QDateTime &requestedTime)
{
    if (!m_storageReady || !m_asyncSnapshotQueryService.isRunning()) {
        MarketSnapshotQueryResult result;
        result.requestedTime = requestedTime.toUTC();
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("异步市场快照数据库不可用");
        finishSnapshotRequest(requestId, result);
        return false;
    }
    if (!m_asyncSnapshotQueryService.request(requestId, requestedTime, 10000)) {
        MarketSnapshotQueryResult result;
        result.requestedTime = requestedTime.toUTC();
        result.health = MarketSnapshotHealth::Error;
        result.error = QStringLiteral("市场快照查询任务提交失败");
        finishSnapshotRequest(requestId, result);
        return false;
    }
    return true;
}

void TradingController::finishSnapshotRequest(quint64 requestId,
                                               const MarketSnapshotQueryResult &result)
{
    if (!m_snapshotRequestGate.finish(requestId)) return;
    applyMarketSnapshotResult(result);
    emit snapshotQueryStateChanged();
}

bool TradingController::applyMarketSnapshotResult(const MarketSnapshotQueryResult &result)
{
    m_requestedSnapshotTime = result.requestedTime;
    m_resolvedSnapshotTime = result.resolvedTime;
    m_snapshotBatchId = result.found ? result.snapshot.batchId : QString{};
    m_snapshotExpectedCount = result.found ? result.snapshot.expectedCount : 0;
    m_snapshotReceivedCount = result.found ? result.snapshot.quotes.size() : 0;
    m_snapshotCompleteness = result.found ? result.snapshot.completenessRatio() : 0.0;
    m_snapshotLatencyMilliseconds = result.found
        ? result.snapshot.durationMilliseconds() : 0;
    m_snapshotStalenessMilliseconds = result.stalenessMilliseconds;
    m_snapshotHealth = static_cast<int>(result.health);
    m_snapshotError = result.error;
    if (result.found) {
        m_marketSnapshotModel.loadSnapshot(
            result.snapshot, result.health == MarketSnapshotHealth::Stale);
    } else {
        m_marketSnapshotModel.clear();
    }
    emit marketSnapshotChanged();
    return result.found;
}

void TradingController::restartLiveIfRunning()
{
    if (!m_realtimeLiveSessionState.requested()) return;
    m_marketData->stop();
    m_marketData->start(buildMarketUniverse());
}

void TradingController::setStrategyEnabled(const QString &strategyId, bool enabled)
{
    m_strategyEngine.setStrategyEnabled(strategyId, enabled);
    m_strategyModel.setStrategyEnabled(strategyId, enabled);
    emit strategyStateChanged();
    setStatusText(QStringLiteral("策略已%1：%2").arg(enabled ? QStringLiteral("启用") : QStringLiteral("停用"), strategyId));
}

void TradingController::setStrategyParameters(const QString &strategyId, double thresholdPercent, int cooldownSeconds)
{
    Strategy *strategy = m_strategyEngine.strategyById(strategyId);
    if (!strategy) {
        setStatusText(QStringLiteral("未找到策略：%1").arg(strategyId));
        return;
    }
    const double normalizedPercent = std::max(0.01, thresholdPercent);
    const int normalizedCooldown = std::max(0, cooldownSeconds);
    if (const auto existing = m_strategyExecutionCoordinator.configuration(strategyId)) {
        StrategyExecutionConfig config = *existing;
        config.cooldownSeconds = normalizedCooldown;
        if (!m_strategyExecutionCoordinator.setConfiguration(config)) {
            setStatusText(QStringLiteral("策略执行配置持久化失败：%1").arg(strategyId));
            return;
        }
        emit strategyExecutionConfigChanged(strategyId);
        emit strategyExecutionStateChanged();
    }
    strategy->setRuntimeParameters(normalizedPercent, normalizedCooldown);
    m_strategyModel.setStrategyParameters(strategyId, normalizedPercent, normalizedCooldown);
    setStatusText(QStringLiteral("策略参数已更新：%1，阈值 %2%，冷却 %3 秒")
        .arg(strategyId).arg(normalizedPercent, 0, 'f', 2).arg(normalizedCooldown));
}

QVector<Instrument> TradingController::buildWatchlist() const { return m_instrumentModel.toVector(); }
QVector<Instrument> TradingController::buildMarketUniverse() const
{
    const QString acceptanceSymbols = qEnvironmentVariable(
        "PPTRADER_REALTIME_ACCEPTANCE_UNIVERSE_SYMBOLS").trimmed();
    if (acceptanceSymbols.isEmpty()) return m_aShareUniverse;

    const QStringList requestedSymbols = acceptanceSymbols.split(
        QLatin1Char(','), Qt::SkipEmptyParts);
    const QSet<QString> requested(requestedSymbols.cbegin(), requestedSymbols.cend());
    QVector<Instrument> filtered;
    for (const Instrument &instrument : m_aShareUniverse) {
        if (requested.contains(instrument.symbol)) filtered.append(instrument);
    }
    return filtered;
}

bool TradingController::reloadAShareUniverse()
{
    const QString previousLiveSymbol = selectedLiveSymbol();
    const AShareUniverseLoadResult result = AShareUniverseLoader::load();
    m_aShareUniverse = result.instruments;
    m_workspaceInstrumentSelections.setLiveUniverse(m_aShareUniverse);
    m_aShareUniverseCachePath = result.cachePath;
    m_aShareUniverseError = result.error;
    m_aShareUniverseSourceCount = result.sourceCount;
    m_aShareUniverseRejectedCount = result.rejectedCount;
    emit marketUniverseChanged();
    if (!m_aShareUniverseError.isEmpty()) {
        if (previousLiveSymbol != selectedLiveSymbol()) emit liveInstrumentChanged();
        setStatusText(m_aShareUniverseError);
        return false;
    }
    if (selectedLiveSymbol().isEmpty() && !m_aShareUniverse.isEmpty()) {
        const QString researchSymbol =
            m_workspaceInstrumentSelections.researchInstrument().symbol;
        if (researchSymbol.isEmpty()
            || !m_workspaceInstrumentSelections.selectLiveInstrument(researchSymbol)) {
            m_workspaceInstrumentSelections.selectLiveInstrument(
                m_aShareUniverse.first().symbol);
        }
    }
    if (m_historyDownloadInstrument.symbol.isEmpty() && !m_aShareUniverse.isEmpty()) {
        const Instrument &researchInstrument =
            m_workspaceInstrumentSelections.researchInstrument();
        const auto match = std::lower_bound(
            m_aShareUniverse.cbegin(), m_aShareUniverse.cend(), researchInstrument.symbol,
            [](const Instrument &instrument, const QString &symbol) {
                return instrument.symbol < symbol;
            });
        m_historyDownloadInstrument = match != m_aShareUniverse.cend()
                && match->symbol == researchInstrument.symbol
            ? *match : m_aShareUniverse.first();
        m_historyDownloadStatusText = QStringLiteral("已预选下载品种；尚未请求网络");
        emit historyDownloadStateChanged();
    }
    if (m_tickDataImportInstrument.symbol.isEmpty() && !m_aShareUniverse.isEmpty()) {
        const Instrument &researchInstrument =
            m_workspaceInstrumentSelections.researchInstrument();
        const auto match = std::lower_bound(
            m_aShareUniverse.cbegin(), m_aShareUniverse.cend(), researchInstrument.symbol,
            [](const Instrument &instrument, const QString &symbol) {
                return instrument.symbol < symbol;
            });
        m_tickDataImportInstrument = match != m_aShareUniverse.cend()
                && match->symbol == researchInstrument.symbol
            ? *match : m_aShareUniverse.first();
        m_tickDataImportStatusText = QStringLiteral("已预选导入品种；尚未读取文件");
        emit tickDataImportStateChanged();
    }
    if (previousLiveSymbol != selectedLiveSymbol()) emit liveInstrumentChanged();
    if (m_realtimeLiveSessionState.requested()) restartLiveIfRunning();
    setStatusText(QStringLiteral("A 股股票池已加载：%1 只，拒绝 %2 条异常候选")
        .arg(m_aShareUniverse.size()).arg(m_aShareUniverseRejectedCount));
    return true;
}

Instrument TradingController::findInstrument(const QString &symbol) const
{
    for (const Instrument &instrument : buildWatchlist()) {
        if (instrument.symbol == symbol) return instrument;
    }
    const auto iterator = std::lower_bound(
        m_aShareUniverse.cbegin(), m_aShareUniverse.cend(), symbol,
        [](const Instrument &instrument, const QString &value) {
            return instrument.symbol < value;
        });
    if (iterator != m_aShareUniverse.cend() && iterator->symbol == symbol) return *iterator;
    return {};
}

void TradingController::initializeRealtimeStrategies()
{
    m_strategyEngine.clearStrategies();
    m_strategyModel.clear();
    for (const Instrument &instrument : buildWatchlist()) {
        const double threshold = instrument.market == Market::Crypto ? 0.006 : 0.0025;
        auto *strategy = new TickMomentumStrategy(instrument, threshold, 20);
        const QString strategyId = strategy->id();
        m_strategyEngine.addStrategy(strategy);
        ensureStrategyExecutionConfiguration(strategyId, strategy->cooldownSeconds());
        m_strategyModel.addStrategy({strategyId, instrument.symbol, strategy->displayName(),
            QStringLiteral("阈值 %1%，冷却 %2 秒").arg(strategy->thresholdPercent(), 0, 'f', 2)
                .arg(strategy->cooldownSeconds()),
            strategy->thresholdPercent(), strategy->cooldownSeconds(), true});
    }

    const auto addPairStrategy = [this](const QString &primarySymbol, const QString &secondarySymbol,
                                        double thresholdPercent) {
        const Instrument primary = findInstrument(primarySymbol);
        const Instrument secondary = findInstrument(secondarySymbol);
        if (primary.symbol.isEmpty() || secondary.symbol.isEmpty()) return;
        auto *strategy = new PairSpreadStrategy(primary, secondary, thresholdPercent, 45);
        const QString strategyId = strategy->id();
        m_strategyEngine.addStrategy(strategy);
        ensureStrategyExecutionConfiguration(strategyId, strategy->cooldownSeconds());
        m_strategyModel.addStrategy({strategyId,
            QStringLiteral("%1/%2").arg(primary.symbol, secondary.symbol), strategy->displayName(),
            QStringLiteral("阈值 %1%，冷却 %2 秒").arg(strategy->thresholdPercent(), 0, 'f', 2)
                .arg(strategy->cooldownSeconds()),
            strategy->thresholdPercent(), strategy->cooldownSeconds(), true});
    };
    addPairStrategy(QStringLiteral("600519"), QStringLiteral("300750"), 1.20);
    emit strategyStateChanged();
}

void TradingController::setStatusText(const QString &text)
{
    if (m_statusText == text) return;
    m_statusText = text;
    LogService::instance().info(QStringLiteral("status"), text);
    emit statusTextChanged();
}

void TradingController::handleProviderState(int health, const QString &message,
                                            const QString &error,
                                            int expectedCount, int receivedCount,
                                            const QString &batchId,
                                            const QDateTime &updatedAt)
{
    m_providerHealth = health;
    m_providerMessage = message;
    m_providerError = error;
    m_providerExpectedCount = std::max(0, expectedCount);
    m_providerReceivedCount = std::max(0, receivedCount);
    m_providerBatchId = batchId;
    m_providerUpdatedAt = updatedAt;
    LogService::instance().info(
        QStringLiteral("provider"),
        QStringLiteral("health=%1 expected=%2 received=%3 batch=%4 updatedUtc=%5 error=%6")
            .arg(health)
            .arg(m_providerExpectedCount)
            .arg(m_providerReceivedCount)
            .arg(batchId.isEmpty() ? QStringLiteral("-") : batchId,
                 updatedAt.isValid() ? updatedAt.toUTC().toString(Qt::ISODateWithMs)
                                     : QStringLiteral("-"),
                 error.isEmpty() ? QStringLiteral("-") : error));
    m_realtimeLiveSessionState.updateProviderHealth(health);
    emit providerStateChanged();
    if (!error.isEmpty()) setStatusText(error);
    else if (!message.isEmpty()) setStatusText(message);
}

void TradingController::handleProviderRoute(
    const QString &providerId, const QString &displayName, int score,
    bool fallbackUsed, int failoverCount, const QString &message,
    const QVariantList &capabilities)
{
    m_activeRealtimeProvider = providerId;
    m_activeRealtimeProviderName = displayName;
    m_realtimeProviderScore = std::clamp(score, 0, 100);
    m_realtimeFallbackUsed = fallbackUsed;
    m_realtimeFailoverCount = std::max(0, failoverCount);
    m_realtimeRoutingMessage = message;
    m_realtimeProviderCapabilities = capabilities;
    LogService::instance().info(
        QStringLiteral("realtime-route"),
        QStringLiteral("provider=%1 name=%2 score=%3 fallback=%4 failovers=%5 message=%6")
            .arg(providerId.isEmpty() ? QStringLiteral("-") : providerId,
                 displayName.isEmpty() ? QStringLiteral("-") : displayName)
            .arg(m_realtimeProviderScore)
            .arg(m_realtimeFallbackUsed ? 1 : 0)
            .arg(m_realtimeFailoverCount)
            .arg(message.isEmpty() ? QStringLiteral("-") : message));
    emit providerStateChanged();
}

void TradingController::degradeProvider(const QString &error)
{
    m_providerHealth = static_cast<int>(ProviderHealthState::Degraded);
    m_providerMessage = QStringLiteral("行情服务降级");
    m_providerError = error;
    m_providerUpdatedAt = QDateTime::currentDateTimeUtc();
    emit providerStateChanged();
}

void TradingController::setBacktestStatus(const QString &text, const QString &error)
{
    m_backtestStatusText = text;
    m_backtestError = error;
    emit backtestStateChanged();
}

void TradingController::handleTick(const Tick &tick)
{
    m_paperStrategyBook.acceptQuote(tick, QDateTime::currentDateTimeUtc());
    if (tick.batchId.isEmpty()) m_tickModel.addTick(tick);
    m_latestTicks.insert(tick.instrument.symbol, tick);
    // 策略绑定后只由其校验通过的源报价驱动账户，避免同一报价撮合两次。
    if (!m_paperStrategyBook.hasRun() && !m_paperReplayBook.hasRun())
        m_tradingAccount.updateMarketPrice(tick);
    m_strategyEngine.dispatchTick(tick);
    if (tick.instrument.symbol == m_workspaceInstrumentSelections.researchInstrument().symbol) {
        emit selectedTickPoint(tick.timestamp, tick.price, tick.instrument.symbol);
    }
}

void TradingController::handleMarketSnapshot(const MarketSnapshot &snapshot)
{
    LogService::instance().info(
        QStringLiteral("snapshot"),
        QStringLiteral("batch=%1 expected=%2 received=%3 completeness=%4 "
                       "durationMs=%5 snapshotUtc=%6 completedUtc=%7")
            .arg(snapshot.batchId)
            .arg(snapshot.expectedCount)
            .arg(snapshot.quotes.size())
            .arg(snapshot.completenessRatio(), 0, 'f', 6)
            .arg(snapshot.durationMilliseconds())
            .arg(snapshot.timestamp.toUTC().toString(Qt::ISODateWithMs),
                 snapshot.completedAt.toUTC().toString(Qt::ISODateWithMs)));
    QVector<Tick> displayTicks;
    displayTicks.reserve(std::min(500, static_cast<int>(snapshot.quotes.size())));
    const QString researchSymbol =
        m_workspaceInstrumentSelections.researchInstrument().symbol;
    const QString selectedSymbol = selectedLiveSymbol().isEmpty()
        ? researchSymbol : selectedLiveSymbol();
    QSet<QString> tickDispatchSymbols;
    if (!selectedSymbol.isEmpty()) tickDispatchSymbols.insert(selectedSymbol);
    if (!researchSymbol.isEmpty()) tickDispatchSymbols.insert(researchSymbol);
    for (const Instrument &instrument : buildWatchlist()) {
        if (!instrument.symbol.isEmpty()) tickDispatchSymbols.insert(instrument.symbol);
    }
    for (const Position &position : m_tradingAccount.positions()) {
        if (!position.instrument.symbol.isEmpty())
            tickDispatchSymbols.insert(position.instrument.symbol);
    }
    for (const Order &order : m_tradingAccount.orders()) {
        if (isActiveOrder(order.status) && !order.request.instrument.symbol.isEmpty())
            tickDispatchSymbols.insert(order.request.instrument.symbol);
    }
    const QStringList paperSymbols = m_paperStrategyBook.subscribedSymbols();
    const auto tickForQuote = [&snapshot](const CrossSectionalQuote &quote) {
        Tick tick;
        tick.instrument = quote.instrument;
        tick.timestamp = quote.timestamp.isValid() ? quote.timestamp : snapshot.timestamp;
        tick.batchId = snapshot.batchId;
        tick.price = quote.price;
        tick.previousClose = quote.previousClose;
        tick.open = quote.open;
        tick.high = quote.high;
        tick.low = quote.low;
        tick.volume = quote.volume;
        tick.amount = quote.amount;
        tick.bid = quote.bid;
        tick.ask = quote.ask;
        return tick;
    };
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        const Tick tick = tickForQuote(quote);
        if (paperSymbols.contains(quote.instrument.symbol)
            && !tickDispatchSymbols.contains(quote.instrument.symbol))
            m_paperStrategyBook.acceptQuote(tick, QDateTime::currentDateTimeUtc());
        // 全市场批次只对会消费逐笔语义的有限标的展开 Tick。其余 5000+
        // 报价保留在快照模型中，避免账户、策略和 QML 为整批建立细粒度
        // 更新积压；持仓、活动委托、观察列表及当前选择仍保持原有语义。
        if (tickDispatchSymbols.contains(quote.instrument.symbol)) handleTick(tick);
        if (quote.instrument.symbol == selectedSymbol) {
            displayTicks.prepend(tick);
            if (displayTicks.size() > 500) displayTicks.removeLast();
        } else if (displayTicks.size() < 500) {
            displayTicks.append(tick);
        }
    }
    m_tickModel.loadTicks(displayTicks);

    m_latestLiveMarketSnapshot = snapshot;
    if (m_followLatestSnapshot) {
        const QDateTime requestedTime = QDateTime::currentDateTimeUtc();
        const quint64 requestId = beginSnapshotRequest(requestedTime);
        finishSnapshotRequest(
            requestId,
            MarketSnapshotQueryService::evaluate(snapshot, requestedTime, 10000));
    }
    const double completeness = snapshot.completenessRatio();
    if (completeness < 0.90) {
        setStatusText(QStringLiteral("A 股行情快照完整率仅 %1%，跳过横截面计算")
            .arg(completeness * 100.0, 0, 'f', 1));
        return;
    }
    if (snapshot.durationMilliseconds() > 10000) {
        setStatusText(QStringLiteral("A 股行情快照耗时 %1 秒，超过 10 秒水位线")
            .arg(snapshot.durationMilliseconds() / 1000.0, 0, 'f', 1));
        return;
    }
    const QVector<FactorFrame> frames = m_crossSectionalEngine.calculate(snapshot);
    if (frames.isEmpty()) return;
    m_latestCrossSectionalFrame = frames.first();
    emit crossSectionalFrameUpdated(m_latestCrossSectionalFrame);
}

void TradingController::handleRealtimeSignal(const TradeSignal &signal)
{
    m_signalModel.addSignal(signal);
    if (m_storageReady && m_dataStore.appendSignal(signal)) {
        ++m_persistedSignalCount;
        emit storageStateChanged();
    }
    setStatusText(QStringLiteral("实时策略触发：%1 %2 %3")
        .arg(signal.instrument.symbol, signalSideName(signal.side), signal.reason));
}

void TradingController::handleStrategySignal(const QString &strategyId, const TradeSignal &signal)
{
    handleRealtimeSignal(signal);
    m_notificationDispatcher.notifySignal(signal);
    if (!m_paperStrategyBook.hasRun() && !m_paperReplayBook.hasRun())
        m_strategyExecutionCoordinator.processSignal(strategyId, signal);
}

void TradingController::ensureStrategyExecutionConfiguration(const QString &strategyId,
                                                              int cooldownSeconds)
{
    if (m_strategyExecutionCoordinator.configuration(strategyId).has_value()) return;
    StrategyExecutionConfig config;
    config.strategyId = strategyId;
    config.enabled = false;
    config.mode = StrategyExecutionMode::Disabled;
    config.cooldownSeconds = std::max(0, cooldownSeconds);
    config.fixedOrderValue = 10000.0;
    config.maxPositionPercent = 10.0;
    m_strategyExecutionCoordinator.setConfiguration(config);
}

void TradingController::handleBacktestPoint(const CandlePoint &point)
{
    m_candleModel.addPoint(point);
    if (point.hasSignal) m_signalModel.addSignal(point.signal);
}

void TradingController::handleBacktestTrade(const BacktestTrade &trade)
{
    m_backtestTradeModel.addTrade(trade);
}

void TradingController::handleEquityPoint(const EquityPoint &point)
{
    m_equityCurveModel.addPoint(point);
}

void TradingController::handleBacktestFinished(const PerformanceSummary &summary)
{
    m_displayedBacktestResult = m_backtest.result();
    m_backtestSummary = summary;
    m_completedBacktestTaskId = m_backtestTaskId;
    m_backtestTradeModel.loadTrades(m_backtest.result().trades);
    m_equityCurveModel.loadPoints(m_backtest.result().equityCurve);
    BacktestArchiveRecord archive;
    archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.result = m_backtest.result();
    m_displayedBacktestCompletedAt = archive.completedAt;
    m_backtestArchiveError.clear();
    m_backtestArchiveSaveError.clear();
    m_backtestResultHistoricalSnapshot = false;
    if (m_storageReady && m_dataStore.saveBacktestArchive(archive)) {
        m_selectedBacktestArchiveId = archive.resultId;
        m_backtestResultArchived = true;
        refreshBacktestArchiveList();
    } else {
        m_selectedBacktestArchiveId.clear();
        m_backtestResultArchived = false;
        m_backtestArchiveSaveError = m_storageReady
            ? QStringLiteral("本次结果未保存：SQLite 归档写入失败")
            : QStringLiteral("本次结果未保存：本地数据库不可用");
        emit backtestArchiveStateChanged();
    }
    m_backtestProgress = 1.0;
    setBacktestStatus(QStringLiteral("回测完成"));
    setStatusText(QStringLiteral("回测完成：收益 %1%，交易 %2 笔")
        .arg(summary.totalReturnPercent, 0, 'f', 2).arg(summary.totalTrades));
    emit backtestSummaryChanged();
    emit backtestRunningChanged();
}

void TradingController::handleBacktestCancelled()
{
    m_backtestPreparing = false;
    m_pendingBacktestHistoryRequestId = 0;
    m_pendingBacktestDatasetRequestId = 0;
    m_pendingCustomBacktestPlan.reset();
    setBacktestStatus(QStringLiteral("已取消"));
    setStatusText(QStringLiteral("回测已取消"));
    emit backtestRunningChanged();
}

void TradingController::handleBacktestError(const QString &message)
{
    m_backtestPreparing = false;
    m_pendingBacktestHistoryRequestId = 0;
    m_pendingBacktestDatasetRequestId = 0;
    m_pendingCustomBacktestPlan.reset();
    setBacktestStatus(QStringLiteral("回测失败"), message);
    setStatusText(QStringLiteral("回测错误：%1").arg(message));
    emit backtestRunningChanged();
}

void TradingController::handleBacktestDatasetPrepared(
    quint64 requestId, const BacktestDatasetIdentity &identity,
    const QVector<Candle> &candles)
{
    if (!m_backtestPreparing || requestId != m_pendingBacktestDatasetRequestId) return;
    m_backtestPreparing = false;
    m_pendingBacktestDatasetRequestId = 0;
    if (identity.isEmpty()) {
        handleBacktestError(QStringLiteral("回测数据集身份不可用：无原始 Tick 事实"));
        return;
    }
    if (candles.size() < m_pendingBacktestConfig.requestedBars) {
        handleBacktestError(QStringLiteral(
            "历史 Tick 派生数据实际只有 %1 根，需要 %2 根；未访问网络 Provider")
            .arg(candles.size()).arg(m_pendingBacktestConfig.requestedBars));
        return;
    }
    m_pendingBacktestConfig.datasetIdentity = identity;

    const bool custom = m_pendingBacktestConfig.strategyId.startsWith(
        QStringLiteral("custom_"));
    setBacktestStatus(custom ? QStringLiteral("正在计算本地自定义策略回测")
                             : QStringLiteral("正在计算历史 Tick 本地回测"));
    setStatusText(QStringLiteral("严格本地回测：%1 · %2 · %3 根 · %4 个交易日")
                      .arg(identity.instrument.symbol,
                           m_pendingBacktestConfig.strategyId)
                      .arg(candles.size())
                      .arg(identity.days.size()));

    std::unique_ptr<BacktestStrategy> strategy;
    if (custom) {
        if (!m_pendingCustomBacktestPlan) {
            handleBacktestError(QStringLiteral("自定义策略编译快照已失效"));
            return;
        }
        strategy = std::make_unique<CustomBacktestStrategy>(
            std::move(*m_pendingCustomBacktestPlan));
    }
    m_pendingCustomBacktestPlan.reset();
    if (!m_backtest.start(m_pendingBacktestConfig, candles, std::move(strategy))) {
        emit backtestRunningChanged();
        return;
    }
    emit backtestRunningChanged();
}

void TradingController::handleBacktestDatasetFailed(
    quint64 requestId, const QString &message)
{
    if (!m_backtestPreparing || requestId != m_pendingBacktestDatasetRequestId) return;
    handleBacktestError(message);
}

void TradingController::handleHistoryKline(quint64 requestId,
                                           const QVector<Candle> &candles,
                                           bool fromCache)
{
    if (m_storageReady) refreshLocalBacktestDataCatalog();
    if (requestId == m_activeHistoryRequestId) {
        m_historyCandleModel.loadCandles(candles);
        const QString sourceText = fromCache ? QStringLiteral("本地缓存") : QStringLiteral("行情源");
        setStatusText(QStringLiteral("历史K线加载完成：%1 条（%2）")
                          .arg(candles.size()).arg(sourceText));
    }

    if (!m_backtestPreparing || requestId != m_pendingBacktestHistoryRequestId) return;
    m_backtestPreparing = false;
    m_pendingBacktestHistoryRequestId = 0;
    setBacktestStatus(QStringLiteral("正在计算"));
    std::unique_ptr<BacktestStrategy> strategy;
    if (m_pendingBacktestConfig.strategyId.startsWith(QStringLiteral("custom_"))) {
        if (!m_pendingCustomBacktestPlan) {
            handleBacktestError(QStringLiteral("自定义策略编译快照已失效"));
            return;
        }
        strategy = std::make_unique<CustomBacktestStrategy>(
            std::move(*m_pendingCustomBacktestPlan));
    }
    m_pendingCustomBacktestPlan.reset();
    if (!m_backtest.start(m_pendingBacktestConfig, candles, std::move(strategy))) {
        emit backtestRunningChanged();
        return;
    }
    emit backtestRunningChanged();
}

void TradingController::handleHistoryError(quint64 requestId, const QString &message)
{
    if (m_backtestPreparing && requestId == m_pendingBacktestHistoryRequestId) {
        handleBacktestError(message);
    } else if (requestId == m_activeHistoryRequestId) {
        setStatusText(QStringLiteral("历史K线错误：%1").arg(message));
    }
}

void TradingController::handleHistoryDownloadReady(
    quint64 requestId, const QVector<Candle> &candles, bool fromCache)
{
    if (requestId != m_activeHistoryDownloadServiceRequestId) return;
    m_historyDownloadRunning = false;
    m_historyDownloadError.clear();
    m_historyDownloadStoredCount = fromCache ? 0 : candles.size();
    const QString candleSource = candles.isEmpty() || candles.last().source.isEmpty()
        ? QStringLiteral("unknown") : candles.last().source;
    m_historyDownloadSource = fromCache
        ? QStringLiteral("缓存复用 · %1").arg(candleSource)
        : QStringLiteral("Provider · %1").arg(candleSource);

    refreshLocalBacktestDataCatalog();
    m_historyDownloadAvailableCount = 0;
    m_historyDownloadFirstTime = {};
    m_historyDownloadLastTime = {};
    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(
        m_historyDownloadFrequency);
    for (const LocalHistoryDataset &item
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (item.instrument.symbol == m_historyDownloadInstrument.symbol
            && item.periodSeconds == periodSeconds) {
            m_historyDownloadAvailableCount = item.candleCount;
            m_historyDownloadFirstTime = item.firstTime;
            m_historyDownloadLastTime = item.lastTime;
            break;
        }
    }
    m_historyDownloadStatusText = fromCache
        ? QStringLiteral("本地缓存已覆盖请求，未访问 Provider")
        : QStringLiteral("下载完成并已持久化到 SQLite");
    setStatusText(QStringLiteral("历史数据准备完成：%1 · %2 · 可用 %3 根（%4）")
                      .arg(m_historyDownloadInstrument.symbol,
                           m_historyDownloadFrequency)
                      .arg(m_historyDownloadAvailableCount)
                      .arg(m_historyDownloadSource));
    emit historyDownloadStateChanged();
}

void TradingController::handleHistoryDownloadError(quint64 requestId,
                                                    const QString &message)
{
    if (requestId != m_activeHistoryDownloadServiceRequestId) return;
    m_historyDownloadRunning = false;
    m_historyDownloadSource = QStringLiteral("Provider");
    m_historyDownloadStatusText = QStringLiteral("显式下载失败；已有缓存保持不变");
    m_historyDownloadError = message;
    setStatusText(QStringLiteral("历史数据下载失败：%1").arg(message));
    emit historyDownloadStateChanged();
}

void TradingController::handleTickDataImportFinished(
    quint64 requestId, const TickDataImportResult &result)
{
    if (m_tickDataBatchRunning && requestId == m_tickDataBatchActiveRequestId) {
        handleTickDataBatchFileFinished(result);
        return;
    }
    if (requestId != m_tickDataImportRequestId) return;
    const bool nasImport = requestId != 0 && requestId == m_nasImportRequestId;
    m_tickDataImportRunning = false;
    m_tickDataImportFilePath = result.filePath;
    m_tickDataImportSource = result.source;
    m_tickDataImportLinesRead = result.linesRead;
    m_tickDataImportValidRecords = result.validRecords;
    m_tickDataImportSkippedRecords = result.skippedRecords;
    m_tickDataImportWrittenBars = 0;
    m_tickDataImportRawTicksWritten = result.rawTicksWritten;
    m_tickDataImportDiagnostics = result.diagnosticMaps();
    m_tickDataImportFirstTime = {};
    m_tickDataImportLastTime = {};
    m_tickDataImportError.clear();
    qint64 candleDatabaseElapsedMilliseconds = 0;
    bool candlesStored = false;
    if (result.succeeded && !result.cancelled) {
        QElapsedTimer candleDatabaseTimer;
        candleDatabaseTimer.start();
        candlesStored = m_dataStore.upsertCandles(result.candles);
        candleDatabaseElapsedMilliseconds = candleDatabaseTimer.elapsed();
    }

    if (result.cancelled) {
        m_tickDataImportStatusText = QStringLiteral("本地 CSV 导入已取消；SQLite 未写入");
        m_tickDataImportError = QStringLiteral("用户取消导入");
    } else if (!result.succeeded) {
        m_tickDataImportStatusText = QStringLiteral("本地 CSV 解析失败；SQLite 未写入");
        for (const TickDataImportDiagnostic &diagnostic : result.diagnostics) {
            if (diagnostic.fatal) {
                m_tickDataImportError = QStringLiteral("%1（行 %2）")
                    .arg(diagnostic.reason).arg(diagnostic.lineNumber);
                break;
            }
        }
        if (m_tickDataImportError.isEmpty()) {
            m_tickDataImportError = QStringLiteral("没有可导入的有效 K 线");
        }
    } else if (!candlesStored) {
        m_tickDataImportStatusText = QStringLiteral("CSV 已解析，但 SQLite 事务写入失败");
        m_tickDataImportError = QStringLiteral("本次导入未写入，本地 catalog 保持原状");
    } else {
        m_tickDataImportWrittenBars = result.candles.size();
        m_tickDataImportFirstTime = result.candles.first().startTime;
        m_tickDataImportLastTime = result.candles.last().startTime;
        if (!nasImport) refreshLocalBacktestDataCatalog();
        m_tickDataImportStatusText = result.skippedRecords > 0
            ? QStringLiteral("导入完成；存在已记录的跳过行")
            : QStringLiteral("导入完成；全部有效记录已写入或更新");
        setStatusText(QStringLiteral("TickData 导入完成：%1 · 有效 %2 · 跳过 %3 · K 线 %4")
                          .arg(m_tickDataImportInstrument.symbol)
                          .arg(result.validRecords).arg(result.skippedRecords)
                          .arg(m_tickDataImportWrittenBars));
    }
    emit tickDataImportStateChanged();
    if (nasImport) {
        m_nasImportRequestId = 0;
        const int syncIndex = m_nasImportingIndex;
        m_nasImportingIndex = -1;
        if (syncIndex >= 0) {
            if (m_tickDataImportError.isEmpty() && result.succeeded) {
                m_nasSyncQueue.markSucceeded(
                    syncIndex, result.rawTicksWritten, m_tickDataImportWrittenBars,
                    result.parseElapsedMilliseconds,
                    result.rawDatabaseElapsedMilliseconds,
                    candleDatabaseElapsedMilliseconds,
                    m_nasPhaseTimer.isValid() ? m_nasPhaseTimer.elapsed() : 0);
                m_nasDataNodeStatusText = QStringLiteral(
                    "NAS Tick 已写入本地事实表：%1 条；继续下一交易日")
                    .arg(result.rawTicksWritten);
                m_nasDataNodeError.clear();
                if (m_tickDataImportInstrument.symbol == selectedLiveSymbol()) {
                    requestLiveDayCandleSeries(
                        m_tickDataImportInstrument.symbol,
                        m_liveDayCandleSeriesPeriodSeconds);
                }
            } else {
                m_nasSyncQueue.markFailed(syncIndex, m_tickDataImportError);
                m_nasDataNodeStatusText = QStringLiteral(
                    "NAS 文件已下载，但本地事务导入失败；继续下一交易日");
            }
        }
        persistNasSyncState();
        emit nasDataNodeStateChanged();
        QTimer::singleShot(0, this, &TradingController::startNextNasImport);
    }
}

void TradingController::handleAShareInstrumentDayImportFinished(
    quint64 requestId, const AShareInstrumentDayImportResult &result)
{
    if (requestId != m_aShareInstrumentDayImportRequestId) return;
    m_aShareInstrumentDayImportRunning = false;
    m_aShareInstrumentDayImportFilePath = result.filePath;
    m_aShareInstrumentDayImportSource = result.source;
    m_aShareInstrumentDayImportSourceVersion = result.sourceVersion;
    m_aShareInstrumentDayImportLinesRead = result.linesRead;
    m_aShareInstrumentDayImportValidRecords = result.validRecords;
    m_aShareInstrumentDayImportWrittenRecords = result.writtenRecords;
    m_aShareInstrumentDayImportNewRecords = result.newRecords;
    m_aShareInstrumentDayImportUnchangedRecords = result.unchangedRecords;
    m_aShareInstrumentDayImportCorrectedRecords = result.correctedRecords;
    m_aShareInstrumentDayImportReused = result.reused;
    m_aShareInstrumentDayImportContentSha256 = result.contentSha256;
    m_aShareInstrumentDayImportFirstDate = result.firstDate;
    m_aShareInstrumentDayImportLastDate = result.lastDate;
    m_aShareInstrumentDayImportDiagnostics = result.diagnosticMaps();
    m_aShareInstrumentDayImportError.clear();
    if (result.previewOnly && result.succeeded && result.previewReady) {
        m_aShareInstrumentDayImportPreviewReady = true;
        m_aShareInstrumentDayImportPreviewFilePath = result.filePath;
        m_aShareInstrumentDayImportPreviewSource = result.source;
        m_aShareInstrumentDayImportPreviewSourceVersion = result.sourceVersion;
        m_aShareInstrumentDayImportPreviewContentSha256 = result.contentSha256;
    } else if (!result.previewOnly) {
        m_aShareInstrumentDayImportPreviewReady = false;
        m_aShareInstrumentDayImportPreviewContentSha256.clear();
    }

    if (result.cancelled) {
        m_aShareInstrumentDayImportStatusText = QStringLiteral(
            "事实导入已取消；SQLite 未写入");
        m_aShareInstrumentDayImportError = QStringLiteral("用户取消导入");
    } else if (!result.succeeded) {
        m_aShareInstrumentDayImportStatusText = QStringLiteral(
            "事实 CSV 校验或事务写入失败；已有事实保持不变");
        if (!result.diagnostics.isEmpty()) {
            const auto &diagnostic = result.diagnostics.first();
            m_aShareInstrumentDayImportError = diagnostic.lineNumber > 0
                ? QStringLiteral("%1（行 %2）")
                      .arg(diagnostic.reason).arg(diagnostic.lineNumber)
                : diagnostic.reason;
        }
        if (m_aShareInstrumentDayImportError.isEmpty())
            m_aShareInstrumentDayImportError = QStringLiteral("导入失败");
    } else if (result.previewOnly) {
        m_aShareInstrumentDayImportStatusText = QStringLiteral(
            "差异预览完成：新增 %1 · 不变 %2 · 修订 %3；尚未写入")
                .arg(result.newRecords)
                .arg(result.unchangedRecords)
                .arg(result.correctedRecords);
        if (result.requiresRevisionReason) {
            m_aShareInstrumentDayImportError = QStringLiteral(
                "存在既有事实变化；确认导入前必须填写可审计修订原因");
        }
    } else if (result.reused) {
        m_aShareInstrumentDayImportStatusText = QStringLiteral(
            "相同来源版本和文件哈希已归档，本次未重复写入");
    } else {
        m_aShareInstrumentDayImportStatusText = QStringLiteral(
            "事实导入完成：%1 条记录已事务写入").arg(result.writtenRecords);
        setStatusText(QStringLiteral(
            "A 股停复牌事实已更新：%1 · %2 条 · %3")
                          .arg(result.source)
                          .arg(result.writtenRecords)
                          .arg(aShareInstrumentDayImportDateRange()));
    }
    refreshAShareInstrumentDayImportHistory();
    emit aShareInstrumentDayImportStateChanged();
}

void TradingController::handleAShareIndustryImportFinished(
    quint64 requestId, const AShareIndustryImportResult &result)
{
    if (requestId
        != m_aShareIndustryImportState.value(QStringLiteral("requestId"))
               .toULongLong()) {
        return;
    }
    m_aShareIndustryImportState.insert(QStringLiteral("running"), false);
    m_aShareIndustryImportState.insert(
        QStringLiteral("filePath"), QDir::toNativeSeparators(result.filePath));
    m_aShareIndustryImportState.insert(
        QStringLiteral("classification"), result.dataset.classification);
    m_aShareIndustryImportState.insert(
        QStringLiteral("source"), result.dataset.source);
    m_aShareIndustryImportState.insert(
        QStringLiteral("sourceVersion"), result.dataset.sourceVersion);
    m_aShareIndustryImportState.insert(
        QStringLiteral("reused"), result.reused);
    m_aShareIndustryImportState.insert(
        QStringLiteral("requiresRevisionReason"),
        result.requiresRevisionReason);
    m_aShareIndustryImportState.insert(
        QStringLiteral("linesRead"), result.linesRead);
    m_aShareIndustryImportState.insert(
        QStringLiteral("validRecords"), result.validRecords);
    m_aShareIndustryImportState.insert(
        QStringLiteral("writtenRecords"), result.writtenRecords);
    m_aShareIndustryImportState.insert(
        QStringLiteral("fileContentSha256"), result.fileContentSha256);
    m_aShareIndustryImportState.insert(
        QStringLiteral("datasetContentSha256"),
        result.dataset.contentSha256);
    m_aShareIndustryImportState.insert(
        QStringLiteral("datasetId"), result.dataset.datasetId);
    m_aShareIndustryImportState.insert(
        QStringLiteral("diagnostics"), result.diagnosticMaps());
    m_aShareIndustryImportState.insert(QStringLiteral("error"), QStringLiteral(""));

    if (result.previewOnly && result.succeeded && result.previewReady) {
        m_aShareIndustryImportState.insert(QStringLiteral("previewReady"), true);
        m_aShareIndustryPreviewFilePath = QDir::toNativeSeparators(
            result.filePath);
        m_aShareIndustryPreviewClassification = result.dataset.classification;
        m_aShareIndustryPreviewSource = result.dataset.source;
        m_aShareIndustryPreviewSourceVersion = result.dataset.sourceVersion;
        m_aShareIndustryPreviewFileSha256 = result.fileContentSha256;
    } else if (!result.previewOnly) {
        m_aShareIndustryImportState.insert(QStringLiteral("previewReady"), false);
        m_aShareIndustryPreviewFileSha256.clear();
    }

    if (result.cancelled) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("行业 CSV 处理已取消；SQLite 未写入"));
        m_aShareIndustryImportState.insert(
            QStringLiteral("error"), QStringLiteral("用户取消导入"));
    } else if (!result.succeeded) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("行业 CSV 校验或事务写入失败；已有数据集保持不变"));
        QString error = QStringLiteral("行业数据集导入失败");
        if (!result.diagnostics.isEmpty()) {
            const auto &diagnostic = result.diagnostics.first();
            error = diagnostic.lineNumber > 0
                ? QStringLiteral("%1（行 %2）")
                      .arg(diagnostic.reason).arg(diagnostic.lineNumber)
                : diagnostic.reason;
        }
        m_aShareIndustryImportState.insert(QStringLiteral("error"), error);
    } else if (result.previewOnly) {
        QString status = QStringLiteral("预览完成：%1 条有效成员；尚未写入")
                             .arg(result.validRecords);
        if (result.reused) {
            status += QStringLiteral("；相同数据集已归档，可确认复用");
        } else if (result.requiresRevisionReason) {
            status += QStringLiteral("；同版本内容有变化，确认前须填写修订原因");
        }
        m_aShareIndustryImportState.insert(QStringLiteral("statusText"), status);
    } else if (result.reused) {
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("相同分类、来源、版本和内容已归档；本次未重复写入"));
    } else {
        m_aShareIndustryImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("行业数据集导入完成：%1 条成员已事务写入")
                .arg(result.writtenRecords));
        setStatusText(QStringLiteral("A 股行业数据集已归档：%1 · %2 · %3 条")
                          .arg(result.dataset.source)
                          .arg(result.dataset.sourceVersion)
                          .arg(result.writtenRecords));
    }
    refreshAShareIndustryImportHistory();
}

void TradingController::handleAuthorizedFactorImportFinished(
    quint64 requestId, const AuthorizedFactorImportResult &result)
{
    if (requestId
        != m_authorizedFactorImportState.value(QStringLiteral("requestId"))
               .toULongLong()) return;
    m_authorizedFactorImportState.insert(QStringLiteral("running"), false);
    m_authorizedFactorImportState.insert(
        QStringLiteral("filePath"), QDir::toNativeSeparators(result.filePath));
    m_authorizedFactorImportState.insert(
        QStringLiteral("factorKey"), result.dataset.factorKey);
    m_authorizedFactorImportState.insert(
        QStringLiteral("sourceVersion"), result.dataset.sourceVersion);
    m_authorizedFactorImportState.insert(
        QStringLiteral("sourceReference"), result.dataset.sourceReference);
    m_authorizedFactorImportState.insert(
        QStringLiteral("contentSha256"), result.contentSha256);
    m_authorizedFactorImportState.insert(
        QStringLiteral("datasetId"), result.dataset.datasetId);
    m_authorizedFactorImportState.insert(
        QStringLiteral("linesRead"), result.linesRead);
    m_authorizedFactorImportState.insert(
        QStringLiteral("validRecords"), result.validRecords);
    m_authorizedFactorImportState.insert(
        QStringLiteral("duplicateRecords"), result.duplicateRecords);
    m_authorizedFactorImportState.insert(
        QStringLiteral("writtenRecords"), result.writtenRecords);
    m_authorizedFactorImportState.insert(
        QStringLiteral("reused"), result.reused);
    m_authorizedFactorImportState.insert(
        QStringLiteral("diagnostics"), result.diagnosticMaps());
    m_authorizedFactorImportState.insert(QStringLiteral("error"), QStringLiteral(""));

    if (result.previewOnly && result.succeeded && result.previewReady) {
        m_authorizedFactorImportState.insert(QStringLiteral("previewReady"), true);
        m_authorizedFactorPreviewFilePath = QDir::toNativeSeparators(
            result.filePath);
        m_authorizedFactorPreviewFactorKey = result.dataset.factorKey;
        m_authorizedFactorPreviewSourceVersion = result.dataset.sourceVersion;
        m_authorizedFactorPreviewSourceReference = result.dataset.sourceReference;
        m_authorizedFactorPreviewDefinitionSha256 =
            result.dataset.factorDefinitionSha256;
        m_authorizedFactorPreviewContentSha256 = result.contentSha256;
    } else if (!result.previewOnly) {
        m_authorizedFactorImportState.insert(QStringLiteral("previewReady"), false);
        m_authorizedFactorPreviewContentSha256.clear();
    }

    if (result.cancelled) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("授权因子 CSV 处理已取消；SQLite 未写入"));
        m_authorizedFactorImportState.insert(
            QStringLiteral("error"), QStringLiteral("用户取消导入"));
    } else if (!result.succeeded) {
        QString error = QStringLiteral("授权因子数据集导入失败");
        if (!result.diagnostics.isEmpty()) {
            const auto &diagnostic = result.diagnostics.first();
            error = diagnostic.lineNumber > 0
                ? QStringLiteral("%1（行 %2）")
                      .arg(diagnostic.reason).arg(diagnostic.lineNumber)
                : diagnostic.reason;
        }
        m_authorizedFactorImportState.insert(QStringLiteral("error"), error);
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("授权因子 CSV 校验或事务写入失败；已有数据保持不变"));
    } else if (result.previewOnly) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("预览完成：%1 条有效观测、%2 条重复；尚未写入%3")
                .arg(result.validRecords)
                .arg(result.duplicateRecords)
                .arg(result.reused ? QStringLiteral("；相同数据集已归档")
                                   : QStringLiteral("")));
    } else if (result.reused) {
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("相同授权因子数据集已归档；本次未重复写入"));
    } else {
        m_authorizedFactorImportState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("授权因子数据集导入完成：%1 条观测已事务写入")
                .arg(result.writtenRecords));
    }
    refreshExternalFactorDatasetCatalog();
}

void TradingController::handleExternalFactorResearchPreflightFinished(
    quint64 requestId,
    const ExternalFactorResearchPreflightResult &result)
{
    if (!m_externalFactorResearchPreflightRequestGate.finish(requestId)) return;
    const QString requestedCandidateId =
        m_externalFactorResearchPreflightState.value(
            QStringLiteral("candidateId")).toString();
    const int requestedCandidateVersion =
        m_externalFactorResearchPreflightState.value(
            QStringLiteral("candidateVersion")).toInt();
    const int requestedMinimumPairs =
        m_externalFactorResearchPreflightState.value(
            QStringLiteral("minimumPairs")).toInt();
    const double requestedOneWayCostBps =
        m_externalFactorResearchPreflightState.value(
            QStringLiteral("oneWayCostBps")).toDouble();
    m_externalFactorResearchPreflightState = result.toVariantMap();
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("running"), false);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("requestId"), requestId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("resolvedRequestId"), requestId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("candidateId"), requestedCandidateId);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("candidateVersion"), requestedCandidateVersion);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("minimumPairs"), requestedMinimumPairs);
    m_externalFactorResearchPreflightState.insert(
        QStringLiteral("oneWayCostBps"), requestedOneWayCostBps);
    if (result.cancelled) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("ready"), false);
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("外部因子研究预检已取消；未生成归档"));
    } else if (!result.ready) {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("预检未通过：%1/%2 标的具备本地 Tick 派生覆盖")
                .arg(result.readySymbolCount).arg(result.symbolCount));
    } else {
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("error"), QStringLiteral(""));
        m_externalFactorResearchPreflightState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("预检通过：%1/%2 标的 · %3 时刻 · 预计 %4 个有效帧")
                .arg(result.readySymbolCount).arg(result.symbolCount)
                .arg(result.timestampCount).arg(result.evaluatedFrameCount));
    }
    emit crossSectionalBacktestStateChanged();
}

void TradingController::handleExternalFactorResearchFinished(
    quint64 requestId,
    const ExternalFactorResearchExecutionResult &result)
{
    if (!m_externalFactorResearchRequestGate.finish(requestId)) return;
    m_externalFactorResearchState.insert(QStringLiteral("running"), false);
    m_externalFactorResearchState.insert(
        QStringLiteral("resolvedRequestId"), requestId);
    m_externalFactorResearchState.insert(
        QStringLiteral("elapsedMilliseconds"), result.elapsedMilliseconds);
    if (result.cancelled) {
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("外部因子研究已取消；未生成归档"));
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), QStringLiteral("用户取消研究"));
    } else if (!result.succeeded) {
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("外部因子研究失败；已有归档和本地数据保持不变"));
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), result.error);
    } else {
        m_selectedExternalFactorResearchArchive = result.archive.toVariantMap();
        m_externalFactorResearchState.insert(
            QStringLiteral("resultId"), result.archive.resultId);
        m_externalFactorResearchState.insert(
            QStringLiteral("error"), QStringLiteral(""));
        m_externalFactorResearchState.insert(
            QStringLiteral("statusText"),
            QStringLiteral("研究完成并归档：%1 帧 · IC %2 · Rank IC %3")
                .arg(result.archive.summary.evaluatedFrameCount)
                .arg(result.archive.summary.meanIc, 0, 'f', 4)
                .arg(result.archive.summary.meanRankIc, 0, 'f', 4));
        refreshExternalFactorResearchArchives();
    }
    emit crossSectionalBacktestStateChanged();
}

void TradingController::startNextTickDataBatchFile()
{
    if (!m_tickDataBatchRunning) return;
    if (m_tickDataBatchCancelRequested) {
        finishTickDataBatchRun(QStringLiteral("cancelled"),
                               QStringLiteral("用户取消；未开始文件可继续"));
        return;
    }
    m_tickDataBatchActiveIndex = -1;
    for (int index = 0; index < m_tickDataBatchPlan.size(); ++index) {
        if (m_tickDataBatchPlan.at(index).status == TickDataBatchItemStatus::Ready) {
            m_tickDataBatchActiveIndex = index;
            break;
        }
    }
    if (m_tickDataBatchActiveIndex < 0) {
        const bool hasProblems = m_tickDataBatchSummary.failedFiles > 0
            || m_tickDataBatchSummary.missingFiles > 0;
        finishTickDataBatchRun(hasProblems ? QStringLiteral("completed_with_errors")
                                           : QStringLiteral("completed"));
        return;
    }

    TickDataBatchPlanItem &item = m_tickDataBatchPlan[m_tickDataBatchActiveIndex];
    item.status = TickDataBatchItemStatus::Running;
    item.error.clear();
    m_tickDataBatchCurrentDate = item.request.tradingDate.toString(Qt::ISODate);
    m_tickDataBatchStatusText = QStringLiteral("正在导入 %1（%2/%3）")
        .arg(m_tickDataBatchCurrentDate)
        .arg(m_tickDataBatchSummary.completedFiles + 1)
        .arg(m_tickDataBatchSummary.totalFiles);
    recomputeTickDataBatchSummary();
    emit tickDataBatchStateChanged();

    m_tickDataBatchActiveRequestId = m_tickDataCsvImportService.start(item.request);
    if (m_tickDataBatchActiveRequestId == 0) {
        item.status = TickDataBatchItemStatus::Ready;
        m_tickDataBatchActiveIndex = -1;
        m_tickDataBatchCurrentDate.clear();
        QTimer::singleShot(10, this, &TradingController::startNextTickDataBatchFile);
    }
}

void TradingController::handleTickDataBatchFileFinished(
    const TickDataImportResult &result)
{
    if (m_tickDataBatchActiveIndex < 0
        || m_tickDataBatchActiveIndex >= m_tickDataBatchPlan.size()) return;
    TickDataBatchPlanItem &item = m_tickDataBatchPlan[m_tickDataBatchActiveIndex];
    item.linesRead = result.linesRead;
    item.validRecords = result.validRecords;
    item.skippedRecords = result.skippedRecords;
    item.missingContinuousBars = result.missingContinuousBars;
    item.qualityStatus = result.qualityStatus;
    item.source = result.source;
    item.writtenBars = 0;
    item.error.clear();

    if (result.cancelled) {
        item.status = TickDataBatchItemStatus::Cancelled;
        item.error = QStringLiteral("用户取消当前文件");
    } else if (!result.succeeded) {
        item.status = TickDataBatchItemStatus::Failed;
        for (const TickDataImportDiagnostic &diagnostic : result.diagnostics) {
            if (diagnostic.fatal) {
                item.error = diagnostic.reason;
                break;
            }
        }
        if (item.error.isEmpty()) item.error = QStringLiteral("文件没有可导入的有效 K 线");
    } else if (!m_dataStore.upsertCandles(result.candles)) {
        item.status = TickDataBatchItemStatus::Failed;
        item.error = QStringLiteral("SQLite 单文件事务写入失败");
    } else {
        item.status = TickDataBatchItemStatus::Succeeded;
        item.writtenBars = result.candles.size();
        m_tickDataBatchAnyWrite = true;
    }

    m_tickDataBatchActiveIndex = -1;
    m_tickDataBatchActiveRequestId = 0;
    recomputeTickDataBatchSummary();
    persistTickDataBatchSummary();
    emit tickDataBatchStateChanged();
    if (m_tickDataBatchCancelRequested) {
        finishTickDataBatchRun(QStringLiteral("cancelled"),
                               QStringLiteral("用户取消；未开始文件可继续"));
    } else {
        QTimer::singleShot(0, this, &TradingController::startNextTickDataBatchFile);
    }
}

void TradingController::recomputeTickDataBatchSummary()
{
    TickDataImportRunSummary &summary = m_tickDataBatchSummary;
    summary.totalFiles = m_tickDataBatchPlan.size();
    summary.completedFiles = 0;
    summary.succeededFiles = 0;
    summary.failedFiles = 0;
    summary.missingFiles = 0;
    summary.cancelledFiles = 0;
    summary.qualityWarningFiles = 0;
    summary.skippedExistingFiles = 0;
    summary.overlapFiles = 0;
    summary.validRecords = 0;
    summary.skippedRecords = 0;
    summary.writtenBars = 0;
    for (const TickDataBatchPlanItem &item : m_tickDataBatchPlan) {
        switch (item.status) {
        case TickDataBatchItemStatus::Missing:
            ++summary.missingFiles;
            ++summary.completedFiles;
            break;
        case TickDataBatchItemStatus::Succeeded:
            ++summary.succeededFiles;
            ++summary.completedFiles;
            break;
        case TickDataBatchItemStatus::Failed:
            ++summary.failedFiles;
            ++summary.completedFiles;
            break;
        case TickDataBatchItemStatus::Cancelled:
            ++summary.cancelledFiles;
            ++summary.completedFiles;
            break;
        case TickDataBatchItemStatus::SkippedExisting:
            ++summary.skippedExistingFiles;
            ++summary.completedFiles;
            break;
        case TickDataBatchItemStatus::Ready:
        case TickDataBatchItemStatus::Running:
            break;
        }
        if (item.qualityStatus == QStringLiteral("warning")) {
            ++summary.qualityWarningFiles;
        }
        if (item.willOverwrite) ++summary.overlapFiles;
        summary.validRecords += item.validRecords;
        summary.skippedRecords += item.skippedRecords;
        summary.writtenBars += item.writtenBars;
    }
}

void TradingController::finishTickDataBatchRun(const QString &status,
                                                const QString &error)
{
    m_tickDataBatchRunning = false;
    m_tickDataBatchCancelRequested = false;
    m_tickDataBatchActiveIndex = -1;
    m_tickDataBatchActiveRequestId = 0;
    m_tickDataBatchCurrentDate.clear();
    m_tickDataBatchSummary.status = status;
    m_tickDataBatchSummary.error = error;
    m_tickDataBatchSummary.completedAt = QDateTime::currentDateTimeUtc();
    m_tickDataBatchError = error;
    recomputeTickDataBatchSummary();
    if (m_tickDataBatchAnyWrite) {
        refreshLocalBacktestDataCatalog();
        m_tickDataBatchAnyWrite = false;
    }
    persistTickDataBatchSummary();
    reloadTickDataImportRunHistory();
    if (status == QStringLiteral("cancelled")) {
        m_tickDataBatchStatusText = QStringLiteral(
            "批量导入已取消：成功 %1，待继续 %2")
            .arg(m_tickDataBatchSummary.succeededFiles)
            .arg(m_tickDataBatchSummary.totalFiles
                 - m_tickDataBatchSummary.completedFiles);
    } else {
        m_tickDataBatchStatusText = QStringLiteral(
            "批量导入结束：成功 %1，跳过完整 %2，失败 %3，缺失 %4，覆盖 %5，质量警告 %6，写入/更新 %7 根")
            .arg(m_tickDataBatchSummary.succeededFiles)
            .arg(m_tickDataBatchSummary.skippedExistingFiles)
            .arg(m_tickDataBatchSummary.failedFiles)
            .arg(m_tickDataBatchSummary.missingFiles)
            .arg(m_tickDataBatchSummary.overlapFiles)
            .arg(m_tickDataBatchSummary.qualityWarningFiles)
            .arg(m_tickDataBatchSummary.writtenBars);
    }
    emit tickDataBatchStateChanged();
}

void TradingController::persistTickDataBatchSummary()
{
    if (!m_storageReady || m_tickDataBatchSummary.runId.isEmpty()) return;
    if (!m_dataStore.upsertTickDataImportRun(
            m_tickDataBatchSummary, m_tickDataBatchPlan)) {
        m_tickDataBatchError = QStringLiteral("批量导入继续执行，但任务摘要未保存");
    }
}

void TradingController::reloadTickDataImportRunHistory()
{
    m_tickDataImportRunHistory.clear();
    if (!m_storageReady) return;
    const QVector<TickDataImportRunSummary> runs = m_dataStore.loadTickDataImportRuns(
        100, m_tickDataImportRunSymbolFilter, m_tickDataImportRunStatusFilter);
    m_tickDataImportRunHistory.reserve(runs.size());
    for (const TickDataImportRunSummary &run : runs) {
        m_tickDataImportRunHistory.append(run.toVariantMap());
    }
}

void TradingController::handleOrderChanged(const Order &order)
{
    m_orderModel.upsertOrder(order);
    if (m_storageReady) m_dataStore.upsertOrder(order);
    scheduleTradingStateSave();
    setStatusText(QStringLiteral("订单 %1：%2").arg(order.orderId, orderStatusName(order.status)));
}

void TradingController::handleExecutionAdded(const Execution &execution)
{
    m_executionModel.addExecution(execution);
    if (m_storageReady) m_dataStore.appendExecution(execution);
    scheduleTradingStateSave();
    QTimer::singleShot(0, this, &TradingController::persistTradingState);
    setStatusText(QStringLiteral("成交：%1 %2 %3 @ %4")
        .arg(execution.instrument.symbol, orderSideName(execution.side))
        .arg(execution.quantity, 0, 'f', 2)
        .arg(execution.price, 0, 'f', 3));
}

void TradingController::handlePositionChanged(const Position &position)
{
    m_positionModel.upsertPosition(position);
    scheduleTradingStateSave();
}

void TradingController::handleAccountChanged(const AccountSnapshot &snapshot)
{
    m_accountSnapshot = snapshot;
    emit accountStateChanged();
    scheduleTradingStateSave();
}

void TradingController::handleTradingError(const QString &message)
{
    m_tradingError = message;
    emit tradingStateChanged();
    setStatusText(QStringLiteral("交易错误：%1").arg(message));
}

void TradingController::scheduleTradingStateSave()
{
    if (!m_storageReady) return;
    m_tradingStateDirty = true;
    if (!m_tradingPersistenceTimer.isActive()) {
        m_tradingPersistenceTimer.start();
    }
}

void TradingController::persistTradingState()
{
    if (!m_tradingStateDirty) return;
    if (m_dataStore.saveTradingState(m_tradingAccount.snapshot(), m_tradingAccount.positions())) {
        m_tradingStateDirty = false;
    }
}

} // namespace pp
