#include "services/ParameterGridBacktestService.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

ParameterGridBacktestService::ParameterGridBacktestService(QObject *parent)
    : QObject(parent)
    , m_worker(new QObject)
{
    qRegisterMetaType<QVector<ParameterGridResult>>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderParameterGrid"));
    m_thread.start();
}

ParameterGridBacktestService::~ParameterGridBacktestService()
{
    cancel();
    m_thread.quit();
    // 无界等待：scan 组合数有限（≤512）且 cancel 标志在组合间检查，任务必然终止，
    // 避免固定超时后 QThread 运行中被析构 + worker 访问已析构成员。
    m_thread.wait();
    m_worker = nullptr;
}

bool ParameterGridBacktestService::request(quint64 requestId,
                                           const CustomStrategyDefinition &definition,
                                           const QVector<ParameterGridAxis> &axes,
                                           const QVector<Candle> &candles,
                                           const BacktestConfig &baseConfig,
                                           int maximumCombinations)
{
    if (!m_worker || requestId == 0 || m_busy.load(std::memory_order_acquire)) return false;
    m_busy.store(true, std::memory_order_release);
    m_cancelRequested.store(false, std::memory_order_release);
    QPointer<ParameterGridBacktestService> guard(this);

    return QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, definition, axes, candles, baseConfig,
         maximumCombinations]() {
            QString error;
            const QVector<ParameterGridResult> results = ParameterGridScanner::scan(
                definition, axes, candles, baseConfig, maximumCombinations,
                &m_cancelRequested, &error);
            m_busy.store(false, std::memory_order_release);
            QMetaObject::invokeMethod(guard, [guard, requestId, results, error]() {
                if (!guard) return;
                if (!error.isEmpty()) {
                    emit guard->failed(requestId, error);
                } else {
                    emit guard->completed(requestId, results);
                }
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void ParameterGridBacktestService::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

bool ParameterGridBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

bool ParameterGridBacktestService::isBusy() const
{
    return m_busy.load(std::memory_order_acquire);
}

} // namespace pp
