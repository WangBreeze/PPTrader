#include "services/AsyncBacktestArchiveService.h"

#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

AsyncBacktestArchiveService::AsyncBacktestArchiveService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderBacktestArchive"));
    m_thread.start();
}

AsyncBacktestArchiveService::~AsyncBacktestArchiveService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool AsyncBacktestArchiveService::start(const QString &databasePath)
{
    if (m_running || databasePath.trimmed().isEmpty() || !m_worker)
        return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    return initialized;
}

void AsyncBacktestArchiveService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool AsyncBacktestArchiveService::request(
    quint64 requestId, const QString &resultId, const QDateTime &completedAt,
    std::shared_ptr<const BacktestResult> result)
{
    if (!m_running || !m_worker || requestId == 0
        || resultId.trimmed().isEmpty() || !result) {
        return false;
    }
    QPointer<AsyncBacktestArchiveService> guard(this);
    return QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, resultId, completedAt,
         result = std::move(result)]() {
            const QString symbol = result->config.instrument.symbol;
            QString archivedResultId;
            QString error;
            BacktestArchiveRecord archive;
            archive.resultId = resultId;
            archive.completedAt = completedAt;
            archive.result = *result;
            if (!m_workerStore) {
                error = QStringLiteral("回测归档数据库不可用");
            } else if (archive.result.config.dataMode == BacktestDataMode::RawTicks
                       && archive.result.rawTickSnapshotDeferred) {
                const auto residencyObserver = [guard, requestId](qint64 residentTicks) {
                    if (!guard) return;
                    QMetaObject::invokeMethod(
                        guard,
                        [guard, requestId, residentTicks]() {
                            if (guard) emit guard->rawTickResidencyChanged(
                                requestId, residentTicks);
                        },
                        Qt::QueuedConnection);
                };
                if (m_workerStore->saveDeferredRawTickBacktestArchive(
                        archive, residencyObserver, &error)) {
                    archivedResultId = archive.resultId;
                } else if (error.isEmpty()) {
                    error = QStringLiteral("SQLite 原始 Tick 回测归档写入失败");
                }
            } else {
                if (m_workerStore->saveBacktestArchive(archive)) {
                    archivedResultId = archive.resultId;
                } else {
                    error = QStringLiteral("SQLite 回测归档写入失败");
                }
            }
            if (!guard) return;
            QMetaObject::invokeMethod(
                guard,
                [guard, requestId, symbol, archivedResultId, error]() {
                    if (guard) emit guard->finished(
                        requestId, symbol, archivedResultId, error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

bool AsyncBacktestArchiveService::requestLoad(
    quint64 requestId, const QString &resultId)
{
    if (!m_running || !m_worker || requestId == 0
        || resultId.trimmed().isEmpty()) {
        return false;
    }
    QPointer<AsyncBacktestArchiveService> guard(this);
    return QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, resultId]() {
            BacktestArchiveRecord record;
            QString error;
            if (!m_workerStore) {
                error = QStringLiteral("回测归档数据库不可用");
            } else {
                const auto restored = m_workerStore->loadBacktestArchive(
                    resultId, BacktestArchiveLoadMode::Display);
                if (restored.has_value()) {
                    record = std::move(*restored);
                } else {
                    error = QStringLiteral("归档不存在、已损坏或无法读取");
                }
            }
            if (!guard) return;
            QMetaObject::invokeMethod(
                guard,
                [guard, requestId, record = std::move(record), error]() {
                    if (guard) emit guard->loadFinished(requestId, record, error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

bool AsyncBacktestArchiveService::requestRawTickPage(
    quint64 requestId, const QString &resultId, int offset, int pageSize)
{
    if (!m_running || !m_worker || requestId == 0
        || resultId.trimmed().isEmpty() || offset < 0 || pageSize <= 0
        || pageSize > 4096) {
        return false;
    }
    QPointer<AsyncBacktestArchiveService> guard(this);
    return QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, resultId, offset, pageSize]() {
            BacktestRawTickPage page;
            QString error;
            if (!m_workerStore) {
                error = QStringLiteral("回测归档数据库不可用");
            } else {
                const auto restored = m_workerStore->loadBacktestRawTickPage(
                    resultId, offset, pageSize);
                if (restored.has_value()) {
                    page = std::move(*restored);
                } else {
                    error = QStringLiteral("原始 Tick 分页不存在、已损坏或无法读取");
                }
            }
            if (!guard) return;
            QMetaObject::invokeMethod(
                guard,
                [guard, requestId, page = std::move(page), error]() {
                    if (guard) emit guard->rawTickPageReady(
                        requestId, page, error);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

bool AsyncBacktestArchiveService::isRunning() const { return m_running; }

} // namespace pp
