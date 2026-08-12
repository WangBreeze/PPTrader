#pragma once

#include "domain/BatchBacktestTypes.h"

#include <QObject>
#include <QThread>

#include <atomic>

namespace pp {

class BatchBacktestWorker;

class BatchBacktestService final : public QObject {
    Q_OBJECT

public:
    explicit BatchBacktestService(QObject *parent = nullptr);
    ~BatchBacktestService() override;

    void setDatabasePath(const QString &databasePath);
    bool start(const BatchBacktestRequest &request);
    void cancel();
    bool isRunning() const;

signals:
    void itemReady(quint64 requestId, const pp::BatchBacktestItemResult &result);
    void itemCompleted(quint64 requestId, const QString &symbol,
                       const pp::BacktestResult &result);
    void progressChanged(quint64 requestId, int completed, int total,
                         int succeeded, int failed);
    void finished(quint64 requestId, bool cancelled, const QString &error);

private:
    friend class BatchBacktestWorker;
    void acceptFinished(quint64 requestId, bool cancelled, const QString &error);
    bool cancelRequested() const;

    QThread m_thread;
    BatchBacktestWorker *m_worker = nullptr;
    QString m_databasePath;
    bool m_running = false;
    quint64 m_activeRequestId = 0;
    std::atomic_bool m_cancelRequested = false;
};

} // namespace pp
