cmake_minimum_required(VERSION 3.24)

project(PPTrader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/FS)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Qml Quick Sql Widgets)
include(GNUInstallDirs)
include(CTest)

qt_standard_project_setup(REQUIRES 6.4)

# Qt 6.8+ requires an explicit policy for QML files kept below the module root.
# PPTrader still supports Qt 6.4 and Main.qml intentionally imports its
# "components" source directory. QTP0004 NEW adds a redirecting qmldir to that
# directory, which makes the explicit directory import ambiguous under 6.11.
# Keep the cross-version layout stable until the QML module is flattened.
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.8")
    qt_policy(SET QTP0004 OLD)
endif()

qt_add_executable(PPTrader
    WIN32
    src/main.cpp
    src/app/TradingController.cpp
    src/app/TradingControllerPaperTrading.cpp
    src/app/TradingController.h
    src/app/BatchBacktestEvidence.cpp
    src/app/BatchBacktestEvidence.h
    src/app/VisibleBacktestLongrunCoordinator.cpp
    src/app/VisibleBacktestLongrunCoordinator.h
    src/app/CustomStrategyWorkspacePolicy.cpp
    src/app/CustomStrategyWorkspacePolicy.h
    src/app/WorkspaceInstrumentSelectionState.cpp
    src/app/WorkspaceInstrumentSelectionState.h
    src/app/WindowPlacement.cpp
    src/app/WindowPlacement.h
    src/app/RealtimeChartSession.cpp
    src/app/RealtimeChartSession.h
    src/app/RealtimeMultiWindowAcceptanceCoordinator.cpp
    src/app/RealtimeMultiWindowAcceptanceCoordinator.h
    src/domain/BacktestTypes.h
    src/domain/BacktestDatasetTypes.cpp
    src/domain/BacktestDatasetTypes.h
    src/domain/BacktestDataQualityTypes.cpp
    src/domain/BacktestDataQualityTypes.h
    src/domain/AShareCalendarTypes.h
    src/domain/AShareIndustryTypes.h
    src/domain/AShareIndustryImportTypes.h
    src/domain/AShareInstrumentDayImportTypes.h
    src/domain/BatchBacktestTypes.cpp
    src/domain/BatchBacktestTypes.h
    src/domain/CrossSectionalTypes.h
    src/domain/CustomStrategyTypes.cpp
    src/domain/CustomStrategyTypes.h
    src/domain/FactorResearchTypes.h
    src/domain/FactorCandidateTypes.h
    src/domain/ExternalFactorResearchTypes.h
    src/domain/AuthorizedFactorImportTypes.h
    src/domain/HistoricalProviderTypes.cpp
    src/domain/HistoricalProviderTypes.h
    src/domain/LocalHistoryTypes.cpp
    src/domain/LocalHistoryTypes.h
    src/domain/LocalCandleSeriesTypes.h
    src/domain/LocalTickSeriesTypes.h
    src/domain/MarketTypes.h
    src/domain/NasDataNodeTypes.cpp
    src/domain/NasDataNodeTypes.h
    src/domain/PortfolioTypes.cpp
    src/domain/PortfolioTypes.h
    src/domain/RawTradeTypes.h
    src/domain/RawTickSourceQualityTypes.cpp
    src/domain/RawTickSourceQualityTypes.h
    src/domain/RealtimeProviderTypes.cpp
    src/domain/RealtimeProviderTypes.h
    src/domain/RealtimeMinuteSeriesTypes.h
    src/domain/TickDataImportTypes.h
    src/domain/StrategyExecutionTypes.h
    src/domain/TradingTypes.h
    src/models/BacktestTradeListModel.cpp
    src/models/BacktestTradeListModel.h
    src/models/BatchBacktestResultListModel.cpp
    src/models/BatchBacktestResultListModel.h
    src/models/BacktestArchiveListModel.cpp
    src/models/BacktestArchiveListModel.h
    src/models/CandleListModel.cpp
    src/models/CandleListModel.h
    src/models/CustomStrategyListModel.cpp
    src/models/CustomStrategyListModel.h
    src/models/EquityCurveListModel.cpp
    src/models/EquityCurveListModel.h
    src/models/FactorResearchComparisonListModel.cpp
    src/models/FactorResearchComparisonListModel.h
    src/models/ExecutionListModel.cpp
    src/models/ExecutionListModel.h
    src/models/InstrumentListModel.cpp
    src/models/InstrumentListModel.h
    src/models/LocalCandleSeriesListModel.cpp
    src/models/LocalCandleSeriesListModel.h
    src/models/LocalTickSeriesListModel.cpp
    src/models/LocalTickSeriesListModel.h
    src/models/MarketSnapshotListModel.cpp
    src/models/MarketSnapshotListModel.h
    src/models/OrderListModel.cpp
    src/models/OrderListModel.h
    src/models/PositionListModel.cpp
    src/models/PositionListModel.h
    src/models/RealtimeChartWindowListModel.cpp
    src/models/RealtimeChartWindowListModel.h
    src/models/RealtimeChartPaneListModel.cpp
    src/models/RealtimeChartPaneListModel.h
    src/models/PortfolioArchiveListModel.cpp
    src/models/PortfolioArchiveListModel.h
    src/models/PortfolioTradeListModel.cpp
    src/models/PortfolioTradeListModel.h
    src/models/SignalListModel.cpp
    src/models/SignalListModel.h
    src/models/StockSearchModel.cpp
    src/models/StockSearchModel.h
    src/models/StrategyListModel.cpp
    src/models/StrategyListModel.h
    src/models/StrategyIntentListModel.cpp
    src/models/StrategyIntentListModel.h
    src/models/TickListModel.cpp
    src/models/TickListModel.h
    src/plugin/StrategyPluginInterface.h
    src/plugin/StrategyPluginManager.cpp
    src/plugin/StrategyPluginManager.h
    src/services/BacktestEngine.cpp
    src/services/BacktestEngine.h
    src/services/BoardPortfolioBacktestEngine.cpp
    src/services/BoardPortfolioBacktestEngine.h
    src/services/BacktestEvaluator.cpp
    src/services/BacktestEvaluator.h
    src/services/BacktestDatasetService.cpp
    src/services/BacktestDatasetService.h
    src/services/BatchBacktestService.cpp
    src/services/BatchBacktestService.h
    src/services/BatchBacktestResultRetention.h
    src/services/BacktestDataVersion.cpp
    src/services/BacktestDataVersion.h
    src/services/BacktestDataQualityGate.cpp
    src/services/BacktestDataQualityGate.h
    src/services/BacktestDataAvailabilityPlanner.cpp
    src/services/BacktestDataAvailabilityPlanner.h
    src/services/AShareRelativeStrengthIndicator.cpp
    src/services/AShareRelativeStrengthIndicator.h
    src/services/AShareUniverseLoader.cpp
    src/services/AShareUniverseLoader.h
    src/services/AShareInstrumentDayCsvImporter.cpp
    src/services/AShareInstrumentDayCsvImporter.h
    src/services/AShareIndustryCsvImporter.cpp
    src/services/AShareIndustryCsvImporter.h
    src/services/AsyncMarketSnapshotQueryService.cpp
    src/services/AsyncMarketSnapshotQueryService.h
    src/services/AsyncLocalCandleSeriesQueryService.cpp
    src/services/AsyncLocalCandleSeriesQueryService.h
    src/services/AsyncLocalTickSeriesQueryService.cpp
    src/services/AsyncLocalTickSeriesQueryService.h
    src/services/AsyncCustomStrategyWorkspaceService.cpp
    src/services/AsyncCustomStrategyWorkspaceService.h
    src/services/AsyncBacktestArchiveService.cpp
    src/services/AsyncBacktestArchiveService.h
    src/services/AsyncRawTickAvailabilityService.cpp
    src/services/AsyncRawTickAvailabilityService.h
    src/services/BrokerGateway.h
    src/services/IndicatorEngine.cpp
    src/services/IndicatorEngine.h
    src/services/LocalDataStore.h
    src/services/LogService.cpp
    src/services/LogService.h
    src/services/MarketSession.cpp
    src/services/MarketSession.h
    src/services/MarketSnapshotAssembler.cpp
    src/services/MarketSnapshotAssembler.h
    src/services/MarketSnapshotQueryService.cpp
    src/services/MarketSnapshotQueryService.h
    src/services/ManagedProcessTree.cpp
    src/services/ManagedProcessTree.h
    src/services/CompositeMarketDataProvider.cpp
    src/services/CompositeMarketDataProvider.h
    src/services/CrossSectionalIndicator.h
    src/services/CrossSectionalIndicatorEngine.cpp
    src/services/CrossSectionalIndicatorEngine.h
    src/services/CustomStrategyStore.h
    src/services/CustomStrategyPreviewService.cpp
    src/services/CustomStrategyPreviewService.h
    src/services/DefaultExchangeRuleService.cpp
    src/services/DefaultExchangeRuleService.h
    src/services/ExchangeRuleService.h
    src/services/HistoricalDataProvider.h
    src/services/HistoricalDataProviderRouter.cpp
    src/services/HistoricalDataProviderRouter.h
    src/services/HistoricalDataService.cpp
    src/services/HistoricalDataService.h
    src/services/KlineDataFetcher.cpp
    src/services/KlineDataFetcher.h
    src/services/LocalCandleSeriesQueryService.cpp
    src/services/LocalCandleSeriesQueryService.h
    src/services/LocalTickSeriesQueryService.cpp
    src/services/LocalTickSeriesQueryService.h
    src/services/MarketDataProvider.h
    src/services/MootdxMarketDataProvider.cpp
    src/services/MootdxMarketDataProvider.h
    src/services/MootdxCompactSnapshotParser.cpp
    src/services/MootdxCompactSnapshotParser.h
    src/services/PortfolioBacktestEngine.cpp
    src/services/PortfolioBacktestEngine.h
    src/services/PortfolioExecutor.cpp
    src/services/PortfolioExecutor.h
    src/services/PortfolioCircuitBreaker.h
    src/services/AlignedBarSeries.cpp
    src/services/AlignedBarSeries.h
    src/services/CrossSectionalBacktestEngine.cpp
    src/services/CrossSectionalBacktestEngine.h
    src/services/CrossSectionalBacktestService.cpp
    src/services/CrossSectionalBacktestService.h
    src/services/FactorResearchAnalyzer.cpp
    src/services/FactorResearchAnalyzer.h
    src/services/FactorOutOfSampleAnalyzer.cpp
    src/services/FactorOutOfSampleAnalyzer.h
    src/services/ExternalFactorResearchEngine.cpp
    src/services/ExternalFactorResearchEngine.h
    src/services/ExternalFactorResearchService.cpp
    src/services/ExternalFactorResearchService.h
    src/services/FactorCandidateGovernance.h
    src/services/AuthorizedFactorCsvImporter.cpp
    src/services/AuthorizedFactorCsvImporter.h
    src/services/MultipleTestingCorrection.cpp
    src/services/MultipleTestingCorrection.h
    src/services/FactorResearchReportBuilder.cpp
    src/services/FactorResearchReportBuilder.h
    src/services/AsyncFactorResearchReportService.cpp
    src/services/AsyncFactorResearchReportService.h
    src/services/PortfolioBacktestService.cpp
    src/services/PortfolioBacktestService.h
    src/services/ParameterGridScanner.cpp
    src/services/ParameterGridScanner.h
    src/services/ParameterGridBacktestService.cpp
    src/services/ParameterGridBacktestService.h
    src/services/RawTickParameterSensitivityScanner.cpp
    src/services/RawTickParameterSensitivityScanner.h
    src/services/RawTickParameterSensitivityTypes.cpp
    src/services/RawTickParameterSensitivityService.cpp
    src/services/RawTickParameterSensitivityService.h
    src/services/NasDataNodeClient.cpp
    src/services/NasDataNodeClient.h
    src/services/NasSyncStateStore.cpp
    src/services/NasSyncStateStore.h
    src/services/NasTickCacheManager.cpp
    src/services/NasTickCacheManager.h
    src/services/NasTickSyncQueue.cpp
    src/services/NasTickSyncQueue.h
    src/services/DesktopNotificationService.cpp
    src/services/DesktopNotificationService.h
    src/services/NotificationDispatcher.cpp
    src/services/NotificationDispatcher.h
    src/services/NotificationService.h
    src/services/WebhookNotificationService.cpp
    src/services/WebhookNotificationService.h
    src/services/OrderSubmissionPort.h
    src/services/PreTradeRiskService.cpp
    src/services/PreTradeRiskService.h
    src/services/RawTickCandleAggregator.cpp
    src/services/RawTickCandleAggregator.h
    src/services/HistoricalTickSessionRules.h
    src/services/RawTradeIncrementalIngestService.cpp
    src/services/RawTradeIncrementalIngestService.h
    src/services/RealtimeProviderRouter.cpp
    src/services/RealtimeProviderRouter.h
    src/services/TencentMarketDataProvider.cpp
    src/services/TencentMarketDataProvider.h
    src/services/TencentMinuteBarService.cpp
    src/services/TencentMinuteBarService.h
    src/services/TencentQuoteParser.cpp
    src/services/TencentQuoteParser.h
    src/services/SimulatedBrokerGateway.cpp
    src/services/SimulatedBrokerGateway.h
    src/services/SqliteLocalDataStore.cpp
    src/services/SqliteLocalDataStore.h
    src/services/StorageProfile.cpp
    src/services/StorageProfile.h
    src/services/StrategyExecutionStore.h
    src/services/TickDataCsvImporter.cpp
    src/services/TickDataCsvImporter.h
    src/services/RawTickSourceQualityAdapter.cpp
    src/services/RawTickSourceQualityAdapter.h
    src/services/TickDataBatchPlanner.cpp
    src/services/TickDataBatchPlanner.h
    src/services/AShareTradingCalendar.cpp
    src/services/AShareTradingCalendar.h
    src/services/TradingAccountService.cpp
    src/services/TradingAccountService.h
    src/services/PaperStrategyRun.cpp
    src/services/PaperStrategyRun.h
    src/services/PaperStrategyBook.cpp
    src/services/PaperStrategyBook.h
    src/strategy/StrategyEngine.cpp
    src/strategy/StrategyEngine.h
    src/strategy/Strategy.h
    src/strategy/BacktestStrategy.cpp
    src/strategy/BacktestStrategy.h
    src/strategy/BacktestStrategyRegistry.cpp
    src/strategy/BacktestStrategyRegistry.h
    src/strategy/CustomBacktestStrategy.cpp
    src/strategy/CustomBacktestStrategy.h
    src/strategy/CustomExecutionStrategy.cpp
    src/strategy/CustomExecutionStrategy.h
    src/strategy/CrossSectionalIndicatorRuntime.cpp
    src/strategy/CrossSectionalIndicatorRuntime.h
    src/strategy/CrossSectionalCustomStrategy.cpp
    src/strategy/CrossSectionalCustomStrategy.h
    src/strategy/CustomIndicatorRuntime.cpp
    src/strategy/CustomIndicatorRuntime.h
    src/strategy/CustomStrategyCompiler.cpp
    src/strategy/CustomStrategyCompiler.h
    src/strategy/CustomStrategyRuntimeManager.cpp
    src/strategy/CustomStrategyRuntimeManager.h
    src/strategy/CustomStrategyValidator.cpp
    src/strategy/CustomStrategyValidator.h
    src/strategy/BollingerMeanReversionStrategy.cpp
    src/strategy/BollingerMeanReversionStrategy.h
    src/strategy/MacdTrendStrategy.cpp
    src/strategy/MacdTrendStrategy.h
    src/strategy/RsiMeanReversionStrategy.cpp
    src/strategy/RsiMeanReversionStrategy.h
    src/strategy/PairSpreadStrategy.cpp
    src/strategy/PairSpreadStrategy.h
    src/strategy/DefaultSignalExecutionPolicy.cpp
    src/strategy/DefaultSignalExecutionPolicy.h
    src/strategy/FixedAmountPositionSizer.cpp
    src/strategy/FixedAmountPositionSizer.h
    src/strategy/PositionSizer.h
    src/strategy/SignalExecutionPolicy.h
    src/strategy/SmaCrossoverStrategy.cpp
    src/strategy/SmaCrossoverStrategy.h
    src/strategy/StrategyExecutionCoordinator.cpp
    src/strategy/StrategyExecutionCoordinator.h
    src/strategy/TickMomentumStrategy.cpp
    src/strategy/TickMomentumStrategy.h
)

target_include_directories(PPTrader PRIVATE src src/visualization)

target_link_libraries(PPTrader
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Qml
        Qt6::Quick
        Qt6::Sql
        Qt6::Widgets
)

# 这两个脚本只通过相邻 QML 的显式相对导入使用，不是模块级脚本。
# Qt 6.11 会对把它们写入 qmldir 的旧式隐式行为发出重复求值警告。
set_source_files_properties(
    qml/components/CustomStrategyWizardContract.js
    qml/components/CrossSectionalBacktestContract.js
    PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)

qt_add_qml_module(PPTrader
    URI PPTrader
    VERSION 1.0
    RESOURCE_PREFIX "/"
    DEPENDENCIES
        QtQuick
    QML_FILES
        qml/Main.qml
        qml/components/PPTheme.qml
        qml/components/PpButton.qml
        qml/components/WorkspaceTabButton.qml
        qml/components/InstrumentSearchPanel.qml
        qml/components/DashboardPage.qml
        qml/components/LiveInstrumentSelector.qml
        qml/components/RealtimeChartWindow.qml
        qml/components/LocalBacktestSelector.qml
        qml/components/LiveSnapshotRangeChart.qml
        qml/components/CandleChartAxes.qml
        qml/components/CandleChartOverlayToolbar.qml
        qml/components/CandleSeriesChart.qml
        qml/components/CustomStrategyWorkbench.qml
        qml/components/CustomStrategyWizard.qml
        qml/components/CustomStrategyWizardContract.js
        qml/components/LocalHistoryDownloadPanel.qml
        qml/components/NasTickSyncPanel.qml
        qml/components/TickDataImportPanel.qml
        qml/components/TickDataBatchImportPanel.qml
        qml/components/AShareInstrumentDayImportPanel.qml
        qml/components/AShareIndustryImportPanel.qml
        qml/components/AuthorizedFactorImportPanel.qml
        qml/components/LocalMinuteSeriesPanel.qml
        qml/components/TickSeriesChart.qml
        qml/components/StrategyResearchPage.qml
        qml/components/BacktestEquityChart.qml
        qml/components/BacktestCandleChart.qml
        qml/components/BacktestArchivePanel.qml
        qml/components/BatchBacktestPanel.qml
        qml/components/PortfolioBacktestPanel.qml
        qml/components/CrossSectionalBacktestPanel.qml
        qml/components/CrossSectionalBacktestContract.js
        qml/components/AppTopBar.qml
        qml/components/SnapshotTimeControls.qml
        qml/components/SnapshotStateOverlay.qml
        qml/components/ScatterPlot.qml
        qml/components/CrossSectionChartCard.qml
        qml/components/SnapshotSummaryPanel.qml
        qml/components/MarketSnapshotPage.qml
        qml/components/AppStatusBar.qml
        qml/components/ExecutionApprovalPanel.qml
        qml/components/PaperStrategyPanel.qml
        qml/components/StrategyPluginPanel.qml
        qml/components/ParameterGridPanel.qml
    SOURCES
        src/visualization/ChartIndicatorPreferences.cpp
        src/visualization/ChartIndicatorPreferences.h
        src/visualization/CrossSectionPlotData.cpp
        src/visualization/CrossSectionPlotData.h
        src/visualization/CrossSectionScatterItem.cpp
        src/visualization/CrossSectionScatterItem.h
        src/visualization/LocalCandlePlotData.cpp
        src/visualization/LocalCandlePlotData.h
        src/visualization/LocalCandleSeriesItem.cpp
        src/visualization/LocalCandleSeriesItem.h
        src/visualization/LocalTickPlotData.cpp
        src/visualization/LocalTickPlotData.h
        src/visualization/LocalTickSeriesItem.cpp
        src/visualization/LocalTickSeriesItem.h
)

set(PPTRADER_PYTHON_BRIDGES
    scripts/generate_stock_cache.py
    scripts/mootdx_bridge.py
    scripts/mootdx_kline.py
    scripts/baostock_kline.py
    scripts/tushare_kline.py
    scripts/probe_mootdx_snapshot.py
    scripts/nas_data_node.py
)

add_custom_command(TARGET PPTrader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PPTrader>/scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PPTrader>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_stock_cache.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mootdx_bridge.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mootdx_kline.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/baostock_kline.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tushare_kline.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe_mootdx_snapshot.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/nas_data_node.py"
        "$<TARGET_FILE_DIR:PPTrader>/scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/astock_cache.json"
        "$<TARGET_FILE_DIR:PPTrader>/data/astock_cache.json"
    VERBATIM
)

install(TARGETS PPTrader
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${PPTRADER_PYTHON_BRIDGES}
    DESTINATION scripts
)

install(FILES data/astock_cache.json
    DESTINATION data
)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    set(PPTRADER_VENV_PYTHON
        "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
    if(EXISTS "${PPTRADER_VENV_PYTHON}")
        add_test(NAME mootdx_bridge_protocol
            COMMAND "${PPTRADER_VENV_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mootdx_bridge_protocol.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mootdx_bridge.py")
        set_tests_properties(mootdx_bridge_protocol PROPERTIES TIMEOUT 15)
    endif()
    qt_add_executable(tst_accessible_scrollbar_lifecycle
        tests/tst_accessible_scrollbar_lifecycle.cpp)
    target_link_libraries(tst_accessible_scrollbar_lifecycle
        PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME accessible_scrollbar_lifecycle COMMAND tst_accessible_scrollbar_lifecycle)
    set_tests_properties(accessible_scrollbar_lifecycle PROPERTIES TIMEOUT 30
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    qt_add_executable(tst_application_exit tests/tst_application_exit.cpp)
    target_link_libraries(tst_application_exit PRIVATE Qt6::Core Qt6::Test)
    add_dependencies(tst_application_exit PPTrader)
    add_test(NAME application_exit COMMAND tst_application_exit)
    set_tests_properties(application_exit PROPERTIES TIMEOUT 270 RUN_SERIAL TRUE
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME qt_scrollbar_runtime_guard COMMAND ${CMAKE_COMMAND}
        "-DDEPLOY_SCRIPT=${CMAKE_SOURCE_DIR}/cmake/qt-scrollbar-fix/DeployRuntime.cmake"
        "-DTEST_ROOT=${CMAKE_BINARY_DIR}/Testing/qt-scrollbar-runtime"
        -P "${CMAKE_SOURCE_DIR}/tests/tst_qt_scrollbar_runtime.cmake")
    set_tests_properties(qt_scrollbar_runtime_guard PROPERTIES TIMEOUT 30)
    target_sources(PPTrader PRIVATE tests/TradingControllerWizardProbe.cpp src/app/WizardContractProbe.h)
    target_sources(PPTrader PRIVATE tests/TradingControllerPaperProbe.cpp)
    target_sources(PPTrader PRIVATE tests/PaperFlowAcceptance.cpp)
    add_test(NAME paper_strategy_controller
        COMMAND PPTrader --paper-panel-probe ${CMAKE_BINARY_DIR}/paper-controller-probe.json)
    set_tests_properties(paper_strategy_controller PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    set_property(SOURCE tests/TradingControllerWizardProbe.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS PPTRADER_CONTRACT_TESTS)
    add_test(NAME trading_controller_wizard
        COMMAND PPTrader --wizard-contract-probe ${CMAKE_BINARY_DIR}/wizard-contract-probe.json)
    set_tests_properties(trading_controller_wizard PROPERTIES TIMEOUT 60)

    qt_add_executable(tst_market_session
        tests/tst_market_session.cpp
        src/domain/MarketTypes.h
        src/services/MarketSession.cpp
        src/services/MarketSession.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
    )
    target_include_directories(tst_market_session PRIVATE src)
    target_link_libraries(tst_market_session PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_sqlite_local_data_store
        tests/tst_sqlite_local_data_store.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/app/WorkspaceInstrumentSelectionState.cpp
        src/app/WorkspaceInstrumentSelectionState.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/HistoricalProviderTypes.cpp
        src/domain/HistoricalProviderTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/models/BacktestArchiveListModel.cpp
        src/models/BacktestArchiveListModel.h
        src/services/AsyncTickWriter.cpp
        src/services/AsyncTickWriter.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/HistoricalDataProvider.h
        src/services/HistoricalDataService.cpp
        src/services/HistoricalDataService.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StorageProfile.cpp
        src/services/StorageProfile.h
        src/services/RawTickParameterSensitivityTypes.cpp
        src/services/RawTickParameterSensitivityScanner.cpp
        src/services/RawTickParameterSensitivityScanner.h
        src/services/RawTickParameterSensitivityService.cpp
        src/services/RawTickParameterSensitivityService.h
        src/services/ParameterGridScanner.cpp
        src/services/ParameterGridScanner.h
        src/services/BacktestEvaluator.cpp
        src/services/BacktestEvaluator.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_sqlite_local_data_store PRIVATE src)
    target_link_libraries(tst_sqlite_local_data_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_backtest_engine
        tests/tst_backtest_engine.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestTypes.h
        src/domain/MarketTypes.h
        src/models/BacktestTradeListModel.cpp
        src/models/BacktestTradeListModel.h
        src/models/CandleListModel.cpp
        src/models/CandleListModel.h
        src/models/EquityCurveListModel.cpp
        src/models/EquityCurveListModel.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BoardPortfolioBacktestEngine.cpp
        src/services/BoardPortfolioBacktestEngine.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_backtest_engine PRIVATE src)
    target_link_libraries(tst_backtest_engine PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(pptrader_board_portfolio_probe
        tools/board_portfolio_backtest_probe.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/services/BoardPortfolioBacktestEngine.cpp
        src/services/BoardPortfolioBacktestEngine.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(pptrader_board_portfolio_probe PRIVATE src)
    target_link_libraries(pptrader_board_portfolio_probe PRIVATE Qt6::Core Qt6::Sql)

    qt_add_executable(tst_batch_backtest
        tests/tst_batch_backtest.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDataQualityTypes.cpp
        src/domain/BacktestDataQualityTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/BatchBacktestTypes.cpp
        src/domain/BatchBacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/models/BatchBacktestResultListModel.cpp
        src/models/BatchBacktestResultListModel.h
        src/models/CandleListModel.cpp
        src/models/CandleListModel.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/BacktestDataQualityGate.cpp
        src/services/BacktestDataQualityGate.h
        src/services/BacktestDataAvailabilityPlanner.cpp
        src/services/BacktestDataAvailabilityPlanner.h
        src/services/BatchBacktestService.cpp
        src/services/BatchBacktestService.h
        src/services/BatchBacktestResultRetention.h
        src/services/AsyncBacktestArchiveService.cpp
        src/services/AsyncBacktestArchiveService.h
        src/services/AsyncRawTickAvailabilityService.cpp
        src/services/AsyncRawTickAvailabilityService.h
        src/services/CustomStrategyStore.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_batch_backtest PRIVATE src)
    target_link_libraries(tst_batch_backtest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_backtest_data_quality_gate
        tests/tst_backtest_data_quality_gate.cpp
        src/domain/BacktestDataQualityTypes.cpp
        src/domain/BacktestDataQualityTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/AShareCalendarTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataQualityGate.cpp
        src/services/BacktestDataQualityGate.h
        src/services/BacktestDataAvailabilityPlanner.cpp
        src/services/BacktestDataAvailabilityPlanner.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
    )
    target_include_directories(tst_backtest_data_quality_gate PRIVATE src)
    target_link_libraries(tst_backtest_data_quality_gate PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(pptrader_backtest_capacity
        tools/backtest_capacity_probe.cpp
        src/domain/BacktestTypes.h
        src/domain/MarketTypes.h
        src/models/CandleListModel.cpp
        src/models/CandleListModel.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(pptrader_backtest_capacity PRIVATE src)
    target_link_libraries(pptrader_backtest_capacity PRIVATE Qt6::Core Qt6::Sql)
    if(WIN32)
        target_link_libraries(pptrader_backtest_capacity PRIVATE Psapi)
    endif()

    qt_add_executable(pptrader_strategy_compute_longrun
        tools/strategy_compute_longrun_probe.cpp
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/FactorResearchTypes.h
        src/domain/MarketTypes.h
        src/services/FactorResearchAnalyzer.cpp
        src/services/FactorResearchAnalyzer.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/Strategy.h
    )
    target_include_directories(pptrader_strategy_compute_longrun PRIVATE src)
    target_link_libraries(pptrader_strategy_compute_longrun PRIVATE Qt6::Core)
    if(WIN32)
        target_link_libraries(pptrader_strategy_compute_longrun PRIVATE Psapi)
    endif()

    qt_add_executable(pptrader_live_strategy_monitor
        tools/live_strategy_monitor_probe.cpp
        src/domain/RealtimeProviderTypes.cpp
        src/domain/RealtimeProviderTypes.h
        src/domain/MarketTypes.h
        src/domain/CrossSectionalTypes.h
        src/services/AShareUniverseLoader.cpp
        src/services/AShareUniverseLoader.h
        src/services/MarketDataProvider.h
        src/services/MarketSnapshotAssembler.cpp
        src/services/MarketSnapshotAssembler.h
        src/services/TencentMarketDataProvider.cpp
        src/services/TencentMarketDataProvider.h
        src/services/TencentQuoteParser.cpp
        src/services/TencentQuoteParser.h
        src/strategy/Strategy.h
        src/strategy/StrategyEngine.cpp
        src/strategy/StrategyEngine.h
        src/strategy/TickMomentumStrategy.cpp
        src/strategy/TickMomentumStrategy.h
    )
    target_include_directories(pptrader_live_strategy_monitor PRIVATE src)
    target_link_libraries(pptrader_live_strategy_monitor PRIVATE Qt6::Core Qt6::Network)
    if(WIN32)
        target_link_libraries(pptrader_live_strategy_monitor PRIVATE Psapi)
    endif()

    qt_add_executable(pptrader_nas_sync_capacity
        tools/nas_sync_capacity_probe.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/NasDataNodeTypes.cpp
        src/domain/NasDataNodeTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/NasDataNodeClient.cpp
        src/services/NasDataNodeClient.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/services/TickDataCsvImporter.cpp
        src/services/TickDataCsvImporter.h
    )
    target_include_directories(pptrader_nas_sync_capacity PRIVATE src)
    target_link_libraries(pptrader_nas_sync_capacity PRIVATE
        Qt6::Core Qt6::Network Qt6::Sql)

    qt_add_executable(pptrader_raw_tick_source_quality_probe
        tools/raw_tick_source_quality_probe.cpp
        src/domain/RawTickSourceQualityTypes.cpp
        src/domain/RawTickSourceQualityTypes.h
        src/services/RawTickSourceQualityAdapter.cpp
        src/services/RawTickSourceQualityAdapter.h
    )
    target_include_directories(pptrader_raw_tick_source_quality_probe PRIVATE src)
    target_link_libraries(pptrader_raw_tick_source_quality_probe PRIVATE Qt6::Core)

    qt_add_executable(pptrader_real_load_probe
        tools/real_load_backtest_probe.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDataQualityTypes.cpp
        src/domain/BacktestDataQualityTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/BatchBacktestTypes.cpp
        src/domain/BatchBacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/BacktestDataQualityGate.cpp
        src/services/BacktestDataQualityGate.h
        src/services/BacktestDataAvailabilityPlanner.cpp
        src/services/BacktestDataAvailabilityPlanner.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BatchBacktestService.cpp
        src/services/BatchBacktestService.h
        src/services/CustomStrategyStore.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/services/LocalDataStore.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/services/TickDataCsvImporter.cpp
        src/services/TickDataCsvImporter.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(pptrader_real_load_probe PRIVATE src)
    target_link_libraries(pptrader_real_load_probe PRIVATE Qt6::Core Qt6::Sql)

    qt_add_executable(pptrader_raw_tick_sensitivity_probe
        tools/raw_tick_parameter_sensitivity_probe.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestEvaluator.cpp
        src/services/BacktestEvaluator.h
        src/services/CustomStrategyStore.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/services/LocalDataStore.h
        src/services/ParameterGridScanner.cpp
        src/services/ParameterGridScanner.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/RawTickParameterSensitivityScanner.cpp
        src/services/RawTickParameterSensitivityScanner.h
        src/services/RawTickParameterSensitivityService.cpp
        src/services/RawTickParameterSensitivityService.h
        src/services/RawTickParameterSensitivityTypes.cpp
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(pptrader_raw_tick_sensitivity_probe PRIVATE src)
    target_link_libraries(pptrader_raw_tick_sensitivity_probe PRIVATE
        Qt6::Core Qt6::Sql)

    qt_add_executable(pptrader_portfolio_backtest_probe
        tools/portfolio_backtest_probe.cpp
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/PortfolioBacktestEngine.cpp
        src/services/PortfolioBacktestEngine.h
        src/services/PortfolioExecutor.cpp
        src/services/PortfolioExecutor.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(pptrader_portfolio_backtest_probe PRIVATE src)
    target_link_libraries(pptrader_portfolio_backtest_probe PRIVATE Qt6::Core Qt6::Sql)

    qt_add_executable(tst_trading_account
        tests/tst_trading_account.cpp
        src/domain/MarketTypes.h
        src/domain/TradingTypes.h
        src/services/BrokerGateway.h
        src/services/DefaultExchangeRuleService.cpp
        src/services/DefaultExchangeRuleService.h
        src/services/ExchangeRuleService.h
        src/services/MarketSession.cpp
        src/services/MarketSession.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/PreTradeRiskService.cpp
        src/services/PreTradeRiskService.h
        src/services/SimulatedBrokerGateway.cpp
        src/services/SimulatedBrokerGateway.h
        src/services/TradingAccountService.cpp
        src/services/TradingAccountService.h
    )
    target_include_directories(tst_trading_account PRIVATE src)
    target_link_libraries(tst_trading_account PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_strategy_execution
        tests/tst_strategy_execution.cpp
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/services/OrderSubmissionPort.h
        src/services/StrategyExecutionStore.h
        src/strategy/DefaultSignalExecutionPolicy.cpp
        src/strategy/DefaultSignalExecutionPolicy.h
        src/strategy/FixedAmountPositionSizer.cpp
        src/strategy/FixedAmountPositionSizer.h
        src/strategy/PositionSizer.h
        src/strategy/SignalExecutionPolicy.h
        src/strategy/Strategy.h
        src/strategy/StrategyEngine.cpp
        src/strategy/StrategyEngine.h
        src/models/StrategyIntentListModel.cpp
        src/models/StrategyIntentListModel.h
        src/strategy/StrategyExecutionCoordinator.cpp
        src/strategy/StrategyExecutionCoordinator.h
    )
    target_include_directories(tst_strategy_execution PRIVATE src)
    target_link_libraries(tst_strategy_execution PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_notification_service
        tests/tst_notification_service.cpp
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/services/NotificationService.h
        src/services/NotificationDispatcher.cpp
        src/services/NotificationDispatcher.h
        src/services/WebhookNotificationService.cpp
        src/services/WebhookNotificationService.h
    )
    target_include_directories(tst_notification_service PRIVATE src)
    target_link_libraries(tst_notification_service PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    qt_add_executable(tst_strategy_plugin
        tests/tst_strategy_plugin.cpp
        src/domain/MarketTypes.h
        src/plugin/StrategyPluginInterface.h
        src/plugin/StrategyPluginManager.cpp
        src/plugin/StrategyPluginManager.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_strategy_plugin PRIVATE src)
    target_link_libraries(tst_strategy_plugin PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(tst_strategy_plugin PRIVATE
        SAMPLE_PLUGIN_PATH="$<TARGET_FILE:sample_strategy_plugin>")
    add_dependencies(tst_strategy_plugin sample_strategy_plugin)

    qt_add_executable(tst_window_placement
        tests/tst_window_placement.cpp
        src/app/WindowPlacement.cpp
        src/app/WindowPlacement.h
    )
    target_include_directories(tst_window_placement PRIVATE src)
    target_link_libraries(tst_window_placement PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_exchange_rules
        tests/tst_exchange_rules.cpp
        src/domain/MarketTypes.h
        src/domain/TradingTypes.h
        src/services/DefaultExchangeRuleService.cpp
        src/services/DefaultExchangeRuleService.h
        src/services/ExchangeRuleService.h
        src/services/MarketSession.cpp
        src/services/MarketSession.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
    )
    target_include_directories(tst_exchange_rules PRIVATE src)
    target_link_libraries(tst_exchange_rules PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_cross_sectional
        tests/tst_cross_sectional.cpp
        src/domain/CrossSectionalTypes.h
        src/domain/MarketTypes.h
        src/services/AShareRelativeStrengthIndicator.cpp
        src/services/AShareRelativeStrengthIndicator.h
        src/services/CrossSectionalIndicator.h
        src/services/CrossSectionalIndicatorEngine.cpp
        src/services/CrossSectionalIndicatorEngine.h
        src/services/MarketSnapshotAssembler.cpp
        src/services/MarketSnapshotAssembler.h
    )
    target_include_directories(tst_cross_sectional PRIVATE src)
    target_link_libraries(tst_cross_sectional PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_market_snapshot_contract
        tests/tst_market_snapshot_contract.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/models/MarketSnapshotListModel.cpp
        src/models/MarketSnapshotListModel.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/AShareUniverseLoader.cpp
        src/services/AShareUniverseLoader.h
        src/services/AsyncMarketSnapshotQueryService.cpp
        src/services/AsyncMarketSnapshotQueryService.h
        src/services/AsyncTickWriter.cpp
        src/services/AsyncTickWriter.h
        src/services/LocalDataStore.h
        src/services/MarketDataProvider.h
        src/services/MarketSnapshotAssembler.cpp
        src/services/MarketSnapshotAssembler.h
        src/services/MarketSnapshotQueryService.cpp
        src/services/MarketSnapshotQueryService.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_market_snapshot_contract PRIVATE src)
    target_compile_definitions(tst_market_snapshot_contract PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(tst_market_snapshot_contract PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_cross_section_plot_data
        tests/tst_cross_section_plot_data.cpp
        src/visualization/CrossSectionPlotData.cpp
        src/visualization/CrossSectionPlotData.h
    )
    target_include_directories(tst_cross_section_plot_data PRIVATE src)
    target_link_libraries(tst_cross_section_plot_data PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_local_candle_series
        tests/tst_local_candle_series.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalCandleSeriesTypes.h
        src/domain/LocalTickSeriesTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/models/LocalCandleSeriesListModel.cpp
        src/models/LocalCandleSeriesListModel.h
        src/models/LocalTickSeriesListModel.cpp
        src/models/LocalTickSeriesListModel.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/AsyncLocalCandleSeriesQueryService.cpp
        src/services/AsyncLocalCandleSeriesQueryService.h
        src/services/AsyncLocalTickSeriesQueryService.cpp
        src/services/AsyncLocalTickSeriesQueryService.h
        src/services/CustomStrategyStore.h
        src/services/LocalCandleSeriesQueryService.cpp
        src/services/LocalCandleSeriesQueryService.h
        src/services/LocalTickSeriesQueryService.cpp
        src/services/LocalTickSeriesQueryService.h
        src/services/LocalDataStore.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/RawTradeIncrementalIngestService.cpp
        src/services/RawTradeIncrementalIngestService.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/visualization/LocalCandlePlotData.cpp
        src/visualization/LocalCandlePlotData.h
        src/visualization/LocalTickPlotData.cpp
        src/visualization/LocalTickPlotData.h
    )
    target_include_directories(tst_local_candle_series PRIVATE src)
    target_link_libraries(tst_local_candle_series PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_candle_series_item
        tests/tst_candle_series_item.cpp
        src/domain/BacktestTypes.h
        src/domain/LocalCandleSeriesTypes.h
        src/domain/MarketTypes.h
        src/models/CandleListModel.cpp
        src/models/CandleListModel.h
        src/models/LocalCandleSeriesListModel.cpp
        src/models/LocalCandleSeriesListModel.h
        src/visualization/LocalCandlePlotData.cpp
        src/visualization/LocalCandlePlotData.h
        src/visualization/LocalCandleSeriesItem.cpp
        src/visualization/LocalCandleSeriesItem.h
    )
    target_include_directories(tst_candle_series_item PRIVATE src)
    target_link_libraries(tst_candle_series_item PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

    qt_add_executable(tst_chart_indicator_preferences
        tests/tst_chart_indicator_preferences.cpp
        src/visualization/ChartIndicatorPreferences.cpp
        src/visualization/ChartIndicatorPreferences.h
    )
    target_include_directories(tst_chart_indicator_preferences PRIVATE src)
    target_link_libraries(tst_chart_indicator_preferences PRIVATE
        Qt6::Core Qt6::Qml Qt6::Test)

    qt_add_executable(tst_frame_timing_statistics
        tests/tst_frame_timing_statistics.cpp
        src/visualization/FrameTimingStatistics.cpp
        src/visualization/FrameTimingStatistics.h
    )
    target_include_directories(tst_frame_timing_statistics PRIVATE src)
    target_link_libraries(tst_frame_timing_statistics PRIVATE
        Qt6::Core Qt6::Test)

    qt_add_executable(pptrader_chart_frame_probe
        tools/chart_frame_probe.cpp
        src/domain/LocalCandleSeriesTypes.h
        src/domain/MarketTypes.h
        src/models/LocalCandleSeriesListModel.cpp
        src/models/LocalCandleSeriesListModel.h
        src/visualization/FrameTimingStatistics.cpp
        src/visualization/FrameTimingStatistics.h
        src/visualization/LocalCandlePlotData.cpp
        src/visualization/LocalCandlePlotData.h
        src/visualization/LocalCandleSeriesItem.cpp
        src/visualization/LocalCandleSeriesItem.h
    )
    target_include_directories(pptrader_chart_frame_probe PRIVATE src)
    target_link_libraries(pptrader_chart_frame_probe PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    if(WIN32)
        target_link_libraries(pptrader_chart_frame_probe PRIVATE Psapi)
    endif()

    qt_add_executable(tst_backtest_dataset_identity
        tests/tst_backtest_dataset_identity.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_backtest_dataset_identity PRIVATE src)
    target_link_libraries(tst_backtest_dataset_identity PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_backtest_dataset_service
        tests/tst_backtest_dataset_service.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/BacktestDatasetService.cpp
        src/services/BacktestDatasetService.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_backtest_dataset_service PRIVATE src)
    target_link_libraries(tst_backtest_dataset_service PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_cross_section_performance
        tests/tst_cross_section_performance.cpp
        src/domain/CrossSectionalTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/models/MarketSnapshotListModel.cpp
        src/models/MarketSnapshotListModel.h
        src/visualization/CrossSectionPlotData.cpp
        src/visualization/CrossSectionPlotData.h
        src/visualization/CrossSectionScatterItem.cpp
        src/visualization/CrossSectionScatterItem.h
    )
    target_include_directories(tst_cross_section_performance PRIVATE src)
    target_link_libraries(tst_cross_section_performance PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

    qt_add_executable(tst_custom_strategy
        tests/tst_custom_strategy.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/app/CustomStrategyWorkspacePolicy.cpp
        src/app/CustomStrategyWorkspacePolicy.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/models/CustomStrategyListModel.cpp
        src/models/CustomStrategyListModel.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyPreviewService.cpp
        src/services/CustomStrategyPreviewService.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/BacktestStrategy.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_custom_strategy PRIVATE src)
    target_link_libraries(tst_custom_strategy PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_async_custom_strategy_workspace
        tests/tst_async_custom_strategy_workspace.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AsyncCustomStrategyWorkspaceService.cpp
        src/services/AsyncCustomStrategyWorkspaceService.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyPreviewService.cpp
        src/services/CustomStrategyPreviewService.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/strategy/BacktestStrategy.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_async_custom_strategy_workspace PRIVATE src)
    target_link_libraries(tst_async_custom_strategy_workspace PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test)

    # 保存、重载和真实引擎执行必须在同一离线行为测试中贯通。
    get_target_property(custom_lifecycle_sources tst_async_custom_strategy_workspace SOURCES)
    list(FILTER custom_lifecycle_sources EXCLUDE REGEX "tests/tst_async_custom_strategy_workspace\\.cpp$")
    qt_add_executable(tst_custom_strategy_lifecycle
        tests/tst_custom_strategy_lifecycle.cpp
        ${custom_lifecycle_sources}
        src/services/TickDataCsvImporter.cpp
        src/services/TickDataCsvImporter.h
        src/services/BacktestDatasetService.cpp
        src/services/BacktestDatasetService.h
        src/services/RawTickCandleAggregator.cpp
        src/services/RawTickCandleAggregator.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestEvaluator.cpp
        src/services/BacktestEvaluator.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
    )
    target_include_directories(tst_custom_strategy_lifecycle PRIVATE src)
    target_compile_definitions(tst_custom_strategy_lifecycle PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_custom_strategy_lifecycle PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test Qt6::Qml)

    qt_add_executable(tst_custom_strategy_wizard
        tests/tst_custom_strategy_wizard.cpp
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
    )
    target_include_directories(tst_custom_strategy_wizard PRIVATE src)
    target_compile_definitions(tst_custom_strategy_wizard PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_custom_strategy_wizard PRIVATE
        Qt6::Core Qt6::Qml Qt6::Test)

    qt_add_executable(tst_custom_strategy_wizard_qml
        tests/tst_custom_strategy_wizard_qml.cpp
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
    )
    target_include_directories(tst_custom_strategy_wizard_qml PRIVATE src)
    target_compile_definitions(tst_custom_strategy_wizard_qml PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_custom_strategy_wizard_qml PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

    qt_add_executable(tst_paper_strategy_run
        tests/tst_paper_strategy_run.cpp
        src/services/PaperStrategyBook.cpp
        src/services/PaperStrategyBook.h
        src/services/PaperStrategyRun.cpp
        src/services/PaperStrategyRun.h
        src/strategy/CustomExecutionStrategy.cpp
        src/strategy/CustomExecutionStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyValidator.cpp
        src/domain/CustomStrategyTypes.cpp
        src/strategy/Strategy.h
        src/services/SimulatedBrokerGateway.cpp
        src/services/BrokerGateway.h
        src/services/TradingAccountService.cpp
        src/services/DefaultExchangeRuleService.cpp
        src/services/PreTradeRiskService.cpp
        src/services/MarketSession.cpp
        src/services/AShareTradingCalendar.cpp
    )
    target_include_directories(tst_paper_strategy_run PRIVATE src)
    target_link_libraries(tst_paper_strategy_run PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME paper_strategy_run COMMAND tst_paper_strategy_run)

    qt_add_executable(tst_custom_strategy_execution
        tests/tst_custom_strategy_execution.cpp
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/services/OrderSubmissionPort.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomExecutionStrategy.cpp
        src/strategy/CustomExecutionStrategy.h
        src/strategy/CustomStrategyRuntimeManager.cpp
        src/strategy/CustomStrategyRuntimeManager.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/Strategy.h
        src/strategy/StrategyEngine.cpp
        src/strategy/StrategyEngine.h
        src/strategy/StrategyExecutionCoordinator.cpp
        src/strategy/StrategyExecutionCoordinator.h
        src/strategy/DefaultSignalExecutionPolicy.cpp
        src/strategy/DefaultSignalExecutionPolicy.h
        src/strategy/FixedAmountPositionSizer.cpp
        src/strategy/FixedAmountPositionSizer.h
        src/strategy/PositionSizer.h
        src/strategy/SignalExecutionPolicy.h
    )
    target_include_directories(tst_custom_strategy_execution PRIVATE src)
    target_link_libraries(tst_custom_strategy_execution PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_strategy_plugin_panel_qml
        tests/tst_strategy_plugin_panel_qml.cpp
    )
    target_include_directories(tst_strategy_plugin_panel_qml PRIVATE src)
    target_compile_definitions(tst_strategy_plugin_panel_qml PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_strategy_plugin_panel_qml PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

    qt_add_executable(tst_backtest_archive_panel_qml
        tests/tst_backtest_archive_panel_qml.cpp
        src/models/BacktestArchiveListModel.cpp
        src/models/BacktestArchiveListModel.h
        src/models/LocalTickSeriesListModel.cpp
        src/models/LocalTickSeriesListModel.h
    )
    target_include_directories(tst_backtest_archive_panel_qml PRIVATE src)
    target_compile_definitions(tst_backtest_archive_panel_qml PRIVATE
        PPTRADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tst_backtest_archive_panel_qml PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)

    qt_add_executable(tst_parameter_grid_scanner
        tests/tst_parameter_grid_scanner.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/MarketTypes.h
        src/services/BacktestEvaluator.cpp
        src/services/BacktestEvaluator.h
        src/services/ParameterGridScanner.cpp
        src/services/ParameterGridScanner.h
        src/services/RawTickParameterSensitivityScanner.cpp
        src/services/RawTickParameterSensitivityScanner.h
        src/services/RawTickParameterSensitivityTypes.cpp
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/IndicatorEngine.cpp
        src/services/IndicatorEngine.h
        src/strategy/BacktestStrategy.h
        src/strategy/BacktestStrategy.cpp
        src/strategy/BacktestStrategyRegistry.cpp
        src/strategy/BacktestStrategyRegistry.h
        src/strategy/BollingerMeanReversionStrategy.cpp
        src/strategy/BollingerMeanReversionStrategy.h
        src/strategy/MacdTrendStrategy.cpp
        src/strategy/MacdTrendStrategy.h
        src/strategy/RsiMeanReversionStrategy.cpp
        src/strategy/RsiMeanReversionStrategy.h
        src/strategy/SmaCrossoverStrategy.cpp
        src/strategy/SmaCrossoverStrategy.h
        src/strategy/CustomBacktestStrategy.cpp
        src/strategy/CustomBacktestStrategy.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/Strategy.h
    )
    target_include_directories(tst_parameter_grid_scanner PRIVATE src)
    target_link_libraries(tst_parameter_grid_scanner PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_log_service
        tests/tst_log_service.cpp
        src/services/LogService.cpp
        src/services/LogService.h
    )
    target_include_directories(tst_log_service PRIVATE src)
    target_link_libraries(tst_log_service PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_workspace_instrument_selection
        tests/tst_workspace_instrument_selection.cpp
        src/app/WorkspaceInstrumentSelectionState.cpp
        src/app/WorkspaceInstrumentSelectionState.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
    )
    target_include_directories(tst_workspace_instrument_selection PRIVATE src)
    target_link_libraries(tst_workspace_instrument_selection PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_portfolio_backtest
        tests/tst_portfolio_backtest.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/MarketTypes.h
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/PortfolioBacktestEngine.cpp
        src/services/PortfolioBacktestEngine.h
        src/services/PortfolioExecutor.cpp
        src/services/PortfolioExecutor.h
    )
    target_include_directories(tst_portfolio_backtest PRIVATE src)
    target_link_libraries(tst_portfolio_backtest PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_aligned_bar_series
        tests/tst_aligned_bar_series.cpp
        src/domain/MarketTypes.h
        src/services/AlignedBarSeries.cpp
        src/services/AlignedBarSeries.h
    )
    target_include_directories(tst_aligned_bar_series PRIVATE src)
    target_link_libraries(tst_aligned_bar_series PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_cross_sectional_backtest
        tests/tst_cross_sectional_backtest.cpp
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/AlignedBarSeries.cpp
        src/services/AlignedBarSeries.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CrossSectionalBacktestEngine.cpp
        src/services/CrossSectionalBacktestEngine.h
        src/services/CrossSectionalBacktestService.cpp
        src/services/CrossSectionalBacktestService.h
        src/services/FactorResearchAnalyzer.cpp
        src/services/FactorResearchAnalyzer.h
        src/services/FactorOutOfSampleAnalyzer.cpp
        src/services/FactorOutOfSampleAnalyzer.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/PortfolioExecutor.cpp
        src/services/PortfolioExecutor.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/strategy/CrossSectionalIndicatorRuntime.cpp
        src/strategy/CrossSectionalIndicatorRuntime.h
        src/strategy/CrossSectionalCustomStrategy.cpp
        src/strategy/CrossSectionalCustomStrategy.h
        src/strategy/CustomStrategyCompiler.cpp
        src/strategy/CustomStrategyCompiler.h
        src/strategy/CustomStrategyValidator.cpp
        src/strategy/CustomStrategyValidator.h
        src/strategy/CustomIndicatorRuntime.cpp
        src/strategy/CustomIndicatorRuntime.h
    )
    target_include_directories(tst_cross_sectional_backtest PRIVATE src)
    target_link_libraries(tst_cross_sectional_backtest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_factor_research
        tests/tst_factor_research.cpp
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/FactorResearchTypes.h
        src/domain/AuthorizedFactorImportTypes.h
        src/domain/ExternalFactorResearchTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/models/FactorResearchComparisonListModel.cpp
        src/models/FactorResearchComparisonListModel.h
        src/services/FactorResearchAnalyzer.cpp
        src/services/FactorResearchAnalyzer.h
        src/services/FactorOutOfSampleAnalyzer.cpp
        src/services/FactorOutOfSampleAnalyzer.h
        src/services/ExternalFactorResearchEngine.cpp
        src/services/ExternalFactorResearchEngine.h
        src/services/FactorResearchReportBuilder.cpp
        src/services/FactorResearchReportBuilder.h
        src/services/MultipleTestingCorrection.cpp
        src/services/MultipleTestingCorrection.h
        src/services/AsyncFactorResearchReportService.cpp
        src/services/AsyncFactorResearchReportService.h
    )
    target_include_directories(tst_factor_research PRIVATE src)
    target_link_libraries(tst_factor_research PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_external_factor_research_service
        tests/tst_external_factor_research_service.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/domain/FactorCandidateTypes.h
        src/domain/AuthorizedFactorImportTypes.h
        src/domain/ExternalFactorResearchTypes.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/services/FactorResearchAnalyzer.cpp
        src/services/FactorResearchAnalyzer.h
        src/services/FactorOutOfSampleAnalyzer.cpp
        src/services/FactorOutOfSampleAnalyzer.h
        src/services/ExternalFactorResearchEngine.cpp
        src/services/ExternalFactorResearchEngine.h
        src/services/ExternalFactorResearchService.cpp
        src/services/ExternalFactorResearchService.h
    )
    target_include_directories(tst_external_factor_research_service PRIVATE src)
    target_link_libraries(tst_external_factor_research_service PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_portfolio_backtest_store
        tests/tst_portfolio_backtest_store.cpp
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/models/FactorResearchComparisonListModel.cpp
        src/models/FactorResearchComparisonListModel.h
        src/services/MultipleTestingCorrection.cpp
        src/services/MultipleTestingCorrection.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/PortfolioBacktestEngine.cpp
        src/services/PortfolioBacktestEngine.h
        src/services/PortfolioBacktestService.cpp
        src/services/PortfolioBacktestService.h
        src/services/PortfolioExecutor.cpp
        src/services/PortfolioExecutor.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_portfolio_backtest_store PRIVATE src)
    target_link_libraries(tst_portfolio_backtest_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_tickdata_csv_import
        tests/tst_tickdata_csv_import.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
        src/services/TickDataCsvImporter.cpp
        src/services/TickDataCsvImporter.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/TickDataBatchPlanner.cpp
        src/services/TickDataBatchPlanner.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
    )
    target_include_directories(tst_tickdata_csv_import PRIVATE src)
    target_link_libraries(tst_tickdata_csv_import PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_raw_tick_source_quality_adapter
        tests/tst_raw_tick_source_quality_adapter.cpp
        src/domain/RawTickSourceQualityTypes.cpp
        src/domain/RawTickSourceQualityTypes.h
        src/services/RawTickSourceQualityAdapter.cpp
        src/services/RawTickSourceQualityAdapter.h
    )
    target_include_directories(tst_raw_tick_source_quality_adapter PRIVATE src)
    target_link_libraries(tst_raw_tick_source_quality_adapter PRIVATE
        Qt6::Core Qt6::Test)

    qt_add_executable(tst_ashare_instrument_day_import
        tests/tst_ashare_instrument_day_import.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/HistoricalProviderTypes.cpp
        src/domain/HistoricalProviderTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/domain/AShareCalendarTypes.h
        src/domain/AShareInstrumentDayImportTypes.h
        src/services/AShareInstrumentDayCsvImporter.cpp
        src/services/AShareInstrumentDayCsvImporter.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/AShareUniverseLoader.cpp
        src/services/AShareUniverseLoader.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_ashare_instrument_day_import PRIVATE src)
    target_link_libraries(tst_ashare_instrument_day_import PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_ashare_industry_import
        tests/tst_ashare_industry_import.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/HistoricalProviderTypes.cpp
        src/domain/HistoricalProviderTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/domain/AShareCalendarTypes.h
        src/domain/AShareIndustryTypes.h
        src/domain/AShareIndustryImportTypes.h
        src/services/AShareIndustryCsvImporter.cpp
        src/services/AShareIndustryCsvImporter.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/AShareUniverseLoader.cpp
        src/services/AShareUniverseLoader.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_ashare_industry_import PRIVATE src)
    target_link_libraries(tst_ashare_industry_import PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_authorized_factor_import
        tests/tst_authorized_factor_import.cpp
        src/domain/PortfolioTypes.cpp
        src/domain/PortfolioTypes.h
        src/domain/BacktestDatasetTypes.cpp
        src/domain/BacktestDatasetTypes.h
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/HistoricalProviderTypes.cpp
        src/domain/HistoricalProviderTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TickDataImportTypes.h
        src/domain/TradingTypes.h
        src/domain/FactorCandidateTypes.h
        src/domain/AuthorizedFactorImportTypes.h
        src/services/AuthorizedFactorCsvImporter.cpp
        src/services/AuthorizedFactorCsvImporter.h
        src/services/AShareTradingCalendar.cpp
        src/services/AShareTradingCalendar.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/CustomStrategyStore.h
        src/services/FactorCandidateGovernance.h
        src/services/LocalDataStore.h
        src/services/SqliteLocalDataStore.cpp
        src/services/SqliteLocalDataStore.h
        src/services/StrategyExecutionStore.h
    )
    target_include_directories(tst_authorized_factor_import PRIVATE src)
    target_link_libraries(tst_authorized_factor_import PRIVATE
        Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_nas_data_node
        tests/tst_nas_data_node.cpp
        src/domain/NasDataNodeTypes.cpp
        src/domain/NasDataNodeTypes.h
        src/services/NasDataNodeClient.cpp
        src/services/NasDataNodeClient.h
        src/services/NasSyncStateStore.cpp
        src/services/NasSyncStateStore.h
        src/services/NasTickCacheManager.cpp
        src/services/NasTickCacheManager.h
        src/services/NasTickSyncQueue.cpp
        src/services/NasTickSyncQueue.h
    )
    target_include_directories(tst_nas_data_node PRIVATE src)
    target_link_libraries(tst_nas_data_node PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    qt_add_executable(tst_historical_provider_router
        tests/tst_historical_provider_router.cpp
        src/domain/HistoricalProviderTypes.cpp
        src/domain/HistoricalProviderTypes.h
        src/domain/MarketTypes.h
        src/services/HistoricalDataProvider.h
        src/services/HistoricalDataProviderRouter.cpp
        src/services/HistoricalDataProviderRouter.h
    )
    target_include_directories(tst_historical_provider_router PRIVATE src)
    target_link_libraries(tst_historical_provider_router PRIVATE Qt6::Core Qt6::Test)

    qt_add_executable(tst_realtime_provider_router
        tests/tst_realtime_provider_router.cpp
        src/domain/RealtimeProviderTypes.cpp
        src/domain/RealtimeProviderTypes.h
        src/domain/RealtimeMinuteSeriesTypes.h
        src/domain/MarketTypes.h
        src/services/MarketDataProvider.h
        src/services/MootdxCompactSnapshotParser.cpp
        src/services/MootdxCompactSnapshotParser.h
        src/services/ManagedProcessTree.cpp
        src/services/ManagedProcessTree.h
        src/services/RealtimeProviderRouter.cpp
        src/services/RealtimeProviderRouter.h
        src/services/TencentQuoteParser.cpp
        src/services/TencentQuoteParser.h
        src/services/TencentMarketDataProvider.cpp
        src/services/TencentMarketDataProvider.h
        src/services/MarketSnapshotAssembler.cpp
        src/services/MarketSnapshotAssembler.h
        src/services/TencentMinuteBarService.cpp
        src/services/TencentMinuteBarService.h
    )
    target_include_directories(tst_realtime_provider_router PRIVATE src)
    target_link_libraries(tst_realtime_provider_router PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    qt_add_executable(tst_realtime_chart_session
        tests/tst_realtime_chart_session.cpp
        src/app/RealtimeChartSession.cpp
        src/app/RealtimeChartSession.h
        src/domain/LocalCandleSeriesTypes.h
        src/domain/RealtimeMinuteSeriesTypes.h
        src/domain/MarketTypes.h
        src/models/LocalCandleSeriesListModel.cpp
        src/models/LocalCandleSeriesListModel.h
        src/models/RealtimeChartWindowListModel.cpp
        src/models/RealtimeChartWindowListModel.h
        src/models/RealtimeChartPaneListModel.cpp
        src/models/RealtimeChartPaneListModel.h
        src/services/MarketDataProvider.h
        src/services/TencentMinuteBarService.cpp
        src/services/TencentMinuteBarService.h
        src/services/TencentQuoteParser.cpp
        src/services/TencentQuoteParser.h
    )
    target_include_directories(tst_realtime_chart_session PRIVATE src)
    target_link_libraries(tst_realtime_chart_session
        PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    qt_add_executable(tst_batch_backtest_evidence
        tests/tst_batch_backtest_evidence.cpp
        src/app/BatchBacktestEvidence.cpp
        src/app/BatchBacktestEvidence.h
    )
    target_include_directories(tst_batch_backtest_evidence PRIVATE src)
    target_link_libraries(tst_batch_backtest_evidence PRIVATE Qt6::Core Qt6::Test)

    # SqliteLocalDataStore 持久化原始 Tick 参数研究记录；所有独立编译该存储实现的
    # 测试/探针目标都必须同时链接其稳定 QVariant 序列化实现。
    foreach(store_target IN ITEMS
        tst_batch_backtest
        pptrader_nas_sync_capacity
        pptrader_real_load_probe
        pptrader_portfolio_backtest_probe
        tst_market_snapshot_contract
        tst_local_candle_series
        tst_backtest_dataset_identity
        tst_backtest_dataset_service
        tst_custom_strategy
        tst_async_custom_strategy_workspace
        tst_custom_strategy_lifecycle
        tst_cross_sectional_backtest
        tst_external_factor_research_service
        tst_portfolio_backtest_store
        tst_tickdata_csv_import
        tst_ashare_instrument_day_import
        tst_ashare_industry_import
        tst_authorized_factor_import)
        target_sources(${store_target} PRIVATE
            src/services/RawTickParameterSensitivityTypes.cpp)
    endforeach()

    add_test(NAME market_session COMMAND tst_market_session)
    add_test(NAME sqlite_local_data_store COMMAND tst_sqlite_local_data_store)
    add_test(NAME backtest_engine COMMAND tst_backtest_engine)
    # CTest must use the exact Qt runtime linked by this build, even when a
    # developer shell has another supported Qt release earlier on PATH.
    set_tests_properties(backtest_engine PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME batch_backtest COMMAND tst_batch_backtest)
    add_test(NAME backtest_data_quality_gate COMMAND tst_backtest_data_quality_gate)
    add_test(NAME trading_account COMMAND tst_trading_account)
    add_test(NAME strategy_execution COMMAND tst_strategy_execution)
    add_test(NAME notification_service COMMAND tst_notification_service)
    add_test(NAME strategy_plugin COMMAND tst_strategy_plugin)
    add_test(NAME window_placement COMMAND tst_window_placement)
    add_test(NAME exchange_rules COMMAND tst_exchange_rules)
    add_test(NAME cross_sectional COMMAND tst_cross_sectional)
    add_test(NAME market_snapshot_contract COMMAND tst_market_snapshot_contract)
    add_test(NAME cross_section_plot_data COMMAND tst_cross_section_plot_data)
    add_test(NAME local_candle_series COMMAND tst_local_candle_series)
    add_test(NAME candle_series_item COMMAND tst_candle_series_item)
    add_test(NAME chart_indicator_preferences COMMAND tst_chart_indicator_preferences)
    add_test(NAME frame_timing_statistics COMMAND tst_frame_timing_statistics)
    add_test(NAME backtest_dataset_identity COMMAND tst_backtest_dataset_identity)
    add_test(NAME backtest_dataset_service COMMAND tst_backtest_dataset_service)
    add_test(NAME cross_section_performance COMMAND tst_cross_section_performance)
    add_test(NAME custom_strategy COMMAND tst_custom_strategy)
    add_test(NAME async_custom_strategy_workspace COMMAND tst_async_custom_strategy_workspace)
    add_test(NAME custom_strategy_lifecycle COMMAND tst_custom_strategy_lifecycle)
    set_tests_properties(custom_strategy_lifecycle PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME custom_strategy_wizard COMMAND tst_custom_strategy_wizard)
    add_test(NAME custom_strategy_wizard_qml COMMAND tst_custom_strategy_wizard_qml)
    add_test(NAME custom_strategy_execution COMMAND tst_custom_strategy_execution)
    add_test(NAME strategy_plugin_panel_qml COMMAND tst_strategy_plugin_panel_qml)
    add_test(NAME backtest_archive_panel_qml COMMAND tst_backtest_archive_panel_qml)
    add_test(NAME parameter_grid_scanner COMMAND tst_parameter_grid_scanner)
    add_test(NAME log_service COMMAND tst_log_service)
    add_test(NAME workspace_instrument_selection COMMAND tst_workspace_instrument_selection)
    add_test(NAME aligned_bar_series COMMAND tst_aligned_bar_series)
    add_test(NAME cross_sectional_backtest COMMAND tst_cross_sectional_backtest)
    add_test(NAME factor_research COMMAND tst_factor_research)
    add_test(NAME external_factor_research_service
        COMMAND tst_external_factor_research_service)
    add_test(NAME portfolio_backtest COMMAND tst_portfolio_backtest)
    add_test(NAME portfolio_backtest_store COMMAND tst_portfolio_backtest_store)
    add_test(NAME tickdata_csv_import COMMAND tst_tickdata_csv_import)
    add_test(NAME raw_tick_source_quality_adapter
        COMMAND tst_raw_tick_source_quality_adapter)
    add_test(NAME ashare_instrument_day_import COMMAND tst_ashare_instrument_day_import)
    add_test(NAME ashare_industry_import COMMAND tst_ashare_industry_import)
    add_test(NAME authorized_factor_import COMMAND tst_authorized_factor_import)
    add_test(NAME nas_data_node COMMAND tst_nas_data_node)
    add_test(NAME historical_provider_router COMMAND tst_historical_provider_router)
    add_test(NAME realtime_provider_router COMMAND tst_realtime_provider_router)
    add_test(NAME realtime_chart_session COMMAND tst_realtime_chart_session)
    add_test(NAME batch_backtest_evidence COMMAND tst_batch_backtest_evidence)

    # Every test executable must load the Qt runtime selected at configure time.
    # A developer shell may contain an older Qt earlier on PATH; maintaining a
    # hand-written subset here made the remaining tests fail with 0xC0000139
    # when the project was configured against Qt 6.11.
    get_property(pptrader_registered_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${pptrader_registered_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

# 示例第三方 C++ 策略插件：MODULE 库生成 .dll，供 QPluginLoader 加载演示。
include(cmake/QtScrollBarRuntime.cmake)

qt_add_library(sample_strategy_plugin MODULE
    examples/sample_strategy_plugin/SampleThresholdStrategy.cpp
    examples/sample_strategy_plugin/SampleThresholdStrategy.h
    examples/sample_strategy_plugin/SampleStrategyPlugin.cpp
    examples/sample_strategy_plugin/SampleStrategyPlugin.h
    src/domain/MarketTypes.h
    src/plugin/StrategyPluginInterface.h
    src/strategy/Strategy.h
)
target_include_directories(sample_strategy_plugin PRIVATE src)
target_link_libraries(sample_strategy_plugin PRIVATE Qt6::Core)
