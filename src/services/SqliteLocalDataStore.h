#pragma once

#include "services/LocalDataStore.h"
#include "services/CustomStrategyStore.h"
#include "services/StrategyExecutionStore.h"
#include "domain/BatchBacktestTypes.h"
#include "domain/PortfolioTypes.h"
#include "domain/TickDataImportTypes.h"
#include "domain/RawTradeTypes.h"

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
    int candleCount(const Instrument &instrument, int periodSeconds, bool *ok = nullptr);
    QVector<Candle> loadCandlePage(const Instrument &instrument, int periodSeconds,
                                   int page, int pageSize, bool *ok = nullptr);
    QVector<LocalHistoryDataset> localHistoryCatalog() override;
    QVector<BacktestDatasetIdentity> loadBacktestDatasetIdentities() override;
    std::optional<BacktestDatasetIdentity> loadBacktestDatasetIdentity(
        const Instrument &instrument, int periodSeconds) override;
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
    bool upsertBatchBacktestRun(const BatchBacktestRunSummary &summary);
    bool upsertBatchBacktestRunItem(const BatchBacktestRunItem &item);
    QVector<BatchBacktestRunSummary> loadBatchBacktestRuns(int limit = 20);
    QVector<BatchBacktestRunItem> loadBatchBacktestRunItems(
        const QString &runId);
    bool savePortfolioBacktestArchive(
        const PortfolioArchiveRecord &record);
    QVector<PortfolioArchiveSummary> loadPortfolioBacktestArchiveSummaries(
        int limit = 50);
    std::optional<PortfolioArchiveRecord> loadPortfolioBacktestArchive(
        const QString &resultId);
    bool saveBacktestArchive(const BacktestArchiveRecord &record) override;
    QVector<BacktestArchiveSummary> loadBacktestArchiveSummaries() override;
    std::optional<BacktestArchiveRecord> loadBacktestArchive(
        const QString &resultId) override;
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
};

} // 命名空间 pp
