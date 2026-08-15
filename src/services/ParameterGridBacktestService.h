#pragma once

#include "domain/BacktestTypes.h"
#include "domain/CustomStrategyTypes.h"
#include "services/ParameterGridScanner.h"

#include <QObject>
#include <QThread>

#include <atomic>

namespace pp {

// 参数网格扫描异步服务：在独立线程跑 ParameterGridScanner::scan，避免阻塞 UI。
class ParameterGridBacktestService final : public QObject {
    Q_OBJECT

public:
    explicit ParameterGridBacktestService(QObject *parent = nullptr);
    ~ParameterGridBacktestService() override;

    bool request(quint64 requestId, const CustomStrategyDefinition &definition,
                 const QVector<ParameterGridAxis> &axes,
                 const QVector<Candle> &candles,
                 const BacktestConfig &baseConfig,
                 int maximumCombinations = 512);
    void cancel();
    bool cancelRequested() const;
    bool isBusy() const;

signals:
    void completed(quint64 requestId, const QVector<pp::ParameterGridResult> &results);
    void failed(quint64 requestId, const QString &message);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    std::atomic_bool m_cancelRequested = false;
    std::atomic_bool m_busy = false;
};

} // namespace pp
