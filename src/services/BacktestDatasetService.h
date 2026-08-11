#pragma once

#include "domain/BacktestDatasetTypes.h"
#include "domain/MarketTypes.h"

#include <QObject>
#include <QThread>

#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 严格本地回测的数据集准备服务：在独立线程读取数据集身份，按需重建过期派生缓存，
// 再读取回测 K 线。全程只访问本地 SQLite，不触发 Provider/NAS。
class BacktestDatasetService final : public QObject {
    Q_OBJECT

public:
    explicit BacktestDatasetService(QObject *parent = nullptr);
    ~BacktestDatasetService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const QString &symbol, int periodSeconds,
                 int barCount, const QVector<Instrument> &knownInstruments);
    bool isRunning() const;

signals:
    void prepared(quint64 requestId, const pp::BacktestDatasetIdentity &identity,
                  const QVector<pp::Candle> &candles);
    void failed(quint64 requestId, const QString &message);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
};

} // namespace pp
