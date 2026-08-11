#include "services/BatchBacktestService.h"

#include "services/BacktestEngine.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>

#include <algorithm>

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

        m_connectionName = QStringLiteral("batch-backtest-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
        m_database.setDatabaseName(databasePath);
        if (!m_database.open()) {
            finish(false, QStringLiteral("批量回测只读 SQLite 连接失败"));
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
        QVector<Candle> candles;
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
            "select start_time_utc, open, high, low, close, volume, buy_volume, "
            "sell_volume, neutral_volume, directional_volume_known, adjustment, "
            "downloaded_at_utc from (select start_time_utc, open, high, low, close, "
            "volume, buy_volume, sell_volume, neutral_volume, directional_volume_known, "
            "adjustment, downloaded_at_utc from candles where symbol = ? "
            "and period_seconds = ? and source = 'raw_tick_cache_v1' and close > 0 "
            "order by start_time_utc desc limit ?) order by start_time_utc asc"));
        query.addBindValue(dataset.instrument.symbol);
        query.addBindValue(dataset.periodSeconds);
        query.addBindValue(m_request.barCount);
        if (!query.exec()) {
            if (error) *error = QStringLiteral("SQLite 读取失败");
            return candles;
        }
        while (query.next()) {
            Candle candle;
            candle.instrument = dataset.instrument;
            candle.startTime = QDateTime::fromString(query.value(0).toString(),
                                                     Qt::ISODateWithMs);
            candle.periodSeconds = dataset.periodSeconds;
            candle.open = query.value(1).toDouble();
            candle.high = query.value(2).toDouble();
            candle.low = query.value(3).toDouble();
            candle.close = query.value(4).toDouble();
            candle.volume = query.value(5).toDouble();
            candle.buyVolume = query.value(6).toDouble();
            candle.sellVolume = query.value(7).toDouble();
            candle.neutralVolume = query.value(8).toDouble();
            candle.directionalVolumeKnown = query.value(9).toBool();
            candle.source = QStringLiteral("raw_tick_cache_v1");
            candle.adjustment = query.value(10).toString();
            candle.downloadedAt = QDateTime::fromString(query.value(11).toString(),
                                                       Qt::ISODateWithMs);
            candles.append(candle);
        }
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
            if (!loadError.isEmpty() || candles.size() < m_request.barCount) {
                BatchBacktestItemResult item;
                item.requestId = m_request.requestId;
                item.instrument = dataset.instrument;
                item.frequency = dataset.frequency();
                item.requestedBars = m_request.barCount;
                item.actualBars = candles.size();
                item.error = loadError.isEmpty()
                    ? QStringLiteral("Tick 派生 K 线不足：%1/%2")
                          .arg(candles.size()).arg(m_request.barCount)
                    : loadError;
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
                item.summary = engine->result().summary;
                item.elapsedMilliseconds = m_clock.elapsed() - startedAt;
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
        if (m_database.isValid()) {
            m_database.close();
            m_database = {};
        }
        if (!m_connectionName.isEmpty()) {
            QSqlDatabase::removeDatabase(m_connectionName);
            m_connectionName.clear();
        }
        postFinished(requestId, cancelled, error);
    }

    QPointer<BatchBacktestService> m_owner;
    BatchBacktestRequest m_request;
    QSqlDatabase m_database;
    QString m_connectionName;
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
