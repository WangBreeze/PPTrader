#pragma once

#include "services/MarketSnapshotQueryService.h"

#include <QObject>
#include <QThread>

#include <memory>

namespace pp {

class SqliteLocalDataStore;

class SnapshotRequestGate final {
public:
    quint64 begin();
    bool isCurrent(quint64 requestId) const;
    bool finish(quint64 requestId);
    quint64 currentRequestId() const;
    quint64 resolvedRequestId() const;
    bool loading() const;

private:
    quint64 m_currentRequestId = 0;
    quint64 m_resolvedRequestId = 0;
    bool m_loading = false;
};

class AsyncMarketSnapshotQueryService final : public QObject {
    Q_OBJECT

public:
    explicit AsyncMarketSnapshotQueryService(QObject *parent = nullptr);
    ~AsyncMarketSnapshotQueryService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const QDateTime &requestedTime,
                 qint64 maximumStalenessMilliseconds = 10000);
    bool isRunning() const;

signals:
    void resultReady(quint64 requestId, const pp::MarketSnapshotQueryResult &result);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
};

} // namespace pp
