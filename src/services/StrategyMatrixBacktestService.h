#pragma once

#include "domain/BacktestTypes.h"
#include "domain/LocalHistoryTypes.h"
#include "domain/StrategyMatrixRunTypes.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QObject>
#include <QSet>
#include <QThread>

#include <atomic>
#include <optional>

namespace pp {

// 原始 Tick 矩阵首版仅开放小样本；受管批次容量门禁完成后再扩大。
inline constexpr int RawMatrixMaxTasks = 20;
inline constexpr qint64 RawMatrixMaxResidentTicksPerSymbol = 1000000;
inline constexpr qint64 RawMatrixMaxProcessedTicks = 1000000;

struct StrategyMatrixEntry {
    QString strategyId;
    QString profileLabel;
    int version = 0;
    QVariantMap parameters;
    bool custom = false;
    CompiledCustomStrategy customPlan;
    CustomStrategyDefinition definitionSnapshot;
};

struct StrategyMatrixRequest {
    QString runId;
    QVector<StrategyMatrixEntry> strategies;
    QVector<LocalHistoryDataset> datasets;
    QVector<BacktestDatasetIdentity> frozenIdentities;
    QSet<QString> skipTaskIds;
    QDate startDate;
    QDate endDate;
    BacktestDataMode dataMode = BacktestDataMode::Candles;
    int periodSeconds = 86400;
    double initialCapital = 1000000.0;
    double commissionRate = 0.0003;
    double slippageRate = 0.0002;
};

struct StrategyMatrixItem {
    QString taskId;
    QString strategyId;
    QString profileLabel;
    QString parameterFingerprint;
    int strategyVersion = 0;
    QString symbol;
    QString datasetVersion;
    QString dataVersion;
    QString resultId;
    QString error;
    PerformanceSummary summary;
    int actualBars = 0;
    qint64 elapsedMilliseconds = 0;
    bool succeeded = false;

    QVariantMap toVariantMap() const;
};

QString strategyMatrixParameterFingerprint(const QVariantMap &parameters);
QString strategyMatrixTaskId(const QString &runId,
                             const StrategyMatrixEntry &strategy,
                             const LocalHistoryDataset &dataset);
QString strategyMatrixRequestToJson(const StrategyMatrixRequest &request);
std::optional<StrategyMatrixRequest> strategyMatrixRequestFromJson(
    const QString &json, QString *error = nullptr);

class StrategyMatrixWorker;

class StrategyMatrixBacktestService final : public QObject {
    Q_OBJECT
public:
    explicit StrategyMatrixBacktestService(QObject *parent = nullptr);
    ~StrategyMatrixBacktestService() override;

    void setDatabasePath(const QString &path);
    bool start(const StrategyMatrixRequest &request);
    void cancel();
    bool isRunning() const;

signals:
    void itemReady(const pp::StrategyMatrixItem &item);
    void finished(const QString &runId, bool cancelled, const QString &error);

private:
    friend class StrategyMatrixWorker;
    void acceptFinished(const QString &runId, bool cancelled, const QString &error);

    QThread m_thread;
    StrategyMatrixWorker *m_worker = nullptr;
    QString m_databasePath;
    QString m_runId;
    bool m_running = false;
    std::atomic_bool m_cancelRequested = false;
};

} // namespace pp

Q_DECLARE_METATYPE(pp::StrategyMatrixItem)
