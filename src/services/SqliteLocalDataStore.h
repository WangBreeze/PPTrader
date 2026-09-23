#pragma once

#include "services/LocalDataStore.h"
#include "services/CustomStrategyStore.h"
#include "services/StrategyExecutionStore.h"
#include "domain/BatchBacktestTypes.h"
#include "domain/BacktestDataQualityTypes.h"
#include "domain/PortfolioTypes.h"
#include "domain/TickDataImportTypes.h"
#include "domain/RawTradeTypes.h"
#include "domain/AShareCalendarTypes.h"
#include "domain/AShareInstrumentDayImportTypes.h"
#include "domain/AShareIndustryTypes.h"
#include "domain/FactorResearchTypes.h"
#include "domain/FactorCandidateTypes.h"
#include "domain/AuthorizedFactorImportTypes.h"
#include "domain/ExternalFactorResearchTypes.h"
#include "services/RawTickParameterSensitivityScanner.h"

#include <QSqlDatabase>
#include <functional>

namespace pp {

class SqliteLocalDataStore final : public LocalDataStore, public StrategyExecutionStore,
                                   public CustomStrategyStore {
    Q_OBJECT

public:
    explicit SqliteLocalDataStore(QObject *parent = nullptr);
    ~SqliteLocalDataStore() override;

    bool initialize(const QString &databasePath);
    // 只接受当前 schema，不创建目录、不迁移、不切换 journal_mode，且关闭时
    // 不执行 WAL checkpoint。供后台可用性/审计查询使用。
    bool initializeReadOnly(const QString &databasePath);
    bool beginReadSnapshot();
    bool endReadSnapshot();
    void close();
    QString databasePath() const;
    int schemaVersion() const;
    QString databaseUuid() const;
    QString lastMigrationBackupPath() const;
    bool verifyIntegrity(QString *error = nullptr) const;

    bool appendTick(const Tick &tick) override;
    bool appendTicks(const QVector<Tick> &ticks) override;
    bool appendMarketSnapshot(const MarketSnapshot &snapshot) override;
    std::optional<MarketSnapshot> loadMarketSnapshotAtOrBefore(
        const QDateTime &requestedTime) override;
    bool appendCandle(const Candle &candle) override;
    bool upsertCandles(const QVector<Candle> &candles) override;
    bool upsertHistoricalDataset(const QVector<Candle> &candles,
                                 int requestedBars,
                                 const QString &dataVersion) override;
    bool beginRawTradeTickImport(const Instrument &instrument, const QDate &tradingDate,
                                 const QString &source);
    bool appendRawTradeTicks(const QVector<RawTradeTick> &ticks);
    RawTradeIncrementalAppendResult appendRawTradeTicksIncremental(
        const QVector<RawTradeTick> &ticks);
    bool commitRawTradeTickImport();
    bool commitRawTradeTickImport(const TickDataImportResult &auditResult);
    void rollbackRawTradeTickImport();
    QVector<QDate> rawTradeTradingDates(const Instrument &instrument);
    RawTradeFingerprint rawTradeFingerprint(const Instrument &instrument,
                                            const QDate &tradingDate);
    QVector<int> rawTickCachedPeriods(const Instrument &instrument,
                                      const QDate &tradingDate);
    bool forEachRawTradeTick(
        const Instrument &instrument, const QDate &tradingDate,
        const std::function<bool(const RawTradeTick &)> &visitor);
    bool rawTickCandleCacheFresh(const Instrument &instrument,
                                 const QDate &tradingDate, int periodSeconds);
    bool replaceRawTickCandleCache(const Instrument &instrument,
                                   const QDate &tradingDate, int periodSeconds,
                                   const RawTradeFingerprint &fingerprint,
                                   const QVector<Candle> &candles);
    bool appendSignal(const TradeSignal &signal) override;
    int tickCount() const override;
    int lastInsertedTickCount() const;
    int signalCount() const override;
    QVector<Candle> loadCandles(const Instrument &instrument, int periodSeconds, const QDateTime &from, const QDateTime &to) override;
    QVector<Candle> loadRecentCandles(const Instrument &instrument, int periodSeconds, int limit) override;
    QVector<Candle> loadRecentCandlesFromSource(
        const Instrument &instrument, int periodSeconds, int limit,
        const QString &source) override;
    QVector<Candle> loadCandlesFromSource(
        const Instrument &instrument, int periodSeconds,
        const QDateTime &from, const QDateTime &to, const QString &source);
    int candleCount(const Instrument &instrument, int periodSeconds, bool *ok = nullptr);
    QVector<Candle> loadCandlePage(const Instrument &instrument, int periodSeconds,
                                   int page, int pageSize, bool *ok = nullptr);
    QVector<LocalHistoryDataset> localHistoryCatalog() override;
    QVector<Instrument> rawBacktestInstruments(
        int maximumSymbols = 0,
        const std::function<bool()> &cancelled = {});
    QVector<BacktestDatasetIdentity> loadBacktestDatasetIdentities() override;
    std::optional<BacktestDatasetIdentity> loadBacktestDatasetIdentity(
        const Instrument &instrument, int periodSeconds) override;
    std::optional<BacktestDatasetIdentity> loadRawBacktestDatasetIdentity(
        const Instrument &instrument);
    bool materializeRawTickSnapshot(const BacktestResult &deferredResult,
                                    BacktestResult *materializedResult,
                                    QString *error = nullptr);
    bool saveDeferredRawTickBacktestArchive(
        const BacktestArchiveRecord &record,
        const std::function<void(qint64)> &residencyObserver = {},
        QString *error = nullptr);
    bool upsertAShareInstrumentDayRecords(
        const QVector<AShareInstrumentDayRecord> &records);
    AShareInstrumentDayImportChangeSummary analyzeAShareInstrumentDayImport(
        const QVector<AShareInstrumentDayRecord> &records);
    bool importAShareInstrumentDayRecords(
        AShareInstrumentDayImportRun &run,
        const QVector<AShareInstrumentDayRecord> &records,
        bool *reused = nullptr, QString *error = nullptr);
    QVector<AShareInstrumentDayImportRun> loadAShareInstrumentDayImportRuns(
        int limit = 20);
    QVector<AShareInstrumentDayRecord> loadAShareInstrumentDayImportRunRecords(
        const QString &runId);
    QVector<AShareInstrumentDayRecord> loadAShareInstrumentDayRecords(
        const QStringList &symbols, const QDate &firstDate,
        const QDate &lastDate);
    bool saveAShareIndustryDataset(AShareIndustryDataset &dataset,
                                   bool *reused = nullptr,
                                   QString *error = nullptr);
    AShareIndustryDatasetAnalysis analyzeAShareIndustryDataset(
        AShareIndustryDataset &dataset);
    QVector<AShareIndustryDataset> loadAShareIndustryDatasets(int limit = 20);
    std::optional<AShareIndustrySnapshot> loadAShareIndustrySnapshot(
        const QString &datasetId, const QDate &asOfDate,
        const QStringList &symbols);
    bool registerFactorDefinitionVersion(FactorDefinitionVersion &definition,
                                         bool *reused = nullptr,
                                         QString *error = nullptr);
    QVector<FactorDefinitionVersion> loadFactorDefinitionVersions(
        int limit = 100);
    bool registerFactorCandidateVersion(FactorCandidateVersion &candidate,
                                        bool *reused = nullptr,
                                        QString *error = nullptr);
    bool freezeFactorCandidateVersion(const QString &candidateId, int version,
                                      const QDateTime &frozenAt,
                                      QString *error = nullptr);
    bool linkFactorCandidateEvaluation(
        const QString &candidateId, int version,
        const PortfolioArchiveRecord &archive,
        FactorCandidateEvaluationRole role,
        FactorCandidateEvaluation &evaluation,
        bool *reused = nullptr, QString *error = nullptr);
    QVector<FactorCandidateVersion> loadFactorCandidateVersions(
        int limit = 100);
    QVector<FactorCandidateEvaluation> loadFactorCandidateEvaluations(
        const QString &candidateId, int version, int limit = 100);
    bool saveExternalFactorDataset(
        ExternalFactorDataset &dataset,
        const QVector<ExternalFactorObservation> &observations,
        bool *reused = nullptr, QString *error = nullptr);
    QVector<ExternalFactorDataset> loadExternalFactorDatasets(int limit = 100);
    QVector<ExternalFactorObservation> loadExternalFactorObservations(
        const QString &datasetId);
    bool bindExternalFactorDataset(
        const QString &candidateId, int candidateVersion,
        const QString &datasetId, ExternalFactorDatasetBinding &binding,
        bool *reused = nullptr, QString *error = nullptr);
    QVector<ExternalFactorDatasetBinding> loadExternalFactorDatasetBindings(
        const QString &candidateId, int candidateVersion, int limit = 100);
    bool saveExternalFactorResearchArchive(
        ExternalFactorResearchArchiveRecord record, QString *error = nullptr);
    QVector<ExternalFactorResearchArchiveSummary>
    loadExternalFactorResearchArchiveSummaries(int limit = 100);
    std::optional<ExternalFactorResearchArchiveRecord>
    loadExternalFactorResearchArchive(const QString &resultId);
    bool saveRawTickParameterSensitivityResult(
        const RawTickParameterSensitivityResult &record,
        QString *error = nullptr);
    QVector<RawTickParameterSensitivityResult>
    loadRawTickParameterSensitivityResults(int limit = 50);
    QHash<QDate, int> candleCountsByTradingDate(
        const Instrument &instrument, int periodSeconds,
        const QDate &startDate, const QDate &endDate);
    bool upsertTickDataImportRun(const TickDataImportRunSummary &summary);
    bool upsertTickDataImportRun(const TickDataImportRunSummary &summary,
                                 const QVector<TickDataBatchPlanItem> &items);
    QVector<TickDataImportRunSummary> loadTickDataImportRuns(
        int limit = 20, const QString &symbolFilter = {},
        const QString &statusFilter = {});
    std::optional<TickDataImportRunSummary> loadTickDataImportRun(
        const QString &runId);
    QVector<TickDataBatchPlanItem> loadTickDataImportRunItems(
        const QString &runId);
    QVector<RawTickImportDayAudit> loadRawTickImportDayAudits(
        const Instrument &instrument, const QDate &startDate,
        const QDate &endDate);
    bool upsertBatchBacktestRun(const BatchBacktestRunSummary &summary);
    bool upsertBatchBacktestRunItem(const BatchBacktestRunItem &item);
    QVector<BatchBacktestRunSummary> loadBatchBacktestRuns(int limit = 20);
    QVector<BatchBacktestRunItem> loadBatchBacktestRunItems(
        const QString &runId);
    bool savePortfolioBacktestArchive(
        const PortfolioArchiveRecord &record, QString *error = nullptr);
    QVector<PortfolioArchiveSummary> loadPortfolioBacktestArchiveSummaries(
        int limit = 50);
    std::optional<PortfolioArchiveRecord> loadPortfolioBacktestArchive(
        const QString &resultId);
    bool saveBacktestArchive(const BacktestArchiveRecord &record) override;
    QVector<BacktestArchiveSummary> loadBacktestArchiveSummaries() override;
    std::optional<BacktestArchiveRecord> loadBacktestArchive(
        const QString &resultId,
        BacktestArchiveLoadMode mode = BacktestArchiveLoadMode::Display) override;
    std::optional<BacktestRawTickPage> loadBacktestRawTickPage(
        const QString &resultId, int offset, int pageSize) override;
    bool saveValidationRun(const ValidationRun &run);
    QVector<ValidationRun> loadValidationRuns(int limit = 10);
    bool upsertOrder(const Order &order) override;
    bool appendExecution(const Execution &execution) override;
    bool saveTradingState(const AccountSnapshot &account, const QVector<Position> &positions) override;
    TradingState loadTradingState() override;
    bool upsertStrategyConfiguration(const StrategyExecutionConfig &config) override;
    bool removeStrategyConfiguration(const QString &strategyId) override;
    bool upsertStrategyIntent(const StrategyOrderIntent &intent) override;
    QVector<StrategyExecutionConfig> loadStrategyConfigurations() override;
    QVector<StrategyOrderIntent> loadStrategyIntents() override;
    bool insertCustomStrategy(const CustomStrategyDefinition &definition) override;
    QVector<CustomStrategyDefinition> loadCustomStrategies() override;

private:
    bool saveBacktestArchiveImpl(
        const BacktestArchiveRecord &record, bool allowDeferredRawSnapshot,
        const std::function<void(qint64)> &residencyObserver,
        QString *error);
    bool writeCandlesInCurrentTransaction(const QVector<Candle> &candles);
    bool migrateSchema();
    bool hasUserSchema() const;
    bool createMigrationBackup(int fromVersion, QString *backupPath);
    bool verifyRequiredTables(QString *error = nullptr) const;
    bool ensureSchema();
    bool ensureColumn(const QString &tableName, const QString &columnName, const QString &definition);
    QString loadOrComputeRawTradeContentSha256(const QString &symbol,
                                               const QDate &tradingDate,
                                               qint64 rawCount,
                                               qint64 rawMaximumId);
    int tableRowCount(const QString &tableName) const;
    void emitSqlError(const QString &operation);

    QString m_connectionName;
    QString m_databasePath;
    QSqlDatabase m_database;
    int m_schemaVersion = 0;
    QString m_databaseUuid;
    QString m_lastMigrationBackupPath;
    int m_lastInsertedTickCount = 0;
    bool m_rawTradeImportActive = false;
    bool m_readOnly = false;
    bool m_readSnapshotActive = false;
    Instrument m_rawTradeImportInstrument;
    QDate m_rawTradeImportDate;
    QString m_rawTradeImportSource;
    qint64 m_rawTradeImportAppendedCount = 0;
};

} // 命名空间 pp
