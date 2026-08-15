#include "app/TradingController.h"

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
    m_historyDownloadRouter.addProvider(&m_tushareHistoryFetcher);
    m_historyDownloadRouter.addProvider(&m_baostockHistoryFetcher);
    m_historyDownloadRouter.addProvider(&m_historyDownloadFetcher);
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
    m_realtimeMinuteRefreshTimer.setInterval(60000);
    m_realtimeMinuteRefreshTimer.setSingleShot(true);
    connect(&m_realtimeMinuteRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_liveRunning && !selectedLiveSymbol().isEmpty())
            requestRealtimeMinuteSeries(selectedLiveSymbol());
    });
    connect(&m_asyncSnapshotQueryService, &AsyncMarketSnapshotQueryService::resultReady,
            this, &TradingController::finishSnapshotRequest);
    connect(&m_asyncLocalCandleQueryService,
            &AsyncLocalCandleSeriesQueryService::resultReady,
            this, &TradingController::finishLocalCandleSeriesRequest);
    connect(&m_asyncLiveDayCandleQueryService,
            &AsyncLocalCandleSeriesQueryService::resultReady,
            this, &TradingController::finishLiveDayCandleSeriesRequest);
    connect(&m_backtestDatasetService, &BacktestDatasetService::prepared,
            this, &TradingController::handleBacktestDatasetPrepared);
    connect(&m_backtestDatasetService, &BacktestDatasetService::failed,
            this, &TradingController::handleBacktestDatasetFailed);
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
                m_batchBacktestModel.addResult(result);
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
                constexpr int kMaxRetainedBatchResults = 32;
                if (!m_batchRetainedResults.contains(symbol)) {
                    m_batchResultOrder.removeAll(symbol);
                    m_batchResultOrder.append(symbol);
                    while (m_batchResultOrder.size() > kMaxRetainedBatchResults) {
                        const QString oldest = m_batchResultOrder.takeFirst();
                        m_batchRetainedResults.remove(oldest);
                    }
                }
                m_batchRetainedResults.insert(symbol, result);
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
                m_batchBacktestStatusText = QStringLiteral("批量回测：%1/%2")
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
                    m_batchBacktestStatusText = QStringLiteral("批量回测完成：成功 %1，失败 %2")
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
    connect(&m_strategyEngine, &StrategyEngine::strategySignalGenerated,
            this, &TradingController::handleStrategySignal);
    connect(&m_strategyEngine, &StrategyEngine::strategyCountChanged, this, &TradingController::strategyStateChanged);
    connect(&m_strategyEngine, &StrategyEngine::maxThreadCountChanged, this, &TradingController::strategyStateChanged);
    connect(&m_marketClockTimer, &QTimer::timeout, this, &TradingController::selectedMarketSessionChanged);
    m_customStrategyReplayTimer.setSingleShot(false);
    connect(&m_customStrategyReplayTimer, &QTimer::timeout,
            this, &TradingController::handleCustomStrategyReplayTick);
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
    connect(&m_tickWriter, &AsyncTickWriter::batchWritten, this, [this](int tickCount) {
        m_persistedTickCount += tickCount;
        emit storageStateChanged();
    });
    connect(&m_tickWriter, &AsyncTickWriter::snapshotWritten, this,
            [this](const QString &, int) {
                if (m_followLatestSnapshot) requestLatestMarketSnapshot();
            });
    connect(&m_tickWriter, &AsyncTickWriter::batchDropped, this, [this](int tickCount) {
        const QString error = QStringLiteral("Tick 写入队列已满，丢弃 %1 条行情").arg(tickCount);
        setStatusText(error);
        degradeProvider(error);
    });
    connect(&m_tickWriter, &AsyncTickWriter::writeFailed, this,
            [this](const QString &error) {
                setStatusText(error);
                degradeProvider(error);
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
                if (!m_nasDownloadIndexByRequestId.contains(requestId)) return;
                m_nasDataNodeReceivedBytes = received;
                m_nasDataNodeExpectedBytes = total > 0 ? total : m_nasDataNodeExpectedBytes;
                emit nasDataNodeStateChanged();
            });
    connect(&m_nasDataNodeClient, &NasDataNodeClient::downloadReady,
            this, [this](quint64 requestId, const NasDownloadResult &result) {
                const int syncIndex = m_nasDownloadIndexByRequestId.take(requestId);
                if (syncIndex < 0) return;
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
        recoverBatchBacktestRuns();
        refreshPortfolioBacktestArchiveList();
        if (!m_tickWriter.start(databasePath)) {
            setStatusText(QStringLiteral("后台 Tick 写入服务启动失败，将使用同步写入"));
        }
        if (!m_asyncSnapshotQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步市场快照查询服务启动失败"));
        }
        if (!m_asyncLocalCandleQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步本地分钟序列查询服务启动失败"));
        }
        if (!m_asyncLiveDayCandleQueryService.start(databasePath)) {
            setStatusText(QStringLiteral("异步真实交易日 K 线查询服务启动失败"));
        }
        if (!m_backtestDatasetService.start(databasePath)) {
            setStatusText(QStringLiteral("回测数据集准备服务启动失败"));
        }
        if (!m_portfolioBacktestService.start(databasePath)) {
            setStatusText(QStringLiteral("组合回测服务启动失败"));
        }
        if (!m_crossSectionalBacktestService.start(databasePath)) {
            setStatusText(QStringLiteral("多股回测服务启动失败"));
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
QAbstractItemModel *TradingController::batchBacktestModel() { return &m_batchBacktestModel; }
QAbstractItemModel *TradingController::orderModel() { return &m_orderModel; }
QAbstractItemModel *TradingController::executionModel() { return &m_executionModel; }
QAbstractItemModel *TradingController::positionModel() { return &m_positionModel; }
QAbstractItemModel *TradingController::marketSnapshotModel() { return &m_marketSnapshotModel; }
QAbstractItemModel *TradingController::localCandleSeriesModel() { return &m_localCandleSeriesModel; }
QAbstractItemModel *TradingController::liveDayCandleSeriesModel() { return &m_liveDayCandleSeriesModel; }
QAbstractItemModel *TradingController::realtimeMinuteCandleModel() { return &m_realtimeMinuteCandleModel; }
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

bool TradingController::liveRunning() const { return m_liveRunning; }
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
QString TradingController::backtestConfigurationError() const
{
    return m_backtestConfigurationError;
}
QVariantMap TradingController::backtestResultConfiguration() const
{
    if (!m_displayedBacktestResult.has_value()) return {};
    QVariantMap result = backtestConfigMap(m_displayedBacktestResult->config);
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
bool TradingController::backtestArchiveLoading() const { return m_backtestArchiveLoading; }
QString TradingController::backtestArchiveError() const { return m_backtestArchiveError; }
QString TradingController::backtestArchiveSaveError() const { return m_backtestArchiveSaveError; }
bool TradingController::backtestResultArchived() const { return m_backtestResultArchived; }
bool TradingController::backtestResultHistoricalSnapshot() const
{
    return m_backtestResultHistoricalSnapshot;
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
int TradingController::localCandleSeriesPage() const { return m_localCandleSeriesPage; }
int TradingController::localCandleSeriesPageCount() const { return m_localCandleSeriesPageCount; }
int TradingController::localCandleSeriesPageSize() const { return m_localCandleSeriesPageSize; }
int TradingController::localCandleSeriesTotalBars() const { return m_localCandleSeriesTotalBars; }
int TradingController::localCandleSeriesRawBars() const { return m_localCandleSeriesRawBars; }
int TradingController::localCandleSeriesDisplayPoints() const { return m_localCandleSeriesModel.count(); }
int TradingController::localCandleSeriesBucketSize() const { return m_localCandleSeriesBucketSize; }
bool TradingController::localCandleSeriesDownsampled() const { return m_localCandleSeriesDownsampled; }
QDateTime TradingController::localCandleSeriesFirstTime() const { return m_localCandleSeriesFirstTime; }
QDateTime TradingController::localCandleSeriesLastTime() const { return m_localCandleSeriesLastTime; }
QString TradingController::localCandleSeriesStatusText() const { return m_localCandleSeriesStatusText; }
QString TradingController::localCandleSeriesError() const { return m_localCandleSeriesError; }
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

bool TradingController::configureNasDataNode(const QString &endpoint)
{
    if (m_nasDataNodeLoading || m_nasDataNodeSyncRunning) {
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
    if (m_nasDataNodeLoading || m_nasDataNodeSyncRunning) {
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
        || m_nasDataNodeSyncRunning) {
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
        || m_nasDataNodeSyncRunning) {
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
    if (m_nasDataNodeSyncRunning || m_tickDataImportRunning || m_tickDataBatchRunning
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
    if (m_nasDataNodeSyncRunning || m_tickDataImportRunning || m_tickDataBatchRunning
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
    const quint64 requestId = ++m_nasDownloadRequestCounter;
    if (requestId == 0) ++m_nasDownloadRequestCounter;
    m_nasDownloadIndexByRequestId.insert(requestId, index);
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
        m_nasDownloadIndexByRequestId.remove(requestId);
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
    m_nasDataNodeSyncRunning = false;
    m_nasSyncElapsedMilliseconds = m_nasSyncTimer.isValid()
        ? m_nasSyncTimer.elapsed() : 0;
    m_nasActiveDownloadRequestId = 0;
    m_nasImportRequestId = 0;
    m_nasDownloadIndexByRequestId.clear();
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
}

void TradingController::cancelNasTickSync()
{
    if (!m_nasDataNodeSyncRunning) return;
    const QList<quint64> downloadRequestIds =
        m_nasDownloadIndexByRequestId.keys();
    m_nasActiveDownloadRequestId = 0;
    for (quint64 requestId : downloadRequestIds) {
        m_nasDataNodeClient.cancel(requestId);
    }
    m_nasDownloadIndexByRequestId.clear();
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

void TradingController::refreshNasTickCacheSummary()
{
    if (m_nasDataNodeSyncRunning) {
        m_nasDataNodeError = QStringLiteral("同步运行时不能扫描 NAS 暂存缓存");
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
    if (m_nasDataNodeSyncRunning || m_nasDataNodeLoading) {
        m_nasDataNodeError = QStringLiteral("查询或同步运行时不能清理 NAS 暂存缓存");
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
    if (m_liveRunning || backtestRunning() || batchBacktestRunning()
        || historyDownloadRunning()
        || m_tickDataImportRunning || m_tickDataBatchRunning
        || m_nasDataNodeLoading || m_nasDataNodeSyncRunning
        || m_tickWriter.pendingBatchCount() > 0 || snapshotLoading()
        || localCandleSeriesLoading() || liveDayCandleSeriesLoading()) {
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
    m_tickWriter.stop();
    m_asyncSnapshotQueryService.stop();
    m_asyncLocalCandleQueryService.stop();
    m_asyncLiveDayCandleQueryService.stop();
    m_backtestDatasetService.stop();
    m_portfolioBacktestService.stop();
    m_crossSectionalBacktestService.stop();
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
    m_batchBacktestService.setDatabasePath(databasePath);
    m_persistedTickCount = m_dataStore.tickCount();
    m_persistedSignalCount = m_dataStore.signalCount();
    reloadCustomStrategies();
    refreshLocalBacktestDataCatalog();
    refreshBacktestArchiveList();
    reloadTickDataImportRunHistory();
    const bool tickWriterReady = m_tickWriter.start(databasePath);
    const bool snapshotReady = m_asyncSnapshotQueryService.start(databasePath);
    const bool localSeriesReady = m_asyncLocalCandleQueryService.start(databasePath);
    const bool liveSeriesReady = m_asyncLiveDayCandleQueryService.start(databasePath);
    const bool datasetReady = m_backtestDatasetService.start(databasePath);
    const bool portfolioReady = m_portfolioBacktestService.start(databasePath);
    const bool crossSectionalReady = m_crossSectionalBacktestService.start(databasePath);
    if (!tickWriterReady || !snapshotReady || !localSeriesReady
        || !liveSeriesReady || !datasetReady || !portfolioReady || !crossSectionalReady) {
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
    if (m_liveRunning) return;
    if (m_aShareUniverse.isEmpty() && !reloadAShareUniverse()) {
        degradeProvider(m_aShareUniverseError);
        return;
    }
    m_marketData->start(buildMarketUniverse());
    m_liveRunning = true;
    emit liveRunningChanged();
    requestRealtimeMinuteSeries(selectedLiveSymbol());
    setStatusText(QStringLiteral("实时行情运行中"));
}

void TradingController::stopLive()
{
    if (!m_liveRunning) return;
    m_marketData->stop();
    m_realtimeMinuteRefreshTimer.stop();
    m_realtimeMinuteService.cancel();
    m_realtimeMinuteRequestGate.cancel();
    m_realtimeMinuteStatusText = QStringLiteral("实时分钟行情已停止；内存数据保留到下次选择或退出");
    emit realtimeMinuteStateChanged();
    m_liveRunning = false;
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

    QVariantMap normalizedParameters;
    QString error;
    if (!m_backtestStrategyRegistry.normalizeParameters(
            strategyId, strategyParameters, &normalizedParameters, &error)
        || !validateBacktestConfiguration(
            frequency, barCount, m_backtestConfiguredInitialCapital,
            m_backtestCommissionRate, m_backtestSlippageRate, &error)) {
        m_batchBacktestError = error;
        m_batchBacktestStatusText = QStringLiteral("批量回测参数无效");
        emit batchBacktestStateChanged();
        return 0;
    }

    const int periodSeconds = HistoricalDataService::periodSecondsForFrequency(frequency);
    QVector<LocalHistoryDataset> datasets;
    const int limit = std::clamp(maximumSymbols, 1, 10000);
    for (const LocalHistoryDataset &dataset
         : m_workspaceInstrumentSelections.localHistoryCatalog()) {
        if (dataset.periodSeconds != periodSeconds || !dataset.isBacktestEligible()) continue;
        datasets.append(dataset);
        if (datasets.size() >= limit) break;
    }
    std::sort(datasets.begin(), datasets.end(),
              [](const LocalHistoryDataset &left, const LocalHistoryDataset &right) {
                  return left.instrument.symbol < right.instrument.symbol;
              });
    if (datasets.isEmpty()) {
        m_batchBacktestError = QStringLiteral(
            "本地目录中没有可用于 %1 回测的 Tick 派生 K 线").arg(frequency);
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
    request.barCount = barCount;
    request.initialCapital = m_backtestConfiguredInitialCapital;
    request.commissionRate = m_backtestCommissionRate;
    request.slippageRate = m_backtestSlippageRate;
    request.lotSize = 100;
    request.maximumActiveEngines = 20;

    const QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return launchBatchBacktest(request, runId);
}

qulonglong TradingController::launchBatchBacktest(
    const BatchBacktestRequest &request, const QString &runId)
{
    m_batchBacktestModel.clear();
    m_batchRetainedResults.clear();
    m_batchResultOrder.clear();
    m_batchBacktestRequestedCount = request.datasets.size();
    m_batchBacktestCompletedCount = 0;
    m_batchBacktestSucceededCount = 0;
    m_batchBacktestFailedCount = 0;
    m_batchBacktestError.clear();
    m_batchBacktestStatusText = QStringLiteral("批量回测准备中：0/%1")
                                    .arg(m_batchBacktestRequestedCount);

    m_activeBatchBacktestRun = {};
    if (m_storageReady && !runId.isEmpty()) {
        m_activeBatchBacktestRun.runId = runId;
        m_activeBatchBacktestRun.createdAt = QDateTime::currentDateTimeUtc();
        m_activeBatchBacktestRun.strategyId = request.strategyId;
        m_activeBatchBacktestRun.strategyParameters = request.strategyParameters;
        m_activeBatchBacktestRun.frequency = request.datasets.isEmpty()
            ? QString{} : request.datasets.first().frequency();
        m_activeBatchBacktestRun.periodSeconds = request.datasets.isEmpty()
            ? 0 : request.datasets.first().periodSeconds;
        m_activeBatchBacktestRun.barCount = request.barCount;
        m_activeBatchBacktestRun.initialCapital = request.initialCapital;
        m_activeBatchBacktestRun.commissionRate = request.commissionRate;
        m_activeBatchBacktestRun.slippageRate = request.slippageRate;
        m_activeBatchBacktestRun.lotSize = request.lotSize;
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

QString TradingController::archiveBatchBacktestItem(const QString &symbol)
{
    const QString normalized = symbol.trimmed();
    m_batchBacktestError.clear();
    emit batchBacktestStateChanged();
    if (m_batchBacktestService.isRunning()) {
        m_batchBacktestError = QStringLiteral("请先等待批量回测完成再归档单项结果");
        emit batchBacktestStateChanged();
        return {};
    }
    const auto retained = m_batchRetainedResults.constFind(normalized);
    if (retained == m_batchRetainedResults.constEnd()) {
        m_batchBacktestError = QStringLiteral(
            "结果已释放或不存在：%1；请重新运行批量回测后再归档").arg(normalized);
        emit batchBacktestStateChanged();
        return {};
    }
    if (retained->config.datasetIdentity.isEmpty()) {
        m_batchBacktestError = QStringLiteral("批量结果缺少数据集身份，拒绝归档：%1")
                                   .arg(normalized);
        emit batchBacktestStateChanged();
        return {};
    }

    BacktestArchiveRecord archive;
    archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    archive.completedAt = QDateTime::currentDateTimeUtc();
    archive.result = *retained;
    if (m_storageReady && m_dataStore.saveBacktestArchive(archive)) {
        m_batchBacktestModel.updateArchiveState(normalized, archive.resultId);
        refreshBacktestArchiveList();
        setStatusText(QStringLiteral("批量单项已归档：%1 · %2")
                          .arg(normalized, archive.resultId));
        emit batchBacktestStateChanged();
        return archive.resultId;
    }
    m_batchBacktestError = QStringLiteral("归档写入失败：%1").arg(normalized);
    emit batchBacktestStateChanged();
    return {};
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

    QVector<LocalHistoryDataset> datasets;
    const QVector<BatchBacktestRunItem> items =
        m_dataStore.loadBatchBacktestRunItems(normalized);
    for (const BatchBacktestRunItem &item : items) {
        if (item.status == QStringLiteral("succeeded")) continue;
        for (const LocalHistoryDataset &candidate
             : m_workspaceInstrumentSelections.localHistoryCatalog()) {
            if (candidate.instrument.symbol == item.instrument.symbol
                && candidate.periodSeconds == source->periodSeconds
                && candidate.isBacktestEligible()
                && candidate.candleCount >= source->barCount) {
                datasets.append(candidate);
                break;
            }
        }
    }
    if (datasets.isEmpty()) {
        m_batchBacktestError = QStringLiteral(
            "没有可恢复的本地 Tick 派生数据集；请先同步或导入数据");
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
    request.barCount = source->barCount;
    request.initialCapital = source->initialCapital;
    request.commissionRate = source->commissionRate;
    request.slippageRate = source->slippageRate;
    request.lotSize = source->lotSize;
    request.maximumActiveEngines = 20;

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
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        strategyJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_crossSectionalBacktestError = QStringLiteral("策略 JSON 无效");
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    CustomStrategyDefinition definition;
    QString validationError;
    if (!m_customStrategyValidator.validate(document.object(), &definition,
                                            &validationError)) {
        m_crossSectionalBacktestError = validationError;
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }
    if (barCount < 2) {
        m_crossSectionalBacktestError = QStringLiteral("回测根数必须大于 1");
        m_crossSectionalBacktestStatusText = QStringLiteral("多股回测未启动");
        emit crossSectionalBacktestStateChanged();
        return 0;
    }

    PortfolioDefinition portfolioDefinition;
    portfolioDefinition.portfolioId = QStringLiteral("cross_sectional_ui");
    portfolioDefinition.name = definition.name;
    portfolioDefinition.periodSeconds = selectedBacktestPeriodSeconds() > 0
        ? selectedBacktestPeriodSeconds() : 60;
    portfolioDefinition.initialCapital = m_backtestConfiguredInitialCapital;
    portfolioDefinition.commissionRate = m_backtestCommissionRate;
    portfolioDefinition.slippageRate = m_backtestSlippageRate;
    portfolioDefinition.lotSize = 100;
    portfolioDefinition.tPlusOne = true;

    ++m_crossSectionalBacktestRequestId;
    if (m_crossSectionalBacktestRequestId == 0) ++m_crossSectionalBacktestRequestId;
    m_displayedCrossSectionalResult.reset();
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
    m_crossSectionalEquityCurveModel.loadPoints(portfolioEquityPoints(result));
    m_crossSectionalTradeModel.loadTrades(result.trades);
    m_crossSectionalBacktestError.clear();
    m_crossSectionalBacktestStatusText = QStringLiteral(
        "多股回测完成：收益 %1%，%2 笔调仓")
        .arg(result.summary.totalReturnPercent, 0, 'f', 2)
        .arg(result.summary.totalTrades);
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
    m_portfolioArchiveModel.loadSummaries(m_storageReady
        ? m_dataStore.loadPortfolioBacktestArchiveSummaries(50)
        : QVector<PortfolioArchiveSummary>{});
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
               QStringLiteral("observed_union"));
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

QVariantMap TradingController::crossSectionalResultConfiguration() const
{
    QVariantMap map;
    if (!m_displayedCrossSectionalResult) return map;
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

void TradingController::saveValidationRun(const QString &name)
{
    if (!m_displayedBacktestResult) return;
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
    m_validationRuns.append(run);
    std::sort(m_validationRuns.begin(), m_validationRuns.end(),
              [](const ValidationRun &left, const ValidationRun &right) {
        return left.totalReturnPercent > right.totalReturnPercent;
    });
    while (m_validationRuns.size() > 10) {
        m_validationRuns.removeLast();
    }
    if (m_storageReady) {
        m_dataStore.saveValidationRun(run);
    }
    emit validationRunsChanged();
}

bool TradingController::loadValidationRun(int index)
{
    if (index < 0 || index >= m_validationRuns.size()) return false;
    const ValidationRun &run = m_validationRuns.at(index);
    m_pendingSampleFrom = run.sampleFrom;
    m_pendingSampleTo = run.sampleTo;
    emit validationRunsChanged();
    return true;
}

void TradingController::setBacktestSampleWindow(const QDateTime &from, const QDateTime &to)
{
    m_pendingSampleFrom = from;
    m_pendingSampleTo = to;
}

void TradingController::clearBacktestSampleWindow()
{
    m_pendingSampleFrom = {};
    m_pendingSampleTo = {};
}

void TradingController::clearBacktestResult()
{
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
        || m_backtestPreparing) {
        m_backtestArchiveError = m_storageReady
            ? QStringLiteral("回测运行或准备期间不能加载归档")
            : QStringLiteral("本地数据库尚未就绪");
        emit backtestArchiveStateChanged();
        return false;
    }
    m_backtestArchiveLoading = true;
    m_backtestArchiveError.clear();
    emit backtestArchiveStateChanged();
    const std::optional<BacktestArchiveRecord> record = m_dataStore.loadBacktestArchive(resultId);
    m_backtestArchiveLoading = false;
    if (!record.has_value()) {
        m_backtestArchiveError = QStringLiteral("归档不存在、已损坏或无法读取");
        emit backtestArchiveStateChanged();
        return false;
    }

    m_displayedBacktestResult = record->result;
    m_displayedBacktestCompletedAt = record->completedAt;
    m_backtestSummary = record->result.summary;
    m_backtestTradeModel.loadTrades(record->result.trades);
    m_equityCurveModel.loadPoints(record->result.equityCurve);
    bool taskIdOk = false;
    const qulonglong restoredTaskId = record->result.config.taskId.toULongLong(&taskIdOk);
    m_completedBacktestTaskId = taskIdOk && restoredTaskId > 0 ? restoredTaskId : 1;
    m_selectedBacktestArchiveId = record->resultId;
    m_backtestResultArchived = true;
    m_backtestResultHistoricalSnapshot = true;
    m_backtestArchiveSaveError.clear();
    setBacktestStatus(QStringLiteral("已加载归档回测"));
    setStatusText(QStringLiteral("已从 SQLite 恢复回测：%1 · %2")
                      .arg(record->result.config.instrument.symbol,
                           record->result.config.strategyId));
    emit backtestSummaryChanged();
    emit backtestArchiveStateChanged();
    emit backtestStateChanged();
    return true;
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
        m_realtimeMinuteStatusText = m_liveRunning
            ? QStringLiteral("正在切换实时 1 分钟行情…")
            : QStringLiteral("已选择实时品种；启动行情后读取 1 分钟数据");
        emit realtimeMinuteStateChanged();
        if (m_liveRunning) requestRealtimeMinuteSeries(selectedLiveSymbol());
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

qulonglong TradingController::requestRealtimeMinuteSeries(const QString &symbol)
{
    const quint64 requestId = m_realtimeMinuteRequestGate.begin();
    const QString requestedSymbol = symbol.trimmed().isEmpty()
        ? selectedLiveSymbol() : symbol.trimmed();
    const Instrument instrument = findInstrument(requestedSymbol);
    if (m_realtimeMinuteSymbol != requestedSymbol) {
        m_realtimeMinuteCandleModel.clear();
        m_realtimeMinuteLastBarTime = {};
    }
    m_realtimeMinuteSymbol = requestedSymbol;
    m_realtimeMinuteError.clear();
    m_realtimeMinuteStale = true;
    m_realtimeMinuteStatusText = QStringLiteral(
        "正在读取选中品种的腾讯 1 分钟行情；仅保存在内存中…");
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
    if (!m_localCandleSeriesRequestGate.finish(requestId)) return;
    m_localCandleSeriesSymbol = result.query.instrument.symbol;
    m_localCandleSeriesFrequency = periodText(result.query.periodSeconds);
    m_localCandleSeriesPage = result.query.page;
    m_localCandleSeriesPageSize = result.query.pageSize;
    m_localCandleSeriesPageCount = result.pageCount;
    m_localCandleSeriesTotalBars = result.totalBars;
    m_localCandleSeriesRawBars = result.rawBarCount;
    m_localCandleSeriesBucketSize = result.bucketSize;
    m_localCandleSeriesDownsampled = result.downsampled;
    m_localCandleSeriesError = result.error;
    if (result.succeeded) {
        m_localCandleSeriesModel.setPoints(result.points);
        m_localCandleSeriesFirstTime = result.points.isEmpty()
            ? QDateTime{} : result.points.first().candle.startTime;
        m_localCandleSeriesLastTime = result.points.isEmpty()
            ? QDateTime{} : result.points.last().endTime;
        if (result.points.isEmpty()) {
            m_localCandleSeriesStatusText = QStringLiteral("该本地分钟数据集为空");
        } else {
            const QString cacheText = result.regeneratedCacheDays > 0
                ? QStringLiteral(" · 重建 %1 个交易日缓存").arg(result.regeneratedCacheDays)
                : QString{};
            m_localCandleSeriesStatusText = (result.downsampled
                ? QStringLiteral("已加载 %1 根原始 K 线，按每桶最多 %2 根聚合为 %3 点")
                      .arg(result.rawBarCount).arg(result.bucketSize).arg(result.points.size())
                : QStringLiteral("已加载 %1 根本地 K 线").arg(result.rawBarCount)) + cacheText;
        }
    } else {
        m_localCandleSeriesModel.clear();
        m_localCandleSeriesFirstTime = {};
        m_localCandleSeriesLastTime = {};
        m_localCandleSeriesStatusText = QStringLiteral("本地分钟序列查询失败");
    }
    emit localCandleSeriesStateChanged();
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
    m_realtimeMinuteSymbol = result.instrument.symbol;
    m_realtimeMinuteSource = result.providerId;
    m_realtimeMinuteError = result.error;
    m_realtimeMinuteLastBarTime = result.lastBarTime;
    const qint64 ageSeconds = result.lastBarTime.isValid()
        ? result.lastBarTime.secsTo(QDateTime::currentDateTimeUtc())
        : std::numeric_limits<qint64>::max();
    m_realtimeMinuteStale = ageSeconds > 120;
    if (result.succeeded) {
        QVector<LocalCandleSeriesPoint> points;
        points.reserve(result.candles.size());
        for (const Candle &candle : result.candles) {
            LocalCandleSeriesPoint point;
            point.candle = candle;
            point.endTime = candle.startTime.addSecs(candle.periodSeconds);
            point.sourceCount = 1;
            points.append(point);
        }
        m_realtimeMinuteCandleModel.setPoints(points);
        m_realtimeMinuteStatusText = QStringLiteral(
            "%1 · 腾讯 1 分钟 · %2 根 · %3 · 仅内存，不进入历史回测库")
            .arg(result.instrument.symbol)
            .arg(points.size())
            .arg(m_realtimeMinuteStale ? QStringLiteral("休市或行情陈旧")
                                       : QStringLiteral("行情已更新"));
    } else {
        m_realtimeMinuteCandleModel.clear();
        m_realtimeMinuteStatusText = QStringLiteral("实时 1 分钟行情不可用");
    }
    emit realtimeMinuteStateChanged();
    if (m_liveRunning) m_realtimeMinuteRefreshTimer.start();
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
    return submitSnapshotRequest(requestId, requestedTime);
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
    if (!m_liveRunning) return;
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
QVector<Instrument> TradingController::buildMarketUniverse() const { return m_aShareUniverse; }

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
    if (m_liveRunning) restartLiveIfRunning();
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
    const bool processActive = health == static_cast<int>(ProviderHealthState::Starting)
        || health == static_cast<int>(ProviderHealthState::Healthy)
        || health == static_cast<int>(ProviderHealthState::Degraded);
    if (m_liveRunning != processActive) {
        m_liveRunning = processActive;
        emit liveRunningChanged();
    }
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
    emit providerStateChanged();
}

void TradingController::degradeProvider(const QString &error)
{
    m_providerHealth = static_cast<int>(ProviderHealthState::Degraded);
    m_providerMessage = QStringLiteral("行情批次未能稳定落库");
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
    if (tick.batchId.isEmpty()) m_tickModel.addTick(tick);
    m_latestTicks.insert(tick.instrument.symbol, tick);
    m_tradingAccount.updateMarketPrice(tick);
    m_strategyEngine.dispatchTick(tick);
    if (m_storageReady && tick.batchId.isEmpty() && m_dataStore.appendTick(tick)) {
        ++m_persistedTickCount;
        emit storageStateChanged();
    }
    if (tick.instrument.symbol == m_workspaceInstrumentSelections.researchInstrument().symbol) {
        emit selectedTickPoint(tick.timestamp, tick.price, tick.instrument.symbol);
    }
}

void TradingController::handleMarketSnapshot(const MarketSnapshot &snapshot)
{
    QVector<Tick> displayTicks;
    displayTicks.reserve(std::min(500, static_cast<int>(snapshot.quotes.size())));
    const auto appendDisplayTick = [&displayTicks, &snapshot](const CrossSectionalQuote &quote) {
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
        displayTicks.append(tick);
    };
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        if (quote.instrument.symbol == m_workspaceInstrumentSelections.researchInstrument().symbol) {
            appendDisplayTick(quote);
            break;
        }
    }
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        if (displayTicks.size() >= 500) break;
        if (quote.instrument.symbol
            != m_workspaceInstrumentSelections.researchInstrument().symbol) {
            appendDisplayTick(quote);
        }
    }
    m_tickModel.loadTicks(displayTicks);

    bool persistenceAccepted = false;
    if (m_storageReady && m_tickWriter.isRunning()) {
        persistenceAccepted = m_tickWriter.enqueue(snapshot);
    } else if (m_storageReady) {
        persistenceAccepted = m_dataStore.appendMarketSnapshot(snapshot);
        if (persistenceAccepted) {
            m_persistedTickCount += m_dataStore.lastInsertedTickCount();
            emit storageStateChanged();
            if (m_followLatestSnapshot) requestLatestMarketSnapshot();
        }
    }
    if (!persistenceAccepted) {
        degradeProvider(m_storageReady
            ? QStringLiteral("全市场快照未进入持久化队列")
            : QStringLiteral("市场快照数据库不可用"));
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
        refreshLocalBacktestDataCatalog();
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
