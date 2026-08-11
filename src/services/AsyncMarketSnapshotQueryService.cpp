#include "services/AsyncMarketSnapshotQueryService.h"

#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

quint64 SnapshotRequestGate::begin()
{
    ++m_currentRequestId;
    if (m_currentRequestId == 0) ++m_currentRequestId;
    m_loading = true;
    return m_currentRequestId;
}

bool SnapshotRequestGate::isCurrent(quint64 requestId) const
{
    return requestId != 0 && requestId == m_currentRequestId;
}

bool SnapshotRequestGate::finish(quint64 requestId)
{
    if (!isCurrent(requestId)) return false;
    m_resolvedRequestId = requestId;
    m_loading = false;
    return true;
}

quint64 SnapshotRequestGate::currentRequestId() const { return m_currentRequestId; }
quint64 SnapshotRequestGate::resolvedRequestId() const { return m_resolvedRequestId; }
bool SnapshotRequestGate::loading() const { return m_loading; }

AsyncMarketSnapshotQueryService::AsyncMarketSnapshotQueryService(QObject *parent)
    : QObject(parent)
    , m_worker(new QObject)
{
    qRegisterMetaType<MarketSnapshotQueryResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderSnapshotQuery"));
    m_thread.start();
}

AsyncMarketSnapshotQueryService::~AsyncMarketSnapshotQueryService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncMarketSnapshotQueryService::start(const QString &databasePath)
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

void AsyncMarketSnapshotQueryService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() {
            m_workerStore.reset();
        }, Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool AsyncMarketSnapshotQueryService::request(quint64 requestId,
                                               const QDateTime &requestedTime,
                                               qint64 maximumStalenessMilliseconds)
{
    if (!m_running || !m_worker || requestId == 0 || !requestedTime.isValid()) {
        return false;
    }
    QPointer<AsyncMarketSnapshotQueryService> guardedThis(this);
    return QMetaObject::invokeMethod(m_worker,
        [this, guardedThis, requestId, requestedTime, maximumStalenessMilliseconds]() {
            MarketSnapshotQueryResult result;
            QString databaseError;
            if (m_workerStore) {
                const QMetaObject::Connection errorConnection = connect(
                    m_workerStore.get(), &SqliteLocalDataStore::writeFailed,
                    m_workerStore.get(), [&databaseError](const QString &message) {
                        databaseError = message;
                    }, Qt::DirectConnection);
                MarketSnapshotQueryService queryService(m_workerStore.get());
                result = queryService.query(requestedTime, maximumStalenessMilliseconds);
                disconnect(errorConnection);
            } else {
                result.requestedTime = requestedTime.toUTC();
                result.health = MarketSnapshotHealth::Error;
                result.error = QStringLiteral("市场快照查询数据库不可用");
            }
            if (!databaseError.isEmpty()) {
                result.health = MarketSnapshotHealth::Error;
                result.error = databaseError;
                result.found = false;
            }
            if (!guardedThis) return;
            QMetaObject::invokeMethod(guardedThis,
                [guardedThis, requestId, result]() {
                    if (guardedThis) emit guardedThis->resultReady(requestId, result);
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

bool AsyncMarketSnapshotQueryService::isRunning() const { return m_running; }

} // namespace pp
