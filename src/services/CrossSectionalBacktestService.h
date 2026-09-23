#pragma once

#include "domain/CustomStrategyTypes.h"
#include "domain/LocalHistoryTypes.h"
#include "domain/PortfolioTypes.h"

#include <QObject>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 多股横截面回测服务：在独立线程编译 DSL v3、逐腿校验数据集身份、读取 K 线、
// 对齐成帧并运行多股回测引擎。只访问本地 SQLite，不触发 Provider/NAS。
class CrossSectionalBacktestService final : public QObject {
    Q_OBJECT

public:
    explicit CrossSectionalBacktestService(QObject *parent = nullptr);
    ~CrossSectionalBacktestService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const CustomStrategyDefinition &definition,
                 const QVariantMap &runtimeParameters,
                 const PortfolioDefinition &portfolioDefinition,
                 int barCount, const QVector<Instrument> &knownInstruments);
    void cancel();
    bool isReady() const;
    bool isRunning() const;
    bool cancelRequested() const;
    static QVariantMap validateLocalDatasets(
        const CustomStrategyDefinition &definition, int periodSeconds,
        int barCount, const QVector<LocalHistoryDataset> &catalog);
    static QVariantMap validateRiskConfiguration(
        double maximumLegWeight, int maximumActiveLegs,
        int maximumConsecutiveMissingBars);
    static QVariantMap validateCircuitBreakerConfiguration(
        double maximumPortfolioLossPercent,
        double maximumPortfolioDrawdownPercent);
    static QVariantMap validateIndustryConfiguration(
        double maximumIndustryWeight, const QString &industryDatasetId);
    static QVariantMap validateFactorExposureConfiguration(
        double maximumAbsoluteFactorExposure, const QString &factorExposureAlias);
    static QVariantMap validateFactorResearchConfiguration(
        const QString &factorAlias, double lowerQuantile,
        double upperQuantile, int minimumPairs, double oneWayCostBps,
        bool industryNeutralization, int minimumGroupSize,
        const QString &industryDatasetId);

signals:
    void completed(quint64 requestId, const pp::PortfolioResult &result);
    void failed(quint64 requestId, const QString &message);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_ready = false;
    bool m_running = false;
    std::atomic_bool m_cancelRequested = false;
};

} // namespace pp
