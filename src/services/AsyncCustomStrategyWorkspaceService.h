#pragma once

#include "domain/MarketTypes.h"

#include <QObject>
#include <QThread>
#include <QVariantMap>

#include <memory>

namespace pp {

class SqliteLocalDataStore;

class AsyncOperationRequestGate final {
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

// 自定义策略向导的数据库读写与预览计算工作线程。
// worker 独占 SQLite 连接，结果通过 requestId 回传，避免阻塞 QML/UI 线程。
class AsyncCustomStrategyWorkspaceService final : public QObject {
    Q_OBJECT

public:
    explicit AsyncCustomStrategyWorkspaceService(QObject *parent = nullptr);
    ~AsyncCustomStrategyWorkspaceService() override;

    bool start(const QString &databasePath);
    void stop();
    bool isRunning() const;
    bool requestPreview(quint64 requestId, const QString &json,
                        const Instrument &instrument, int periodSeconds,
                        int barCount);
    bool requestSave(quint64 requestId, const QString &json);

signals:
    void previewReady(quint64 requestId, const QVariantMap &result);
    void saveFinished(quint64 requestId, const QVariantMap &result);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
};

} // namespace pp
