#include "services/BatchBacktestService.h"

#include "services/AShareTradingCalendar.h"
#include "services/BacktestEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <memory>

namespace pp {

class BatchBacktestWorker final : public QObject {
public:
    explicit BatchBacktestWorker(BatchBacktestService *owner)
        : m_owner(owner)
    {
    }

    void start(const QString &databasePath, const BatchBacktestRequest &request)
    {
        if (m_running) {
            postFinished(request.requestId, false, QStringLiteral("已有批量回测正在运行"));
            return;
        }
        m_request = request;
        m_request.maximumActiveEngines = std::clamp(
            m_request.maximumActiveEngines, 1, 20);
        m_nextDataset = 0;
        m_completed = 0;
        m_succeeded = 0;
        m_failed = 0;
        m_running = true;
        m_clock.start();
        if (m_owner && m_owner->cancelRequested()) {
            finish(true, QString{});
            return;
        }

        m_store = std::make_unique<SqliteLocalDataStore>();
        if (!m_store->initialize(databasePath)) {
            finish(false, QStringLiteral("批量回测 SQLite 连接失败"));
            return;
        }
        if (m_request.requestId == 0 || m_request.datasets.isEmpty()
            || m_request.strategyId.trimmed().isEmpty() || m_request.barCount < 2) {
            finish(false, QStringLiteral("批量回测请求无效"));
            return;
        }
        postProgress();
        launchMore();
    }

    void cancel()
    {
        if (!m_running) return;
        const auto engines = m_engines;
        m_engines.clear();
        for (BacktestEngine *engine : engines) {
            disconnect(engine, nullptr, this, nullptr);
            engine->cancel();
            engine->deleteLater();
        }
        finish(true, QString{});
    }

private:
    QVector<Candle> loadCandles(const LocalHistoryDataset &dataset, QString *error)
    {
        QVector<Candle> candles = m_store->loadRecentCandlesFromSource(
            dataset.instrument, dataset.periodSeconds, m_request.barCount,
            QStringLiteral("raw_tick_cache_v1"));
        if (error) error->clear();
        return candles;
    }

    void launchMore()
    {
        if (!m_running) return;
        if (m_owner && m_owner->cancelRequested()) {
            cancel();
            return;
        }
        while (m_engines.size() < m_request.maximumActiveEngines
               && m_nextDataset < m_request.datasets.size()) {
            const LocalHistoryDataset dataset = m_request.datasets.at(m_nextDataset++);
            QString loadError;
            QVector<Candle> candles = loadCandles(dataset, &loadError);
            auto identity = m_store->loadBacktestDatasetIdentity(
                dataset.instrument, dataset.periodSeconds);
            const bool identityUnavailable = !identity || identity->isEmpty();
            const bool identityStale = !identityUnavailable && identity->staleDayCount > 0;
            if (!loadError.isEmpty() || candles.size() < m_request.barCount
                || identityUnavailable || identityStale) {
                BatchBacktestItemResult item;
                item.requestId = m_request.requestId;
                item.instrument = dataset.instrument;
                item.frequency = dataset.frequency();
                item.requestedBars = m_request.barCount;
                item.actualBars = candles.size();
                if (identity && !identity->isEmpty()) {
                    item.datasetVersion = identity->datasetVersion;
                    item.datasetDayCount = identity->days.size();
                    item.datasetRawTickCount = identity->rawTickTotal();
                    item.datasetGapDayCount = identity->gapDayCount;
                    item.datasetStaleDayCount = identity->staleDayCount;
                }
                if (!loadError.isEmpty()) {
                    item.error = loadError;
                } else if (candles.size() < m_request.barCount) {
                    item.error = QStringLiteral("Tick 派生 K 线不足：%1/%2")
                                     .arg(candles.size()).arg(m_request.barCount);
                } else if (identityUnavailable) {
                    item.error = QStringLiteral("数据集身份不可用：无原始 Tick 事实");
                } else {
                    item.error = QStringLiteral(
                        "派生缓存过期（%1 日），请先在本地分钟序列/导入面板刷新")
                                     .arg(identity->staleDayCount);
                }
                ++m_completed;
                ++m_failed;
                postItem(item);
                postProgress();
                continue;
            }

            auto *engine = new BacktestEngine(this);
            m_engines.append(engine);
            const qint64 startedAt = m_clock.elapsed();
            BacktestConfig config;
            config.taskId = QStringLiteral("%1:%2")
                .arg(m_request.requestId).arg(dataset.instrument.symbol);
            config.strategyId = m_request.strategyId;
            config.strategyParameters = m_request.strategyParameters;
            config.instrument = dataset.instrument;
            config.frequency = dataset.frequency();
            config.periodSeconds = dataset.periodSeconds;
            config.requestedBars = m_request.barCount;
            config.initialCapital = m_request.initialCapital;
            config.commissionRate = m_request.commissionRate;
            config.slippageRate = m_request.slippageRate;
            config.lotSize = m_request.lotSize;
            config.datasetIdentity = *identity;

            connect(engine, &BacktestEngine::finished, this,
                    [this, engine, dataset, startedAt](const PerformanceSummary &) {
                if (!m_running || !m_engines.contains(engine)) return;
                BatchBacktestItemResult item;
                item.requestId = m_request.requestId;
                item.instrument = dataset.instrument;
                item.frequency = dataset.frequency();
                item.requestedBars = m_request.barCount;
                item.actualBars = engine->result().config.actualBars;
                item.succeeded = true;
                item.dataVersion = engine->result().config.dataVersion;
                item.datasetVersion = engine->result().config.datasetIdentity.datasetVersion;
                item.datasetDayCount =
                    engine->result().config.datasetIdentity.days.size();
                item.datasetRawTickCount =
                    engine->result().config.datasetIdentity.rawTickTotal();
                item.datasetGapDayCount =
                    engine->result().config.datasetIdentity.gapDayCount;
                item.datasetStaleDayCount =
                    engine->result().config.datasetIdentity.staleDayCount;
                item.summary = engine->result().summary;
                item.elapsedMilliseconds = m_clock.elapsed() - startedAt;
                postCompleted(dataset.instrument.symbol, engine->result());
                completeEngine(engine, item);
            });
            connect(engine, &BacktestEngine::errorOccurred, this,
                    [this, engine, dataset, startedAt](const QString &message) {
                if (!m_running || !m_engines.contains(engine)) return;
                BatchBacktestItemResult item;
                item.requestId = m_request.requestId;
                item.instrument = dataset.instrument;
                item.frequency = dataset.frequency();
                item.requestedBars = m_request.barCount;
                item.error = message;
                item.elapsedMilliseconds = m_clock.elapsed() - startedAt;
                completeEngine(engine, item);
            });
            engine->start(config, std::move(candles));
        }
        if (m_running && m_completed >= m_request.datasets.size()
            && m_engines.isEmpty()) {
            finish(false, QString{});
        }
    }

    void completeEngine(BacktestEngine *engine, const BatchBacktestItemResult &item)
    {
        m_engines.removeOne(engine);
        engine->deleteLater();
        ++m_completed;
        if (item.succeeded) ++m_succeeded;
        else ++m_failed;
        postItem(item);
        postProgress();
        if (m_owner && m_owner->cancelRequested()) {
            cancel();
            return;
        }
        QTimer::singleShot(0, this, [this]() { launchMore(); });
    }

    void postItem(const BatchBacktestItemResult &item)
    {
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, item]() {
            if (guard) emit guard->itemReady(item.requestId, item);
        }, Qt::QueuedConnection);
    }

    void postCompleted(const QString &symbol, const BacktestResult &result)
    {
        const quint64 requestId = m_request.requestId;
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, requestId, symbol, result]() {
            if (guard) emit guard->itemCompleted(requestId, symbol, result);
        }, Qt::QueuedConnection);
    }

    void postProgress()
    {
        const quint64 requestId = m_request.requestId;
        const int total = m_request.datasets.size();
        const int completed = m_completed;
        const int succeeded = m_succeeded;
        const int failed = m_failed;
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner,
            [guard, requestId, completed, total, succeeded, failed]() {
                if (guard) emit guard->progressChanged(
                    requestId, completed, total, succeeded, failed);
            }, Qt::QueuedConnection);
    }

    void postFinished(quint64 requestId, bool cancelled, const QString &error)
    {
        QPointer<BatchBacktestService> guard(m_owner);
        QMetaObject::invokeMethod(m_owner, [guard, requestId, cancelled, error]() {
            if (guard) guard->acceptFinished(requestId, cancelled, error);
        }, Qt::QueuedConnection);
    }

    void finish(bool cancelled, const QString &error)
    {
        if (!m_running) return;
        const quint64 requestId = m_request.requestId;
        m_running = false;
        m_store.reset();
        postFinished(requestId, cancelled, error);
    }

    QPointer<BatchBacktestService> m_owner;
    BatchBacktestRequest m_request;
    std::unique_ptr<SqliteLocalDataStore> m_store;
    QVector<BacktestEngine *> m_engines;
    QElapsedTimer m_clock;
    int m_nextDataset = 0;
    int m_completed = 0;
    int m_succeeded = 0;
    int m_failed = 0;
    bool m_running = false;
};

BatchBacktestService::BatchBacktestService(QObject *parent)
    : QObject(parent), m_worker(new BatchBacktestWorker(this))
{
    qRegisterMetaType<BatchBacktestRequest>();
    qRegisterMetaType<BatchBacktestItemResult>();
    qRegisterMetaType<BacktestResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderBatchBacktest"));
    m_thread.start();
}

BatchBacktestService::~BatchBacktestService()
{
    if (m_worker && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

void BatchBacktestService::setDatabasePath(const QString &databasePath)
{
    if (!m_running) m_databasePath = databasePath;
}

bool BatchBacktestService::start(const BatchBacktestRequest &request)
{
    if (m_running || !m_worker || m_databasePath.isEmpty() || request.requestId == 0) return false;
    m_running = true;
    m_activeRequestId = request.requestId;
    m_cancelRequested.store(false, std::memory_order_release);
    const QString databasePath = m_databasePath;
    const bool queued = QMetaObject::invokeMethod(m_worker,
        [worker = m_worker, databasePath, request]() { worker->start(databasePath, request); },
        Qt::QueuedConnection);
    if (!queued) {
        m_running = false;
        m_activeRequestId = 0;
    }
    return queued;
}

void BatchBacktestService::cancel()
{
    if (!m_running || !m_worker) return;
    m_cancelRequested.store(true, std::memory_order_release);
    QMetaObject::invokeMethod(m_worker, [worker = m_worker]() { worker->cancel(); },
                              Qt::QueuedConnection);
}

bool BatchBacktestService::isRunning() const { return m_running; }

bool BatchBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

void BatchBacktestService::acceptFinished(
    quint64 requestId, bool cancelled, const QString &error)
{
    if (requestId != m_activeRequestId) return;
    m_running = false;
    emit finished(requestId, cancelled, error);
}

} // namespace pp
