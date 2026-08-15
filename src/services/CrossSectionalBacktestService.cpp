#include "services/CrossSectionalBacktestService.h"

#include "services/AlignedBarSeries.h"
#include "services/CrossSectionalBacktestEngine.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/CrossSectionalCustomStrategy.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QMetaObject>
#include <QPointer>

namespace pp {

CrossSectionalBacktestService::CrossSectionalBacktestService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<PortfolioResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderCrossSectionalBacktest"));
    m_thread.start();
}

CrossSectionalBacktestService::~CrossSectionalBacktestService()
{
    stop();
    m_thread.quit();
    m_thread.wait(5000);
    m_worker = nullptr;
}

bool CrossSectionalBacktestService::start(const QString &databasePath)
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

void CrossSectionalBacktestService::stop()
{
    if (!m_thread.isRunning()) return;
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
}

bool CrossSectionalBacktestService::request(
    quint64 requestId, const CustomStrategyDefinition &definition,
    const QVariantMap &runtimeParameters,
    const PortfolioDefinition &portfolioDefinition,
    int barCount, const QVector<Instrument> &knownInstruments)
{
    if (!m_running || !m_worker || requestId == 0 || barCount < 2) return false;
    m_cancelRequested.store(false, std::memory_order_release);
    QPointer<CrossSectionalBacktestService> guard(this);
    return QMetaObject::invokeMethod(m_worker,
        [this, guard, requestId, definition, runtimeParameters,
         portfolioDefinition, barCount, knownInstruments]() {
            if (!m_workerStore) {
                QMetaObject::invokeMethod(guard, [guard, requestId]() {
                    if (guard) emit guard->failed(
                        requestId, QStringLiteral("多股回测数据库不可用"));
                }, Qt::QueuedConnection);
                return;
            }

            // 1. 编译 DSL v3。
            CustomStrategyCompiler compiler;
            QString error;
            const auto plan = compiler.compile(definition, runtimeParameters, &error);
            if (!plan) {
                QMetaObject::invokeMethod(guard, [guard, requestId, error]() {
                    if (guard) emit guard->failed(requestId, error);
                }, Qt::QueuedConnection);
                return;
            }

            // 2. 逐腿校验身份并读取 K 线。
            QHash<QString, QVector<Candle>> series;
            QStringList datasetVersions;
            QString failure;
            for (const QString &symbol : plan->universeSymbols) {
                if (guard && guard->cancelRequested()) {
                    failure = QStringLiteral("多股回测已取消");
                    break;
                }
                Instrument instrument;
                for (const Instrument &candidate : knownInstruments) {
                    if (candidate.symbol == symbol && candidate.market == Market::AShare) {
                        instrument = candidate;
                        break;
                    }
                }
                if (instrument.symbol.isEmpty()) {
                    instrument.symbol = symbol;
                    instrument.market = Market::AShare;
                }
                const auto identity = m_workerStore->loadBacktestDatasetIdentity(
                    instrument, portfolioDefinition.periodSeconds);
                if (!identity || identity->isEmpty()) {
                    failure = QStringLiteral("组合腿缺少数据集身份：%1").arg(symbol);
                    break;
                }
                if (identity->staleDayCount > 0) {
                    failure = QStringLiteral("组合腿派生缓存过期：%1").arg(symbol);
                    break;
                }
                QVector<Candle> candles = m_workerStore->loadRecentCandlesFromSource(
                    instrument, portfolioDefinition.periodSeconds, barCount,
                    QStringLiteral("raw_tick_cache_v1"));
                if (candles.size() < barCount) {
                    failure = QStringLiteral("组合腿 K 线不足：%1（%2/%3）")
                                  .arg(symbol).arg(candles.size()).arg(barCount);
                    break;
                }
                series.insert(symbol, candles);
                datasetVersions.append(identity->datasetVersion);
            }
            if (!failure.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, failure]() {
                    if (guard) emit guard->failed(requestId, failure);
                }, Qt::QueuedConnection);
                return;
            }

            // 3. 对齐成帧。
            QString alignError;
            const AlignedBarSeries aligned = AlignedBarSeries::build(series, &alignError);
            if (aligned.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, alignError]() {
                    if (guard) emit guard->failed(requestId, alignError);
                }, Qt::QueuedConnection);
                return;
            }

            // 4. 运行多股引擎。
            CrossSectionalCustomStrategy strategy(*plan);
            QString engineError;
            PortfolioResult result = CrossSectionalBacktestEngine::run(
                strategy, aligned, portfolioDefinition, &engineError);
            if (result.equityCurve.isEmpty() || !engineError.isEmpty()) {
                QMetaObject::invokeMethod(guard, [guard, requestId, engineError]() {
                    if (guard) emit guard->failed(
                        requestId, engineError.isEmpty()
                            ? QStringLiteral("多股回测无可用结果") : engineError);
                }, Qt::QueuedConnection);
                return;
            }
            result.datasetVersions = datasetVersions;
            QMetaObject::invokeMethod(guard, [guard, requestId, result]() {
                if (guard) emit guard->completed(requestId, result);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void CrossSectionalBacktestService::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

bool CrossSectionalBacktestService::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_acquire);
}

bool CrossSectionalBacktestService::isRunning() const { return m_running; }

} // namespace pp
