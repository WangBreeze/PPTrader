#include "services/AsyncTickWriter.h"

#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

AsyncTickWriter::AsyncTickWriter(QObject *parent)
    : QObject(parent)
    , m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderTickWriter"));
    m_thread.start();
}

AsyncTickWriter::~AsyncTickWriter()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncTickWriter::start(const QString &databasePath)
{
    if (m_running || databasePath.isEmpty() || !m_worker) return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        connect(m_workerStore.get(), &SqliteLocalDataStore::writeFailed,
                this, &AsyncTickWriter::writeFailed, Qt::QueuedConnection);
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    return initialized;
}

void AsyncTickWriter::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() {
            m_workerStore.reset();
        }, Qt::BlockingQueuedConnection);
    }
    m_running = false;
    m_pendingBatches.store(0);
}

bool AsyncTickWriter::enqueue(const MarketSnapshot &snapshot)
{
    if (!m_running || !m_worker || snapshot.batchId.isEmpty()
        || !snapshot.timestamp.isValid()) return false;
    const int previousPending = m_pendingBatches.fetch_add(1);
    if (previousPending >= m_maxPendingBatches) {
        m_pendingBatches.fetch_sub(1);
        emit batchDropped(snapshot.quotes.size());
        return false;
    }

    QPointer<AsyncTickWriter> guardedThis(this);
    const bool queued = QMetaObject::invokeMethod(m_worker, [this, guardedThis, snapshot]() {
        const bool succeeded = m_workerStore && m_workerStore->appendMarketSnapshot(snapshot);
        const int writtenCount = succeeded ? m_workerStore->lastInsertedTickCount() : 0;
        if (!guardedThis) return;
        QMetaObject::invokeMethod(guardedThis,
            [guardedThis, succeeded, writtenCount, batchId = snapshot.batchId]() {
            if (!guardedThis) return;
            guardedThis->m_pendingBatches.fetch_sub(1);
            if (succeeded) {
                emit guardedThis->batchWritten(writtenCount);
                emit guardedThis->snapshotWritten(batchId, writtenCount);
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    if (!queued) {
        m_pendingBatches.fetch_sub(1);
        emit batchDropped(snapshot.quotes.size());
    }
    return queued;
}

bool AsyncTickWriter::isRunning() const { return m_running; }
int AsyncTickWriter::pendingBatchCount() const { return m_pendingBatches.load(); }

} // namespace pp
