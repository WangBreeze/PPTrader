#include "services/BacktestDatasetService.h"

#include "services/AShareTradingCalendar.h"
#include "services/RawTickCandleAggregator.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QSet>

namespace pp {

namespace {

void reportPrepared(QPointer<BacktestDatasetService> guard, quint64 requestId,
                    const BacktestDatasetIdentity &identity,
                    const QVector<Candle> &candles)
{
    QMetaObject::invokeMethod(guard, [guard, requestId, identity, candles]() {
        if (guard) emit guard->prepared(requestId, identity, candles);
    }, Qt::QueuedConnection);
}

void reportFailed(QPointer<BacktestDatasetService> guard, quint64 requestId,
                  const QString &message)
{
    QMetaObject::invokeMethod(guard, [guard, requestId, message]() {
        if (guard) emit guard->failed(requestId, message);
    }, Qt::QueuedConnection);
}

bool prepareDataset(SqliteLocalDataStore *store, const Instrument &instrument,
                    int periodSeconds, int barCount,
                    BacktestDatasetIdentity *preparedIdentity,
                    QVector<Candle> *preparedCandles, QString *error,
                    const std::shared_ptr<std::atomic_bool> &cancellation = {})
{
    if (error) error->clear();
    const auto cancelled = [&cancellation]() {
        return cancellation
            && cancellation->load(std::memory_order_acquire);
    };
    if (cancelled()) return false;
    auto identity = store->loadBacktestDatasetIdentity(instrument, periodSeconds);
    if (cancelled()) return false;
    if (!identity || identity->isEmpty()) {
        if (error) {
            *error = QStringLiteral("回测数据集身份不可用：%1 · %2s 无原始 Tick 事实")
                         .arg(instrument.symbol).arg(periodSeconds);
        }
        return false;
    }

    for (const RawTickDayFingerprint &day : identity->days) {
        if (cancelled()) return false;
        if (day.cacheFresh) continue;
        RawTickCandleAggregator aggregator(instrument, periodSeconds);
        bool interrupted = false;
        const bool readOk = store->forEachRawTradeTick(
            instrument, day.tradingDate,
            [&](const RawTradeTick &tick) {
                if (cancelled()) {
                    interrupted = true;
                    return false;
                }
                aggregator.append(tick);
                return true;
            });
        if (interrupted || cancelled()) return false;
        RawTradeFingerprint fingerprint;
        fingerprint.count = day.rawCount;
        fingerprint.maximumId = day.rawMaximumId;
        fingerprint.contentSha256 = day.rawContentSha256;
        if (!readOk || fingerprint.count <= 0) continue;
        if (cancelled()) return false;
        if (!store->replaceRawTickCandleCache(
                instrument, day.tradingDate, periodSeconds, fingerprint,
                aggregator.finish())) {
            if (error) {
                *error = QStringLiteral("重建派生 K 线缓存失败：%1 · %2")
                             .arg(instrument.symbol,
                                  day.tradingDate.toString(Qt::ISODate));
            }
            return false;
        }
    }

    if (cancelled()) return false;
    identity = store->loadBacktestDatasetIdentity(instrument, periodSeconds);
    if (cancelled()) return false;
    if (!identity || identity->hasStaleCache()) {
        if (error) {
            *error = QStringLiteral("回测数据集缓存重建后仍不一致：%1 · %2s")
                         .arg(instrument.symbol).arg(periodSeconds);
        }
        return false;
    }
    AShareTradingCalendar::applyDatasetCoverage(*identity);

    const QVector<Candle> candles = store->loadRecentCandlesFromSource(
        instrument, periodSeconds, barCount, QStringLiteral("raw_tick_cache_v1"));
    if (candles.size() < barCount) {
        if (error) {
            *error = QStringLiteral(
                "历史 Tick 派生数据实际只有 %1 根，需要 %2 根；未访问网络 Provider")
                         .arg(candles.size()).arg(barCount);
        }
        return false;
    }
    if (preparedIdentity) *preparedIdentity = *identity;
    if (preparedCandles) *preparedCandles = candles;
    return true;
}

Instrument resolveInstrument(const QString &symbol,
                             const QVector<Instrument> &knownInstruments)
{
    for (const Instrument &candidate : knownInstruments) {
        if (candidate.symbol == symbol && candidate.market == Market::AShare) {
            return candidate;
        }
    }
    Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = Market::AShare;
    return instrument;
}

} // namespace

BacktestDatasetService::BacktestDatasetService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<BacktestDatasetIdentity>();
    qRegisterMetaType<QVector<Candle>>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderBacktestDataset"));
    m_thread.start();
}

BacktestDatasetService::~BacktestDatasetService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool BacktestDatasetService::start(const QString &databasePath)
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

void BacktestDatasetService::stop()
{
    if (!m_thread.isRunning()) return;
    cancelBatch();
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
    m_batchRunning = false;
}

bool BacktestDatasetService::request(
    quint64 requestId, const QString &symbol, int periodSeconds,
    int barCount, const QVector<Instrument> &knownInstruments)
{
    if (!m_running || !m_worker || requestId == 0 || symbol.trimmed().isEmpty()
        || periodSeconds <= 0 || barCount < 2) {
        return false;
    }
    const QString normalizedSymbol = symbol.trimmed();
    QPointer<BacktestDatasetService> guard(this);
    const bool invoked = QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, normalizedSymbol, periodSeconds, barCount,
         knownInstruments]() {
            if (!m_workerStore) {
                reportFailed(guard, requestId, QStringLiteral("回测数据集数据库不可用"));
                return;
            }
            const Instrument instrument = resolveInstrument(
                normalizedSymbol, knownInstruments);
            BacktestDatasetIdentity identity;
            QVector<Candle> candles;
            QString error;
            if (!prepareDataset(m_workerStore.get(), instrument, periodSeconds,
                                barCount, &identity, &candles, &error)) {
                reportFailed(guard, requestId, error);
                return;
            }
            reportPrepared(guard, requestId, identity, candles);
        }, Qt::QueuedConnection);
    return invoked;
}

bool BacktestDatasetService::requestBatch(
    quint64 requestId, const QStringList &symbols, int periodSeconds,
    int minimumBars, const QVector<Instrument> &knownInstruments)
{
    if (!m_running || !m_worker || requestId == 0 || m_batchRunning
        || periodSeconds < 1 || periodSeconds > 86400 || minimumBars < 2) {
        return false;
    }
    QStringList normalizedSymbols;
    QSet<QString> seen;
    for (const QString &raw : symbols) {
        const QString symbol = raw.trimmed();
        if (symbol.size() != 6 || seen.contains(symbol)) continue;
        bool numeric = true;
        for (const QChar character : symbol) numeric = numeric && character.isDigit();
        if (!numeric) continue;
        seen.insert(symbol);
        normalizedSymbols.append(symbol);
    }
    if (normalizedSymbols.isEmpty()) return false;

    m_batchRunning = true;
    m_batchCancellation = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = m_batchCancellation;
    QPointer<BacktestDatasetService> guard(this);
    const bool invoked = QMetaObject::invokeMethod(m_worker,
        [this, guard, cancellation, requestId, normalizedSymbols,
         periodSeconds, minimumBars, knownInstruments]() {
            QElapsedTimer timer;
            timer.start();
            QStringList preparedSymbols;
            QVariantMap failedSymbols;
            int completed = 0;
            for (const QString &symbol : normalizedSymbols) {
                if (cancellation->load()) break;
                QMetaObject::invokeMethod(
                    guard,
                    [guard, requestId, symbol]() {
                        if (guard) emit guard->batchItemStarted(requestId, symbol);
                    },
                    Qt::BlockingQueuedConnection);
                if (cancellation->load()) break;
                QString error;
                const Instrument instrument = resolveInstrument(
                    symbol, knownInstruments);
                const bool prepared = m_workerStore && prepareDataset(
                    m_workerStore.get(), instrument, periodSeconds,
                    minimumBars, nullptr, nullptr, &error, cancellation);
                if (cancellation->load()) break;
                if (prepared) {
                    preparedSymbols.append(symbol);
                } else {
                    failedSymbols.insert(symbol, error.isEmpty()
                        ? QStringLiteral("回测数据集数据库不可用") : error);
                }
                ++completed;
                QMetaObject::invokeMethod(guard,
                    [guard, requestId, completed, normalizedSymbols, symbol]() {
                        if (guard) emit guard->batchProgress(
                            requestId, completed, normalizedSymbols.size(), symbol);
                    }, Qt::QueuedConnection);
            }
            const bool cancelled = cancellation->load();
            const qint64 elapsed = timer.elapsed();
            QMetaObject::invokeMethod(guard,
                [guard, cancellation, requestId, periodSeconds, preparedSymbols,
                 failedSymbols, elapsed, cancelled]() {
                    if (!guard) return;
                    if (guard->m_batchCancellation == cancellation) {
                        guard->m_batchCancellation.reset();
                        guard->m_batchRunning = false;
                    }
                    emit guard->batchFinished(
                        requestId, periodSeconds, preparedSymbols,
                        failedSymbols, elapsed, cancelled);
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    if (!invoked) {
        m_batchCancellation.reset();
        m_batchRunning = false;
    }
    return invoked;
}

void BacktestDatasetService::cancelBatch()
{
    if (m_batchCancellation) m_batchCancellation->store(true);
}

bool BacktestDatasetService::isRunning() const { return m_running; }

} // namespace pp
