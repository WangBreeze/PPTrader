#include "services/PortfolioBacktestService.h"

#include "services/PortfolioBacktestEngine.h"
#include "services/SqliteLocalDataStore.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

PortfolioBacktestService::PortfolioBacktestService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<PortfolioResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderPortfolioBacktest"));
    m_thread.start();
}

PortfolioBacktestService::~PortfolioBacktestService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool PortfolioBacktestService::start(const QString &databasePath)
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

void PortfolioBacktestService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool PortfolioBacktestService::request(
    quint64 requestId, const PortfolioDefinition &definition,
    int barCount, const QVector<Instrument> &knownInstruments)
{
    if (!m_running || !m_worker || requestId == 0 || barCount < 2) return false;
    m_cancelRequested.store(false, std::memory_order_release);
    QString validationError;
    if (!definition.isValid(&validationError)) {
        QMetaObject::invokeMethod(this, [this, requestId, validationError]() {
            emit failed(requestId, validationError);
        }, Qt::QueuedConnection);
        return true;
    }
    QPointer<PortfolioBacktestService> guard(this);
    return QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, definition, barCount, knownInstruments]() {
            if (!m_workerStore) {
                QMetaObject::invokeMethod(guard, [guard, requestId]() {
                    if (guard) emit guard->failed(
                        requestId, QStringLiteral("组合回测数据库不可用"));
                }, Qt::QueuedConnection);
                return;
            }
            QHash<QString, QVector<Candle>> series;
            QStringList datasetVersions;
            QString failure;
            for (const PortfolioLeg &leg : definition.legs) {
                if (guard && guard->cancelRequested()) {
                    failure = QStringLiteral("组合回测已取消");
                    break;
                }
                Instrument instrument;
                for (const Instrument &candidate : knownInstruments) {
                    if (candidate.symbol == leg.instrument.symbol
                        && candidate.market == Market::AShare) {
                        instrument = candidate;
                        break;
                    }
                }
                if (instrument.symbol.isEmpty()) {
                    instrument = leg.instrument;
                    instrument.market = Market::AShare;
                }
                const auto identity = m_workerStore->loadBacktestDatasetIdentity(
                    instrument, definition.periodSeconds);
                if (!identity || identity->isEmpty()) {
                    failure = QStringLiteral("组合腿缺少数据集身份：%1")
                                  .arg(instrument.symbol);
                    break;
                }
                if (identity->staleDayCount > 0) {
                    failure = QStringLiteral("组合腿派生缓存过期：%1")
                                  .arg(instrument.symbol);
                    break;
                }
                QVector<Candle> candles =
                    m_workerStore->loadRecentCandlesFromSource(
                        instrument, definition.periodSeconds, barCount,
                        QStringLiteral("raw_tick_cache_v1"));
                if (candles.size() < barCount) {
                    failure = QStringLiteral("组合腿 K 线不足：%1（%2/%3）")
                                  .arg(instrument.symbol)
                                  .arg(candles.size()).arg(barCount);
                    break;
                }
                series.insert(instrument.symbol, candles);
                datasetVersions.append(identity->datasetVersion);
            }
            if (!failure.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, failure]() {
                    if (guard) emit guard->failed(requestId, failure);
                }, Qt::QueuedConnection);
                return;
            }
            QString engineError;
            PortfolioResult result = PortfolioBacktestEngine::run(
                definition, series, &engineError);
            if (result.equityCurve.isEmpty() || !engineError.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, engineError]() {
                    if (guard) emit guard->failed(
                        requestId, engineError.isEmpty()
                            ? QStringLiteral("组合回测无可用结果") : engineError);
                }, Qt::QueuedConnection);
                return;
            }
            result.datasetVersions = datasetVersions;
            QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
                if (guard) emit guard->completed(requestId, result);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void PortfolioBacktestService::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

bool PortfolioBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

bool PortfolioBacktestService::isRunning() const { return m_running; }

} // namespace pp
