#pragma once

#include "domain/PortfolioTypes.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 组合资金回测服务：在独立线程为每腿校验数据集身份、读取 K 线并运行组合引擎。
// 只访问本地 SQLite，不触发 Provider/NAS。
class PortfolioBacktestService final : public QObject {
    Q_OBJECT

public:
    explicit PortfolioBacktestService(QObject *parent = nullptr);
    ~PortfolioBacktestService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const PortfolioDefinition &definition,
                 int barCount, const QVector<Instrument> &knownInstruments);
    void cancel();
    bool isReady() const;
    bool isRunning() const;
    bool cancelRequested() const;

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
