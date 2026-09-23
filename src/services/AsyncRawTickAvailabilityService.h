#pragma once

#include "domain/LocalHistoryTypes.h"

#include <QDate>
#include <QObject>
#include <QThread>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <memory>

namespace pp {

// 只读异步扫描本地 SQLite。旧响应由 TradingController 的 requestId 门控；
// 服务不访问 Provider/NAS，也不生成或修补任何 Tick。
class AsyncRawTickAvailabilityService final : public QObject {
    Q_OBJECT

public:
    explicit AsyncRawTickAvailabilityService(QObject *parent = nullptr);
    ~AsyncRawTickAvailabilityService() override;

    bool request(const QString &databasePath, quint64 requestId,
                 const QDate &startDate, const QDate &endDate,
                 int maximumSymbols);
    void cancel();
    bool isBusy() const;

signals:
    void resultReady(quint64 requestId, const QVariantMap &result);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::shared_ptr<std::atomic<quint64>> m_latestRequestId;
    std::shared_ptr<std::atomic<int>> m_pendingJobs;
};

} // namespace pp
