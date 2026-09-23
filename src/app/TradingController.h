#pragma once

#include "app/WorkspaceInstrumentSelectionState.h"
#include "models/BacktestArchiveListModel.h"
#include "models/BacktestTradeListModel.h"
#include "models/BatchBacktestResultListModel.h"
#include "models/CandleListModel.h"
#include "models/CustomStrategyListModel.h"
#include "models/EquityCurveListModel.h"
#include "models/FactorResearchComparisonListModel.h"
#include "models/InstrumentListModel.h"
#include "models/LocalCandleSeriesListModel.h"
#include "models/LocalTickSeriesListModel.h"
#include "models/MarketSnapshotListModel.h"
#include "models/ExecutionListModel.h"
#include "models/OrderListModel.h"
#include "models/PositionListModel.h"
#include "models/PortfolioArchiveListModel.h"
#include "models/PortfolioTradeListModel.h"
#include "models/RealtimeChartWindowListModel.h"
#include "models/SignalListModel.h"
#include "models/StockSearchModel.h"
#include "models/StrategyListModel.h"
#include "models/StrategyIntentListModel.h"
#include "models/TickListModel.h"
#include "services/BacktestEngine.h"
#include "services/BatchBacktestResultRetention.h"
#include "services/BatchBacktestService.h"
#include "services/AShareRelativeStrengthIndicator.h"
#include "services/AShareUniverseLoader.h"
#include "services/AShareInstrumentDayCsvImporter.h"
#include "services/AShareIndustryCsvImporter.h"
#include "services/AuthorizedFactorCsvImporter.h"
#include "services/ExternalFactorResearchService.h"
#include "services/AsyncMarketSnapshotQueryService.h"
#include "services/AsyncLocalCandleSeriesQueryService.h"
#include "services/AsyncLocalTickSeriesQueryService.h"
#include "services/AsyncCustomStrategyWorkspaceService.h"
#include "services/AsyncBacktestArchiveService.h"
#include "services/AsyncRawTickAvailabilityService.h"
#include "services/AsyncFactorResearchReportService.h"
#include "services/BacktestDatasetService.h"
#include "services/NasDataNodeClient.h"
#include "services/NasSyncStateStore.h"
#include "services/NasTickCacheManager.h"
#include "services/NasTickSyncQueue.h"
#include "services/PortfolioBacktestService.h"
#include "services/CrossSectionalBacktestService.h"
#include "services/CrossSectionalIndicatorEngine.h"
#include "services/HistoricalDataService.h"
#include "services/HistoricalDataProviderRouter.h"
#include "services/KlineDataFetcher.h"
#include "services/MarketDataProvider.h"
#include "services/MarketSnapshotQueryService.h"
#include "services/SimulatedBrokerGateway.h"
#include "services/SqliteLocalDataStore.h"
#include "services/TradingAccountService.h"
#include "services/PaperStrategyBook.h"
#include "services/TickDataCsvImporter.h"
#include "services/TickDataBatchPlanner.h"
#include "services/TencentMinuteBarService.h"
#include "services/DesktopNotificationService.h"
#include "services/NotificationDispatcher.h"
#include "services/WebhookNotificationService.h"
#include "strategy/DefaultSignalExecutionPolicy.h"
#include "strategy/CustomStrategyValidator.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomExecutionStrategy.h"
#include "strategy/CustomStrategyRuntimeManager.h"
#include "strategy/FixedAmountPositionSizer.h"
#include "plugin/StrategyPluginManager.h"
#include "services/ParameterGridBacktestService.h"
#include "services/RawTickParameterSensitivityService.h"
#include "services/LogService.h"
#include "strategy/StrategyEngine.h"
#include "strategy/StrategyExecutionCoordinator.h"

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace pp {

class TradingController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList paperStrategyRuns READ paperStrategyRuns NOTIFY paperStrategyStateChanged)
    Q_PROPERTY(QString paperStrategyError READ paperStrategyError NOTIFY paperStrategyStateChanged)
    Q_PROPERTY(QAbstractItemModel *tickModel READ tickModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *candleModel READ candleModel CONSTANT)
    Q_PROPERTY(QVariantList validationRuns READ validationRuns NOTIFY validationRunsChanged)
    Q_PROPERTY(QAbstractItemModel *historyCandleModel READ historyCandleModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *signalModel READ signalModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *strategyModel READ strategyModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *strategyIntentModel READ strategyIntentModel CONSTANT)
    Q_PROPERTY(bool customStrategyReplayRunning READ customStrategyReplayRunning
               NOTIFY customStrategyReplayStateChanged)
    Q_PROPERTY(int customStrategyReplayedBars READ customStrategyReplayedBars
               NOTIFY customStrategyReplayStateChanged)
    Q_PROPERTY(int customStrategyReplayTotalBars READ customStrategyReplayTotalBars
               NOTIFY customStrategyReplayStateChanged)
    Q_PROPERTY(QVariantList customStrategyExecutionCatalog READ customStrategyExecutionCatalog
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(QVariantList loadedStrategyPlugins READ loadedStrategyPlugins
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(QString strategyPluginDirectory READ strategyPluginDirectory
               NOTIFY strategyPluginStateChanged)
    Q_PROPERTY(bool parameterGridRunning READ parameterGridRunning NOTIFY parameterGridStateChanged)
    Q_PROPERTY(QString parameterGridError READ parameterGridError NOTIFY parameterGridStateChanged)
    Q_PROPERTY(QVariantList parameterGridResults READ parameterGridResults NOTIFY parameterGridStateChanged)
    Q_PROPERTY(bool rawTickSensitivityRunning READ rawTickSensitivityRunning
               NOTIFY rawTickSensitivityStateChanged)
    Q_PROPERTY(qulonglong rawTickSensitivityRequestId READ rawTickSensitivityRequestId
               NOTIFY rawTickSensitivityStateChanged)
    Q_PROPERTY(QString rawTickSensitivityError READ rawTickSensitivityError
               NOTIFY rawTickSensitivityStateChanged)
    Q_PROPERTY(QVariantMap rawTickSensitivityResult READ rawTickSensitivityResult
               NOTIFY rawTickSensitivityStateChanged)
    Q_PROPERTY(QVariantList rawTickSensitivityHistory READ rawTickSensitivityHistory
               NOTIFY rawTickSensitivityStateChanged)
    Q_PROPERTY(QAbstractItemModel *customStrategyModel READ customStrategyModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *searchModel READ searchModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *instrumentModel READ instrumentModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *backtestTradeModel READ backtestTradeModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *equityCurveModel READ equityCurveModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *backtestArchiveModel READ backtestArchiveModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *backtestArchiveRawTickModel
               READ backtestArchiveRawTickModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *batchBacktestModel READ batchBacktestModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *orderModel READ orderModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *executionModel READ executionModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *positionModel READ positionModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *marketSnapshotModel READ marketSnapshotModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *localCandleSeriesModel READ localCandleSeriesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *localTickSeriesModel READ localTickSeriesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *liveDayCandleSeriesModel READ liveDayCandleSeriesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *realtimeMinuteCandleModel READ realtimeMinuteCandleModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *realtimeChartWindowModel READ realtimeChartWindowModel CONSTANT)
    Q_PROPERTY(int realtimeChartWindowCount READ realtimeChartWindowCount
               NOTIFY realtimeChartWindowsChanged)
    Q_PROPERTY(QString realtimeChartWindowError READ realtimeChartWindowError
               NOTIFY realtimeChartWindowsChanged)
    Q_PROPERTY(QVariantList backtestStrategyCatalog READ backtestStrategyCatalog CONSTANT)
    Q_PROPERTY(QVariantMap customStrategySchema READ customStrategySchema CONSTANT)
    Q_PROPERTY(int customStrategyCount READ customStrategyCount NOTIFY customStrategyStateChanged)
    Q_PROPERTY(QString customStrategyError READ customStrategyError NOTIFY customStrategyStateChanged)
    Q_PROPERTY(bool customStrategyPreviewLoading READ customStrategyPreviewLoading NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(qulonglong customStrategyPreviewRequestId READ customStrategyPreviewRequestId NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(qulonglong customStrategyPreviewResolvedRequestId READ customStrategyPreviewResolvedRequestId NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(QVariantMap customStrategyPreviewResult READ customStrategyPreviewResult NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(QString customStrategyPreviewError READ customStrategyPreviewError NOTIFY customStrategyPreviewStateChanged)
    Q_PROPERTY(bool customStrategySaveLoading READ customStrategySaveLoading NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(qulonglong customStrategySaveRequestId READ customStrategySaveRequestId NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(qulonglong customStrategySaveResolvedRequestId READ customStrategySaveResolvedRequestId NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(QVariantMap customStrategySaveResult READ customStrategySaveResult NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(QString customStrategySaveError READ customStrategySaveError NOTIFY customStrategySaveStateChanged)
    Q_PROPERTY(bool liveRunning READ liveRunning NOTIFY liveRunningChanged)
    Q_PROPERTY(bool backtestRunning READ backtestRunning NOTIFY backtestRunningChanged)
    Q_PROPERTY(double backtestProgress READ backtestProgress NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestStatusText READ backtestStatusText NOTIFY backtestStateChanged)
    Q_PROPERTY(QString backtestError READ backtestError NOTIFY backtestStateChanged)
    Q_PROPERTY(double backtestInitialCapital READ backtestInitialCapital NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double finalEquity READ finalEquity NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double totalReturnPercent READ totalReturnPercent NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double maxDrawdownPercent READ maxDrawdownPercent NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double winRatePercent READ winRatePercent NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double sharpeRatio READ sharpeRatio NOTIFY backtestSummaryChanged)
    Q_PROPERTY(double totalCommission READ totalCommission NOTIFY backtestSummaryChanged)
    Q_PROPERTY(int backtestTradeCount READ backtestTradeCount NOTIFY backtestSummaryChanged)
    Q_PROPERTY(QString selectedBacktestStrategyId READ selectedBacktestStrategyId
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(QVariantMap selectedBacktestStrategyDescriptor
               READ selectedBacktestStrategyDescriptor NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(QVariantMap selectedBacktestParameters READ selectedBacktestParameters
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(QString backtestFrequency READ backtestFrequency
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(int backtestBarCount READ backtestBarCount
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(double backtestConfiguredInitialCapital READ backtestConfiguredInitialCapital
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(double backtestCommissionRate READ backtestCommissionRate
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(double backtestSlippageRate READ backtestSlippageRate
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(QString backtestSampleFromDate READ backtestSampleFromDate
               NOTIFY backtestSampleWindowChanged)
    Q_PROPERTY(QString backtestSampleToDate READ backtestSampleToDate
               NOTIFY backtestSampleWindowChanged)
    Q_PROPERTY(QString backtestConfigurationError READ backtestConfigurationError
               NOTIFY backtestConfigurationChanged)
    Q_PROPERTY(QVariantMap backtestResultConfiguration READ backtestResultConfiguration
               NOTIFY backtestSummaryChanged)
    Q_PROPERTY(qulonglong backtestTaskId READ backtestTaskId NOTIFY backtestStateChanged)
    Q_PROPERTY(qulonglong completedBacktestTaskId READ completedBacktestTaskId
               NOTIFY backtestSummaryChanged)
    Q_PROPERTY(QString backtestResultStrategyId READ backtestResultStrategyId
               NOTIFY backtestSummaryChanged)
    Q_PROPERTY(QString backtestResultSymbol READ backtestResultSymbol
               NOTIFY backtestSummaryChanged)
    Q_PROPERTY(QDateTime backtestResultCompletedAt READ backtestResultCompletedAt
               NOTIFY backtestSummaryChanged)
    Q_PROPERTY(int backtestArchiveCount READ backtestArchiveCount NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(QString selectedBacktestArchiveId READ selectedBacktestArchiveId
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestArchiveLoading READ backtestArchiveLoading
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(qulonglong backtestArchiveRequestId READ backtestArchiveRequestId
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(qulonglong backtestArchiveResolvedRequestId
               READ backtestArchiveResolvedRequestId
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(QString backtestArchiveError READ backtestArchiveError
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(QString backtestArchiveSaveError READ backtestArchiveSaveError
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestResultArchived READ backtestResultArchived
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestResultHistoricalSnapshot READ backtestResultHistoricalSnapshot
               NOTIFY backtestArchiveStateChanged)
    Q_PROPERTY(bool backtestArchiveRawTickPageLoading
               READ backtestArchiveRawTickPageLoading
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(qulonglong backtestArchiveRawTickPageRequestId
               READ backtestArchiveRawTickPageRequestId
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(qulonglong backtestArchiveRawTickPageResolvedRequestId
               READ backtestArchiveRawTickPageResolvedRequestId
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(QString backtestArchiveRawTickPageResultId
               READ backtestArchiveRawTickPageResultId
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(int backtestArchiveRawTickPageOffset
               READ backtestArchiveRawTickPageOffset
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(int backtestArchiveRawTickPageSize
               READ backtestArchiveRawTickPageSize
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(int backtestArchiveRawTickTotalCount
               READ backtestArchiveRawTickTotalCount
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(bool backtestArchiveRawTickHasPrevious
               READ backtestArchiveRawTickHasPrevious
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(bool backtestArchiveRawTickHasNext
               READ backtestArchiveRawTickHasNext
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(QString backtestArchiveRawTickPageError
               READ backtestArchiveRawTickPageError
               NOTIFY backtestArchiveRawTickPageStateChanged)
    Q_PROPERTY(bool batchBacktestRunning READ batchBacktestRunning
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(qulonglong batchBacktestRequestId READ batchBacktestRequestId
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(int batchBacktestRequestedCount READ batchBacktestRequestedCount
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(int batchBacktestCompletedCount READ batchBacktestCompletedCount
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(int batchBacktestSucceededCount READ batchBacktestSucceededCount
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(int batchBacktestFailedCount READ batchBacktestFailedCount
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(double batchBacktestProgress READ batchBacktestProgress
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QString batchBacktestStatusText READ batchBacktestStatusText
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QString batchBacktestError READ batchBacktestError
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(bool batchBacktestArchiveLoading READ batchBacktestArchiveLoading
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(qulonglong batchBacktestArchiveRequestId
               READ batchBacktestArchiveRequestId NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(qulonglong batchBacktestArchiveResolvedRequestId
               READ batchBacktestArchiveResolvedRequestId NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QString batchBacktestArchiveSymbol READ batchBacktestArchiveSymbol
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QString batchBacktestArchiveResultId READ batchBacktestArchiveResultId
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QString batchBacktestArchiveError READ batchBacktestArchiveError
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(QVariantList batchBacktestRunHistory READ batchBacktestRunHistory
               NOTIFY batchBacktestStateChanged)
    Q_PROPERTY(bool rawTickAvailabilityLoading READ rawTickAvailabilityLoading
               NOTIFY rawTickAvailabilityStateChanged)
    Q_PROPERTY(qulonglong rawTickAvailabilityRequestId
               READ rawTickAvailabilityRequestId
               NOTIFY rawTickAvailabilityStateChanged)
    Q_PROPERTY(qulonglong rawTickAvailabilityResolvedRequestId
               READ rawTickAvailabilityResolvedRequestId
               NOTIFY rawTickAvailabilityStateChanged)
    Q_PROPERTY(QVariantMap rawTickAvailabilityResult
               READ rawTickAvailabilityResult
               NOTIFY rawTickAvailabilityStateChanged)
    Q_PROPERTY(QString rawTickAvailabilityError READ rawTickAvailabilityError
               NOTIFY rawTickAvailabilityStateChanged)
    Q_PROPERTY(bool portfolioBacktestRunning READ portfolioBacktestRunning
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(QString portfolioBacktestStatusText READ portfolioBacktestStatusText
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(QString portfolioBacktestError READ portfolioBacktestError
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(QVariantMap portfolioResultConfiguration
               READ portfolioResultConfiguration
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(QAbstractItemModel *portfolioEquityCurveModel
               READ portfolioEquityCurveModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *portfolioTradeModel
               READ portfolioTradeModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *portfolioArchiveModel
               READ portfolioArchiveModel CONSTANT)
    Q_PROPERTY(int portfolioArchiveCount READ portfolioArchiveCount
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(bool portfolioArchiveLoading READ portfolioArchiveLoading
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(QString portfolioArchiveError READ portfolioArchiveError
               NOTIFY portfolioBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalBacktestRunning READ crossSectionalBacktestRunning
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalBacktestStatusText READ crossSectionalBacktestStatusText
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalBacktestError READ crossSectionalBacktestError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap crossSectionalRiskConfiguration
               READ crossSectionalRiskConfiguration
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalRiskConfigurationError
               READ crossSectionalRiskConfigurationError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList crossSectionalFactorDefinitionCatalog
               READ crossSectionalFactorDefinitionCatalog
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap crossSectionalResultConfiguration
               READ crossSectionalResultConfiguration
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QAbstractItemModel *crossSectionalEquityCurveModel
               READ crossSectionalEquityCurveModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *crossSectionalTradeModel
               READ crossSectionalTradeModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *crossSectionalArchiveModel
               READ crossSectionalArchiveModel CONSTANT)
    Q_PROPERTY(int crossSectionalArchiveCount READ crossSectionalArchiveCount
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalArchiveLoading READ crossSectionalArchiveLoading
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalArchiveError READ crossSectionalArchiveError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QAbstractItemModel *crossSectionalFactorComparisonModel
               READ crossSectionalFactorComparisonModel CONSTANT)
    Q_PROPERTY(int crossSectionalFactorComparisonCount
               READ crossSectionalFactorComparisonCount
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(int crossSectionalMultipleTestingCandidateCount
               READ crossSectionalMultipleTestingCandidateCount
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalMultipleTestingCandidateSetSha256
               READ crossSectionalMultipleTestingCandidateSetSha256
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalMultipleTestingStatusText
               READ crossSectionalMultipleTestingStatusText
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalFactorComparisonError
               READ crossSectionalFactorComparisonError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList factorCandidateCatalog READ factorCandidateCatalog
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList factorCandidateEvaluationCatalog
               READ factorCandidateEvaluationCatalog
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString selectedFactorCandidateId READ selectedFactorCandidateId
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(int selectedFactorCandidateVersion READ selectedFactorCandidateVersion
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString factorCandidateGovernanceStatusText
               READ factorCandidateGovernanceStatusText
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString factorCandidateGovernanceError
               READ factorCandidateGovernanceError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap authorizedFactorImportState
               READ authorizedFactorImportState
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList externalFactorDatasetCatalog
               READ externalFactorDatasetCatalog
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList selectedFactorCandidateDatasetBindings
               READ selectedFactorCandidateDatasetBindings
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap externalFactorResearchState
               READ externalFactorResearchState
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap externalFactorResearchPreflightState
               READ externalFactorResearchPreflightState
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantList externalFactorResearchArchiveCatalog
               READ externalFactorResearchArchiveCatalog
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QVariantMap selectedExternalFactorResearchArchive
               READ selectedExternalFactorResearchArchive
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString selectedCrossSectionalArchiveId
               READ selectedCrossSectionalArchiveId
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalResultHistoricalSnapshot
               READ crossSectionalResultHistoricalSnapshot
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(bool crossSectionalReportExporting
               READ crossSectionalReportExporting
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(qulonglong crossSectionalReportExportRequestId
               READ crossSectionalReportExportRequestId
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportStatusText
               READ crossSectionalReportExportStatusText
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportError
               READ crossSectionalReportExportError
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString crossSectionalReportExportPath
               READ crossSectionalReportExportPath
               NOTIFY crossSectionalBacktestStateChanged)
    Q_PROPERTY(QString brokerName READ brokerName CONSTANT)
    Q_PROPERTY(bool brokerLoggedIn READ brokerLoggedIn NOTIFY brokerStateChanged)
    Q_PROPERTY(QString tradingError READ tradingError NOTIFY tradingStateChanged)
    Q_PROPERTY(double accountInitialEquity READ accountInitialEquity NOTIFY accountStateChanged)
    Q_PROPERTY(double accountCash READ accountCash NOTIFY accountStateChanged)
    Q_PROPERTY(double accountAvailableCash READ accountAvailableCash NOTIFY accountStateChanged)
    Q_PROPERTY(double accountFrozenCash READ accountFrozenCash NOTIFY accountStateChanged)
    Q_PROPERTY(double accountMarketValue READ accountMarketValue NOTIFY accountStateChanged)
    Q_PROPERTY(double accountTotalEquity READ accountTotalEquity NOTIFY accountStateChanged)
    Q_PROPERTY(double accountRealizedPnl READ accountRealizedPnl NOTIFY accountStateChanged)
    Q_PROPERTY(double accountUnrealizedPnl READ accountUnrealizedPnl NOTIFY accountStateChanged)
    Q_PROPERTY(double accountTotalCommission READ accountTotalCommission NOTIFY accountStateChanged)
    Q_PROPERTY(double maxOrderValue READ maxOrderValue NOTIFY riskLimitsChanged)
    Q_PROPERTY(double maxPositionValue READ maxPositionValue NOTIFY riskLimitsChanged)
    Q_PROPERTY(double maxDailyLoss READ maxDailyLoss NOTIFY riskLimitsChanged)
    Q_PROPERTY(int maxActiveOrders READ maxActiveOrders NOTIFY riskLimitsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString stockSearchText READ stockSearchText WRITE setStockSearchText
               NOTIFY stockSearchTextChanged)
    Q_PROPERTY(QString selectedInstrumentTitle READ selectedInstrumentTitle NOTIFY selectedInstrumentChanged)
    Q_PROPERTY(QString selectedSymbol READ selectedSymbol NOTIFY selectedInstrumentChanged)
    Q_PROPERTY(QString selectedPriceUnit READ selectedPriceUnit NOTIFY selectedInstrumentChanged)
    Q_PROPERTY(QString selectedMarketSessionText READ selectedMarketSessionText NOTIFY selectedMarketSessionChanged)
    Q_PROPERTY(QString selectedLiveSymbol READ selectedLiveSymbol NOTIFY liveInstrumentChanged)
    Q_PROPERTY(QString selectedLiveInstrumentTitle READ selectedLiveInstrumentTitle
               NOTIFY liveInstrumentChanged)
    Q_PROPERTY(QString selectedBacktestSymbol READ selectedBacktestSymbol
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QString selectedBacktestInstrumentTitle READ selectedBacktestInstrumentTitle
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(bool selectedBacktestDataAvailable READ selectedBacktestDataAvailable
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QString selectedBacktestFrequency READ selectedBacktestFrequency
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(int selectedBacktestPeriodSeconds READ selectedBacktestPeriodSeconds
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(int selectedBacktestCandleCount READ selectedBacktestCandleCount
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QDateTime selectedBacktestFirstTime READ selectedBacktestFirstTime
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QDateTime selectedBacktestLastTime READ selectedBacktestLastTime
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QString backtestInstrumentSelectionError READ backtestInstrumentSelectionError
               NOTIFY backtestInstrumentChanged)
    Q_PROPERTY(QVariantList localBacktestDataCatalog READ localBacktestDataCatalog
               NOTIFY localBacktestDataCatalogChanged)
    Q_PROPERTY(QVariantList localHistoryDataCatalog READ localHistoryDataCatalog
               NOTIFY localBacktestDataCatalogChanged)
    Q_PROPERTY(bool localCandleSeriesLoading READ localCandleSeriesLoading
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(qulonglong localCandleSeriesRequestId READ localCandleSeriesRequestId
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(qulonglong localCandleSeriesResolvedRequestId READ localCandleSeriesResolvedRequestId
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QString localCandleSeriesSymbol READ localCandleSeriesSymbol
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QString localCandleSeriesFrequency READ localCandleSeriesFrequency
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesPeriodSeconds READ localCandleSeriesPeriodSeconds
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesPage READ localCandleSeriesPage
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesPageCount READ localCandleSeriesPageCount
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesPageSize READ localCandleSeriesPageSize
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(bool localCandleSeriesHasOlderData READ localCandleSeriesHasOlderData
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesTotalBars READ localCandleSeriesTotalBars
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesRawBars READ localCandleSeriesRawBars
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesDisplayPoints READ localCandleSeriesDisplayPoints
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(int localCandleSeriesBucketSize READ localCandleSeriesBucketSize
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(bool localCandleSeriesDownsampled READ localCandleSeriesDownsampled
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QDateTime localCandleSeriesFirstTime READ localCandleSeriesFirstTime
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QDateTime localCandleSeriesLastTime READ localCandleSeriesLastTime
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QString localCandleSeriesStatusText READ localCandleSeriesStatusText
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(QString localCandleSeriesError READ localCandleSeriesError
               NOTIFY localCandleSeriesStateChanged)
    Q_PROPERTY(bool localTickSeriesLoading READ localTickSeriesLoading NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(qulonglong localTickSeriesRequestId READ localTickSeriesRequestId NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(qulonglong localTickSeriesResolvedRequestId READ localTickSeriesResolvedRequestId NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(QString localTickSeriesSymbol READ localTickSeriesSymbol NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(QDate localTickSeriesTradingDate READ localTickSeriesTradingDate NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(qint64 localTickSeriesRawCount READ localTickSeriesRawCount NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(int localTickSeriesDisplayCount READ localTickSeriesDisplayCount NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(bool localTickSeriesDownsampled READ localTickSeriesDownsampled NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(QString localTickSeriesStatusText READ localTickSeriesStatusText NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(QString localTickSeriesError READ localTickSeriesError NOTIFY localTickSeriesStateChanged)
    Q_PROPERTY(bool liveDayCandleSeriesLoading READ liveDayCandleSeriesLoading
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(QString liveDayCandleSeriesStatusText READ liveDayCandleSeriesStatusText
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(QString liveDayCandleSeriesError READ liveDayCandleSeriesError
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(int liveDayCandleSeriesPeriodSeconds READ liveDayCandleSeriesPeriodSeconds
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(QDate liveDayCandleSeriesTradingDate READ liveDayCandleSeriesTradingDate
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(int liveDayCandleSeriesCount READ liveDayCandleSeriesCount
               NOTIFY liveDayCandleSeriesStateChanged)
    Q_PROPERTY(bool realtimeMinuteLoading READ realtimeMinuteLoading
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QString realtimeMinuteStatusText READ realtimeMinuteStatusText
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QString realtimeMinuteError READ realtimeMinuteError
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QString realtimeMinuteSymbol READ realtimeMinuteSymbol
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QString realtimeMinuteSource READ realtimeMinuteSource
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(int realtimeMinutePeriodSeconds READ realtimeMinutePeriodSeconds
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QVariantList realtimeMinutePeriodOptions READ realtimeMinutePeriodOptions CONSTANT)
    Q_PROPERTY(int realtimeMinuteCount READ realtimeMinuteCount
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QDateTime realtimeMinuteLastBarTime READ realtimeMinuteLastBarTime
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(bool realtimeMinuteStale READ realtimeMinuteStale
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(qulonglong realtimeMinuteRequestId READ realtimeMinuteRequestId
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(qulonglong realtimeMinuteResolvedRequestId READ realtimeMinuteResolvedRequestId
               NOTIFY realtimeMinuteStateChanged)
    Q_PROPERTY(QString historyDownloadSymbol READ historyDownloadSymbol
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadInstrumentTitle READ historyDownloadInstrumentTitle
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadFrequency READ historyDownloadFrequency
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(int historyDownloadTargetBars READ historyDownloadTargetBars
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(bool historyDownloadRunning READ historyDownloadRunning
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(qulonglong historyDownloadRequestVersion READ historyDownloadRequestVersion
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadSource READ historyDownloadSource
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QVariantList historicalProviderCapabilities READ historicalProviderCapabilities
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadProvider READ historyDownloadProvider
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadAdjustment READ historyDownloadAdjustment
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadDataVersion READ historyDownloadDataVersion
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QDateTime historyDownloadDownloadedAt READ historyDownloadDownloadedAt
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(bool historyDownloadFallbackUsed READ historyDownloadFallbackUsed
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(int historyDownloadStoredCount READ historyDownloadStoredCount
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(int historyDownloadAvailableCount READ historyDownloadAvailableCount
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QDateTime historyDownloadFirstTime READ historyDownloadFirstTime
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QDateTime historyDownloadLastTime READ historyDownloadLastTime
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadStatusText READ historyDownloadStatusText
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString historyDownloadError READ historyDownloadError
               NOTIFY historyDownloadStateChanged)
    Q_PROPERTY(QString tickDataImportRootDirectory READ tickDataImportRootDirectory
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportSymbol READ tickDataImportSymbol
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportInstrumentTitle READ tickDataImportInstrumentTitle
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportTradingDate READ tickDataImportTradingDate
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(int tickDataImportFormat READ tickDataImportFormat
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(int tickDataImportPeriodSeconds READ tickDataImportPeriodSeconds
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(int tickDataImportFiveMinuteVolumeUnit READ tickDataImportFiveMinuteVolumeUnit
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(bool tickDataImportRunning READ tickDataImportRunning
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(qulonglong tickDataImportRequestId READ tickDataImportRequestId
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportFilePath READ tickDataImportFilePath
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportSource READ tickDataImportSource
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(qint64 tickDataImportLinesRead READ tickDataImportLinesRead
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(qint64 tickDataImportValidRecords READ tickDataImportValidRecords
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(qint64 tickDataImportSkippedRecords READ tickDataImportSkippedRecords
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(int tickDataImportWrittenBars READ tickDataImportWrittenBars
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(qint64 tickDataImportRawTicksWritten READ tickDataImportRawTicksWritten
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QDateTime tickDataImportFirstTime READ tickDataImportFirstTime
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QDateTime tickDataImportLastTime READ tickDataImportLastTime
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QVariantList tickDataImportDiagnostics READ tickDataImportDiagnostics
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportStatusText READ tickDataImportStatusText
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString tickDataImportError READ tickDataImportError
               NOTIFY tickDataImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportFilePath
               READ aShareInstrumentDayImportFilePath
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportSource
               READ aShareInstrumentDayImportSource
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportSourceVersion
               READ aShareInstrumentDayImportSourceVersion
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(bool aShareInstrumentDayImportRunning
               READ aShareInstrumentDayImportRunning
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qulonglong aShareInstrumentDayImportRequestId
               READ aShareInstrumentDayImportRequestId
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportLinesRead
               READ aShareInstrumentDayImportLinesRead
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportValidRecords
               READ aShareInstrumentDayImportValidRecords
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportWrittenRecords
               READ aShareInstrumentDayImportWrittenRecords
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportNewRecords
               READ aShareInstrumentDayImportNewRecords
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportUnchangedRecords
               READ aShareInstrumentDayImportUnchangedRecords
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(qint64 aShareInstrumentDayImportCorrectedRecords
               READ aShareInstrumentDayImportCorrectedRecords
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(bool aShareInstrumentDayImportPreviewReady
               READ aShareInstrumentDayImportPreviewReady
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(bool aShareInstrumentDayImportReused
               READ aShareInstrumentDayImportReused
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportContentSha256
               READ aShareInstrumentDayImportContentSha256
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportDateRange
               READ aShareInstrumentDayImportDateRange
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QVariantList aShareInstrumentDayImportDiagnostics
               READ aShareInstrumentDayImportDiagnostics
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QVariantList aShareInstrumentDayImportHistory
               READ aShareInstrumentDayImportHistory
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportStatusText
               READ aShareInstrumentDayImportStatusText
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QString aShareInstrumentDayImportError
               READ aShareInstrumentDayImportError
               NOTIFY aShareInstrumentDayImportStateChanged)
    Q_PROPERTY(QVariantMap aShareIndustryImportState
               READ aShareIndustryImportState
               NOTIFY aShareIndustryImportStateChanged)
    Q_PROPERTY(QVariantList aShareIndustryImportHistory
               READ aShareIndustryImportHistory
               NOTIFY aShareIndustryImportStateChanged)
    Q_PROPERTY(QVariantList tickDataBatchPlan READ tickDataBatchPlan
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QVariantMap tickDataBatchSummary READ tickDataBatchSummary
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QVariantList tickDataImportRunHistory READ tickDataImportRunHistory
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(bool tickDataBatchPlanReady READ tickDataBatchPlanReady
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(bool tickDataBatchRunning READ tickDataBatchRunning
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(double tickDataBatchProgress READ tickDataBatchProgress
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QString tickDataBatchCurrentDate READ tickDataBatchCurrentDate
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QString tickDataBatchStatusText READ tickDataBatchStatusText
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QString tickDataBatchError READ tickDataBatchError
               NOTIFY tickDataBatchStateChanged)
    Q_PROPERTY(QString storagePath READ storagePath NOTIFY storageStateChanged)
    Q_PROPERTY(QString databaseUuid READ databaseUuid NOTIFY storageStateChanged)
    Q_PROPERTY(int databaseSchemaVersion READ databaseSchemaVersion NOTIFY storageStateChanged)
    Q_PROPERTY(bool storageRelocationRunning READ storageRelocationRunning NOTIFY storageStateChanged)
    Q_PROPERTY(QString storageRelocationError READ storageRelocationError NOTIFY storageStateChanged)
    Q_PROPERTY(QString nasDataNodeEndpoint READ nasDataNodeEndpoint NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(bool nasDataNodeLoading READ nasDataNodeLoading NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(bool nasDataNodeSyncRunning READ nasDataNodeSyncRunning NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(qulonglong nasDataNodeRequestVersion READ nasDataNodeRequestVersion NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantList nasDataNodeCatalog READ nasDataNodeCatalog NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantList nasDataNodeSyncPlan READ nasDataNodeSyncPlan NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantMap nasDataNodeSyncSummary READ nasDataNodeSyncSummary NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantMap nasDataNodeManifestSummary READ nasDataNodeManifestSummary NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantMap nasDataNodeCacheSummary READ nasDataNodeCacheSummary NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantMap nasNodeChangesSummary READ nasNodeChangesSummary NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(bool nasDataNodePlanRecovered READ nasDataNodePlanRecovered NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(double nasDataNodeSyncProgress READ nasDataNodeSyncProgress NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QString nasDataNodeSyncCurrentSymbol READ nasDataNodeSyncCurrentSymbol NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QString nasDataNodeSyncCurrentDate READ nasDataNodeSyncCurrentDate NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(bool nasDataNodeSyncCanResume READ nasDataNodeSyncCanResume NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(qint64 nasDataNodeReceivedBytes READ nasDataNodeReceivedBytes NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(qint64 nasDataNodeExpectedBytes READ nasDataNodeExpectedBytes NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QString nasDataNodeStatusText READ nasDataNodeStatusText NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QString nasDataNodeError READ nasDataNodeError NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(QVariantMap nasBacktestPreparationSummary READ nasBacktestPreparationSummary
               NOTIFY nasDataNodeStateChanged)
    Q_PROPERTY(int persistedTickCount READ persistedTickCount NOTIFY storageStateChanged)
    Q_PROPERTY(int persistedSignalCount READ persistedSignalCount NOTIFY storageStateChanged)
    Q_PROPERTY(int strategyCount READ strategyCount NOTIFY strategyStateChanged)
    Q_PROPERTY(int strategyThreadCount READ strategyThreadCount NOTIFY strategyStateChanged)
    Q_PROPERTY(int pendingStrategyIntentCount READ pendingStrategyIntentCount NOTIFY strategyExecutionStateChanged)
    Q_PROPERTY(bool marketIsOpen READ marketIsOpen NOTIFY selectedMarketSessionChanged)
    Q_PROPERTY(int aShareUniverseCount READ aShareUniverseCount NOTIFY marketUniverseChanged)
    Q_PROPERTY(int aShareUniverseSourceCount READ aShareUniverseSourceCount NOTIFY marketUniverseChanged)
    Q_PROPERTY(int aShareUniverseRejectedCount READ aShareUniverseRejectedCount NOTIFY marketUniverseChanged)
    Q_PROPERTY(QString aShareUniverseCachePath READ aShareUniverseCachePath NOTIFY marketUniverseChanged)
    Q_PROPERTY(QString aShareUniverseError READ aShareUniverseError NOTIFY marketUniverseChanged)
    Q_PROPERTY(bool snapshotLoading READ snapshotLoading NOTIFY snapshotQueryStateChanged)
    Q_PROPERTY(qulonglong snapshotRequestId READ snapshotRequestId NOTIFY snapshotQueryStateChanged)
    Q_PROPERTY(qulonglong resolvedSnapshotRequestId READ resolvedSnapshotRequestId NOTIFY snapshotQueryStateChanged)
    Q_PROPERTY(QDateTime requestedSnapshotTime READ requestedSnapshotTime NOTIFY marketSnapshotChanged)
    Q_PROPERTY(QDateTime resolvedSnapshotTime READ resolvedSnapshotTime NOTIFY marketSnapshotChanged)
    Q_PROPERTY(QString snapshotBatchId READ snapshotBatchId NOTIFY marketSnapshotChanged)
    Q_PROPERTY(int snapshotExpectedCount READ snapshotExpectedCount NOTIFY marketSnapshotChanged)
    Q_PROPERTY(int snapshotReceivedCount READ snapshotReceivedCount NOTIFY marketSnapshotChanged)
    Q_PROPERTY(double snapshotCompleteness READ snapshotCompleteness NOTIFY marketSnapshotChanged)
    Q_PROPERTY(qint64 snapshotLatencyMilliseconds READ snapshotLatencyMilliseconds NOTIFY marketSnapshotChanged)
    Q_PROPERTY(qint64 snapshotStalenessMilliseconds READ snapshotStalenessMilliseconds NOTIFY marketSnapshotChanged)
    Q_PROPERTY(bool snapshotStale READ snapshotStale NOTIFY marketSnapshotChanged)
    Q_PROPERTY(int snapshotHealth READ snapshotHealth NOTIFY marketSnapshotChanged)
    Q_PROPERTY(QString snapshotHealthText READ snapshotHealthText NOTIFY marketSnapshotChanged)
    Q_PROPERTY(QString snapshotError READ snapshotError NOTIFY marketSnapshotChanged)
    Q_PROPERTY(int providerHealth READ providerHealth NOTIFY providerStateChanged)
    Q_PROPERTY(QString providerHealthText READ providerHealthText NOTIFY providerStateChanged)
    Q_PROPERTY(QString providerMessage READ providerMessage NOTIFY providerStateChanged)
    Q_PROPERTY(QString providerError READ providerError NOTIFY providerStateChanged)
    Q_PROPERTY(int providerExpectedCount READ providerExpectedCount NOTIFY providerStateChanged)
    Q_PROPERTY(int providerReceivedCount READ providerReceivedCount NOTIFY providerStateChanged)
    Q_PROPERTY(double providerCompleteness READ providerCompleteness NOTIFY providerStateChanged)
    Q_PROPERTY(QString providerBatchId READ providerBatchId NOTIFY providerStateChanged)
    Q_PROPERTY(QDateTime providerUpdatedAt READ providerUpdatedAt NOTIFY providerStateChanged)
    Q_PROPERTY(QString activeRealtimeProvider READ activeRealtimeProvider NOTIFY providerStateChanged)
    Q_PROPERTY(QString activeRealtimeProviderName READ activeRealtimeProviderName NOTIFY providerStateChanged)
    Q_PROPERTY(int realtimeProviderScore READ realtimeProviderScore NOTIFY providerStateChanged)
    Q_PROPERTY(bool realtimeFallbackUsed READ realtimeFallbackUsed NOTIFY providerStateChanged)
    Q_PROPERTY(int realtimeFailoverCount READ realtimeFailoverCount NOTIFY providerStateChanged)
    Q_PROPERTY(QString realtimeRoutingMessage READ realtimeRoutingMessage NOTIFY providerStateChanged)
    Q_PROPERTY(QVariantList realtimeProviderCapabilities READ realtimeProviderCapabilities NOTIFY providerStateChanged)

public:
    enum SnapshotHealthState {
        SnapshotEmpty = 0,
        SnapshotHealthy,
        SnapshotIncomplete,
        SnapshotStale,
        SnapshotError
    };
    Q_ENUM(SnapshotHealthState)

    explicit TradingController(QObject *parent = nullptr);
    ~TradingController() override;

    // Idempotent shutdown used before QApplication tears down its event loop.
    void shutdown();

    QAbstractItemModel *tickModel();
    QAbstractItemModel *candleModel();
    QVariantList validationRuns() const;
    QAbstractItemModel *historyCandleModel();
    QAbstractItemModel *signalModel();
    QAbstractItemModel *strategyModel();
    QAbstractItemModel *strategyIntentModel();
    QAbstractItemModel *customStrategyModel();
    QAbstractItemModel *searchModel();
    QAbstractItemModel *instrumentModel();
    QAbstractItemModel *backtestTradeModel();
    QAbstractItemModel *equityCurveModel();
    QAbstractItemModel *backtestArchiveModel();
    QAbstractItemModel *backtestArchiveRawTickModel();
    QAbstractItemModel *batchBacktestModel();
    QAbstractItemModel *orderModel();
    QAbstractItemModel *executionModel();
    QAbstractItemModel *positionModel();
    QAbstractItemModel *marketSnapshotModel();
    QAbstractItemModel *localCandleSeriesModel();
    QAbstractItemModel *localTickSeriesModel();
    QAbstractItemModel *liveDayCandleSeriesModel();
    QAbstractItemModel *realtimeMinuteCandleModel();
    QAbstractItemModel *realtimeChartWindowModel();
    int realtimeChartWindowCount() const;
    QString realtimeChartWindowError() const;
    QVariantList backtestStrategyCatalog() const;
    QVariantMap customStrategySchema() const;
    int customStrategyCount() const;
    QString customStrategyError() const;
    bool customStrategyPreviewLoading() const;
    qulonglong customStrategyPreviewRequestId() const;
    qulonglong customStrategyPreviewResolvedRequestId() const;
    QVariantMap customStrategyPreviewResult() const;
    QString customStrategyPreviewError() const;
    bool customStrategySaveLoading() const;
    qulonglong customStrategySaveRequestId() const;
    qulonglong customStrategySaveResolvedRequestId() const;
    QVariantMap customStrategySaveResult() const;
    QString customStrategySaveError() const;

    bool liveRunning() const;
    bool backtestRunning() const;
    bool marketIsOpen() const;
    double backtestProgress() const;
    QString backtestStatusText() const;
    QString backtestError() const;
    double backtestInitialCapital() const;
    double finalEquity() const;
    double totalReturnPercent() const;
    double maxDrawdownPercent() const;
    double winRatePercent() const;
    double sharpeRatio() const;
    double totalCommission() const;
    int backtestTradeCount() const;
    QString selectedBacktestStrategyId() const;
    QVariantMap selectedBacktestStrategyDescriptor() const;
    QVariantMap selectedBacktestParameters() const;
    QString backtestFrequency() const;
    int backtestBarCount() const;
    double backtestConfiguredInitialCapital() const;
    double backtestCommissionRate() const;
    double backtestSlippageRate() const;
    QString backtestSampleFromDate() const;
    QString backtestSampleToDate() const;
    QString backtestConfigurationError() const;
    QVariantMap backtestResultConfiguration() const;
    qulonglong backtestTaskId() const;
    qulonglong completedBacktestTaskId() const;
    QString backtestResultStrategyId() const;
    QString backtestResultSymbol() const;
    QDateTime backtestResultCompletedAt() const;
    int backtestArchiveCount() const;
    QString selectedBacktestArchiveId() const;
    bool backtestArchiveLoading() const;
    qulonglong backtestArchiveRequestId() const;
    qulonglong backtestArchiveResolvedRequestId() const;
    QString backtestArchiveError() const;
    QString backtestArchiveSaveError() const;
    bool backtestResultArchived() const;
    bool backtestResultHistoricalSnapshot() const;
    bool backtestArchiveRawTickPageLoading() const;
    qulonglong backtestArchiveRawTickPageRequestId() const;
    qulonglong backtestArchiveRawTickPageResolvedRequestId() const;
    QString backtestArchiveRawTickPageResultId() const;
    int backtestArchiveRawTickPageOffset() const;
    int backtestArchiveRawTickPageSize() const;
    int backtestArchiveRawTickTotalCount() const;
    bool backtestArchiveRawTickHasPrevious() const;
    bool backtestArchiveRawTickHasNext() const;
    QString backtestArchiveRawTickPageError() const;
    bool batchBacktestRunning() const;
    bool portfolioBacktestRunning() const;
    qulonglong batchBacktestRequestId() const;
    int batchBacktestRequestedCount() const;
    int batchBacktestCompletedCount() const;
    int batchBacktestSucceededCount() const;
    int batchBacktestFailedCount() const;
    double batchBacktestProgress() const;
    QString batchBacktestStatusText() const;
    QString batchBacktestError() const;
    bool batchBacktestArchiveLoading() const;
    qulonglong batchBacktestArchiveRequestId() const;
    qulonglong batchBacktestArchiveResolvedRequestId() const;
    QString batchBacktestArchiveSymbol() const;
    QString batchBacktestArchiveResultId() const;
    QString batchBacktestArchiveError() const;
    QVariantList batchBacktestRunHistory() const;
    bool rawTickAvailabilityLoading() const;
    qulonglong rawTickAvailabilityRequestId() const;
    qulonglong rawTickAvailabilityResolvedRequestId() const;
    QVariantMap rawTickAvailabilityResult() const;
    QString rawTickAvailabilityError() const;
    QString portfolioBacktestStatusText() const;
    QString portfolioBacktestError() const;
    QVariantMap portfolioResultConfiguration() const;
    QAbstractItemModel *portfolioEquityCurveModel();
    QAbstractItemModel *portfolioTradeModel();
    QAbstractItemModel *portfolioArchiveModel();
    int portfolioArchiveCount() const;
    bool portfolioArchiveLoading() const;
    QString portfolioArchiveError() const;
    bool crossSectionalBacktestRunning() const;
    QString crossSectionalBacktestStatusText() const;
    QString crossSectionalBacktestError() const;
    QVariantMap crossSectionalRiskConfiguration() const;
    QString crossSectionalRiskConfigurationError() const;
    QVariantList crossSectionalFactorDefinitionCatalog() const;
    QVariantMap crossSectionalResultConfiguration() const;
    QAbstractItemModel *crossSectionalEquityCurveModel();
    QAbstractItemModel *crossSectionalTradeModel();
    QAbstractItemModel *crossSectionalArchiveModel();
    int crossSectionalArchiveCount() const;
    bool crossSectionalArchiveLoading() const;
    QString crossSectionalArchiveError() const;
    QAbstractItemModel *crossSectionalFactorComparisonModel();
    int crossSectionalFactorComparisonCount() const;
    int crossSectionalMultipleTestingCandidateCount() const;
    QString crossSectionalMultipleTestingCandidateSetSha256() const;
    QString crossSectionalMultipleTestingStatusText() const;
    QString crossSectionalFactorComparisonError() const;
    QVariantList factorCandidateCatalog() const;
    QVariantList factorCandidateEvaluationCatalog() const;
    QString selectedFactorCandidateId() const;
    int selectedFactorCandidateVersion() const;
    QString factorCandidateGovernanceStatusText() const;
    QString factorCandidateGovernanceError() const;
    QVariantMap authorizedFactorImportState() const;
    QVariantList externalFactorDatasetCatalog() const;
    QVariantList selectedFactorCandidateDatasetBindings() const;
    QVariantMap externalFactorResearchState() const;
    QVariantMap externalFactorResearchPreflightState() const;
    QVariantList externalFactorResearchArchiveCatalog() const;
    QVariantMap selectedExternalFactorResearchArchive() const;
    QString selectedCrossSectionalArchiveId() const;
    bool crossSectionalResultHistoricalSnapshot() const;
    bool crossSectionalReportExporting() const;
    qulonglong crossSectionalReportExportRequestId() const;
    QString crossSectionalReportExportStatusText() const;
    QString crossSectionalReportExportError() const;
    QString crossSectionalReportExportPath() const;
    QString brokerName() const;
    bool brokerLoggedIn() const;
    QString tradingError() const;
    double accountInitialEquity() const;
    double accountCash() const;
    double accountAvailableCash() const;
    double accountFrozenCash() const;
    double accountMarketValue() const;
    double accountTotalEquity() const;
    double accountRealizedPnl() const;
    double accountUnrealizedPnl() const;
    double accountTotalCommission() const;
    double maxOrderValue() const;
    double maxPositionValue() const;
    double maxDailyLoss() const;
    int maxActiveOrders() const;
    QString statusText() const;
    QString stockSearchText() const;
    void setStockSearchText(const QString &text);
    QString selectedInstrumentTitle() const;
    QString selectedSymbol() const;
    QString selectedPriceUnit() const;
    QString selectedMarketSessionText() const;
    QString selectedLiveSymbol() const;
    QString selectedLiveInstrumentTitle() const;
    QString selectedBacktestSymbol() const;
    QString selectedBacktestInstrumentTitle() const;
    bool selectedBacktestDataAvailable() const;
    QString selectedBacktestFrequency() const;
    int selectedBacktestPeriodSeconds() const;
    int selectedBacktestCandleCount() const;
    QDateTime selectedBacktestFirstTime() const;
    QDateTime selectedBacktestLastTime() const;
    QString backtestInstrumentSelectionError() const;
    QVariantList localBacktestDataCatalog() const;
    QVariantList localHistoryDataCatalog() const;
    bool localCandleSeriesLoading() const;
    qulonglong localCandleSeriesRequestId() const;
    qulonglong localCandleSeriesResolvedRequestId() const;
    QString localCandleSeriesSymbol() const;
    QString localCandleSeriesFrequency() const;
    int localCandleSeriesPeriodSeconds() const;
    int localCandleSeriesPage() const;
    int localCandleSeriesPageCount() const;
    int localCandleSeriesPageSize() const;
    bool localCandleSeriesHasOlderData() const;
    int localCandleSeriesTotalBars() const;
    int localCandleSeriesRawBars() const;
    int localCandleSeriesDisplayPoints() const;
    int localCandleSeriesBucketSize() const;
    bool localCandleSeriesDownsampled() const;
    QDateTime localCandleSeriesFirstTime() const;
    QDateTime localCandleSeriesLastTime() const;
    QString localCandleSeriesStatusText() const;
    QString localCandleSeriesError() const;
    bool localTickSeriesLoading() const;
    qulonglong localTickSeriesRequestId() const;
    qulonglong localTickSeriesResolvedRequestId() const;
    QString localTickSeriesSymbol() const;
    QDate localTickSeriesTradingDate() const;
    qint64 localTickSeriesRawCount() const;
    int localTickSeriesDisplayCount() const;
    bool localTickSeriesDownsampled() const;
    QString localTickSeriesStatusText() const;
    QString localTickSeriesError() const;
    bool liveDayCandleSeriesLoading() const;
    QString liveDayCandleSeriesStatusText() const;
    QString liveDayCandleSeriesError() const;
    int liveDayCandleSeriesPeriodSeconds() const;
    QDate liveDayCandleSeriesTradingDate() const;
    int liveDayCandleSeriesCount() const;
    bool realtimeMinuteLoading() const;
    QString realtimeMinuteStatusText() const;
    QString realtimeMinuteError() const;
    QString realtimeMinuteSymbol() const;
    QString realtimeMinuteSource() const;
    int realtimeMinutePeriodSeconds() const;
    QVariantList realtimeMinutePeriodOptions() const;
    int realtimeMinuteCount() const;
    QDateTime realtimeMinuteLastBarTime() const;
    bool realtimeMinuteStale() const;
    qulonglong realtimeMinuteRequestId() const;
    qulonglong realtimeMinuteResolvedRequestId() const;
    QString historyDownloadSymbol() const;
    QString historyDownloadInstrumentTitle() const;
    QString historyDownloadFrequency() const;
    int historyDownloadTargetBars() const;
    bool historyDownloadRunning() const;
    qulonglong historyDownloadRequestVersion() const;
    QString historyDownloadSource() const;
    QVariantList historicalProviderCapabilities() const;
    QString historyDownloadProvider() const;
    QString historyDownloadAdjustment() const;
    QString historyDownloadDataVersion() const;
    QDateTime historyDownloadDownloadedAt() const;
    bool historyDownloadFallbackUsed() const;
    int historyDownloadStoredCount() const;
    int historyDownloadAvailableCount() const;
    QDateTime historyDownloadFirstTime() const;
    QDateTime historyDownloadLastTime() const;
    QString historyDownloadStatusText() const;
    QString historyDownloadError() const;
    QString tickDataImportRootDirectory() const;
    QString tickDataImportSymbol() const;
    QString tickDataImportInstrumentTitle() const;
    QString tickDataImportTradingDate() const;
    int tickDataImportFormat() const;
    int tickDataImportPeriodSeconds() const;
    int tickDataImportFiveMinuteVolumeUnit() const;
    bool tickDataImportRunning() const;
    qulonglong tickDataImportRequestId() const;
    QString tickDataImportFilePath() const;
    QString tickDataImportSource() const;
    qint64 tickDataImportLinesRead() const;
    qint64 tickDataImportValidRecords() const;
    qint64 tickDataImportSkippedRecords() const;
    int tickDataImportWrittenBars() const;
    qint64 tickDataImportRawTicksWritten() const;
    QDateTime tickDataImportFirstTime() const;
    QDateTime tickDataImportLastTime() const;
    QVariantList tickDataImportDiagnostics() const;
    QString tickDataImportStatusText() const;
    QString tickDataImportError() const;
    QString aShareInstrumentDayImportFilePath() const;
    QString aShareInstrumentDayImportSource() const;
    QString aShareInstrumentDayImportSourceVersion() const;
    bool aShareInstrumentDayImportRunning() const;
    qulonglong aShareInstrumentDayImportRequestId() const;
    qint64 aShareInstrumentDayImportLinesRead() const;
    qint64 aShareInstrumentDayImportValidRecords() const;
    qint64 aShareInstrumentDayImportWrittenRecords() const;
    qint64 aShareInstrumentDayImportNewRecords() const;
    qint64 aShareInstrumentDayImportUnchangedRecords() const;
    qint64 aShareInstrumentDayImportCorrectedRecords() const;
    bool aShareInstrumentDayImportPreviewReady() const;
    bool aShareInstrumentDayImportReused() const;
    QString aShareInstrumentDayImportContentSha256() const;
    QString aShareInstrumentDayImportDateRange() const;
    QVariantList aShareInstrumentDayImportDiagnostics() const;
    QVariantList aShareInstrumentDayImportHistory() const;
    QString aShareInstrumentDayImportStatusText() const;
    QString aShareInstrumentDayImportError() const;
    QVariantMap aShareIndustryImportState() const;
    QVariantList aShareIndustryImportHistory() const;
    QVariantList tickDataBatchPlan() const;
    QVariantMap tickDataBatchSummary() const;
    QVariantList tickDataImportRunHistory() const;
    bool tickDataBatchPlanReady() const;
    bool tickDataBatchRunning() const;
    double tickDataBatchProgress() const;
    QString tickDataBatchCurrentDate() const;
    QString tickDataBatchStatusText() const;
    QString tickDataBatchError() const;
    QString storagePath() const;
    QString databaseUuid() const;
    int databaseSchemaVersion() const;
    bool storageRelocationRunning() const;
    QString storageRelocationError() const;
    QString nasDataNodeEndpoint() const;
    bool nasDataNodeLoading() const;
    bool nasDataNodeSyncRunning() const;
    qulonglong nasDataNodeRequestVersion() const;
    QVariantList nasDataNodeCatalog() const;
    QVariantList nasDataNodeSyncPlan() const;
    QVariantMap nasDataNodeSyncSummary() const;
    QVariantMap nasDataNodeManifestSummary() const;
    QVariantMap nasDataNodeCacheSummary() const;
    QVariantMap nasNodeChangesSummary() const;
    bool nasDataNodePlanRecovered() const;
    double nasDataNodeSyncProgress() const;
    QString nasDataNodeSyncCurrentSymbol() const;
    QString nasDataNodeSyncCurrentDate() const;
    bool nasDataNodeSyncCanResume() const;
    qint64 nasDataNodeReceivedBytes() const;
    qint64 nasDataNodeExpectedBytes() const;
    QString nasDataNodeStatusText() const;
    QString nasDataNodeError() const;
    QVariantMap nasBacktestPreparationSummary() const;
    int persistedTickCount() const;
    int persistedSignalCount() const;
    int strategyCount() const;
    int strategyThreadCount() const;
    int pendingStrategyIntentCount() const;
    bool customStrategyReplayRunning() const;
    int customStrategyReplayedBars() const;
    int customStrategyReplayTotalBars() const;
    bool parameterGridRunning() const;
    QString parameterGridError() const;
    QVariantList parameterGridResults() const;
    bool rawTickSensitivityRunning() const;
    qulonglong rawTickSensitivityRequestId() const;
    QString rawTickSensitivityError() const;
    QVariantMap rawTickSensitivityResult() const;
    QVariantList rawTickSensitivityHistory() const;
    int aShareUniverseCount() const;
    int aShareUniverseSourceCount() const;
    int aShareUniverseRejectedCount() const;
    QString aShareUniverseCachePath() const;
    QString aShareUniverseError() const;
    bool snapshotLoading() const;
    qulonglong snapshotRequestId() const;
    qulonglong resolvedSnapshotRequestId() const;
    QDateTime requestedSnapshotTime() const;
    QDateTime resolvedSnapshotTime() const;
    QString snapshotBatchId() const;
    int snapshotExpectedCount() const;
    int snapshotReceivedCount() const;
    double snapshotCompleteness() const;
    qint64 snapshotLatencyMilliseconds() const;
    qint64 snapshotStalenessMilliseconds() const;
    bool snapshotStale() const;
    int snapshotHealth() const;
    QString snapshotHealthText() const;
    QString snapshotError() const;
    int providerHealth() const;
    QString providerHealthText() const;
    QString providerMessage() const;
    QString providerError() const;
    int providerExpectedCount() const;
    int providerReceivedCount() const;
    double providerCompleteness() const;
    QString providerBatchId() const;
    QDateTime providerUpdatedAt() const;
    QString activeRealtimeProvider() const;
    QString activeRealtimeProviderName() const;
    int realtimeProviderScore() const;
    bool realtimeFallbackUsed() const;
    int realtimeFailoverCount() const;
    QString realtimeRoutingMessage() const;
    QVariantList realtimeProviderCapabilities() const;

    Q_INVOKABLE void startLive();
    Q_INVOKABLE void stopLive();
    Q_INVOKABLE void runBacktest();
    Q_INVOKABLE void runHistoricalBacktest(const QString &symbol, const QString &frequency,
                                           int barCount, double initialCapital,
                                           double commissionRate, double slippageRate);
    Q_INVOKABLE qulonglong runStrategyBacktest(const QString &strategyId,
                                               const QVariantMap &strategyParameters,
                                               const QString &symbol, const QString &frequency,
                                               int barCount, double initialCapital,
                                               double commissionRate, double slippageRate);
    Q_INVOKABLE qulonglong runCustomStrategyBacktest(
        const QString &strategyId, int version, const QVariantMap &strategyParameters,
        const QString &symbol, const QString &frequency, int barCount,
        double initialCapital, double commissionRate, double slippageRate);
    Q_INVOKABLE QVariantMap customStrategyLocalRunAvailability(
        const QString &strategyId, int version,
        const QVariantMap &strategyParameters = {}) const;
    QVariantMap customStrategyPreview(
        const QString &json, const QString &symbol,
        int periodSeconds, int barCount);
    Q_INVOKABLE qulonglong requestCustomStrategyPreview(
        const QString &json, const QString &symbol,
        int periodSeconds, int barCount);
    Q_INVOKABLE qulonglong runSelectedLocalCustomStrategyBacktest(
        const QString &strategyId, int version,
        const QVariantMap &strategyParameters = {});
    Q_INVOKABLE bool backtestResultMatchesCustomStrategy(
        const QString &strategyId, int version) const;
    Q_INVOKABLE qulonglong runSelectedBacktest();
    Q_INVOKABLE bool selectBacktestStrategy(const QString &strategyId);
    Q_INVOKABLE bool setSelectedBacktestParameter(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetSelectedBacktestParameters();
    Q_INVOKABLE QVariant backtestParameterValue(const QString &key) const;
    Q_INVOKABLE bool setBacktestConfiguration(const QString &frequency, int barCount,
                                               double initialCapital, double commissionRate,
                                               double slippageRate);
    Q_INVOKABLE QVariantMap backtestStrategyDescriptor(const QString &strategyId) const;
    Q_INVOKABLE QVariantMap validateCustomStrategyJson(const QString &json) const;
    QVariantList paperStrategyRuns() const;
    QString paperStrategyError() const;
    Q_INVOKABLE qulonglong publishPaperStrategyVersion(const QString &strategyId, int version);
    Q_INVOKABLE QString startPaperStrategyRun(const QString &strategyId, int version,
        const QString &symbol, double initialCapital, double commissionRate, double slippageRate);
    Q_INVOKABLE bool stopPaperStrategyRun(const QString &runId);
    Q_INVOKABLE void stopAllPaperStrategies();
    Q_INVOKABLE bool exportPaperStrategyReport(const QUrl &destination);
    QString startPaperReplayRun(const QString &strategyId, int version, const QString &symbol,
                                const QDateTime &sourceStart, double initialCapital = 100000);
    QString startPaperReplayPortfolio(const QString &strategyId, int version, const QStringList &symbols,
                                     const QDateTime &sourceStart, const PaperStrategyConfig &config);
    bool acceptPaperReplayBar(const Candle &bar, const QDateTime &sourceClock, bool warmup);
    void acceptPaperReplayQuote(const Tick &quote);
    Q_INVOKABLE QVariantMap analyzeCustomStrategyJson(
        const QString &json, const QVariantMap &runtimeParameters = {}) const;
    bool saveCustomStrategyJson(const QString &json);
    Q_INVOKABLE qulonglong requestSaveCustomStrategyJson(const QString &json);
    Q_INVOKABLE QVariantMap customStrategyDefinition(const QString &strategyId,
                                                     int version = 0) const;
    Q_INVOKABLE QString exportCustomStrategyJson(const QString &strategyId,
                                                 int version = 0) const;
    Q_INVOKABLE void cancelBacktest();
    Q_INVOKABLE qulonglong runLocalBatchBacktest(
        const QString &strategyId, const QVariantMap &strategyParameters,
        const QString &frequency, int barCount, int maximumSymbols = 5207);
    Q_INVOKABLE qulonglong runLocalBatchBacktestRange(
        const QString &strategyId, const QVariantMap &strategyParameters,
        const QString &dataMode, const QString &frequency,
        const QString &startDate, const QString &endDate,
        int maximumSymbols = 5207, int holdoutTradingDays = 0,
        int walkForwardFoldCount = 0, bool segmentedResearch = false);
    Q_INVOKABLE qulonglong analyzeLocalRawTickAvailability(
        const QString &startDate, const QString &endDate,
        int maximumSymbols = 5207);
    Q_INVOKABLE void cancelLocalBatchBacktest();
    Q_INVOKABLE qulonglong archiveBatchBacktestItem(const QString &symbol);
    Q_INVOKABLE QString resumeBatchBacktest(const QString &runId);
    Q_INVOKABLE qulonglong runPortfolioBacktest(const QString &portfolioJson,
                                                int barCount);
    Q_INVOKABLE void cancelPortfolioBacktest();
    Q_INVOKABLE bool loadPortfolioBacktestArchive(const QString &resultId);
    Q_INVOKABLE void refreshPortfolioBacktestArchiveList();
    Q_INVOKABLE qulonglong runCrossSectionalBacktest(const QString &strategyJson,
                                                      int barCount);
    Q_INVOKABLE qulonglong runCrossSectionalBacktestForPeriod(
        const QString &strategyJson, int barCount, int periodSeconds);
    Q_INVOKABLE bool setCrossSectionalRiskConfiguration(
        double maximumLegWeight, int maximumActiveLegs,
        int maximumConsecutiveMissingBars);
    Q_INVOKABLE bool setCrossSectionalCircuitBreakerConfiguration(
        double maximumPortfolioLossPercent,
        double maximumPortfolioDrawdownPercent);
    Q_INVOKABLE bool setCrossSectionalIndustryConfiguration(
        double maximumIndustryWeight, const QString &industryDatasetId);
    Q_INVOKABLE bool setCrossSectionalFactorExposureConfiguration(
        double maximumAbsoluteFactorExposure, const QString &factorExposureAlias);
    Q_INVOKABLE bool setCrossSectionalFactorResearchConfiguration(
        const QString &factorAlias, double lowerPercentile,
        double upperPercentile, int minimumPairs, double oneWayCostBps,
        bool industryNeutralization, int minimumIndustryGroupSize);
    Q_INVOKABLE QVariantMap validateCrossSectionalBacktestRequest(
        const QString &strategyJson, int barCount, int periodSeconds) const;
    Q_INVOKABLE void cancelCrossSectionalBacktest();
    Q_INVOKABLE bool loadCrossSectionalBacktestArchive(const QString &resultId);
    Q_INVOKABLE void refreshCrossSectionalBacktestArchiveList();
    Q_INVOKABLE bool toggleCrossSectionalFactorComparison(
        const QString &resultId);
    Q_INVOKABLE bool isCrossSectionalFactorComparisonSelected(
        const QString &resultId) const;
    Q_INVOKABLE void clearCrossSectionalFactorComparison();
    Q_INVOKABLE bool registerFactorCandidate(
        const QString &candidateId, const QString &name,
        const QString &hypothesis, const QString &sourceKind,
        const QString &sourceReference, const QVariantMap &factorDefinition);
    Q_INVOKABLE bool selectFactorCandidate(const QString &candidateId,
                                           int version);
    Q_INVOKABLE bool freezeFactorCandidate(const QString &candidateId,
                                           int version);
    Q_INVOKABLE bool linkSelectedArchiveToFactorCandidate(bool prospective);
    Q_INVOKABLE void refreshFactorCandidateCatalog();
    Q_INVOKABLE bool setAuthorizedFactorImportFile(const QString &pathOrUrl);
    Q_INVOKABLE qulonglong previewAuthorizedFactorImport(
        const QString &factorKey, const QString &sourceVersion,
        const QString &sourceReference);
    Q_INVOKABLE qulonglong commitAuthorizedFactorImport(
        const QString &factorKey, const QString &sourceVersion,
        const QString &sourceReference);
    Q_INVOKABLE void cancelAuthorizedFactorImport();
    Q_INVOKABLE void refreshExternalFactorDatasetCatalog();
    Q_INVOKABLE bool bindSelectedExternalFactorDataset(
        const QString &datasetId);
    Q_INVOKABLE qulonglong preflightExternalFactorResearch(
        const QString &datasetId, int periodSeconds,
        int minimumPairs = 3, double oneWayCostBps = 0.0);
    Q_INVOKABLE qulonglong startExternalFactorResearch(
        const QString &datasetId, int periodSeconds,
        int minimumPairs = 3, double oneWayCostBps = 0.0);
    Q_INVOKABLE void cancelExternalFactorResearch();
    Q_INVOKABLE void refreshExternalFactorResearchArchives();
    Q_INVOKABLE bool loadExternalFactorResearchArchive(
        const QString &resultId);
    Q_INVOKABLE qulonglong exportCrossSectionalFactorReport(
        const QString &pathOrUrl);
    Q_INVOKABLE void clearBacktestResult();
    Q_INVOKABLE QVariantMap replayBacktestBar(int index) const;
    Q_INVOKABLE bool saveValidationRun(const QString &name);
    Q_INVOKABLE bool loadValidationRun(int index);
    Q_INVOKABLE void setBacktestSampleWindow(const QDateTime &from, const QDateTime &to);
    Q_INVOKABLE bool setBacktestSampleDateRange(const QString &from, const QString &to);
    Q_INVOKABLE void clearBacktestSampleWindow();
    Q_INVOKABLE void refreshBacktestArchiveList();
    Q_INVOKABLE bool loadBacktestArchive(const QString &resultId);
    Q_INVOKABLE qulonglong requestBacktestArchiveRawTickPage(
        const QString &resultId, int offset, int pageSize = 256);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectInstrument(const QString &symbol);
    Q_INVOKABLE bool selectLiveInstrument(const QString &symbol);
    Q_INVOKABLE bool selectLocalBacktestInstrument(const QString &symbol,
                                                    const QString &frequency,
                                                    int minimumBars = 2);
    Q_INVOKABLE QVariantList refreshLocalBacktestDataCatalog();
    Q_INVOKABLE qulonglong requestLocalCandleSeries(
        const QString &symbol, const QString &frequency, int page = 0,
        int pageSize = 1000, int maximumPoints = 800);
    Q_INVOKABLE qulonglong requestLocalCandleSeriesPeriod(
        const QString &symbol, int periodSeconds, int page = 0,
        int pageSize = 1000, int maximumPoints = 800);
    Q_INVOKABLE qulonglong requestOlderLocalCandleSeries();
    Q_INVOKABLE qulonglong prepareLocalBacktestPeriod(
        const QString &symbol, int periodSeconds);
    Q_INVOKABLE qulonglong requestLocalTickSeries(
        const QString &symbol, const QString &tradingDate = {},
        int maximumPoints = 100000);
    Q_INVOKABLE qulonglong requestLiveDayCandleSeries(
        const QString &symbol, int periodSeconds, int maximumPoints = 800);
    Q_INVOKABLE qulonglong requestRealtimeMinuteSeries(const QString &symbol = {});
    Q_INVOKABLE qulonglong requestRealtimeMinuteSeriesPeriod(
        const QString &symbol, int periodSeconds);
    Q_INVOKABLE bool setRealtimeMinutePeriod(int periodSeconds);
    Q_INVOKABLE QString openRealtimeChartWindow(const QString &symbol = {});
    Q_INVOKABLE bool closeRealtimeChartWindow(const QString &windowId);
    Q_INVOKABLE bool setRealtimeChartWindowInstrument(
        const QString &windowId, const QString &symbol);
    Q_INVOKABLE QVariantList searchRealtimeChartWindowInstruments(
        const QString &windowId, const QString &query, int limit = 30) const;
    Q_INVOKABLE QString addRealtimeChart(
        const QString &windowId, int periodSeconds = 0);
    Q_INVOKABLE bool removeRealtimeChart(
        const QString &windowId, const QString &chartId);
    Q_INVOKABLE bool setRealtimeChartPeriod(
        const QString &windowId, const QString &chartId, int periodSeconds);
    Q_INVOKABLE qulonglong refreshRealtimeChartWindow(const QString &windowId);
    Q_INVOKABLE bool selectHistoryDownloadInstrument(const QString &symbol);
    Q_INVOKABLE qulonglong downloadAndStoreHistory(const QString &frequency,
                                                   int targetBars);
    Q_INVOKABLE qulonglong downloadAndStoreHistoryAdjusted(
        const QString &frequency, int targetBars, const QString &adjustment);
    Q_INVOKABLE bool setTickDataImportRootDirectory(const QString &pathOrUrl);
    Q_INVOKABLE bool selectTickDataImportInstrument(const QString &symbol);
    Q_INVOKABLE qulonglong startTickDataImport(const QString &tradingDate,
                                               int format, int periodSeconds,
                                               int fiveMinuteVolumeUnit);
    Q_INVOKABLE void cancelTickDataImport();
    Q_INVOKABLE bool setAShareInstrumentDayImportFile(const QString &pathOrUrl);
    Q_INVOKABLE qulonglong startAShareInstrumentDayImport(
        const QString &source, const QString &sourceVersion);
    Q_INVOKABLE qulonglong previewAShareInstrumentDayImport(
        const QString &source, const QString &sourceVersion);
    Q_INVOKABLE qulonglong commitAShareInstrumentDayImport(
        const QString &source, const QString &sourceVersion,
        const QString &revisionReason);
    Q_INVOKABLE void cancelAShareInstrumentDayImport();
    Q_INVOKABLE void refreshAShareInstrumentDayImportHistory();
    Q_INVOKABLE bool setAShareIndustryImportFile(const QString &pathOrUrl);
    Q_INVOKABLE qulonglong previewAShareIndustryImport(
        const QString &classification, const QString &source,
        const QString &sourceVersion);
    Q_INVOKABLE qulonglong commitAShareIndustryImport(
        const QString &classification, const QString &source,
        const QString &sourceVersion, const QString &revisionReason);
    Q_INVOKABLE void cancelAShareIndustryImport();
    Q_INVOKABLE void refreshAShareIndustryImportHistory();
    Q_INVOKABLE bool prepareTickDataBatchImport(const QString &startDate,
                                                const QString &endDate,
                                                int format, int periodSeconds,
                                                int fiveMinuteVolumeUnit,
                                                bool skipCompleteExisting = true);
    Q_INVOKABLE bool startTickDataBatchImport();
    Q_INVOKABLE void cancelTickDataBatchImport();
    Q_INVOKABLE bool resumeTickDataBatchImport();
    Q_INVOKABLE bool restoreTickDataBatchRetryPlan(
        const QString &runId, bool skipCompleteExisting = true);
    Q_INVOKABLE bool refreshTickDataImportRunHistory(
        const QString &symbolFilter = {}, const QString &statusFilter = {});
    Q_INVOKABLE void searchStocks(const QString &text);
    Q_INVOKABLE bool openInstrumentResearch(const QString &symbol);
    Q_INVOKABLE void addInstrument(const QString &code);
    Q_INVOKABLE void fetchHistory(const QString &frequency, int count);
    Q_INVOKABLE void setStrategyEnabled(const QString &strategyId, bool enabled);
    Q_INVOKABLE void setStrategyParameters(const QString &strategyId, double thresholdPercent, int cooldownSeconds);
    Q_INVOKABLE void loginBroker();
    Q_INVOKABLE void logoutBroker();
    Q_INVOKABLE QString placeOrder(const QString &symbol, const QString &side,
                                   const QString &orderType, double quantity, double limitPrice = 0.0);
    Q_INVOKABLE void cancelOrder(const QString &orderId);
    Q_INVOKABLE void resetPaperAccount(double initialCapital = 1000000.0);
    Q_INVOKABLE void configureRiskLimits(double maxOrderValue, double maxPositionValue,
                                         double maxDailyLoss, int maxActiveOrders);
    Q_INVOKABLE bool approveStrategyIntent(const QString &intentId);
    Q_INVOKABLE bool rejectStrategyIntent(const QString &intentId, const QString &reason = {});
    Q_INVOKABLE bool configureStrategyExecution(const QString &strategyId, int executionMode,
                                                int timeInForce, double fixedOrderValue,
                                                double maxPositionPercent, int approvalTimeoutSeconds,
                                                int orderTimeoutSeconds, int maxOrdersPerDay,
                                                double maxDailyOrderValue);
    Q_INVOKABLE bool configureStrategyOrderPolicy(const QString &strategyId, int orderType,
                                                  bool sellAll, double limitOffsetBps,
                                                  int lotSize);
    Q_INVOKABLE QVariantMap strategyExecutionConfiguration(const QString &strategyId) const;
    Q_INVOKABLE QVariantList strategyExecutionConfigurations() const;
    Q_INVOKABLE QVariantMap notificationSettings() const;
    Q_INVOKABLE bool configureNotifications(bool desktopEnabled, bool webhookEnabled,
                                            const QString &webhookUrl, bool notifySignal,
                                            bool notifyApproval, bool notifyOrderSubmitted,
                                            bool notifyExecutionRejected, bool notifyIntentExpired);
    Q_INVOKABLE bool enableCustomStrategyExecution(const QString &strategyId, int version,
                                                   const QString &symbol);
    Q_INVOKABLE bool disableCustomStrategyExecution(const QString &strategyId);
    Q_INVOKABLE bool isCustomStrategyExecuting(const QString &strategyId) const;
    QVariantList customStrategyExecutionCatalog() const;
    Q_INVOKABLE QVariantList customStrategyRuntimePlugins() const;
    Q_INVOKABLE QVariantList strategyPluginScan();
    Q_INVOKABLE bool loadStrategyPlugin(const QString &fileName);
    Q_INVOKABLE bool unloadStrategyPlugin(const QString &pluginId);
    Q_INVOKABLE QVariantList loadedStrategyPlugins() const;
    Q_INVOKABLE QVariantList strategyPluginParameterSchema(const QString &pluginId) const;
    Q_INVOKABLE bool activateStrategyPluginStrategy(const QString &pluginId,
                                                    const QVariantMap &config);
    Q_INVOKABLE void setStrategyPluginDirectory(const QString &path);
    Q_INVOKABLE QString strategyPluginDirectory() const;
    Q_INVOKABLE qulonglong runParameterGridBacktest(const QString &strategyId, int version,
                                                    const QString &symbol, int periodSeconds,
                                                    int barCount, const QVariantList &axes);
    Q_INVOKABLE void cancelParameterGridBacktest();
    Q_INVOKABLE qulonglong runRawTickParameterSensitivity(
        const QString &strategyId, const QVariantMap &strategyParameters,
        const QString &symbol, const QString &startDate, const QString &endDate,
        int validationTradingDays, const QVariantList &axes);
    Q_INVOKABLE qulonglong runRawTickParameterSensitivity(
        const QString &strategyId, const QVariantMap &strategyParameters,
        const QString &symbol, const QString &startDate, const QString &endDate,
        int validationTradingDays, int walkForwardFoldCount,
        const QVariantList &axes);
    Q_INVOKABLE void cancelRawTickParameterSensitivity();
    Q_INVOKABLE bool loadRawTickParameterSensitivityResult(const QString &resultId);
    Q_INVOKABLE bool startCustomStrategyReplay(const QString &strategyId, const QString &symbol,
                                               int periodSeconds, int barCount, int intervalMs);
    Q_INVOKABLE void stopCustomStrategyReplay();
    Q_INVOKABLE bool requestMarketSnapshot(const QDateTime &requestedTime);
    Q_INVOKABLE bool requestMarketSnapshotAt(const QString &marketTime);
    Q_INVOKABLE bool requestLatestMarketSnapshot();
    Q_INVOKABLE bool reloadAShareUniverse();
    Q_INVOKABLE bool relocateStorage(const QString &pathOrUrl);
    Q_INVOKABLE bool configureNasDataNode(const QString &endpoint);
    Q_INVOKABLE qulonglong requestNasNodeChanges();
    Q_INVOKABLE qulonglong requestNasTickCatalog(
        const QString &symbol, const QString &startDate, const QString &endDate);
    Q_INVOKABLE qulonglong requestNasTickManifest(
        const QString &symbolsText, const QString &startDate, const QString &endDate);
    Q_INVOKABLE qulonglong syncNasTickDay(
        const QString &symbol, const QString &tradingDate);
    Q_INVOKABLE qulonglong syncNasTickRange(const QString &symbol);
    Q_INVOKABLE qulonglong syncNasTickManifest();
    Q_INVOKABLE void cancelNasTickSync();
    Q_INVOKABLE bool resumeNasTickSync();
    Q_INVOKABLE bool configureNasBacktestPreparation(int periodSeconds,
                                                     int minimumBars = 2);
    Q_INVOKABLE qulonglong prepareNasBacktestDatasets();
    Q_INVOKABLE void cancelNasBacktestPreparation();
    Q_INVOKABLE void refreshNasTickCacheSummary();
    Q_INVOKABLE bool pruneNasTickCache(int maximumAgeDays);

signals:
    void paperStrategyStateChanged();
    void liveRunningChanged();
    void backtestRunningChanged();
    void backtestStateChanged();
    void backtestSummaryChanged();
    void backtestSampleWindowChanged();
    void validationRunsChanged();
    void backtestArchiveStateChanged();
    void backtestArchiveRawTickPageStateChanged();
    void backtestConfigurationChanged();
    void batchBacktestStateChanged();
    void rawTickAvailabilityStateChanged();
    void portfolioBacktestStateChanged();
    void crossSectionalBacktestStateChanged();
    void customStrategyStateChanged();
    void customStrategyPreviewStateChanged();
    void customStrategySaveStateChanged();
    void statusTextChanged();
    void stockSearchTextChanged();
    void selectedInstrumentChanged();
    void liveInstrumentChanged();
    void backtestInstrumentChanged();
    void localBacktestDataCatalogChanged();
    void localCandleSeriesStateChanged();
    void localTickSeriesStateChanged();
    void liveDayCandleSeriesStateChanged();
    void realtimeMinuteStateChanged();
    void realtimeChartWindowsChanged();
    void historyDownloadStateChanged();
    void tickDataImportStateChanged();
    void aShareInstrumentDayImportStateChanged();
    void aShareIndustryImportStateChanged();
    void tickDataBatchStateChanged();
    void instrumentResearchRequested(const QString &symbol);
    void selectedMarketSessionChanged();
    void storageStateChanged();
    void nasDataNodeStateChanged();
    void strategyStateChanged();
    void brokerStateChanged();
    void tradingStateChanged();
    void accountStateChanged();
    void riskLimitsChanged();
    void strategyExecutionStateChanged();
    void strategyExecutionConfigChanged(const QString &strategyId);
    void notificationSettingsChanged();
    void customStrategyReplayStateChanged();
    void strategyPluginStateChanged();
    void parameterGridStateChanged();
    void rawTickSensitivityStateChanged();
    void crossSectionalFrameUpdated(const pp::FactorFrame &frame);
    void marketSnapshotChanged();
    void snapshotQueryStateChanged();
    void marketUniverseChanged();
    void providerStateChanged();
    void selectedTickPoint(const QDateTime &timestamp, double price, const QString &symbol);

private:
    void initializePaperTrading();
    void refreshPaperMinuteSubscriptions();
    void clearUnusedPaperMinuteSubscriptions();
    bool canStartPaperStrategy();
    QHash<QString, TencentMinuteBarService *> m_paperMinuteServices;
    QTimer m_paperMinuteTimer;
    QTimer m_paperStateNotifyTimer;
    QString m_paperStrategyError;
    QVector<Instrument> buildWatchlist() const;
    QVector<Instrument> buildMarketUniverse() const;
    Instrument findInstrument(const QString &symbol) const;
    void initializeRealtimeStrategies();
    void restoreNotificationSettings();
    void persistNotificationSettings() const;
    void restartLiveIfRunning();
    void setStatusText(const QString &text);
    void setBacktestStatus(const QString &text, const QString &error = {});
    void handleTick(const Tick &tick);
    void handleMarketSnapshot(const MarketSnapshot &snapshot);
    void handleRealtimeSignal(const TradeSignal &signal);
    void handleStrategySignal(const QString &strategyId, const TradeSignal &signal);
    void handleCustomStrategyReplayTick();
    void handleParameterGridCompleted(quint64 requestId,
                                      const QVector<ParameterGridResult> &results);
    void handleParameterGridFailed(quint64 requestId, const QString &error);
    void handleRawTickSensitivityCompleted(
        quint64 requestId, const RawTickParameterSensitivityResult &result);
    void handleRawTickSensitivityFailed(quint64 requestId, const QString &error);
    void reloadRawTickParameterSensitivityHistory();
    void handleBacktestPoint(const CandlePoint &point);
    void handleBacktestTrade(const BacktestTrade &trade);
    void handleHistoryDownloadReady(quint64 requestId,
                                    const QVector<Candle> &candles,
                                    bool fromCache);
    void handleHistoryDownloadError(quint64 requestId, const QString &message);
    void handleTickDataImportFinished(quint64 requestId,
                                      const TickDataImportResult &result);
    void handleAShareInstrumentDayImportFinished(
        quint64 requestId, const AShareInstrumentDayImportResult &result);
    void handleAShareIndustryImportFinished(
        quint64 requestId, const AShareIndustryImportResult &result);
    void handleAuthorizedFactorImportFinished(
        quint64 requestId, const AuthorizedFactorImportResult &result);
    void handleExternalFactorResearchFinished(
        quint64 requestId,
        const ExternalFactorResearchExecutionResult &result);
    void handleExternalFactorResearchPreflightFinished(
        quint64 requestId,
        const ExternalFactorResearchPreflightResult &result);
    void startNextTickDataBatchFile();
    void handleTickDataBatchFileFinished(const TickDataImportResult &result);
    void recomputeTickDataBatchSummary();
    void finishTickDataBatchRun(const QString &status, const QString &error = {});
    void persistTickDataBatchSummary();
    void reloadTickDataImportRunHistory();
    void handleEquityPoint(const EquityPoint &point);
    void handleBacktestFinished(const PerformanceSummary &summary);
    void handleBacktestCancelled();
    void handleBacktestError(const QString &message);
    void handleBacktestDatasetPrepared(
        quint64 requestId, const BacktestDatasetIdentity &identity,
        const QVector<Candle> &candles);
    void handleBacktestDatasetFailed(quint64 requestId, const QString &message);
    void handleBatchBacktestArchiveFinished(
        quint64 requestId, const QString &symbol,
        const QString &resultId, const QString &error);
    void finishBacktestArchiveLoad(
        quint64 requestId, const BacktestArchiveRecord &record,
        const QString &error);
    void finishBacktestArchiveRawTickPage(
        quint64 requestId, const BacktestRawTickPage &page,
        const QString &error);
    void clearBacktestArchiveRawTickPage();
    void handlePortfolioBacktestCompleted(
        quint64 requestId, const PortfolioResult &result);
    void handleCrossSectionalBacktestCompleted(
        quint64 requestId, const PortfolioResult &result);
    void handleCrossSectionalBacktestFailed(
        quint64 requestId, const QString &message);
    void finishCustomStrategyPreview(quint64 requestId, const QVariantMap &result);
    void finishCustomStrategySave(quint64 requestId, const QVariantMap &result);
    void handleHistoryKline(quint64 requestId, const QVector<Candle> &candles, bool fromCache);
    void handleHistoryError(quint64 requestId, const QString &message);
    void handleOrderChanged(const Order &order);
    void handleExecutionAdded(const Execution &execution);
    void handlePositionChanged(const Position &position);
    void handleAccountChanged(const AccountSnapshot &snapshot);
    void handleTradingError(const QString &message);
    void scheduleTradingStateSave();
    void persistTradingState();
    void ensureStrategyExecutionConfiguration(const QString &strategyId, int cooldownSeconds);
    bool applyMarketSnapshotResult(const MarketSnapshotQueryResult &result);
    quint64 beginSnapshotRequest(const QDateTime &requestedTime);
    bool submitSnapshotRequest(quint64 requestId, const QDateTime &requestedTime);
    void finishSnapshotRequest(quint64 requestId,
                               const MarketSnapshotQueryResult &result);
    void finishLocalCandleSeriesRequest(
        quint64 requestId, const LocalCandleSeriesResult &result);
    void finishLocalTickSeriesRequest(
        quint64 requestId, const LocalTickSeriesResult &result);
    void finishLiveDayCandleSeriesRequest(
        quint64 requestId, const LocalCandleSeriesResult &result);
    void finishRealtimeMinuteRequest(
        quint64 requestId, const RealtimeMinuteSeriesResult &result);
    void scheduleRealtimeMinuteRefresh();
    void rebuildRealtimeMinuteModel();
    void handleProviderState(int health, const QString &message, const QString &error,
                             int expectedCount, int receivedCount,
                             const QString &batchId, const QDateTime &updatedAt);
    void handleProviderRoute(const QString &providerId, const QString &displayName,
                             int score, bool fallbackUsed, int failoverCount,
                             const QString &message,
                             const QVariantList &capabilities);
    void degradeProvider(const QString &error);
    void reloadCustomStrategies();
    bool validateBacktestConfiguration(const QString &frequency, int barCount,
                                       double initialCapital, double commissionRate,
                                       double slippageRate, QString *error) const;
    bool restartStorageServices(const QString &databasePath);
    void finishStorageRelocation(const QString &sourcePath,
                                 const QString &targetPath,
                                 const QString &databaseUuid,
                                 const QString &error);
    bool startNasSyncPlan(const QVector<NasCatalogEntry> &entries,
                          const QString &symbol);
    void startNextNasSyncItem();
    void launchNasDownload(int index);
    void startNextNasImport();
    void finishNasSyncRun(bool cancelled = false);
    void handleNasBacktestPreparationProgress(
        quint64 requestId, int completedSymbols, int totalSymbols,
        const QString &currentSymbol);
    void handleNasBacktestPreparationFinished(
        quint64 requestId, int periodSeconds,
        const QStringList &preparedSymbols, const QVariantMap &failedSymbols,
        qint64 elapsedMilliseconds, bool cancelled);
    void persistNasSyncState();
    void clearNasSyncState();
    qulonglong launchBatchBacktest(const BatchBacktestRequest &request,
                                   const QString &runId);
    void reloadBatchBacktestRunHistory();
    void recoverBatchBacktestRuns();

    TickListModel m_tickModel;
    StockSearchModel m_searchModel;
    CandleListModel m_candleModel;
    QVector<ValidationRun> m_validationRuns;
    QDateTime m_pendingSampleFrom;
    QDateTime m_pendingSampleTo;
    CandleListModel m_historyCandleModel;
    SignalListModel m_signalModel;
    StrategyListModel m_strategyModel;
    StrategyIntentListModel m_strategyIntentModel;
    CustomStrategyListModel m_customStrategyModel;
    BacktestTradeListModel m_backtestTradeModel;
    EquityCurveListModel m_equityCurveModel;
    BacktestArchiveListModel m_backtestArchiveModel;
    LocalTickSeriesListModel m_backtestArchiveRawTickModel;
    BatchBacktestResultListModel m_batchBacktestModel;
    EquityCurveListModel m_portfolioEquityCurveModel;
    PortfolioTradeListModel m_portfolioTradeModel;
    PortfolioArchiveListModel m_portfolioArchiveModel;
    EquityCurveListModel m_crossSectionalEquityCurveModel;
    PortfolioTradeListModel m_crossSectionalTradeModel;
    PortfolioArchiveListModel m_crossSectionalArchiveModel;
    FactorResearchComparisonListModel m_crossSectionalFactorComparisonModel;
    OrderListModel m_orderModel;
    ExecutionListModel m_executionModel;
    PositionListModel m_positionModel;
    MarketSnapshotListModel m_marketSnapshotModel;
    LocalCandleSeriesListModel m_localCandleSeriesModel;
    LocalTickSeriesListModel m_localTickSeriesModel;
    LocalCandleSeriesListModel m_liveDayCandleSeriesModel;
    LocalCandleSeriesListModel m_realtimeMinuteCandleModel;
    RealtimeChartWindowListModel m_realtimeChartWindowModel;
    MarketDataProvider *m_marketData = nullptr;
    SqliteLocalDataStore m_dataStore;
    AsyncMarketSnapshotQueryService m_asyncSnapshotQueryService;
    AsyncLocalCandleSeriesQueryService m_asyncLocalCandleQueryService;
    LocalCandleSeriesRequestGate m_localCandleSeriesRequestGate;
    AsyncLocalTickSeriesQueryService m_asyncLocalTickQueryService;
    LocalTickSeriesRequestGate m_localTickSeriesRequestGate;
    AsyncLocalCandleSeriesQueryService m_asyncLiveDayCandleQueryService;
    LocalCandleSeriesRequestGate m_liveDayCandleSeriesRequestGate;
    AsyncCustomStrategyWorkspaceService m_asyncCustomStrategyWorkspaceService;
    AsyncBacktestArchiveService m_asyncBacktestArchiveService;
    AsyncRawTickAvailabilityService m_asyncRawTickAvailabilityService;
    AsyncOperationRequestGate m_backtestArchiveLoadRequestGate;
    AsyncOperationRequestGate m_backtestArchiveRawTickPageRequestGate;
    AsyncFactorResearchReportService m_asyncFactorResearchReportService;
    AsyncOperationRequestGate m_customStrategyPreviewRequestGate;
    AsyncOperationRequestGate m_customStrategySaveRequestGate;
    BacktestDatasetService m_backtestDatasetService;
    BacktestDatasetService m_nasBacktestPreparationService;
    TencentMinuteBarService m_realtimeMinuteService;
    RealtimeMinuteRequestGate m_realtimeMinuteRequestGate;
    RealtimeLiveSessionState m_realtimeLiveSessionState;
    QTimer m_realtimeMinuteRefreshTimer;
    SnapshotRequestGate m_snapshotRequestGate;
    MarketSnapshot m_latestLiveMarketSnapshot;
    NasDataNodeClient m_nasDataNodeClient;
    CrossSectionalIndicatorEngine m_crossSectionalEngine;
    KlineDataFetcher m_klineFetcher;
    HistoricalDataService m_historicalDataService;
    KlineDataFetcher m_tushareHistoryFetcher;
    KlineDataFetcher m_baostockHistoryFetcher;
    KlineDataFetcher m_historyDownloadFetcher;
    HistoricalDataProviderRouter m_historyDownloadRouter;
    HistoricalDataService m_historyDownloadService;
    TickDataCsvImportService m_tickDataCsvImportService;
    AShareInstrumentDayImportService m_aShareInstrumentDayImportService;
    AShareIndustryImportService m_aShareIndustryImportService;
    AuthorizedFactorImportService m_authorizedFactorImportService;
    ExternalFactorResearchService m_externalFactorResearchService;
    ExternalFactorResearchRequestGate m_externalFactorResearchRequestGate;
    ExternalFactorResearchRequestGate m_externalFactorResearchPreflightRequestGate;
    BacktestEngine m_backtest;
    BatchBacktestService m_batchBacktestService;
    PortfolioBacktestService m_portfolioBacktestService;
    CrossSectionalBacktestService m_crossSectionalBacktestService;
    BacktestStrategyRegistry m_backtestStrategyRegistry;
    CustomStrategyValidator m_customStrategyValidator;
    CustomStrategyCompiler m_customStrategyCompiler;
    SimulatedBrokerGateway m_broker;
    TradingAccountService m_tradingAccount;
    // 运行实例借用账户；逆序析构时必须先于账户和撮合器回收。
    PaperStrategyBook m_paperStrategyBook;
    PaperStrategyBook m_paperReplayBook{nullptr, PaperStrategyBook::InputMode::HistoricalReplay};
    FixedAmountPositionSizer m_positionSizer;
    DefaultSignalExecutionPolicy m_executionPolicy;
    StrategyExecutionCoordinator m_strategyExecutionCoordinator;
    DesktopNotificationService m_desktopNotifications;
    WebhookNotificationService m_webhookNotifications;
    NotificationDispatcher m_notificationDispatcher;
    StrategyEngine m_strategyEngine;
    StrategyPluginManager m_strategyPluginManager;
    QHash<QString, QStringList> m_pluginStrategyIds; // 插件 pluginId → 其创建的策略 id（卸载前停用）
    CustomStrategyRuntimeManager m_customStrategyRuntimeManager;
    WorkspaceInstrumentSelectionState m_workspaceInstrumentSelections;
    InstrumentListModel m_instrumentModel;
    QVector<Instrument> m_aShareUniverse;
    QTimer m_marketClockTimer;
    QTimer m_tradingPersistenceTimer;
    QTimer m_customStrategyReplayTimer;
    QVector<Candle> m_customStrategyReplayCandles;
    int m_customStrategyReplayIndex = 0;
    QString m_customStrategyReplaySymbol;
    ParameterGridBacktestService m_parameterGridBacktestService;
    QVector<ParameterGridResult> m_parameterGridResults;
    QString m_parameterGridError;
    quint64 m_parameterGridRequestId = 0;
    RawTickParameterSensitivityService m_rawTickSensitivityService;
    QVector<RawTickParameterSensitivityResult> m_rawTickSensitivityHistory;
    std::optional<RawTickParameterSensitivityResult> m_rawTickSensitivityResult;
    QString m_rawTickSensitivityError;
    quint64 m_rawTickSensitivityRequestId = 0;
    bool m_shutdownRequested = false;
    BacktestConfig m_pendingBacktestConfig;
    std::optional<CompiledCustomStrategy> m_pendingCustomBacktestPlan;
    std::optional<BacktestResult> m_displayedBacktestResult;
    QDateTime m_displayedBacktestCompletedAt;
    PerformanceSummary m_backtestSummary;
    AccountSnapshot m_accountSnapshot;
    QHash<QString, Tick> m_latestTicks;
    BatchBacktestResultRetention m_batchRetainedResults;
    FactorFrame m_latestCrossSectionalFrame;
    bool m_backtestPreparing = false;
    bool m_tradingStateDirty = false;
    bool m_storageReady = false;
    bool m_storageRelocationRunning = false;
    QString m_storageRelocationError;
    QVector<NasCatalogEntry> m_nasCatalogEntries;
    QStringList m_nasManifestSymbols;
    NasSyncStateStore m_nasSyncStateStore;
    NasTickCacheManager m_nasTickCacheManager;
    NasTickSyncQueue m_nasSyncQueue;
    QVariantMap m_nasDataNodeCacheSummary;
    QVariantMap m_nasNodeChangesSummary;
    bool m_nasDataNodePlanRecovered = false;
    QString m_nasDataNodeEndpoint;
    QString m_nasDataNodeStatusText = QStringLiteral("NAS 数据节点尚未配置");
    QString m_nasDataNodeError;
    bool m_nasDataNodeLoading = false;
    bool m_nasDataNodeSyncRunning = false;
    quint64 m_nasDataNodeRequestVersion = 0;
    quint64 m_nasDownloadRequestCounter = 0;
    quint64 m_nasActiveCatalogRequestId = 0;
    quint64 m_nasActiveDownloadRequestId = 0;
    quint64 m_nasImportRequestId = 0;
    int m_nasMaxConcurrentDownloads = 4;
    int m_nasImportingIndex = -1;
    NasDownloadRequestGate m_nasDownloadRequestGate;
    QHash<int, qint64> m_nasDownloadStartedAtMilliseconds;
    QVector<int> m_nasImportQueue;
    QByteArray m_nasBearerToken;
    qint64 m_nasTokenFileLastModified = 0;
    qint64 m_nasDataNodeReceivedBytes = 0;
    qint64 m_nasDataNodeExpectedBytes = 0;
    qint64 m_nasManifestElapsedMilliseconds = 0;
    qint64 m_nasSyncElapsedMilliseconds = 0;
    int m_nasManifestRequestedSymbols = 0;
    int m_nasManifestAvailableSymbols = 0;
    int m_nasManifestMissingSymbols = 0;
    qint64 m_nasManifestTotalBytes = 0;
    int m_nasBacktestPeriodSeconds = 60;
    int m_nasBacktestMinimumBars = 200;
    bool m_nasBacktestPreparationRunning = false;
    quint64 m_nasBacktestPreparationRequestId = 0;
    int m_nasBacktestPreparationCompletedSymbols = 0;
    int m_nasBacktestPreparationTotalSymbols = 0;
    QString m_nasBacktestPreparationCurrentSymbol;
    QStringList m_nasBacktestPreparedSymbols;
    QVariantMap m_nasBacktestPreparationFailures;
    qint64 m_nasBacktestPreparationElapsedMilliseconds = 0;
    QString m_nasBacktestPreparationStatusText =
        QStringLiteral("尚未准备 NAS 回测数据集");
    QElapsedTimer m_nasManifestTimer;
    QElapsedTimer m_nasSyncTimer;
    QElapsedTimer m_nasPhaseTimer;
    bool m_followLatestSnapshot = true;
    double m_backtestProgress = 0.0;
    int m_persistedTickCount = 0;
    int m_persistedSignalCount = 0;
    QString m_backtestStatusText;
    QString m_backtestError;
    QString m_selectedBacktestStrategyId = QStringLiteral("sma_crossover");
    QVariantMap m_selectedBacktestParameters;
    QString m_backtestFrequency = QStringLiteral("day");
    int m_backtestBarCount = 500;
    double m_backtestConfiguredInitialCapital = 1000000.0;
    double m_backtestCommissionRate = 0.0003;
    double m_backtestSlippageRate = 0.0002;
    QString m_backtestConfigurationError;
    QString m_selectedBacktestArchiveId;
    QString m_backtestArchiveError;
    QString m_backtestArchiveSaveError;
    bool m_backtestResultArchived = false;
    bool m_backtestResultHistoricalSnapshot = false;
    QString m_backtestArchiveRawTickPageResultId;
    int m_backtestArchiveRawTickPageOffset = 0;
    int m_backtestArchiveRawTickPageSize = 256;
    int m_backtestArchiveRawTickTotalCount = 0;
    bool m_backtestArchiveRawTickHasNext = false;
    QString m_backtestArchiveRawTickPageError;
    quint64 m_batchBacktestRequestId = 0;
    int m_batchBacktestRequestedCount = 0;
    int m_batchBacktestCompletedCount = 0;
    int m_batchBacktestSucceededCount = 0;
    int m_batchBacktestFailedCount = 0;
    QString m_batchBacktestStatusText = QStringLiteral("未运行");
    QString m_batchBacktestError;
    bool m_batchBacktestSegmentedResearch = false;
    AsyncOperationRequestGate m_batchBacktestArchiveRequestGate;
    QString m_batchBacktestArchiveSymbol;
    QString m_batchBacktestArchiveResultId;
    QString m_batchBacktestArchiveError;
    QString m_activeBatchBacktestRunId;
    BatchBacktestRunSummary m_activeBatchBacktestRun;
    QVariantList m_batchBacktestRunHistory;
    AsyncOperationRequestGate m_rawTickAvailabilityRequestGate;
    QVariantMap m_rawTickAvailabilityResult;
    QString m_rawTickAvailabilityError;
    quint64 m_portfolioBacktestRequestId = 0;
    QString m_portfolioBacktestStatusText = QStringLiteral("未运行");
    QString m_portfolioBacktestError;
    std::optional<PortfolioResult> m_displayedPortfolioResult;
    QDateTime m_displayedPortfolioCompletedAt;
    bool m_portfolioArchiveLoading = false;
    QString m_portfolioArchiveError;
    QString m_selectedPortfolioArchiveId;
    quint64 m_crossSectionalBacktestRequestId = 0;
    QString m_crossSectionalBacktestStatusText = QStringLiteral("未运行");
    QString m_crossSectionalBacktestError;
    QString m_crossSectionalRiskConfigurationError;
    double m_crossSectionalMaximumLegWeight = 0.35;
    int m_crossSectionalMaximumActiveLegs = 10;
    int m_crossSectionalMaximumConsecutiveMissingBars = 5;
    double m_crossSectionalMaximumPortfolioLossPercent = 0.0;
    double m_crossSectionalMaximumPortfolioDrawdownPercent = 0.0;
    double m_crossSectionalMaximumIndustryWeight = 0.0;
    QString m_crossSectionalIndustryDatasetId;
    double m_crossSectionalMaximumAbsoluteFactorExposure = 0.0;
    QString m_crossSectionalFactorExposureAlias;
    QString m_crossSectionalFactorResearchAlias;
    FactorPreprocessingPolicy m_crossSectionalFactorPreprocessing;
    FactorResearchCostPolicy m_crossSectionalFactorResearchCost;
    FactorNeutralizationPolicy m_crossSectionalFactorNeutralization;
    QVariantList m_crossSectionalFactorDefinitionCatalog;
    std::optional<PortfolioResult> m_displayedCrossSectionalResult;
    QDateTime m_displayedCrossSectionalCompletedAt;
    bool m_crossSectionalArchiveLoading = false;
    QString m_crossSectionalArchiveError;
    QString m_crossSectionalFactorComparisonError;
    QVariantList m_factorCandidateCatalog;
    QVariantList m_factorCandidateEvaluationCatalog;
    QString m_selectedFactorCandidateId;
    int m_selectedFactorCandidateVersion = 0;
    QString m_factorCandidateGovernanceStatusText =
        QStringLiteral("尚未登记候选因子");
    QString m_factorCandidateGovernanceError;
    QVariantMap m_authorizedFactorImportState{
        {QStringLiteral("filePath"), QStringLiteral("")},
        {QStringLiteral("running"), false},
        {QStringLiteral("requestId"), qulonglong(0)},
        {QStringLiteral("previewReady"), false},
        {QStringLiteral("reused"), false},
        {QStringLiteral("factorKey"), QStringLiteral("")},
        {QStringLiteral("sourceVersion"), QStringLiteral("")},
        {QStringLiteral("sourceReference"), QStringLiteral("")},
        {QStringLiteral("contentSha256"), QStringLiteral("")},
        {QStringLiteral("datasetId"), QStringLiteral("")},
        {QStringLiteral("linesRead"), qint64(0)},
        {QStringLiteral("validRecords"), qint64(0)},
        {QStringLiteral("duplicateRecords"), qint64(0)},
        {QStringLiteral("writtenRecords"), qint64(0)},
        {QStringLiteral("statusText"),
         QStringLiteral("未选择授权因子 CSV；不会自动读取或联网")},
        {QStringLiteral("error"), QStringLiteral("")},
        {QStringLiteral("diagnostics"), QVariantList{}}};
    QVariantList m_externalFactorDatasetCatalog;
    QVariantList m_selectedFactorCandidateDatasetBindings;
    QVariantMap m_externalFactorResearchState{
        {QStringLiteral("running"), false},
        {QStringLiteral("requestId"), qulonglong(0)},
        {QStringLiteral("resolvedRequestId"), qulonglong(0)},
        {QStringLiteral("datasetId"), QStringLiteral("")},
        {QStringLiteral("periodSeconds"), 0},
        {QStringLiteral("statusText"),
         QStringLiteral("尚未运行授权外部因子研究；只读取本地 Tick 派生数据")},
        {QStringLiteral("error"), QStringLiteral("")},
        {QStringLiteral("elapsedMilliseconds"), qint64(0)},
        {QStringLiteral("resultId"), QStringLiteral("")}};
    QVariantMap m_externalFactorResearchPreflightState{
        {QStringLiteral("running"), false},
        {QStringLiteral("ready"), false},
        {QStringLiteral("requestId"), qulonglong(0)},
        {QStringLiteral("resolvedRequestId"), qulonglong(0)},
        {QStringLiteral("candidateId"), QStringLiteral("")},
        {QStringLiteral("candidateVersion"), 0},
        {QStringLiteral("datasetId"), QStringLiteral("")},
        {QStringLiteral("periodSeconds"), 0},
        {QStringLiteral("minimumPairs"), 0},
        {QStringLiteral("oneWayCostBps"), 0.0},
        {QStringLiteral("observationCount"), 0},
        {QStringLiteral("timestampCount"), 0},
        {QStringLiteral("symbolCount"), 0},
        {QStringLiteral("readySymbolCount"), 0},
        {QStringLiteral("evaluatedFrameCount"), 0},
        {QStringLiteral("missingSymbols"), QStringList{}},
        {QStringLiteral("statusText"),
         QStringLiteral("尚未预检真实授权数据与本地 Tick 覆盖")},
        {QStringLiteral("error"), QStringLiteral("")},
        {QStringLiteral("elapsedMilliseconds"), qint64(0)}};
    QVariantList m_externalFactorResearchArchiveCatalog;
    QVariantMap m_selectedExternalFactorResearchArchive;
    QString m_authorizedFactorPreviewFilePath;
    QString m_authorizedFactorPreviewFactorKey;
    QString m_authorizedFactorPreviewSourceVersion;
    QString m_authorizedFactorPreviewSourceReference;
    QString m_authorizedFactorPreviewDefinitionSha256;
    QString m_authorizedFactorPreviewContentSha256;
    QString m_selectedCrossSectionalArchiveId;
    bool m_crossSectionalResultHistoricalSnapshot = false;
    bool m_crossSectionalReportExporting = false;
    quint64 m_crossSectionalReportExportRequestId = 0;
    QString m_crossSectionalReportExportStatusText = QStringLiteral("尚未导出报告");
    QString m_crossSectionalReportExportError;
    QString m_crossSectionalReportExportPath;
    QString m_backtestInstrumentSelectionError;
    QString m_localCandleSeriesSymbol;
    QString m_localCandleSeriesFrequency;
    int m_localCandleSeriesPeriodSeconds = 60;
    int m_localCandleSeriesPage = 0;
    int m_localCandleSeriesPageCount = 0;
    int m_localCandleSeriesPageSize = 1000;
    int m_localCandleSeriesMaximumPoints = 800;
    quint64 m_localCandleSeriesPrependRequestId = 0;
    quint64 m_prepareLocalBacktestPeriodRequestId = 0;
    int m_localCandleSeriesTotalBars = 0;
    int m_localCandleSeriesRawBars = 0;
    int m_localCandleSeriesBucketSize = 1;
    bool m_localCandleSeriesDownsampled = false;
    QDateTime m_localCandleSeriesFirstTime;
    QDateTime m_localCandleSeriesLastTime;
    QString m_localCandleSeriesStatusText = QStringLiteral("尚未查询本地分钟序列");
    QString m_localCandleSeriesError;
    QString m_localTickSeriesSymbol;
    QDate m_localTickSeriesTradingDate;
    qint64 m_localTickSeriesRawCount = 0;
    bool m_localTickSeriesDownsampled = false;
    QString m_localTickSeriesStatusText = QStringLiteral("尚未查询逐笔事件序列");
    QString m_localTickSeriesError;
    QString m_liveDayCandleSeriesStatusText = QStringLiteral("尚未读取真实交易日逐笔数据");
    QString m_liveDayCandleSeriesError;
    int m_liveDayCandleSeriesPeriodSeconds = 60;
    QDate m_liveDayCandleSeriesTradingDate;
    QString m_realtimeMinuteStatusText = QStringLiteral("实时分钟行情尚未启动");
    QString m_realtimeMinuteError;
    QString m_realtimeMinuteSymbol;
    QString m_realtimeMinuteSource;
    int m_realtimeMinutePeriodSeconds = 60;
    QVector<Candle> m_realtimeMinuteRawCandles;
    QDateTime m_realtimeMinuteLastBarTime;
    bool m_realtimeMinuteStale = true;
    QString m_realtimeChartWindowError;
    quint64 m_realtimeChartWindowSequence = 0;
    Instrument m_historyDownloadInstrument;
    QString m_historyDownloadFrequency = QStringLiteral("day");
    int m_historyDownloadTargetBars = 500;
    bool m_historyDownloadRunning = false;
    quint64 m_historyDownloadRequestVersion = 0;
    quint64 m_activeHistoryDownloadServiceRequestId = 0;
    QString m_historyDownloadSource;
    QString m_historyDownloadProvider;
    QString m_historyDownloadAdjustment = QStringLiteral("none");
    QString m_historyDownloadDataVersion;
    QDateTime m_historyDownloadDownloadedAt;
    bool m_historyDownloadFallbackUsed = false;
    int m_historyDownloadStoredCount = 0;
    int m_historyDownloadAvailableCount = 0;
    QDateTime m_historyDownloadFirstTime;
    QDateTime m_historyDownloadLastTime;
    QString m_historyDownloadStatusText;
    QString m_historyDownloadError;
    QString m_tickDataImportRootDirectory;
    Instrument m_tickDataImportInstrument;
    QString m_tickDataImportTradingDate;
    int m_tickDataImportFormat = static_cast<int>(TickDataCsvFormat::Tick);
    int m_tickDataImportPeriodSeconds = 60;
    int m_tickDataImportFiveMinuteVolumeUnit = static_cast<int>(FiveMinuteVolumeUnit::Unknown);
    bool m_tickDataImportRunning = false;
    quint64 m_tickDataImportRequestId = 0;
    QString m_tickDataImportFilePath;
    QString m_tickDataImportSource;
    qint64 m_tickDataImportLinesRead = 0;
    qint64 m_tickDataImportValidRecords = 0;
    qint64 m_tickDataImportSkippedRecords = 0;
    int m_tickDataImportWrittenBars = 0;
    qint64 m_tickDataImportRawTicksWritten = 0;
    QDateTime m_tickDataImportFirstTime;
    QDateTime m_tickDataImportLastTime;
    QVariantList m_tickDataImportDiagnostics;
    QString m_tickDataImportStatusText;
    QString m_tickDataImportError;
    QString m_aShareInstrumentDayImportFilePath;
    QString m_aShareInstrumentDayImportSource = QStringLiteral("SSE");
    QString m_aShareInstrumentDayImportSourceVersion;
    bool m_aShareInstrumentDayImportRunning = false;
    quint64 m_aShareInstrumentDayImportRequestId = 0;
    qint64 m_aShareInstrumentDayImportLinesRead = 0;
    qint64 m_aShareInstrumentDayImportValidRecords = 0;
    qint64 m_aShareInstrumentDayImportWrittenRecords = 0;
    qint64 m_aShareInstrumentDayImportNewRecords = 0;
    qint64 m_aShareInstrumentDayImportUnchangedRecords = 0;
    qint64 m_aShareInstrumentDayImportCorrectedRecords = 0;
    bool m_aShareInstrumentDayImportPreviewReady = false;
    QString m_aShareInstrumentDayImportPreviewFilePath;
    QString m_aShareInstrumentDayImportPreviewSource;
    QString m_aShareInstrumentDayImportPreviewSourceVersion;
    QString m_aShareInstrumentDayImportPreviewContentSha256;
    bool m_aShareInstrumentDayImportReused = false;
    QString m_aShareInstrumentDayImportContentSha256;
    QDate m_aShareInstrumentDayImportFirstDate;
    QDate m_aShareInstrumentDayImportLastDate;
    QVariantList m_aShareInstrumentDayImportDiagnostics;
    QVariantList m_aShareInstrumentDayImportHistory;
    QString m_aShareInstrumentDayImportStatusText =
        QStringLiteral("未选择停复牌事实 CSV；不会自动读取或联网");
    QString m_aShareInstrumentDayImportError;
    QVariantMap m_aShareIndustryImportState{
        {QStringLiteral("filePath"), QStringLiteral("")},
        {QStringLiteral("classification"), QStringLiteral("CSRC")},
        {QStringLiteral("source"), QStringLiteral("")},
        {QStringLiteral("sourceVersion"), QStringLiteral("")},
        {QStringLiteral("running"), false},
        {QStringLiteral("requestId"), qulonglong(0)},
        {QStringLiteral("previewReady"), false},
        {QStringLiteral("reused"), false},
        {QStringLiteral("requiresRevisionReason"), false},
        {QStringLiteral("linesRead"), qint64(0)},
        {QStringLiteral("validRecords"), qint64(0)},
        {QStringLiteral("writtenRecords"), qint64(0)},
        {QStringLiteral("fileContentSha256"), QStringLiteral("")},
        {QStringLiteral("datasetContentSha256"), QStringLiteral("")},
        {QStringLiteral("datasetId"), QStringLiteral("")},
        {QStringLiteral("statusText"),
         QStringLiteral("未选择行业分类 CSV；不会自动读取或联网")},
        {QStringLiteral("error"), QStringLiteral("")},
        {QStringLiteral("diagnostics"), QVariantList{}}};
    QVariantList m_aShareIndustryImportHistory;
    QString m_aShareIndustryPreviewFilePath;
    QString m_aShareIndustryPreviewClassification;
    QString m_aShareIndustryPreviewSource;
    QString m_aShareIndustryPreviewSourceVersion;
    QString m_aShareIndustryPreviewFileSha256;
    QVector<TickDataBatchPlanItem> m_tickDataBatchPlan;
    TickDataImportRunSummary m_tickDataBatchSummary;
    QVariantList m_tickDataImportRunHistory;
    bool m_tickDataBatchPlanReady = false;
    bool m_tickDataBatchRunning = false;
    bool m_tickDataBatchCancelRequested = false;
    bool m_tickDataBatchAnyWrite = false;
    int m_tickDataBatchActiveIndex = -1;
    quint64 m_tickDataBatchActiveRequestId = 0;
    QString m_tickDataBatchCurrentDate;
    QString m_tickDataBatchStatusText;
    QString m_tickDataBatchError;
    QString m_tickDataImportRunSymbolFilter;
    QString m_tickDataImportRunStatusFilter;
    QString m_customStrategyError;
    QVector<CustomStrategyDefinition> m_customStrategyDefinitions;
    QVariantMap m_customStrategyPreviewResult;
    QString m_customStrategyPreviewError;
    QVariantMap m_customStrategySaveResult;
    QString m_customStrategySaveError;
    QString m_stockSearchText;
    QString m_tradingError;
    QString m_statusText;
    QDateTime m_requestedSnapshotTime;
    QDateTime m_resolvedSnapshotTime;
    QString m_snapshotBatchId;
    QString m_snapshotError;
    QString m_aShareUniverseCachePath;
    QString m_aShareUniverseError;
    QString m_providerMessage;
    QString m_providerError;
    QString m_providerBatchId;
    QString m_activeRealtimeProvider;
    QString m_activeRealtimeProviderName;
    QString m_realtimeRoutingMessage;
    QVariantList m_realtimeProviderCapabilities;
    QDateTime m_providerUpdatedAt;
    int m_aShareUniverseSourceCount = 0;
    int m_aShareUniverseRejectedCount = 0;
    int m_providerHealth = static_cast<int>(ProviderHealthState::Stopped);
    int m_providerExpectedCount = 0;
    int m_providerReceivedCount = 0;
    int m_realtimeProviderScore = 0;
    int m_realtimeFailoverCount = 0;
    bool m_realtimeFallbackUsed = false;
    int m_snapshotExpectedCount = 0;
    quint64 m_backtestTaskId = 0;
    quint64 m_completedBacktestTaskId = 0;
    quint64 m_pendingBacktestHistoryRequestId = 0;
    quint64 m_pendingBacktestDatasetRequestId = 0;
    quint64 m_activeHistoryRequestId = 0;
    int m_snapshotReceivedCount = 0;
    int m_snapshotHealth = SnapshotEmpty;
    double m_snapshotCompleteness = 0.0;
    qint64 m_snapshotLatencyMilliseconds = 0;
    qint64 m_snapshotStalenessMilliseconds = 0;
};

} // namespace pp
