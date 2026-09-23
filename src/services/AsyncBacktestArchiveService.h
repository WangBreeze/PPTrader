#pragma once

#include "domain/BacktestTypes.h"

#include <QObject>
#include <QThread>

#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 在独立 SQLite 连接上保存回测归档；延迟原始 Tick 快照直接流入归档事务，
// 避免 UI 线程阻塞和工作线程持有完整输入数组。
class AsyncBacktestArchiveService final : public QObject {
    Q_OBJECT

public:
    explicit AsyncBacktestArchiveService(QObject *parent = nullptr);
    ~AsyncBacktestArchiveService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const QString &resultId,
                 const QDateTime &completedAt,
                 std::shared_ptr<const BacktestResult> result);
    bool requestLoad(quint64 requestId, const QString &resultId);
    bool requestRawTickPage(quint64 requestId, const QString &resultId,
                            int offset, int pageSize);
    bool isRunning() const;

signals:
    void rawTickResidencyChanged(quint64 requestId, qint64 residentTicks);
    void finished(quint64 requestId, const QString &symbol,
                  const QString &resultId, const QString &error);
    void loadFinished(quint64 requestId, const BacktestArchiveRecord &record,
                      const QString &error);
    void rawTickPageReady(quint64 requestId, const BacktestRawTickPage &page,
                          const QString &error);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
};

} // namespace pp
