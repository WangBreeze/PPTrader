#include "services/AShareUniverseLoader.h"
#include "services/TencentMarketDataProvider.h"
#include "strategy/StrategyEngine.h"
#include "strategy/TickMomentumStrategy.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

struct MemorySample {
    qint64 elapsedMilliseconds = 0;
    quint64 privateBytes = 0;
    quint64 workingSetBytes = 0;
};

double percentile(QVector<qint64> values, double ratio)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const int index = std::clamp(
        static_cast<int>(std::ceil(values.size() * ratio)) - 1,
        0, static_cast<int>(values.size()) - 1);
    return static_cast<double>(values.at(index));
}

double minimum(const QVector<double> &values)
{
    return values.isEmpty() ? 0.0
                            : *std::min_element(values.cbegin(), values.cend());
}

double slopePerMinute(const QVector<MemorySample> &samples, bool privateBytes)
{
    if (samples.size() < 2) return 0.0;
    double meanX = 0.0;
    double meanY = 0.0;
    for (const MemorySample &sample : samples) {
        meanX += static_cast<double>(sample.elapsedMilliseconds) / 60000.0;
        meanY += static_cast<double>(privateBytes ? sample.privateBytes
                                                 : sample.workingSetBytes);
    }
    meanX /= samples.size();
    meanY /= samples.size();
    double covariance = 0.0;
    double variance = 0.0;
    for (const MemorySample &sample : samples) {
        const double x = static_cast<double>(sample.elapsedMilliseconds) / 60000.0;
        const double y = static_cast<double>(privateBytes ? sample.privateBytes
                                                          : sample.workingSetBytes);
        covariance += (x - meanX) * (y - meanY);
        variance += (x - meanX) * (x - meanX);
    }
    return variance > 0.0 ? covariance / variance : 0.0;
}

MemorySample processMemory(qint64 elapsedMilliseconds)
{
    MemorySample sample;
    sample.elapsedMilliseconds = elapsedMilliseconds;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        sample.privateBytes = static_cast<quint64>(counters.PrivateUsage);
        sample.workingSetBytes = static_cast<quint64>(counters.WorkingSetSize);
    }
#endif
    return sample;
}

bool writeReport(const QString &path, const QJsonObject &report)
{
    if (path.isEmpty()) return true;
    QSaveFile output(path);
    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    return output.open(QIODevice::WriteOnly)
        && output.write(payload) == payload.size() && output.commit();
}

class LiveMonitorState final {
public:
    LiveMonitorState(pp::StrategyEngine *engine, QElapsedTimer *clock)
        : strategyEngine(engine)
        , runClock(clock)
    {
    }

    void recordSignal(const pp::TradeSignal &signal)
    {
        const QString key = QStringLiteral("%1|%2|%3")
            .arg(signal.instrument.symbol,
                 signal.timestamp.toUTC().toString(Qt::ISODateWithMs))
            .arg(static_cast<int>(signal.side));
        QMutexLocker locker(&signalMutex);
        if (signalKeys.contains(key)) ++duplicateSignalCount;
        else signalKeys.insert(key);
        ++signalCount;
    }

    void beginBatch(const QString &batchId)
    {
        if (batchId.isEmpty() || activeBatchId == batchId) return;
        activeBatchId = batchId;
        activeBatchStarted = runClock->elapsed();
        dispatchedBefore = strategyEngine->dispatchedTaskCount();
        completedBefore = strategyEngine->completedTaskCount();
        {
            QMutexLocker locker(&signalMutex);
            signalsBefore = signalCount;
            duplicateSignalsBefore = duplicateSignalCount;
        }
        enqueueMicroseconds.clear();
        submittedSymbols.clear();
        duplicateQuotes = 0;
    }

    void recordProviderError(const QString &error)
    {
        if (!error.isEmpty()) providerErrors.append(error);
    }

    void handleTick(const pp::Tick &tick)
    {
        beginBatch(tick.batchId);
        if (submittedSymbols.contains(tick.instrument.symbol)) ++duplicateQuotes;
        else submittedSymbols.insert(tick.instrument.symbol);
        QElapsedTimer timer;
        timer.start();
        strategyEngine->dispatchTick(tick);
        enqueueMicroseconds.append(timer.nsecsElapsed() / 1000);
    }

    void handleSnapshot(const pp::MarketSnapshot &snapshot)
    {
        beginBatch(snapshot.batchId);
        QElapsedTimer drainTimer;
        drainTimer.start();
        const bool drained = strategyEngine->waitForIdle(10000);
        const qint64 drainMilliseconds = drainTimer.elapsed();
        const quint64 dispatched = strategyEngine->dispatchedTaskCount()
            - dispatchedBefore;
        const quint64 completed = strategyEngine->completedTaskCount()
            - completedBefore;
        quint64 generatedSignals = 0;
        quint64 duplicateSignals = 0;
        {
            QMutexLocker locker(&signalMutex);
            generatedSignals = signalCount - signalsBefore;
            duplicateSignals = duplicateSignalCount - duplicateSignalsBefore;
        }
        const MemorySample memory = processMemory(runClock->elapsed());
        memorySamples.append(memory);
        const qint64 quoteLag = snapshot.timestamp.isValid()
            ? snapshot.timestamp.msecsTo(QDateTime::currentDateTimeUtc()) : -1;
        QJsonObject row;
        row.insert(QStringLiteral("sequence"), batchReports.size() + 1);
        row.insert(QStringLiteral("batchId"), snapshot.batchId);
        row.insert(QStringLiteral("quoteTime"),
                   snapshot.timestamp.toUTC().toString(Qt::ISODateWithMs));
        row.insert(QStringLiteral("expected"), snapshot.expectedCount);
        row.insert(QStringLiteral("received"), snapshot.quotes.size());
        row.insert(QStringLiteral("completeness"), snapshot.completenessRatio());
        row.insert(QStringLiteral("providerElapsedMs"),
                   static_cast<double>(runClock->elapsed() - activeBatchStarted));
        row.insert(QStringLiteral("quoteLagMs"), static_cast<double>(quoteLag));
        row.insert(QStringLiteral("submittedSymbols"), submittedSymbols.size());
        row.insert(QStringLiteral("duplicateQuotes"), duplicateQuotes);
        row.insert(QStringLiteral("strategyTasksDispatched"),
                   static_cast<double>(dispatched));
        row.insert(QStringLiteral("strategyTasksCompleted"),
                   static_cast<double>(completed));
        row.insert(QStringLiteral("strategyTasksMissing"),
                   static_cast<double>(dispatched >= completed
                                           ? dispatched - completed : 0));
        row.insert(QStringLiteral("strategyDrainMs"),
                   static_cast<double>(drainMilliseconds));
        row.insert(QStringLiteral("strategyDrainTimedOut"), !drained);
        row.insert(QStringLiteral("enqueueP95Us"),
                   percentile(enqueueMicroseconds, 0.95));
        row.insert(QStringLiteral("enqueueMaxUs"),
                   percentile(enqueueMicroseconds, 1.0));
        row.insert(QStringLiteral("signals"),
                   static_cast<double>(generatedSignals));
        row.insert(QStringLiteral("duplicateSignals"),
                   static_cast<double>(duplicateSignals));
        row.insert(QStringLiteral("privateBytes"),
                   static_cast<double>(memory.privateBytes));
        row.insert(QStringLiteral("workingSetBytes"),
                   static_cast<double>(memory.workingSetBytes));
        batchReports.append(row);
        QTextStream(stderr)
            << QStringLiteral("batch=%1 quotes=%2/%3 providerMs=%4 drainMs=%5 missing=%6 privateMiB=%7")
                   .arg(batchReports.size()).arg(snapshot.quotes.size())
                   .arg(snapshot.expectedCount)
                   .arg(row.value(QStringLiteral("providerElapsedMs")).toDouble(), 0, 'f', 0)
                   .arg(drainMilliseconds)
                   .arg(row.value(QStringLiteral("strategyTasksMissing")).toInt())
                   .arg(memory.privateBytes / 1048576.0, 0, 'f', 1)
            << Qt::endl;
        activeBatchId.clear();
    }

    quint64 totalSignalCount()
    {
        QMutexLocker locker(&signalMutex);
        return signalCount;
    }

    pp::StrategyEngine *strategyEngine = nullptr;
    QElapsedTimer *runClock = nullptr;
    QString activeBatchId;
    qint64 activeBatchStarted = 0;
    quint64 dispatchedBefore = 0;
    quint64 completedBefore = 0;
    quint64 signalsBefore = 0;
    quint64 duplicateSignalsBefore = 0;
    QVector<qint64> enqueueMicroseconds;
    QSet<QString> submittedSymbols;
    int duplicateQuotes = 0;
    QVector<QJsonObject> batchReports;
    QVector<MemorySample> memorySamples;
    QStringList providerErrors;

private:
    QMutex signalMutex;
    QSet<QString> signalKeys;
    quint64 signalCount = 0;
    quint64 duplicateSignalCount = 0;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("pptrader_live_strategy_monitor"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("c"), QStringLiteral("cache")},
         QStringLiteral("A-share universe cache"), QStringLiteral("path"),
         QStringLiteral("data/astock_cache.json")},
        {{QStringLiteral("n"), QStringLiteral("symbols")},
         QStringLiteral("Distinct symbols to monitor"), QStringLiteral("count"),
         QStringLiteral("500")},
        {{QStringLiteral("d"), QStringLiteral("duration-seconds")},
         QStringLiteral("Acceptance duration after start"), QStringLiteral("seconds"),
         QStringLiteral("1800")},
        {{QStringLiteral("i"), QStringLiteral("interval-seconds")},
         QStringLiteral("Tencent polling interval"), QStringLiteral("seconds"),
         QStringLiteral("60")},
        {{QStringLiteral("s"), QStringLiteral("start-at")},
         QStringLiteral("Optional ISO start time with UTC offset"),
         QStringLiteral("datetime")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("Atomic JSON report path"), QStringLiteral("path")}
    });
    parser.process(application);

    const int symbolCount = std::clamp(
        parser.value(QStringLiteral("symbols")).toInt(), 1, 5207);
    const int durationSeconds = std::clamp(
        parser.value(QStringLiteral("duration-seconds")).toInt(), 60, 86400);
    const int intervalSeconds = std::clamp(
        parser.value(QStringLiteral("interval-seconds")).toInt(), 60, 3600);
    const auto universeResult = pp::AShareUniverseLoader::load(
        parser.value(QStringLiteral("cache")));
    if (!universeResult.error.isEmpty()
        || universeResult.instruments.size() < symbolCount) {
        QTextStream(stderr) << (!universeResult.error.isEmpty()
            ? universeResult.error
            : QStringLiteral("股票池只有 %1 只，少于请求的 %2 只")
                  .arg(universeResult.instruments.size()).arg(symbolCount))
                            << Qt::endl;
        return 2;
    }
    const QVector<pp::Instrument> instruments =
        universeResult.instruments.sliced(0, symbolCount);

    qputenv("PPTRADER_TENCENT_INTERVAL_SECONDS",
            QByteArray::number(intervalSeconds));
    qunsetenv("PPTRADER_TENCENT_MAX_BATCHES");

    pp::StrategyEngine strategyEngine;
    strategyEngine.setMaxThreadCount(
        std::max(1, QThread::idealThreadCount() - 1));
    strategyEngine.setPerformanceMetricsEnabled(true);
    strategyEngine.resetPerformanceMetrics();

    QElapsedTimer runClock;
    runClock.start();
    LiveMonitorState state(&strategyEngine, &runClock);
    for (const pp::Instrument &instrument : instruments) {
        auto *strategy = new pp::TickMomentumStrategy(instrument, 0.0025, 20);
        QObject::connect(strategy, &pp::Strategy::signalGenerated, &application,
                         [&state](const pp::TradeSignal &signal) {
            state.recordSignal(signal);
        }, Qt::DirectConnection);
        strategyEngine.addStrategy(strategy);
    }

    pp::TencentMarketDataProvider provider;
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    qint64 lastHeartbeat = 0;
    QVector<qint64> heartbeatGaps;
    QTimer heartbeat;
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, &application, [&] {
        const qint64 now = heartbeatClock.elapsed();
        if (lastHeartbeat > 0) heartbeatGaps.append(now - lastHeartbeat);
        lastHeartbeat = now;
    });
    heartbeat.start();

    bool started = false;
    bool finished = false;

    QObject::connect(&provider, &pp::MarketDataProvider::providerStateChanged,
                     &application,
                     [&state](int, const QString &, const QString &error,
                         int, int, const QString &batchId, const QDateTime &) {
        state.beginBatch(batchId);
        state.recordProviderError(error);
    });
    QObject::connect(&provider, &pp::MarketDataProvider::providerError,
                     &application, [&state](const QString &error) {
        state.recordProviderError(error);
    });
    QObject::connect(&provider, &pp::MarketDataProvider::tickReceived,
                     &application, [&state](const pp::Tick &tick) {
        state.handleTick(tick);
    });
    QObject::connect(&provider, &pp::MarketDataProvider::snapshotReceived,
                     &application, [&state](const pp::MarketSnapshot &snapshot) {
        state.handleSnapshot(snapshot);
    });

    QTimer finishTimer;
    finishTimer.setSingleShot(true);
    QObject::connect(&finishTimer, &QTimer::timeout, &application, [&] {
        if (finished) return;
        finished = true;
        provider.stop();
        strategyEngine.waitForIdle(10000);
        application.quit();
    });

    const auto start = [&] {
        if (started) return;
        started = true;
        runClock.restart();
        heartbeatClock.restart();
        lastHeartbeat = 0;
        provider.start(instruments);
        finishTimer.start(durationSeconds * 1000);
    };
    const QString startText = parser.value(QStringLiteral("start-at"));
    const QDateTime startAt = QDateTime::fromString(startText, Qt::ISODate);
    if (!startText.isEmpty() && !startAt.isValid()) {
        QTextStream(stderr) << QStringLiteral("--start-at 必须是带时区的 ISO 时间")
                            << Qt::endl;
        return 3;
    }
    const qint64 delay = startAt.isValid()
        ? std::max<qint64>(0, QDateTime::currentDateTimeUtc().msecsTo(startAt.toUTC()))
        : 0;
    QTimer::singleShot(delay, &application, start);
    application.exec();
    heartbeat.stop();

    QVector<double> completeness;
    QVector<qint64> providerDurations;
    QVector<qint64> quoteLags;
    QVector<qint64> drainDurations;
    QVector<qint64> enqueueP95Values;
    quint64 totalMissing = 0;
    quint64 totalDuplicateQuotes = 0;
    quint64 totalDuplicateSignals = 0;
    for (const QJsonObject &row : std::as_const(state.batchReports)) {
        completeness.append(row.value(QStringLiteral("completeness")).toDouble());
        providerDurations.append(
            static_cast<qint64>(row.value(QStringLiteral("providerElapsedMs")).toDouble()));
        const qint64 lag = static_cast<qint64>(
            row.value(QStringLiteral("quoteLagMs")).toDouble(-1));
        if (lag >= 0) quoteLags.append(lag);
        drainDurations.append(
            static_cast<qint64>(row.value(QStringLiteral("strategyDrainMs")).toDouble()));
        enqueueP95Values.append(
            static_cast<qint64>(row.value(QStringLiteral("enqueueP95Us")).toDouble()));
        totalMissing += static_cast<quint64>(
            row.value(QStringLiteral("strategyTasksMissing")).toDouble());
        totalDuplicateQuotes += static_cast<quint64>(
            row.value(QStringLiteral("duplicateQuotes")).toInt());
        totalDuplicateSignals += static_cast<quint64>(
            row.value(QStringLiteral("duplicateSignals")).toDouble());
    }
    QVector<MemorySample> stableMemory = state.memorySamples.size() > 3
        ? state.memorySamples.sliced(3) : state.memorySamples;
    quint64 privateMaximum = 0;
    for (const MemorySample &sample : std::as_const(state.memorySamples)) {
        privateMaximum = std::max(privateMaximum, sample.privateBytes);
    }
    const int minimumBatches = std::max(1, durationSeconds / intervalSeconds - 1);
    const double privateSlope = slopePerMinute(stableMemory, true);
    const bool accepted = state.batchReports.size() >= minimumBatches
        && minimum(completeness) >= 0.95
        && state.providerErrors.isEmpty() && totalMissing == 0
        && totalDuplicateQuotes == 0 && totalDuplicateSignals == 0
        && percentile(drainDurations, 0.95) <= 100.0
        && percentile(enqueueP95Values, 0.95) <= 1000.0
        && percentile(heartbeatGaps, 0.95) <= 50.0
        && privateSlope <= 1048576.0;

    QJsonArray batches;
    for (const QJsonObject &row : std::as_const(state.batchReports)) batches.append(row);
    QJsonArray errors;
    for (const QString &error : std::as_const(state.providerErrors)) errors.append(error);
    QJsonObject report;
    report.insert(QStringLiteral("accepted"), accepted);
    report.insert(QStringLiteral("executionMode"),
                  QStringLiteral("signal_only_no_order_port"));
    report.insert(QStringLiteral("provider"),
                  QStringLiteral("tencent_public_quote"));
    report.insert(QStringLiteral("pollingPeriodSeconds"), intervalSeconds);
    report.insert(QStringLiteral("requestedDurationSeconds"), durationSeconds);
    report.insert(QStringLiteral("symbols"), symbolCount);
    report.insert(QStringLiteral("strategyCount"), strategyEngine.strategyCount());
    report.insert(QStringLiteral("batchCount"), state.batchReports.size());
    report.insert(QStringLiteral("minimumRequiredBatches"), minimumBatches);
    report.insert(QStringLiteral("minimumCompleteness"), minimum(completeness));
    report.insert(QStringLiteral("providerP50Ms"), percentile(providerDurations, 0.50));
    report.insert(QStringLiteral("providerP95Ms"), percentile(providerDurations, 0.95));
    report.insert(QStringLiteral("quoteLagP50Ms"), percentile(quoteLags, 0.50));
    report.insert(QStringLiteral("quoteLagP95Ms"), percentile(quoteLags, 0.95));
    report.insert(QStringLiteral("strategyDrainP50Ms"), percentile(drainDurations, 0.50));
    report.insert(QStringLiteral("strategyDrainP95Ms"), percentile(drainDurations, 0.95));
    report.insert(QStringLiteral("enqueueP95OfBatchesUs"),
                  percentile(enqueueP95Values, 0.95));
    report.insert(QStringLiteral("heartbeatP95Ms"), percentile(heartbeatGaps, 0.95));
    report.insert(QStringLiteral("heartbeatMaxMs"), percentile(heartbeatGaps, 1.0));
    report.insert(QStringLiteral("strategyTasksDispatched"),
                  static_cast<double>(strategyEngine.dispatchedTaskCount()));
    report.insert(QStringLiteral("strategyTasksCompleted"),
                  static_cast<double>(strategyEngine.completedTaskCount()));
    report.insert(QStringLiteral("strategyTasksMissing"),
                  static_cast<double>(totalMissing));
    const quint64 finalSignalCount = state.totalSignalCount();
    report.insert(QStringLiteral("signals"),
                  static_cast<double>(finalSignalCount));
    report.insert(QStringLiteral("duplicateQuotes"),
                  static_cast<double>(totalDuplicateQuotes));
    report.insert(QStringLiteral("duplicateSignals"),
                  static_cast<double>(totalDuplicateSignals));
    report.insert(QStringLiteral("providerErrors"), errors);
    report.insert(QStringLiteral("privateBytesMax"),
                  static_cast<double>(privateMaximum));
    report.insert(QStringLiteral("privateBytesSlopePerMinute"), privateSlope);
    report.insert(QStringLiteral("persistenceWrites"), 0);
    report.insert(QStringLiteral("batches"), batches);
    const QString outputPath = parser.value(QStringLiteral("output"));
    if (!writeReport(outputPath, report)) {
        QTextStream(stderr) << QStringLiteral("无法写入实时策略验收报告") << Qt::endl;
        return 5;
    }
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Compact)
                        << Qt::endl;
    return accepted ? 0 : 4;
}
