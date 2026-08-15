#pragma once

#include "domain/CustomStrategyTypes.h"
#include "domain/PortfolioTypes.h"

#include <QObject>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace pp {

class SqliteLocalDataStore;

// 多股横截面回测服务：在独立线程编译 DSL v3、逐腿校验数据集身份、读取 K 线、
// 对齐成帧并运行多股回测引擎。只访问本地 SQLite，不触发 Provider/NAS。
class CrossSectionalBacktestService final : public QObject {
    Q_OBJECT

public:
    explicit CrossSectionalBacktestService(QObject *parent = nullptr);
    ~CrossSectionalBacktestService() override;

    bool start(const QString &databasePath);
    void stop();
    bool request(quint64 requestId, const CustomStrategyDefinition &definition,
                 const QVariantMap &runtimeParameters,
                 const PortfolioDefinition &portfolioDefinition,
                 int barCount, const QVector<Instrument> &knownInstruments);
    void cancel();
    bool isRunning() const;
    bool cancelRequested() const;

signals:
    void completed(quint64 requestId, const pp::PortfolioResult &result);
    void failed(quint64 requestId, const QString &message);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::unique_ptr<SqliteLocalDataStore> m_workerStore;
    bool m_running = false;
    std::atomic_bool m_cancelRequested = false;
};

} // namespace pp
