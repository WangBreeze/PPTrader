#pragma once

#include "domain/CrossSectionalTypes.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

namespace pp {

class SqliteLocalDataStore;

class AsyncTickWriter final : public QObject {
    Q_OBJECT

public:
    explicit AsyncTickWriter(QObject *parent = nullptr);
    ~AsyncTickWriter() override;

    bool start(const QString &databasePath);
    void stop();
    bool enqueue(const MarketSnapshot &snapshot);
    bool isRunning() const;
    int pendingBatchCount() const;

signals:
    void batchWritten(int tickCount);
    void snapshotWritten(const QString &batchId, int tickCount);
    void batchDropped(int tickCount);
    void writeFailed(const QString &message);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    std::atomic_int m_pendingBatches {0};
    int m_maxPendingBatches = 3;
    bool m_running = false;
};

} // namespace pp
