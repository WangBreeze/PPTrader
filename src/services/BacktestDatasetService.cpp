#include "services/BacktestDatasetService.h"

#include "services/AShareTradingCalendar.h"
#include "services/RawTickCandleAggregator.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

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
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
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
    return QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, normalizedSymbol, periodSeconds, barCount,
         knownInstruments]() {
            if (!m_workerStore) {
                reportFailed(guard, requestId, QStringLiteral("回测数据集数据库不可用"));
                return;
            }
            Instrument instrument;
            for (const Instrument &candidate : knownInstruments) {
                if (candidate.symbol == normalizedSymbol && candidate.market == Market::AShare) {
                    instrument = candidate;
                    break;
                }
            }
            if (instrument.symbol.isEmpty()) {
                instrument.symbol = normalizedSymbol;
                instrument.market = Market::AShare;
            }

            auto identity = m_workerStore->loadBacktestDatasetIdentity(
                instrument, periodSeconds);
            if (!identity || identity->isEmpty()) {
                reportFailed(guard, requestId, QStringLiteral(
                    "回测数据集身份不可用：%1 · %2s 无原始 Tick 事实")
                    .arg(normalizedSymbol).arg(periodSeconds));
                return;
            }

            for (const RawTickDayFingerprint &day : identity->days) {
                if (m_workerStore->rawTickCandleCacheFresh(
                        instrument, day.tradingDate, periodSeconds)) {
                    continue;
                }
                RawTickCandleAggregator aggregator(instrument, periodSeconds);
                const bool readOk = m_workerStore->forEachRawTradeTick(
                    instrument, day.tradingDate,
                    [&](const RawTradeTick &tick) {
                        aggregator.append(tick);
                        return true;
                    });
                const RawTradeFingerprint fingerprint =
                    m_workerStore->rawTradeFingerprint(instrument, day.tradingDate);
                if (!readOk || fingerprint.count <= 0) continue;
                if (!m_workerStore->replaceRawTickCandleCache(
                        instrument, day.tradingDate, periodSeconds, fingerprint,
                        aggregator.finish())) {
                    reportFailed(guard, requestId, QStringLiteral(
                        "重建派生 K 线缓存失败：%1 · %2")
                        .arg(normalizedSymbol,
                             day.tradingDate.toString(Qt::ISODate)));
                    return;
                }
            }

            identity = m_workerStore->loadBacktestDatasetIdentity(
                instrument, periodSeconds);
            if (!identity || identity->hasStaleCache()) {
                reportFailed(guard, requestId, QStringLiteral(
                    "回测数据集缓存重建后仍不一致：%1 · %2s")
                    .arg(normalizedSymbol).arg(periodSeconds));
                return;
            }
            AShareTradingCalendar::applyDatasetCoverage(*identity);

            const QVector<Candle> candles = m_workerStore->loadRecentCandlesFromSource(
                instrument, periodSeconds, barCount, QStringLiteral("raw_tick_cache_v1"));
            if (candles.size() < barCount) {
                reportFailed(guard, requestId, QStringLiteral(
                    "历史 Tick 派生数据实际只有 %1 根，需要 %2 根；未访问网络 Provider")
                    .arg(candles.size()).arg(barCount));
                return;
            }
            reportPrepared(guard, requestId, *identity, candles);
        }, Qt::QueuedConnection);
}

bool BacktestDatasetService::isRunning() const { return m_running; }

} // namespace pp
