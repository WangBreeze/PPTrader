#include "services/BacktestEngine.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

struct ProcessMemorySample {
    qint64 elapsedMilliseconds = 0;
    quint64 workingSetBytes = 0;
    quint64 privateBytes = 0;
};

double percentile(QVector<qint64> values, double fraction)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const int valueCount = static_cast<int>(values.size());
    const int index = std::clamp(
        static_cast<int>(std::ceil(fraction * valueCount)) - 1,
        0, valueCount - 1);
    return static_cast<double>(values.at(index));
}

ProcessMemorySample currentProcessMemory(qint64 elapsedMilliseconds)
{
    ProcessMemorySample sample;
    sample.elapsedMilliseconds = elapsedMilliseconds;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        sample.workingSetBytes = static_cast<quint64>(counters.WorkingSetSize);
        sample.privateBytes = static_cast<quint64>(counters.PrivateUsage);
    }
#endif
    return sample;
}

double slopePerMinute(const QVector<ProcessMemorySample> &samples, bool usePrivate)
{
    if (samples.size() < 2) return 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    for (const ProcessMemorySample &sample : samples) {
        sumX += static_cast<double>(sample.elapsedMilliseconds) / 60000.0;
        sumY += static_cast<double>(usePrivate ? sample.privateBytes
                                              : sample.workingSetBytes);
    }
    const double count = static_cast<double>(samples.size());
    const double meanX = sumX / count;
    const double meanY = sumY / count;
    double covariance = 0.0;
    double variance = 0.0;
    for (const ProcessMemorySample &sample : samples) {
        const double x = static_cast<double>(sample.elapsedMilliseconds) / 60000.0;
        const double y = static_cast<double>(usePrivate ? sample.privateBytes
                                                        : sample.workingSetBytes);
        covariance += (x - meanX) * (y - meanY);
        variance += (x - meanX) * (x - meanX);
    }
    return variance > 0.0 ? covariance / variance : 0.0;
}

QVector<pp::Candle> loadTemplate(const QString &databasePath,
                                 const QString &symbol, int requestedBars,
                                 QString *error)
{
    QVector<pp::Candle> candles;
    const QString connectionName = QStringLiteral("capacity-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            if (error) *error = QStringLiteral("无法打开容量基准 SQLite");
        } else {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "select name, market, venue, start_time_utc, period_seconds, "
                "open, high, low, close, volume, buy_volume, sell_volume, "
                "neutral_volume, directional_volume_known "
                "from candles where symbol = ? and source = 'raw_tick_cache_v1' "
                "and close > 0 order by start_time_utc desc limit ?"));
            query.addBindValue(symbol);
            query.addBindValue(requestedBars);
            if (!query.exec()) {
                if (error) *error = QStringLiteral("读取 Tick 派生 K 线失败");
            } else {
                while (query.next()) {
                    pp::Candle candle;
                    candle.instrument.symbol = symbol;
                    candle.instrument.name = query.value(0).toString();
                    candle.instrument.market = pp::Market::AShare;
                    candle.instrument.venue = query.value(2).toString();
                    candle.startTime = QDateTime::fromString(
                        query.value(3).toString(), Qt::ISODateWithMs);
                    candle.periodSeconds = query.value(4).toInt();
                    candle.open = query.value(5).toDouble();
                    candle.high = query.value(6).toDouble();
                    candle.low = query.value(7).toDouble();
                    candle.close = query.value(8).toDouble();
                    candle.volume = query.value(9).toDouble();
                    candle.buyVolume = query.value(10).toDouble();
                    candle.sellVolume = query.value(11).toDouble();
                    candle.neutralVolume = query.value(12).toDouble();
                    candle.directionalVolumeKnown = query.value(13).toBool();
                    candle.source = QStringLiteral("raw_tick_cache_v1");
                    candles.prepend(candle);
                }
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (candles.isEmpty() && error && error->isEmpty()) {
        *error = QStringLiteral("没有找到 Tick 派生 K 线模板");
    }
    return candles;
}

QStringList loadDistinctSymbols(const QString &databasePath, int requestedBars,
                                int symbolCount, QString *error)
{
    QStringList symbols;
    const QString connectionName = QStringLiteral("capacity-symbols-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!database.open()) {
            if (error) *error = QStringLiteral("无法打开容量基准 SQLite");
        } else {
            QSqlQuery query(database);
            query.prepare(QStringLiteral(
                "select symbol from candles where source = 'raw_tick_cache_v1' "
                "and close > 0 group by symbol having count(*) >= ? "
                "order by symbol asc limit ?"));
            query.addBindValue(requestedBars);
            query.addBindValue(symbolCount);
            if (!query.exec()) {
                if (error) *error = QStringLiteral("读取真实回测数据集目录失败");
            } else {
                while (query.next()) symbols.append(query.value(0).toString());
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    if (symbols.size() < symbolCount && error && error->isEmpty()) {
        *error = QStringLiteral("只有 %1 个数据集满足 %2 根要求，少于请求的 %3 个")
            .arg(symbols.size()).arg(requestedBars).arg(symbolCount);
    }
    return symbols;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("pptrader_backtest_capacity"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("d"), QStringLiteral("database")},
         QStringLiteral("SQLite database path"), QStringLiteral("path")},
        {{QStringLiteral("s"), QStringLiteral("symbol")},
         QStringLiteral("Template symbol"), QStringLiteral("symbol"),
         QStringLiteral("600519")},
        {{QStringLiteral("n"), QStringLiteral("symbols")},
         QStringLiteral("Number of independent symbols"), QStringLiteral("count"),
         QStringLiteral("100")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("Bars per symbol"), QStringLiteral("count"),
         QStringLiteral("239")},
        {{QStringLiteral("c"), QStringLiteral("concurrency")},
         QStringLiteral("Maximum active engines"), QStringLiteral("count"),
         QStringLiteral("1")},
        {{QStringLiteral("r"), QStringLiteral("rounds")},
         QStringLiteral("Sequential rounds in the same process"),
         QStringLiteral("count"), QStringLiteral("1")},
        {{QStringLiteral("t"), QStringLiteral("duration-seconds")},
         QStringLiteral("Run complete rounds until this duration; overrides rounds"),
         QStringLiteral("seconds"), QStringLiteral("0")},
        {{QStringLiteral("w"), QStringLiteral("memory-warmup-rounds")},
         QStringLiteral("Rounds excluded from memory slope"),
         QStringLiteral("count"), QStringLiteral("3")},
        {QStringLiteral("distinct-datasets"),
         QStringLiteral("Use independent local raw_tick_cache_v1 datasets")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("Optional JSON output path"), QStringLiteral("path")},
        {{QStringLiteral("p"), QStringLiteral("progress-seconds")},
         QStringLiteral("Progress report interval"), QStringLiteral("seconds"),
         QStringLiteral("30")}
    });
    parser.process(application);

    const QString databasePath = parser.value(QStringLiteral("database"));
    const QString templateSymbol = parser.value(QStringLiteral("symbol")).trimmed();
    const int symbolCount = std::max(1, parser.value(QStringLiteral("symbols")).toInt());
    const int requestedBars = std::max(20, parser.value(QStringLiteral("bars")).toInt());
    const int concurrency = std::clamp(
        parser.value(QStringLiteral("concurrency")).toInt(), 1, 128);
    const int requestedRounds = std::max(
        1, parser.value(QStringLiteral("rounds")).toInt());
    const int durationSeconds = std::clamp(
        parser.value(QStringLiteral("duration-seconds")).toInt(), 0, 86400);
    const int memoryWarmupRounds = std::clamp(
        parser.value(QStringLiteral("memory-warmup-rounds")).toInt(), 0, 1000);
    const bool distinctDatasets = parser.isSet(QStringLiteral("distinct-datasets"));
    const int progressSeconds = std::clamp(
        parser.value(QStringLiteral("progress-seconds")).toInt(), 1, 3600);
    if (databasePath.isEmpty()) {
        QTextStream(stderr) << "--database is required\n";
        return 2;
    }

    QString loadError;
    QVector<QVector<pp::Candle>> templates;
    if (distinctDatasets) {
        const QStringList symbols = loadDistinctSymbols(
            databasePath, requestedBars, symbolCount, &loadError);
        templates.reserve(symbols.size());
        for (const QString &symbol : symbols) {
            QVector<pp::Candle> candles = loadTemplate(
                databasePath, symbol, requestedBars, &loadError);
            if (!loadError.isEmpty()) break;
            templates.append(std::move(candles));
        }
    } else {
        templates.append(loadTemplate(
            databasePath, templateSymbol, requestedBars, &loadError));
    }
    if (!loadError.isEmpty()) {
        QTextStream(stderr) << loadError << '\n';
        return 3;
    }
    if (templates.isEmpty() || templates.first().isEmpty()) return 3;
    const int barsPerSymbol = templates.first().size();

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    QElapsedTimer progressTimer;
    progressTimer.start();
    QVector<qint64> heartbeatGaps;
    QVector<qint64> taskDurations;
    QVector<qint64> roundDurations;
    QVector<double> roundChecksums;
    QVector<ProcessMemorySample> memorySamples;
    qint64 lastHeartbeat = 0;
    quint64 observedTaskDurations = 0;
    constexpr int kMaximumTaskDurationSamples = 200000;
    const auto recordTaskDuration = [&](qint64 duration) {
        ++observedTaskDurations;
        if (taskDurations.size() < kMaximumTaskDurationSamples) {
            taskDurations.append(duration);
        } else {
            const int row = static_cast<int>(
                observedTaskDurations % kMaximumTaskDurationSamples);
            taskDurations[row] = duration;
        }
    };
    QTimer heartbeat;
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, &application, [&] {
        const qint64 now = heartbeatClock.elapsed();
        if (lastHeartbeat > 0) heartbeatGaps.append(now - lastHeartbeat);
        lastHeartbeat = now;
    });
    heartbeat.start();

    if (memoryWarmupRounds == 0)
        memorySamples.append(currentProcessMemory(0));

    int currentRound = 0;
    int nextTask = 0;
    int active = 0;
    int peakActive = 0;
    int completed = 0;
    int failed = 0;
    int roundCompleted = 0;
    int roundFailed = 0;
    double roundChecksum = 0.0;
    double finalEquityChecksum = 0.0;
    bool deterministic = true;
    QElapsedTimer roundTimer;
    std::function<void()> fillSlots;
    std::function<void()> startRound;

    const auto roundFinished = [&] {
        roundDurations.append(roundTimer.elapsed());
        roundChecksums.append(roundChecksum);
        const int finishedRound = currentRound + 1;
        if (finishedRound == memoryWarmupRounds) {
            memorySamples.clear();
            memorySamples.append(currentProcessMemory(totalTimer.elapsed()));
        } else if (finishedRound > memoryWarmupRounds) {
            memorySamples.append(currentProcessMemory(totalTimer.elapsed()));
        }
        if (roundChecksums.size() > 1) {
            const double baseline = roundChecksums.first();
            const double tolerance = std::max(1.0, std::abs(baseline)) * 1.0e-10;
            deterministic = deterministic
                && std::abs(roundChecksums.last() - baseline) <= tolerance;
        }
        ++currentRound;
        if (progressTimer.elapsed() >= static_cast<qint64>(progressSeconds) * 1000) {
            const ProcessMemorySample sample = currentProcessMemory(totalTimer.elapsed());
            QTextStream(stderr)
                << QStringLiteral("progress rounds=%1 tasks=%2 elapsedSeconds=%3 privateMiB=%4")
                       .arg(currentRound)
                       .arg(completed + failed)
                       .arg(totalTimer.elapsed() / 1000)
                       .arg(sample.privateBytes / 1048576.0, 0, 'f', 1)
                << Qt::endl;
            progressTimer.restart();
        }
        const bool durationReached = durationSeconds > 0
            && totalTimer.elapsed() >= static_cast<qint64>(durationSeconds) * 1000;
        const bool roundLimitReached = durationSeconds == 0
            && currentRound >= requestedRounds;
        if (durationReached || roundLimitReached) {
            application.quit();
        } else {
            QTimer::singleShot(0, &application, startRound);
        }
    };

    fillSlots = [&] {
        while (active < concurrency && nextTask < symbolCount) {
            const int taskIndex = nextTask++;
            auto *engine = new pp::BacktestEngine(&application);
            QVector<pp::Candle> candles = templates.at(
                distinctDatasets ? taskIndex : 0);
            pp::Instrument instrument = candles.first().instrument;
            if (!distinctDatasets) {
                instrument.symbol = QStringLiteral("%1").arg(
                    taskIndex + 1, 6, 10, QLatin1Char('0'));
                instrument.name = QStringLiteral("容量标的 %1").arg(taskIndex + 1);
                for (pp::Candle &candle : candles) candle.instrument = instrument;
            }

            pp::BacktestConfig config;
            config.taskId = QStringLiteral("%1-%2")
                .arg(currentRound + 1).arg(taskIndex + 1);
            config.strategyId = QStringLiteral("sma_crossover");
            config.instrument = instrument;
            config.frequency = QStringLiteral("1m");
            config.periodSeconds = candles.first().periodSeconds;
            config.requestedBars = candles.size();
            config.initialCapital = 100000.0;
            config.commissionRate = 0.0003;
            config.slippageRate = 0.0002;
            config.lotSize = 100;

            const qint64 startedAt = totalTimer.elapsed();
            ++active;
            peakActive = std::max(peakActive, active);
            QObject::connect(engine, &pp::BacktestEngine::finished,
                             &application, [&, engine, startedAt](const pp::PerformanceSummary &) {
                recordTaskDuration(totalTimer.elapsed() - startedAt);
                const double finalEquity = engine->result().summary.finalEquity;
                roundChecksum += finalEquity;
                finalEquityChecksum += finalEquity;
                --active;
                ++completed;
                ++roundCompleted;
                engine->deleteLater();
                if (roundCompleted + roundFailed >= symbolCount) roundFinished();
                else QTimer::singleShot(0, &application, fillSlots);
            });
            QObject::connect(engine, &pp::BacktestEngine::errorOccurred,
                             &application, [&, engine, startedAt](const QString &) {
                recordTaskDuration(totalTimer.elapsed() - startedAt);
                --active;
                ++failed;
                ++roundFailed;
                engine->deleteLater();
                if (roundCompleted + roundFailed >= symbolCount) roundFinished();
                else QTimer::singleShot(0, &application, fillSlots);
            });
            engine->start(config, std::move(candles));
        }
    };

    startRound = [&] {
        nextTask = 0;
        roundCompleted = 0;
        roundFailed = 0;
        roundChecksum = 0.0;
        roundTimer.restart();
        fillSlots();
    };

    QTimer::singleShot(0, &application, startRound);
    application.exec();
    heartbeat.stop();
    const qint64 elapsed = totalTimer.elapsed();

    QJsonObject report;
    report.insert(QStringLiteral("symbols"), symbolCount);
    report.insert(QStringLiteral("rounds"), currentRound);
    report.insert(QStringLiteral("requestedRounds"), requestedRounds);
    report.insert(QStringLiteral("requestedDurationSeconds"), durationSeconds);
    report.insert(QStringLiteral("memoryWarmupRounds"), memoryWarmupRounds);
    report.insert(QStringLiteral("barsPerSymbol"), barsPerSymbol);
    report.insert(QStringLiteral("distinctDatasets"), distinctDatasets);
    report.insert(QStringLiteral("totalBars"),
                  static_cast<double>(completed + failed) * barsPerSymbol);
    report.insert(QStringLiteral("concurrency"), concurrency);
    report.insert(QStringLiteral("peakActiveEngines"), peakActive);
    report.insert(QStringLiteral("completed"), completed);
    report.insert(QStringLiteral("failed"), failed);
    report.insert(QStringLiteral("elapsedMs"), static_cast<double>(elapsed));
    report.insert(QStringLiteral("symbolsPerSecond"), elapsed > 0
        ? static_cast<double>(completed + failed) * 1000.0 / elapsed : 0.0);
    report.insert(QStringLiteral("barsPerSecond"), elapsed > 0
        ? static_cast<double>(completed + failed) * barsPerSymbol * 1000.0 / elapsed
        : 0.0);
    report.insert(QStringLiteral("taskP50Ms"), percentile(taskDurations, 0.50));
    report.insert(QStringLiteral("taskP95Ms"), percentile(taskDurations, 0.95));
    report.insert(QStringLiteral("heartbeatP95Ms"), percentile(heartbeatGaps, 0.95));
    report.insert(QStringLiteral("heartbeatMaxMs"),
                  heartbeatGaps.isEmpty() ? 0.0
                                          : static_cast<double>(*std::max_element(
                                                heartbeatGaps.cbegin(), heartbeatGaps.cend())));
    report.insert(QStringLiteral("roundP50Ms"), percentile(roundDurations, 0.50));
    report.insert(QStringLiteral("roundP95Ms"), percentile(roundDurations, 0.95));
    report.insert(QStringLiteral("roundMaxMs"),
                  roundDurations.isEmpty() ? 0.0
                                           : static_cast<double>(*std::max_element(
                                                 roundDurations.cbegin(), roundDurations.cend())));
    report.insert(QStringLiteral("deterministic"), deterministic);
    report.insert(QStringLiteral("roundChecksumFirst"),
                  roundChecksums.isEmpty() ? 0.0 : roundChecksums.first());
    report.insert(QStringLiteral("roundChecksumLast"),
                  roundChecksums.isEmpty() ? 0.0 : roundChecksums.last());
    if (!memorySamples.isEmpty()) {
        quint64 privateMaximum = 0;
        quint64 workingSetMaximum = 0;
        for (const ProcessMemorySample &sample : memorySamples) {
            privateMaximum = std::max(privateMaximum, sample.privateBytes);
            workingSetMaximum = std::max(workingSetMaximum, sample.workingSetBytes);
        }
        report.insert(QStringLiteral("privateBytesFirst"),
                      static_cast<double>(memorySamples.first().privateBytes));
        report.insert(QStringLiteral("privateBytesLast"),
                      static_cast<double>(memorySamples.last().privateBytes));
        report.insert(QStringLiteral("privateBytesMax"),
                      static_cast<double>(privateMaximum));
        report.insert(QStringLiteral("privateBytesSlopePerMinute"),
                      slopePerMinute(memorySamples, true));
        report.insert(QStringLiteral("workingSetBytesFirst"),
                      static_cast<double>(memorySamples.first().workingSetBytes));
        report.insert(QStringLiteral("workingSetBytesLast"),
                      static_cast<double>(memorySamples.last().workingSetBytes));
        report.insert(QStringLiteral("workingSetBytesMax"),
                      static_cast<double>(workingSetMaximum));
        report.insert(QStringLiteral("workingSetBytesSlopePerMinute"),
                      slopePerMinute(memorySamples, false));
    }
    report.insert(QStringLiteral("logicalProcessors"), QThread::idealThreadCount());
    report.insert(QStringLiteral("resultChecksum"), finalEquityChecksum);
    report.insert(QStringLiteral("taskDurationSamples"), taskDurations.size());
    report.insert(QStringLiteral("dataSource"), distinctDatasets
        ? QStringLiteral("raw_tick_cache_v1 distinct local datasets")
        : QStringLiteral("raw_tick_cache_v1 template"));
    const QByteArray compactPayload = QJsonDocument(report).toJson(QJsonDocument::Compact);
    const QString outputPath = parser.value(QStringLiteral("output"));
    if (!outputPath.isEmpty()) {
        QSaveFile output(outputPath);
        const QByteArray indentedPayload = QJsonDocument(report).toJson(
            QJsonDocument::Indented);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(indentedPayload) != indentedPayload.size()
            || !output.commit()) {
            QTextStream(stderr) << QStringLiteral("无法写入结果 JSON") << Qt::endl;
            return 5;
        }
    }
    QTextStream(stdout) << compactPayload << '\n';
    return failed == 0 && deterministic ? 0 : 4;
}
