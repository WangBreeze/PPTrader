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

qt_add_executable(PPTrader
    WIN32
    src/main.cpp
    src/app/TradingController.cpp
    src/app/TradingController.h
    src/app/CustomStrategyWorkspacePolicy.cpp
    src/app/CustomStrategyWorkspacePolicy.h
    src/app/WorkspaceInstrumentSelectionState.cpp
    src/app/WorkspaceInstrumentSelectionState.h
    src/domain/BacktestTypes.h
    src/domain/BacktestDatasetTypes.cpp
    src/domain/BacktestDatasetTypes.h
    src/domain/BatchBacktestTypes.cpp
    src/domain/BatchBacktestTypes.h
    src/domain/CrossSectionalTypes.h
    src/domain/CustomStrategyTypes.cpp
    src/domain/CustomStrategyTypes.h
    src/domain/HistoricalProviderTypes.cpp
    src/domain/HistoricalProviderTypes.h
    src/domain/LocalHistoryTypes.cpp
    src/domain/LocalHistoryTypes.h
    src/domain/LocalCandleSeriesTypes.h
    src/domain/MarketTypes.h
    src/domain/NasDataNodeTypes.cpp
    src/domain/NasDataNodeTypes.h
    src/domain/RawTradeTypes.h
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
    src/models/ExecutionListModel.cpp
    src/models/ExecutionListModel.h
    src/models/InstrumentListModel.cpp
    src/models/InstrumentListModel.h
    src/models/LocalCandleSeriesListModel.cpp
    src/models/LocalCandleSeriesListModel.h
    src/models/MarketSnapshotListModel.cpp
    src/models/MarketSnapshotListModel.h
    src/models/OrderListModel.cpp
    src/models/OrderListModel.h
    src/models/PositionListModel.cpp
    src/models/PositionListModel.h
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
    src/services/BacktestEngine.cpp
    src/services/BacktestEngine.h
    src/services/BatchBacktestService.cpp
    src/services/BatchBacktestService.h
    src/services/BacktestDataVersion.cpp
    src/services/BacktestDataVersion.h
    src/services/AShareRelativeStrengthIndicator.cpp
    src/services/AShareRelativeStrengthIndicator.h
    src/services/AShareUniverseLoader.cpp
    src/services/AShareUniverseLoader.h
    src/services/AsyncMarketSnapshotQueryService.cpp
    src/services/AsyncMarketSnapshotQueryService.h
    src/services/AsyncLocalCandleSeriesQueryService.cpp
    src/services/AsyncLocalCandleSeriesQueryService.h
    src/services/AsyncTickWriter.cpp
    src/services/AsyncTickWriter.h
    src/services/BrokerGateway.h
    src/services/IndicatorEngine.cpp
    src/services/IndicatorEngine.h
    src/services/LocalDataStore.h
    src/services/MarketSession.cpp
    src/services/MarketSession.h
    src/services/MarketSnapshotAssembler.cpp
    src/services/MarketSnapshotAssembler.h
    src/services/MarketSnapshotQueryService.cpp
    src/services/MarketSnapshotQueryService.h
    src/services/CompositeMarketDataProvider.cpp
    src/services/CompositeMarketDataProvider.h
    src/services/CrossSectionalIndicator.h
    src/services/CrossSectionalIndicatorEngine.cpp
    src/services/CrossSectionalIndicatorEngine.h
    src/services/CustomStrategyStore.h
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
    src/services/MarketDataProvider.h
    src/services/MootdxMarketDataProvider.cpp
    src/services/MootdxMarketDataProvider.h
    src/services/NasDataNodeClient.cpp
    src/services/NasDataNodeClient.h
    src/services/NasSyncStateStore.cpp
    src/services/NasSyncStateStore.h
    src/services/NasTickCacheManager.cpp
    src/services/NasTickCacheManager.h
    src/services/NasTickSyncQueue.cpp
    src/services/NasTickSyncQueue.h
    src/services/NotificationService.h
    src/services/OrderSubmissionPort.h
    src/services/PreTradeRiskService.cpp
    src/services/PreTradeRiskService.h
    src/services/RawTickCandleAggregator.cpp
    src/services/RawTickCandleAggregator.h
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
    src/services/TickDataBatchPlanner.cpp
    src/services/TickDataBatchPlanner.h
    src/services/AShareTradingCalendar.cpp
    src/services/AShareTradingCalendar.h
    src/services/TradingAccountService.cpp
    src/services/TradingAccountService.h
    src/strategy/StrategyEngine.cpp
    src/strategy/StrategyEngine.h
    src/strategy/Strategy.h
    src/strategy/BacktestStrategy.cpp
    src/strategy/BacktestStrategy.h
    src/strategy/BacktestStrategyRegistry.cpp
    src/strategy/BacktestStrategyRegistry.h
    src/strategy/CustomBacktestStrategy.cpp
    src/strategy/CustomBacktestStrategy.h
    src/strategy/CustomIndicatorRuntime.cpp
    src/strategy/CustomIndicatorRuntime.h
    src/strategy/CustomStrategyCompiler.cpp
    src/strategy/CustomStrategyCompiler.h
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
        qml/components/LocalBacktestSelector.qml
        qml/components/LiveSnapshotRangeChart.qml
        qml/components/CandleSeriesChart.qml
        qml/components/CustomStrategyWorkbench.qml
        qml/components/LocalHistoryDownloadPanel.qml
        qml/components/NasTickSyncPanel.qml
        qml/components/TickDataImportPanel.qml
        qml/components/TickDataBatchImportPanel.qml
        qml/components/LocalMinuteSeriesPanel.qml
        qml/components/StrategyResearchPage.qml
        qml/components/BacktestEquityChart.qml
        qml/components/BacktestArchivePanel.qml
        qml/components/AppTopBar.qml
        qml/components/SnapshotTimeControls.qml
        qml/components/SnapshotStateOverlay.qml
        qml/components/ScatterPlot.qml
        qml/components/CrossSectionChartCard.qml
        qml/components/SnapshotSummaryPanel.qml
        qml/components/MarketSnapshotPage.qml
        qml/components/AppStatusBar.qml
    SOURCES
        src/visualization/CrossSectionPlotData.cpp
        src/visualization/CrossSectionPlotData.h
        src/visualization/CrossSectionScatterItem.cpp
        src/visualization/CrossSectionScatterItem.h
        src/visualization/LocalCandlePlotData.cpp
        src/visualization/LocalCandlePlotData.h
        src/visualization/LocalCandleSeriesItem.cpp
        src/visualization/LocalCandleSeriesItem.h
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
    )
    target_include_directories(tst_sqlite_local_data_store PRIVATE src)
    target_link_libraries(tst_sqlite_local_data_store PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_backtest_engine
        tests/tst_backtest_engine.cpp
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

    qt_add_executable(tst_batch_backtest
        tests/tst_batch_backtest.cpp
        src/domain/BacktestTypes.h
        src/domain/BatchBacktestTypes.cpp
        src/domain/BatchBacktestTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/models/BatchBacktestResultListModel.cpp
        src/models/BatchBacktestResultListModel.h
        src/models/CandleListModel.cpp
        src/models/CandleListModel.h
        src/services/BacktestEngine.cpp
        src/services/BacktestEngine.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/BatchBacktestService.cpp
        src/services/BatchBacktestService.h
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
    target_include_directories(tst_batch_backtest PRIVATE src)
    target_link_libraries(tst_batch_backtest PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

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

    qt_add_executable(pptrader_nas_sync_capacity
        tools/nas_sync_capacity_probe.cpp
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
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
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
        src/domain/BacktestTypes.h
        src/domain/CrossSectionalTypes.h
        src/domain/CustomStrategyTypes.cpp
        src/domain/CustomStrategyTypes.h
        src/domain/LocalCandleSeriesTypes.h
        src/domain/LocalHistoryTypes.cpp
        src/domain/LocalHistoryTypes.h
        src/domain/MarketTypes.h
        src/domain/RawTradeTypes.h
        src/domain/StrategyExecutionTypes.h
        src/domain/TradingTypes.h
        src/models/LocalCandleSeriesListModel.cpp
        src/models/LocalCandleSeriesListModel.h
        src/services/BacktestDataVersion.cpp
        src/services/BacktestDataVersion.h
        src/services/AsyncLocalCandleSeriesQueryService.cpp
        src/services/AsyncLocalCandleSeriesQueryService.h
        src/services/CustomStrategyStore.h
        src/services/LocalCandleSeriesQueryService.cpp
        src/services/LocalCandleSeriesQueryService.h
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
    )
    target_include_directories(tst_local_candle_series PRIVATE src)
    target_link_libraries(tst_local_candle_series PRIVATE Qt6::Core Qt6::Sql Qt6::Test)

    qt_add_executable(tst_backtest_dataset_identity
        tests/tst_backtest_dataset_identity.cpp
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

    qt_add_executable(tst_tickdata_csv_import
        tests/tst_tickdata_csv_import.cpp
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
        src/services/RealtimeProviderRouter.cpp
        src/services/RealtimeProviderRouter.h
        src/services/TencentQuoteParser.cpp
        src/services/TencentQuoteParser.h
        src/services/TencentMinuteBarService.cpp
        src/services/TencentMinuteBarService.h
    )
    target_include_directories(tst_realtime_provider_router PRIVATE src)
    target_link_libraries(tst_realtime_provider_router PRIVATE Qt6::Core Qt6::Network Qt6::Test)

    add_test(NAME market_session COMMAND tst_market_session)
    add_test(NAME sqlite_local_data_store COMMAND tst_sqlite_local_data_store)
    add_test(NAME backtest_engine COMMAND tst_backtest_engine)
    add_test(NAME batch_backtest COMMAND tst_batch_backtest)
    add_test(NAME trading_account COMMAND tst_trading_account)
    add_test(NAME strategy_execution COMMAND tst_strategy_execution)
    add_test(NAME exchange_rules COMMAND tst_exchange_rules)
    add_test(NAME cross_sectional COMMAND tst_cross_sectional)
    add_test(NAME market_snapshot_contract COMMAND tst_market_snapshot_contract)
    add_test(NAME cross_section_plot_data COMMAND tst_cross_section_plot_data)
    add_test(NAME local_candle_series COMMAND tst_local_candle_series)
    add_test(NAME backtest_dataset_identity COMMAND tst_backtest_dataset_identity)
    add_test(NAME cross_section_performance COMMAND tst_cross_section_performance)
    add_test(NAME custom_strategy COMMAND tst_custom_strategy)
    add_test(NAME workspace_instrument_selection COMMAND tst_workspace_instrument_selection)
    add_test(NAME tickdata_csv_import COMMAND tst_tickdata_csv_import)
    add_test(NAME nas_data_node COMMAND tst_nas_data_node)
    add_test(NAME historical_provider_router COMMAND tst_historical_provider_router)
    add_test(NAME realtime_provider_router COMMAND tst_realtime_provider_router)

    set_tests_properties(market_session sqlite_local_data_store backtest_engine batch_backtest trading_account strategy_execution exchange_rules cross_sectional market_snapshot_contract cross_section_plot_data cross_section_performance custom_strategy workspace_instrument_selection tickdata_csv_import nas_data_node historical_provider_router realtime_provider_router local_candle_series backtest_dataset_identity PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
