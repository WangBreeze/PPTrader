#include "services/AsyncLocalTickSeriesQueryService.h"

#include "services/LocalTickSeriesQueryService.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

quint64 LocalTickSeriesRequestGate::begin()
{
    if (++m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = true;
    return m_currentRequestId;
}
bool LocalTickSeriesRequestGate::finish(quint64 requestId)
{
    if (!requestId || requestId != m_currentRequestId) return false;
    m_resolvedRequestId = requestId;
    m_loading = false;
    return true;
}
void LocalTickSeriesRequestGate::cancel()
{
    if (++m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = false;
}
quint64 LocalTickSeriesRequestGate::currentRequestId() const { return m_currentRequestId; }
quint64 LocalTickSeriesRequestGate::resolvedRequestId() const { return m_resolvedRequestId; }
bool LocalTickSeriesRequestGate::loading() const { return m_loading; }

AsyncLocalTickSeriesQueryService::AsyncLocalTickSeriesQueryService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<LocalTickSeriesResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderLocalTickQuery"));
    m_thread.start();
}

AsyncLocalTickSeriesQueryService::~AsyncLocalTickSeriesQueryService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncLocalTickSeriesQueryService::start(const QString &databasePath)
{
    if (m_running || databasePath.isEmpty() || !m_worker) return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    if (initialized) m_cancelled = std::make_shared<std::atomic_bool>(false);
    return initialized;
}

void AsyncLocalTickSeriesQueryService::stop()
{
    // 先取消扫描和排队请求，再在所属线程关闭 SQLite；不截断已开始的事务。
    m_cancelled->store(true);
    m_running = false;
    if (!m_thread.isRunning()) return;
    if (m_worker) QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                            Qt::BlockingQueuedConnection);
    m_running = false;
}

bool AsyncLocalTickSeriesQueryService::request(
    quint64 requestId, const LocalTickSeriesQuery &query)
{
    if (!m_running || !m_worker || !requestId) return false;
    QPointer<AsyncLocalTickSeriesQueryService> guard(this);
    const auto cancelled = m_cancelled;
    return QMetaObject::invokeMethod(m_worker, [this, guard, requestId, query, cancelled]() {
        if (cancelled->load()) return;
        LocalTickSeriesResult result;
        if (m_workerStore) result = LocalTickSeriesQueryService(m_workerStore.get()).query(query, cancelled.get());
        else { result.query = query; result.error = QStringLiteral("本地逐笔数据库不可用"); }
        if (!guard || cancelled->load()) return;
        QMetaObject::invokeMethod(guard, [guard, requestId, result, cancelled]() {
            if (guard && !cancelled->load()) emit guard->resultReady(requestId, result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

bool AsyncLocalTickSeriesQueryService::isRunning() const { return m_running; }

} // namespace pp
