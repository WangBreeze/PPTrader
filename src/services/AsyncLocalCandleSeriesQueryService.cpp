#include "services/AsyncLocalCandleSeriesQueryService.h"

#include "services/LocalCandleSeriesQueryService.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

quint64 LocalCandleSeriesRequestGate::begin()
{
    ++m_currentRequestId;
    if (m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = true;
    return m_currentRequestId;
}

bool LocalCandleSeriesRequestGate::finish(quint64 requestId)
{
    if (requestId == 0 || requestId != m_currentRequestId) return false;
    m_resolvedRequestId = requestId;
    m_loading = false;
    return true;
}

quint64 LocalCandleSeriesRequestGate::currentRequestId() const { return m_currentRequestId; }
quint64 LocalCandleSeriesRequestGate::resolvedRequestId() const { return m_resolvedRequestId; }
bool LocalCandleSeriesRequestGate::loading() const { return m_loading; }

AsyncLocalCandleSeriesQueryService::AsyncLocalCandleSeriesQueryService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<LocalCandleSeriesResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderLocalCandleQuery"));
    m_thread.start();
}

AsyncLocalCandleSeriesQueryService::~AsyncLocalCandleSeriesQueryService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncLocalCandleSeriesQueryService::start(const QString &databasePath)
{
    if (m_running || databasePath.isEmpty() || !m_worker) return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    return initialized;
}

void AsyncLocalCandleSeriesQueryService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool AsyncLocalCandleSeriesQueryService::request(
    quint64 requestId, const LocalCandleSeriesQuery &query)
{
    if (!m_running || !m_worker || requestId == 0) return false;
    QPointer<AsyncLocalCandleSeriesQueryService> guard(this);
    return QMetaObject::invokeMethod(m_worker, [this, guard, requestId, query]() {
        LocalCandleSeriesResult result;
        if (m_workerStore) {
            result = LocalCandleSeriesQueryService(m_workerStore.get()).query(query);
        } else {
            result.query = query;
            result.error = QStringLiteral("本地分钟序列数据库不可用");
        }
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
            if (guard) emit guard->resultReady(requestId, result);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

bool AsyncLocalCandleSeriesQueryService::isRunning() const { return m_running; }

} // namespace pp
