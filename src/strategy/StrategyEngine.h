#pragma once

#include "strategy/Strategy.h"

#include <QObject>
#include <QHash>
#include <QThreadPool>
#include <QVector>

#include <atomic>

namespace pp {

class StrategyEngine final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int maxThreadCount READ maxThreadCount WRITE setMaxThreadCount NOTIFY maxThreadCountChanged)
    Q_PROPERTY(int strategyCount READ strategyCount NOTIFY strategyCountChanged)

public:
    explicit StrategyEngine(QObject *parent = nullptr);
    ~StrategyEngine() override;

    int maxThreadCount() const;
    void setMaxThreadCount(int count);

    int strategyCount() const;
    Strategy *strategyById(const QString &id) const;
    bool isStrategyEnabled(const QString &id) const;
    void addStrategy(Strategy *strategy);
    void clearStrategies();
    bool removeStrategy(const QString &id);
    void setStrategyEnabled(const QString &id, bool enabled);

    // 默认关闭，仅供性能验收和退出收敛观测；关闭时不执行原子计数。
    void setPerformanceMetricsEnabled(bool enabled);
    bool performanceMetricsEnabled() const;
    void resetPerformanceMetrics();
    quint64 dispatchedTaskCount() const;
    quint64 completedTaskCount() const;
    bool waitForIdle(int timeoutMilliseconds = -1);

signals:
    void signalGenerated(const pp::TradeSignal &signal);
    void strategySignalGenerated(const QString &strategyId, const pp::TradeSignal &signal);
    void maxThreadCountChanged();
    void strategyCountChanged();

public slots:
    void dispatchTick(const pp::Tick &tick);
    void dispatchCandle(const pp::Candle &candle);

private:
    QThreadPool m_pool;
    QVector<Strategy *> m_strategies;
    QVector<Strategy *> m_broadcastStrategies;
    QHash<QString, QVector<Strategy *>> m_strategiesBySymbol;
    QHash<QString, bool> m_enabledById;
    std::atomic_bool m_performanceMetricsEnabled {false};
    std::atomic<quint64> m_dispatchedTaskCount {0};
    std::atomic<quint64> m_completedTaskCount {0};
};

} // 命名空间 pp
