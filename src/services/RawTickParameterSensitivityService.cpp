#include "services/RawTickParameterSensitivityService.h"

#include "services/BacktestEngine.h"
#include "services/BacktestDataVersion.h"
#include "services/SqliteLocalDataStore.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace pp {

namespace {

void reportFailure(QPointer<RawTickParameterSensitivityService> guard,
                   quint64 requestId, const QString &message)
{
    QMetaObject::invokeMethod(guard, [guard, requestId, message]() {
        if (guard) emit guard->failed(requestId, message);
    }, Qt::QueuedConnection);
}

void reportResidency(QPointer<RawTickParameterSensitivityService> guard,
                     quint64 requestId, int residentObservations)
{
    QMetaObject::invokeMethod(guard, [guard, requestId, residentObservations]() {
        if (guard) {
            emit guard->rawTickResidencyChanged(
                requestId, std::max(0, residentObservations));
        }
    }, Qt::QueuedConnection);
}

BacktestDatasetIdentity identityForDateRange(
    BacktestDatasetIdentity identity, const QDate &firstDate,
    const QDate &lastDate)
{
    identity.days.erase(std::remove_if(identity.days.begin(), identity.days.end(),
        [&](const RawTickDayFingerprint &day) {
            return day.tradingDate < firstDate || day.tradingDate > lastDate;
        }), identity.days.end());
    std::sort(identity.days.begin(), identity.days.end(),
              [](const RawTickDayFingerprint &left,
                 const RawTickDayFingerprint &right) {
        return left.tradingDate < right.tradingDate;
    });
    identity.firstTradingDate = identity.days.isEmpty()
        ? QDate{} : identity.days.first().tradingDate;
    identity.lastTradingDate = identity.days.isEmpty()
        ? QDate{} : identity.days.last().tradingDate;
    identity.datasetVersion = identity.days.isEmpty()
        ? QString{} : computeBacktestDatasetVersion(identity);
    return identity;
}

bool rawFingerprintMatches(const RawTickDayFingerprint &expected,
                           const RawTradeFingerprint &current)
{
    if (current.count != expected.rawCount) return false;
    if (!expected.rawContentSha256.isEmpty()) {
        return current.contentSha256 == expected.rawContentSha256;
    }
    return current.maximumId == expected.rawMaximumId;
}

std::optional<BacktestResult> runStreamedRawTickSegment(
    SqliteLocalDataStore *store,
    const BacktestDatasetIdentity &fullIdentity,
    BacktestConfig config,
    const QDate &firstDate,
    const QDate &lastDate,
    const std::atomic_bool *cancelFlag,
    QPointer<RawTickParameterSensitivityService> guard,
    quint64 requestId,
    qint64 *loadMilliseconds,
    QString *error)
{
    if (error) error->clear();
    if (!store) {
        if (error) *error = QStringLiteral("原始 Tick 参数敏感性数据库不可用");
        return std::nullopt;
    }
    BacktestDatasetIdentity identity = identityForDateRange(
        fullIdentity, firstDate, lastDate);
    if (identity.isEmpty()) {
        if (error) *error = QStringLiteral("参数敏感性执行段没有原始 Tick 数据");
        return std::nullopt;
    }
    config.dataMode = BacktestDataMode::RawTicks;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.sampleFrom = {};
    config.sampleTo = {};
    config.datasetIdentity = identity;

    auto dayIndex = std::make_shared<int>(0);
    BacktestEngine::RawTickChunkLoader loader =
        [store, identity, dayIndex, cancelFlag, loadMilliseconds](
            QVector<RawTradeTick> *ticks, QString *loadError) mutable {
        if (!ticks) return false;
        ticks->clear();
        if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
            if (loadError) *loadError = QStringLiteral("原始 Tick 参数敏感性已取消");
            return false;
        }
        if (*dayIndex >= identity.days.size()) {
            if (loadError) loadError->clear();
            return true;
        }
        QElapsedTimer dayLoadTimer;
        dayLoadTimer.start();
        const auto recordLoadTime = [loadMilliseconds, &dayLoadTimer]() {
            if (loadMilliseconds)
                *loadMilliseconds += dayLoadTimer.elapsed();
        };
        const RawTickDayFingerprint expected = identity.days.at(*dayIndex);
        const RawTradeFingerprint before = store->rawTradeFingerprint(
            identity.instrument, expected.tradingDate);
        if (!rawFingerprintMatches(expected, before)) {
            if (loadError) {
                *loadError = QStringLiteral("原始 Tick 身份在执行前已变化：%1 · %2")
                    .arg(identity.instrument.symbol,
                         expected.tradingDate.toString(Qt::ISODate));
            }
            recordLoadTime();
            return false;
        }
        ticks->reserve(static_cast<int>(std::min<qint64>(
            expected.rawCount, std::numeric_limits<int>::max())));
        bool interrupted = false;
        const bool loaded = store->forEachRawTradeTick(
            identity.instrument, expected.tradingDate,
            [ticks, cancelFlag, &interrupted](const RawTradeTick &tick) {
                if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
                    interrupted = true;
                    return false;
                }
                ticks->append(tick);
                return true;
            });
        const RawTradeFingerprint after = store->rawTradeFingerprint(
            identity.instrument, expected.tradingDate);
        if (interrupted || !loaded || ticks->size() != expected.rawCount
            || !rawFingerprintMatches(expected, after)) {
            ticks->clear();
            if (loadError) {
                *loadError = interrupted
                    ? QStringLiteral("原始 Tick 参数敏感性已取消")
                    : QStringLiteral(
                        "原始 Tick 身份在分段读取期间变化或读取不完整：%1 · %2")
                          .arg(identity.instrument.symbol,
                               expected.tradingDate.toString(Qt::ISODate));
            }
            recordLoadTime();
            return false;
        }
        ++(*dayIndex);
        recordLoadTime();
        if (loadError) loadError->clear();
        return true;
    };

    BacktestEngine engine;
    QEventLoop loop;
    QString engineError;
    bool completed = false;
    QObject::connect(&engine, &BacktestEngine::finished, &loop,
                     [&](const PerformanceSummary &) {
        completed = true;
        loop.quit();
    });
    QObject::connect(&engine, &BacktestEngine::errorOccurred, &loop,
                     [&](const QString &message) {
        engineError = message;
        loop.quit();
    });
    QObject::connect(&engine, &BacktestEngine::rawTickChunkResidencyChanged,
                     &loop, [guard, requestId](int observations) {
        reportResidency(guard, requestId, observations);
    });
    QTimer cancelTimer;
    cancelTimer.setInterval(10);
    QObject::connect(&cancelTimer, &QTimer::timeout, &loop,
                     [&engine, &loop, cancelFlag]() {
        if (!cancelFlag
            || !cancelFlag->load(std::memory_order_acquire)) return;
        engine.cancel();
        loop.quit();
    });
    if (!engine.startStreamedRawTicks(
            config, identity.rawTickTotal(), std::move(loader))) {
        reportResidency(guard, requestId, 0);
        if (error) {
            *error = engineError.isEmpty()
                ? QStringLiteral("原始 Tick 参数组合无法启动") : engineError;
        }
        return std::nullopt;
    }
    cancelTimer.start();
    loop.exec();
    cancelTimer.stop();
    reportResidency(guard, requestId, 0);
    if (!completed) {
        if (error) {
            *error = cancelFlag && cancelFlag->load(std::memory_order_acquire)
                ? QStringLiteral("原始 Tick 参数敏感性已取消")
                : (engineError.isEmpty()
                   ? QStringLiteral("原始 Tick 参数组合执行失败") : engineError);
        }
        return std::nullopt;
    }
    return engine.result();
}

} // namespace

RawTickParameterSensitivityService::RawTickParameterSensitivityService(QObject *parent)
    : QObject(parent), m_worker(new QObject)
{
    qRegisterMetaType<RawTickParameterSensitivityResult>();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName(QStringLiteral("PPTraderRawTickSensitivity"));
    m_thread.start();
}

RawTickParameterSensitivityService::~RawTickParameterSensitivityService()
{
    stop();
    m_thread.quit();
    // 扫描组合数受限且每个组合结束后检查取消；等待退出避免悬空工作线程。
    m_thread.wait();
    m_worker = nullptr;
}

bool RawTickParameterSensitivityService::start(const QString &databasePath)
{
    if (m_running || !m_worker || databasePath.trimmed().isEmpty()) return m_running;
    bool initialized = false;
    QMetaObject::invokeMethod(m_worker, [this, databasePath, &initialized]() {
        m_workerStore = std::make_unique<SqliteLocalDataStore>();
        initialized = m_workerStore->initialize(databasePath);
        if (!initialized) m_workerStore.reset();
    }, Qt::BlockingQueuedConnection);
    m_running = initialized;
    return initialized;
}

void RawTickParameterSensitivityService::stop()
{
    if (!m_thread.isRunning()) return;
    cancel();
    // 流式候选的分段读取器直接使用工作线程 SQLite 连接。先等当前请求
    // 观察到取消并退出，避免嵌套事件循环提前处理 reset 导致连接悬空。
    while (m_busy.load(std::memory_order_acquire) && m_thread.isRunning()) {
        QThread::msleep(1);
    }
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, [this]() { m_workerStore.reset(); },
                                  Qt::BlockingQueuedConnection);
    }
    m_running = false;
    m_busy.store(false, std::memory_order_release);
}

bool RawTickParameterSensitivityService::request(
    quint64 requestId, const RawTickParameterSensitivityRequest &request,
    const QDate &firstDate, const QDate &lastDate)
{
    if (!m_running || !m_worker || requestId == 0
        || !firstDate.isValid() || !lastDate.isValid() || firstDate > lastDate
        || m_busy.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    m_cancelRequested.store(false, std::memory_order_release);
    QPointer<RawTickParameterSensitivityService> guard(this);
    RawTickParameterSensitivityRequest mutableRequest = request;
    const bool invoked = QMetaObject::invokeMethod(
        m_worker,
        [this, guard, requestId, request = std::move(mutableRequest),
         firstDate, lastDate]() mutable {
            const auto finishFailure = [this, guard, requestId](const QString &message) {
                m_busy.store(false, std::memory_order_release);
                reportFailure(guard, requestId, message);
            };
            if (!m_workerStore) {
                finishFailure(QStringLiteral("原始 Tick 参数敏感性数据库不可用"));
                return;
            }
            QElapsedTimer loadTimer;
            loadTimer.start();
            auto identity = m_workerStore->loadRawBacktestDatasetIdentity(
                request.baseConfig.instrument);
            if (!identity || identity->isEmpty()) {
                finishFailure(QStringLiteral("没有可用的本地原始 Tick 数据：%1")
                                  .arg(request.baseConfig.instrument.symbol));
                return;
            }
            QVector<RawTickDayFingerprint> selectedDays;
            for (const RawTickDayFingerprint &day : identity->days) {
                if (day.tradingDate >= firstDate && day.tradingDate <= lastDate) {
                    selectedDays.append(day);
                }
            }
            std::sort(selectedDays.begin(), selectedDays.end(),
                      [](const RawTickDayFingerprint &left,
                         const RawTickDayFingerprint &right) {
                return left.tradingDate < right.tradingDate;
            });
            identity->days = selectedDays;
            identity->firstTradingDate = selectedDays.isEmpty()
                ? QDate{} : selectedDays.first().tradingDate;
            identity->lastTradingDate = selectedDays.isEmpty()
                ? QDate{} : selectedDays.last().tradingDate;
            identity->datasetVersion = computeBacktestDatasetVersion(*identity);
            const int requiredValidationDays = request.validationTradingDays
                * request.walkForwardFoldCount;
            if (selectedDays.size() < requiredValidationDays + 2) {
                finishFailure(QStringLiteral(
                    "所选范围只有 %1 个原始 Tick 交易日；至少需要 2 个初始训练日和 %2 折×%3 个验证日")
                                  .arg(selectedDays.size())
                                  .arg(request.walkForwardFoldCount)
                                  .arg(request.validationTradingDays));
                return;
            }

            request.datasetVersion = identity->datasetVersion;
            const qint64 identityLoadMilliseconds = loadTimer.elapsed();
            qint64 streamedLoadMilliseconds = 0;
            QElapsedTimer scanTimer;
            scanTimer.start();
            QString error;
            const BacktestDatasetIdentity selectedIdentity = *identity;
            const RawTickParameterSensitivityScanner::SegmentRunner runner =
                [this, selectedIdentity, guard, requestId,
                 &streamedLoadMilliseconds](
                    BacktestConfig config, const QDate &segmentFirstDate,
                    const QDate &segmentLastDate, QString *runError) {
                return runStreamedRawTickSegment(
                    m_workerStore.get(), selectedIdentity, std::move(config),
                    segmentFirstDate, segmentLastDate, &m_cancelRequested,
                    guard, requestId, &streamedLoadMilliseconds, runError);
            };
            auto result = RawTickParameterSensitivityScanner::scanSegments(
                request, selectedDays, runner, &m_cancelRequested, &error);
            if (!result) {
                finishFailure(error.isEmpty()
                    ? QStringLiteral("原始 Tick 参数敏感性执行失败") : error);
                return;
            }
            result->loadMilliseconds = identityLoadMilliseconds
                + streamedLoadMilliseconds;
            result->scanMilliseconds = std::max<qint64>(
                0, scanTimer.elapsed() - streamedLoadMilliseconds);
            for (const RawTickParameterSensitivityResult &existing :
                 m_workerStore->loadRawTickParameterSensitivityResults(1000)) {
                if (existing.identitySha256 != result->identitySha256) continue;
                RawTickParameterSensitivityResult reused = existing;
                reused.loadMilliseconds = result->loadMilliseconds;
                reused.scanMilliseconds = result->scanMilliseconds;
                m_busy.store(false, std::memory_order_release);
                QMetaObject::invokeMethod(guard,
                    [guard, requestId, reused]() {
                        if (guard) emit guard->completed(requestId, reused);
                    }, Qt::QueuedConnection);
                return;
            }
            result->resultId = QStringLiteral("raw-sensitivity-%1").arg(
                QUuid::createUuid().toString(QUuid::WithoutBraces));
            result->completedAt = QDateTime::currentDateTimeUtc();
            QString saveError;
            if (!m_workerStore->saveRawTickParameterSensitivityResult(*result,
                                                                       &saveError)) {
                finishFailure(QStringLiteral("参数敏感性完成但归档失败：%1")
                                  .arg(saveError));
                return;
            }
            m_busy.store(false, std::memory_order_release);
            const RawTickParameterSensitivityResult completedResult = *result;
            QMetaObject::invokeMethod(guard,
                [guard, requestId, completedResult]() {
                    if (guard) emit guard->completed(requestId, completedResult);
                }, Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    if (!invoked) m_busy.store(false, std::memory_order_release);
    return invoked;
}

void RawTickParameterSensitivityService::cancel()
{
    m_cancelRequested.store(true, std::memory_order_release);
}

bool RawTickParameterSensitivityService::isRunning() const
{
    return m_running;
}

bool RawTickParameterSensitivityService::isBusy() const
{
    return m_busy.load(std::memory_order_acquire);
}

} // namespace pp
