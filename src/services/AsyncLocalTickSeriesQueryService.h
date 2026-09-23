#pragma once

#include "domain/LocalTickSeriesTypes.h"

#include <QObject>
#include <QThread>
#include <memory>
#include <atomic>

namespace pp {

class SqliteLocalDataStore;

class LocalTickSeriesRequestGate final {
public:
    quint64 begin();
    bool finish(quint64 requestId);
    void cancel();
    quint64 currentRequestId() const;
    quint64 resolvedRequestId() const;
    bool loading() const;
private:
    quint64 m_currentRequestId = 0;
    quint64 m_resolvedRequestId = 0;
    bool m_loading = false;
};

class AsyncLocalTickSeriesQueryService final : public QObject {
    Q_OBJECT
public:
    explicit AsyncLocalTickSeriesQueryService(QObject *parent = nullptr);
    ~AsyncLocalTickSeriesQueryService() override;
    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const LocalTickSeriesQuery &query);
    bool isRunning() const;
signals:
    void resultReady(quint64 requestId, const pp::LocalTickSeriesResult &result);
private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
    std::shared_ptr<std::atomic_bool> m_cancelled =
        std::make_shared<std::atomic_bool>(true);
};

} // namespace pp
