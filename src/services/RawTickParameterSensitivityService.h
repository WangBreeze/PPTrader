#pragma once

#include "services/RawTickParameterSensitivityScanner.h"

#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 严格本地的原始 Tick 参数敏感性异步服务。工作线程负责读取 SQLite、扫描、
// 最终留出验证和归档；不会调用 Provider 或 NAS。
class RawTickParameterSensitivityService final : public QObject {
    Q_OBJECT

public:
    explicit RawTickParameterSensitivityService(QObject *parent = nullptr);
    ~RawTickParameterSensitivityService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId,
                 const RawTickParameterSensitivityRequest &request,
                 const QDate &firstDate, const QDate &lastDate);
    void cancel();
    bool isRunning() const;
    bool isBusy() const;

signals:
    void completed(quint64 requestId,
                   const pp::RawTickParameterSensitivityResult &result);
    void failed(quint64 requestId, const QString &message);
    // 仅用于性能可观测性与回归测试；0 表示当前候选未驻留 Tick 分段。
    void rawTickResidencyChanged(quint64 requestId, int residentObservations);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    std::atomic_bool m_cancelRequested = false;
    std::atomic_bool m_busy = false;
    bool m_running = false;
};

} // namespace pp
