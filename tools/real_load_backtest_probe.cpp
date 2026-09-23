#include "domain/BatchBacktestTypes.h"
#include "domain/LocalHistoryTypes.h"
#include "domain/RawTradeTypes.h"
#include "services/BatchBacktestService.h"
#include "services/RawTickCandleAggregator.h"
#include "services/SqliteLocalDataStore.h"
#include "services/TickDataCsvImporter.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>
#include <cmath>
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

pp::Instrument instrumentFor(const QString &symbol)
{
    pp::Instrument instrument;
    instrument.symbol = symbol;
    instrument.name = symbol;
    instrument.market = pp::Market::AShare;
    instrument.venue = symbol.startsWith(QLatin1Char('6'))
        ? QStringLiteral("XSHG") : QStringLiteral("XSHE");
    return instrument;
}

struct ProcessMemorySample {
    qint64 workingSetBytes = 0;
    qint64 privateBytes = 0;
};

ProcessMemorySample currentProcessMemory()
{
    ProcessMemorySample sample;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters {};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                             sizeof(counters))) {
        sample.workingSetBytes = static_cast<qint64>(counters.WorkingSetSize);
        sample.privateBytes = static_cast<qint64>(counters.PrivateUsage);
    }
#endif
    return sample;
}

struct TimedProcessMemorySample {
    qint64 elapsedMilliseconds = 0;
    ProcessMemorySample memory;
};

double percentile(QVector<qint64> values, double quantile)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = quantile * static_cast<double>(values.size() - 1);
    const qsizetype lower = static_cast<qsizetype>(std::floor(position));
    const qsizetype upper = static_cast<qsizetype>(std::ceil(position));
    if (lower == upper) return static_cast<double>(values.at(lower));
    const double fraction = position - static_cast<double>(lower);
    return static_cast<double>(values.at(lower)) * (1.0 - fraction)
        + static_cast<double>(values.at(upper)) * fraction;
}

double memorySlopePerMinute(
    const QVector<TimedProcessMemorySample> &samples, bool privateBytes)
{
    if (samples.size() < 2) return 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumX2 = 0.0;
    double sumXY = 0.0;
    for (const TimedProcessMemorySample &sample : samples) {
        const double x = static_cast<double>(sample.elapsedMilliseconds) / 60000.0;
        const double y = static_cast<double>(privateBytes
            ? sample.memory.privateBytes : sample.memory.workingSetBytes);
        sumX += x;
        sumY += y;
        sumX2 += x * x;
        sumXY += x * y;
    }
    const double count = static_cast<double>(samples.size());
    const double denominator = count * sumX2 - sumX * sumX;
    if (std::abs(denominator) < 1.0e-12) return 0.0;
    return (count * sumXY - sumX * sumY) / denominator;
}

QByteArray canonicalItemDigestPayload(const pp::BatchBacktestItemResult &item)
{
    QByteArray payload;
    QTextStream stream(&payload, QIODevice::WriteOnly);
    stream.setRealNumberNotation(QTextStream::ScientificNotation);
    stream.setRealNumberPrecision(17);
    stream << item.instrument.symbol << QLatin1Char('|')
           << (item.succeeded ? 1 : 0) << QLatin1Char('|')
           << item.actualBars << QLatin1Char('|')
           << item.dataVersion << QLatin1Char('|')
           << item.datasetVersion << QLatin1Char('|')
           << item.summary.finalEquity << QLatin1Char('|')
           << item.summary.totalReturnPercent << QLatin1Char('|')
           << item.summary.maxDrawdownPercent << QLatin1Char('|')
           << item.summary.totalTrades << QLatin1Char('|')
           << item.finalPosition.quantity << QLatin1Char('|')
           << item.error;
    stream.flush();
    return payload;
}

int runRawTickBacktestLongrun(
    QCoreApplication &application, const QString &databasePath,
    const QStringList &symbols, const QString &strategyId,
    const QDate &startDate, const QDate &endDate, int durationSeconds,
    int requestedRounds, int memoryWarmupRounds, int progressSeconds,
    const QString &outputPath)
{
    pp::BatchBacktestService service;
    service.setDatabasePath(databasePath);

    QVector<pp::LocalHistoryDataset> datasets;
    datasets.reserve(symbols.size());
    for (const QString &symbol : symbols) {
        pp::LocalHistoryDataset dataset;
        dataset.instrument = instrumentFor(symbol);
        dataset.periodSeconds = 0;
        dataset.source = QStringLiteral("raw_trade_ticks_v1");
        dataset.adjustment = QStringLiteral("none");
        datasets.append(dataset);
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    QElapsedTimer progressTimer;
    progressTimer.start();
    QVector<qint64> heartbeatGaps;
    QVector<qint64> roundDurations;
    QVector<qint64> itemDurations;
    QVector<TimedProcessMemorySample> memorySamples;
    qint64 lastHeartbeat = 0;
    constexpr qsizetype kMaximumHeartbeatSamples = 1'000'000;
    constexpr qsizetype kMaximumItemDurationSamples = 200'000;
    QTimer heartbeat;
    heartbeat.setInterval(16);
    QObject::connect(&heartbeat, &QTimer::timeout, &application, [&] {
        const qint64 now = heartbeatClock.elapsed();
        if (lastHeartbeat > 0) {
            const qint64 gap = now - lastHeartbeat;
            if (heartbeatGaps.size() < kMaximumHeartbeatSamples) {
                heartbeatGaps.append(gap);
            } else {
                heartbeatGaps[static_cast<qsizetype>(now)
                              % kMaximumHeartbeatSamples] = gap;
            }
        }
        lastHeartbeat = now;
    });
    heartbeat.start();

    if (memoryWarmupRounds == 0) {
        memorySamples.append({0, currentProcessMemory()});
    }

    int completedRounds = 0;
    qint64 completedItems = 0;
    qint64 succeededItems = 0;
    qint64 failedItems = 0;
    qint64 completedTrades = 0;
    qint64 tPlusOneViolations = 0;
    int openPositionCount = 0;
    int peakResidentDatasets = 0;
    qint64 peakResidentObservations = 0;
    bool deterministic = true;
    QByteArray firstRoundDigest;
    QByteArray lastRoundDigest;
    QStringList errors;
    constexpr int kMaximumReportedErrors = 100;

    while (true) {
        const quint64 requestId = static_cast<quint64>(completedRounds) + 1;
        pp::BatchBacktestRequest request;
        request.requestId = requestId;
        request.datasets = datasets;
        request.strategyId = strategyId;
        request.dataMode = pp::BacktestDataMode::RawTicks;
        request.startDate = startDate;
        request.endDate = endDate;
        request.periodSeconds = 0;
        request.barCount = 0;
        request.initialCapital = 1000000.0;
        request.commissionRate = 0.0003;
        request.slippageRate = 0.0002;
        request.lotSize = 100;
        request.maximumActiveEngines = 20;

        QMap<QString, QByteArray> roundItems;
        int roundCompletedItems = 0;
        int roundSucceededItems = 0;
        int roundFailedItems = 0;
        qint64 roundTrades = 0;
        qint64 roundTPlusOneViolations = 0;
        int roundOpenPositions = 0;
        bool roundCancelled = false;
        QString roundServiceError;
        QEventLoop loop;
        QElapsedTimer roundTimer;

        QObject::connect(&service, &pp::BatchBacktestService::itemReady,
                         &loop,
                         [&](quint64 emittedRequestId,
                             const pp::BatchBacktestItemResult &item) {
            if (emittedRequestId != requestId) return;
            ++roundCompletedItems;
            if (item.succeeded) {
                ++roundSucceededItems;
            } else {
                ++roundFailedItems;
                if (errors.size() < kMaximumReportedErrors) {
                    errors.append(QStringLiteral("round %1 %2: %3")
                        .arg(requestId).arg(item.instrument.symbol, item.error));
                }
            }
            if (item.finalPosition.quantity > 0.0) ++roundOpenPositions;
            if (itemDurations.size() < kMaximumItemDurationSamples) {
                itemDurations.append(item.elapsedMilliseconds);
            } else {
                itemDurations[static_cast<qsizetype>(completedItems
                    + roundCompletedItems) % kMaximumItemDurationSamples]
                    = item.elapsedMilliseconds;
            }
            roundItems.insert(item.instrument.symbol,
                              canonicalItemDigestPayload(item));
        });
        QObject::connect(&service, &pp::BatchBacktestService::itemCompleted,
                         &loop,
                         [&](quint64 emittedRequestId, const QString &,
                             const pp::BacktestResult &result) {
            if (emittedRequestId != requestId) return;
            static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
            roundTrades += result.trades.size();
            for (const pp::BacktestTrade &trade : result.trades) {
                if (trade.exitTime.toTimeZone(shanghai).date()
                    <= trade.entryTime.toTimeZone(shanghai).date()) {
                    ++roundTPlusOneViolations;
                }
            }
        });
        QObject::connect(&service,
                         &pp::BatchBacktestService::rawTickResidencyChanged,
                         &loop,
                         [&](quint64 emittedRequestId, int residentDatasets,
                             qint64 residentObservations) {
            if (emittedRequestId != requestId) return;
            peakResidentDatasets = std::max(
                peakResidentDatasets, residentDatasets);
            peakResidentObservations = std::max(
                peakResidentObservations, residentObservations);
        });
        QObject::connect(&service, &pp::BatchBacktestService::finished,
                         &loop,
                         [&](quint64 emittedRequestId, bool cancelled,
                             const QString &error) {
            if (emittedRequestId != requestId) return;
            roundCancelled = cancelled;
            roundServiceError = error;
            loop.quit();
        });

        roundTimer.start();
        if (!service.start(request)) {
            errors.append(QStringLiteral("round %1: 批量回测服务未能启动")
                              .arg(requestId));
            break;
        }
        loop.exec();
        roundDurations.append(roundTimer.elapsed());

        QByteArray digestPayload;
        for (auto it = roundItems.cbegin(); it != roundItems.cend(); ++it) {
            digestPayload.append(it.value());
            digestPayload.append('\n');
        }
        lastRoundDigest = QCryptographicHash::hash(
            digestPayload, QCryptographicHash::Sha256).toHex();
        if (completedRounds == 0) {
            firstRoundDigest = lastRoundDigest;
        } else {
            deterministic = deterministic && lastRoundDigest == firstRoundDigest;
        }

        ++completedRounds;
        completedItems += roundCompletedItems;
        succeededItems += roundSucceededItems;
        failedItems += roundFailedItems;
        completedTrades += roundTrades;
        tPlusOneViolations += roundTPlusOneViolations;
        openPositionCount += roundOpenPositions;

        if (completedRounds == memoryWarmupRounds) {
            memorySamples.clear();
            memorySamples.append({totalTimer.elapsed(), currentProcessMemory()});
        } else if (completedRounds > memoryWarmupRounds) {
            memorySamples.append({totalTimer.elapsed(), currentProcessMemory()});
        }

        if (progressTimer.elapsed()
            >= static_cast<qint64>(progressSeconds) * 1000) {
            const ProcessMemorySample memory = currentProcessMemory();
            QTextStream(stderr)
                << QStringLiteral("progress rounds=%1 items=%2 elapsedSeconds=%3 privateMiB=%4")
                       .arg(completedRounds)
                       .arg(completedItems)
                       .arg(totalTimer.elapsed() / 1000)
                       .arg(memory.privateBytes / 1048576.0, 0, 'f', 1)
                << Qt::endl;
            progressTimer.restart();
        }

        if (roundCancelled || !roundServiceError.isEmpty()
            || roundCompletedItems != symbols.size() || roundFailedItems > 0
            || roundTPlusOneViolations > 0 || !deterministic) {
            if (errors.size() < kMaximumReportedErrors
                && (roundCancelled || !roundServiceError.isEmpty())) {
                errors.append(QStringLiteral("round %1: cancelled=%2 error=%3")
                    .arg(requestId).arg(roundCancelled).arg(roundServiceError));
            }
            break;
        }

        const bool durationReached = durationSeconds > 0
            && totalTimer.elapsed()
                >= static_cast<qint64>(durationSeconds) * 1000;
        const bool roundLimitReached = durationSeconds == 0
            && completedRounds >= requestedRounds;
        if (durationReached || roundLimitReached) break;
    }

    heartbeat.stop();
    const qint64 elapsedMilliseconds = totalTimer.elapsed();
    quint64 privateMaximum = 0;
    quint64 workingSetMaximum = 0;
    for (const TimedProcessMemorySample &sample : memorySamples) {
        privateMaximum = std::max(
            privateMaximum, static_cast<quint64>(sample.memory.privateBytes));
        workingSetMaximum = std::max(
            workingSetMaximum,
            static_cast<quint64>(sample.memory.workingSetBytes));
    }
    const bool completedRequestedWork = durationSeconds > 0
        ? elapsedMilliseconds >= static_cast<qint64>(durationSeconds) * 1000
        : completedRounds >= requestedRounds;
    const bool passed = completedRequestedWork && failedItems == 0
        && errors.isEmpty() && deterministic && tPlusOneViolations == 0;

    QJsonObject report;
    report.insert(QStringLiteral("probe"),
                  QStringLiteral("raw_tick_backtest_longrun_v1"));
    report.insert(QStringLiteral("passed"), passed);
    report.insert(QStringLiteral("requestedDurationSeconds"), durationSeconds);
    report.insert(QStringLiteral("requestedRounds"), requestedRounds);
    report.insert(QStringLiteral("completedRounds"), completedRounds);
    report.insert(QStringLiteral("elapsedMilliseconds"),
                  static_cast<double>(elapsedMilliseconds));
    report.insert(QStringLiteral("strategyId"), strategyId);
    report.insert(QStringLiteral("startDate"), startDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("endDate"), endDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("symbolCount"), symbols.size());
    QJsonArray symbolArray;
    for (const QString &symbol : symbols) symbolArray.append(symbol);
    report.insert(QStringLiteral("symbols"), symbolArray);
    report.insert(QStringLiteral("completedItems"),
                  static_cast<double>(completedItems));
    report.insert(QStringLiteral("succeededItems"),
                  static_cast<double>(succeededItems));
    report.insert(QStringLiteral("failedItems"),
                  static_cast<double>(failedItems));
    report.insert(QStringLiteral("completedTrades"),
                  static_cast<double>(completedTrades));
    report.insert(QStringLiteral("tPlusOneViolations"),
                  static_cast<double>(tPlusOneViolations));
    report.insert(QStringLiteral("openPositionCount"), openPositionCount);
    report.insert(QStringLiteral("deterministic"), deterministic);
    report.insert(QStringLiteral("firstRoundDigest"),
                  QString::fromLatin1(firstRoundDigest));
    report.insert(QStringLiteral("lastRoundDigest"),
                  QString::fromLatin1(lastRoundDigest));
    report.insert(QStringLiteral("roundP50Milliseconds"),
                  percentile(roundDurations, 0.50));
    report.insert(QStringLiteral("roundP95Milliseconds"),
                  percentile(roundDurations, 0.95));
    report.insert(QStringLiteral("roundMaxMilliseconds"),
                  roundDurations.isEmpty()
                      ? 0.0 : static_cast<double>(*std::max_element(
                            roundDurations.cbegin(), roundDurations.cend())));
    report.insert(QStringLiteral("itemP50Milliseconds"),
                  percentile(itemDurations, 0.50));
    report.insert(QStringLiteral("itemP95Milliseconds"),
                  percentile(itemDurations, 0.95));
    report.insert(QStringLiteral("heartbeatP95Milliseconds"),
                  percentile(heartbeatGaps, 0.95));
    report.insert(QStringLiteral("heartbeatMaxMilliseconds"),
                  heartbeatGaps.isEmpty()
                      ? 0.0 : static_cast<double>(*std::max_element(
                            heartbeatGaps.cbegin(), heartbeatGaps.cend())));
    report.insert(QStringLiteral("peakResidentDatasets"), peakResidentDatasets);
    report.insert(QStringLiteral("peakResidentObservations"),
                  static_cast<double>(peakResidentObservations));
    report.insert(QStringLiteral("memoryWarmupRounds"), memoryWarmupRounds);
    report.insert(QStringLiteral("memorySampleCount"), memorySamples.size());
    if (!memorySamples.isEmpty()) {
        report.insert(QStringLiteral("privateBytesFirst"),
                      static_cast<double>(memorySamples.first().memory.privateBytes));
        report.insert(QStringLiteral("privateBytesLast"),
                      static_cast<double>(memorySamples.last().memory.privateBytes));
        report.insert(QStringLiteral("privateBytesMax"),
                      static_cast<double>(privateMaximum));
        report.insert(QStringLiteral("privateBytesSlopePerMinute"),
                      memorySlopePerMinute(memorySamples, true));
        report.insert(QStringLiteral("workingSetBytesFirst"),
                      static_cast<double>(
                          memorySamples.first().memory.workingSetBytes));
        report.insert(QStringLiteral("workingSetBytesLast"),
                      static_cast<double>(
                          memorySamples.last().memory.workingSetBytes));
        report.insert(QStringLiteral("workingSetBytesMax"),
                      static_cast<double>(workingSetMaximum));
        report.insert(QStringLiteral("workingSetBytesSlopePerMinute"),
                      memorySlopePerMinute(memorySamples, false));
    }
    QJsonArray errorArray;
    for (const QString &error : errors) errorArray.append(error);
    report.insert(QStringLiteral("errors"), errorArray);

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(payload) != payload.size() || !output.commit()) {
        QTextStream(stderr) << QStringLiteral("无法原子写入长稳结果 JSON")
                            << Qt::endl;
        return 7;
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return passed ? 0 : 6;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("s"), QStringLiteral("staging")},
         QStringLiteral("暂存根目录（YYYYMMDD/代码.csv）"), QStringLiteral("path")},
        {{QStringLiteral("m"), QStringLiteral("symbols")},
         QStringLiteral("逗号分隔代码"), QStringLiteral("codes")},
        {QStringLiteral("all-raw-symbols"),
         QStringLiteral("从 --existing-db 读取全部原始 Tick 标的，避免全市场代码超过命令行长度")},
        {QStringLiteral("maximum-symbols"),
         QStringLiteral("--all-raw-symbols 最多读取的标的数"),
         QStringLiteral("count"), QStringLiteral("10000")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("K 线模式回测根数；原始 Tick 模式忽略"),
         QStringLiteral("count"), QStringLiteral("0")},
        {{QStringLiteral("d"), QStringLiteral("data-mode")},
         QStringLiteral("回测数据模式：raw_ticks（默认）或 candles"),
         QStringLiteral("mode"), QStringLiteral("raw_ticks")},
        {QStringLiteral("strategy"),
         QStringLiteral("内置策略 ID；留空时原始 Tick 使用 tick_momentum，K 线使用 sma_crossover"),
         QStringLiteral("id")},
        {QStringLiteral("start-date"), QStringLiteral("回测开始日期 yyyy-MM-dd"),
         QStringLiteral("date")},
        {QStringLiteral("end-date"), QStringLiteral("回测结束日期 yyyy-MM-dd"),
         QStringLiteral("date")},
        {{QStringLiteral("p"), QStringLiteral("period")},
         QStringLiteral("由逐笔聚合的 K 线周期（秒）"), QStringLiteral("seconds"),
         QStringLiteral("60")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("结果 JSON 路径"), QStringLiteral("path")},
        {{QStringLiteral("k"), QStringLiteral("keep-db")},
         QStringLiteral("可选：保留临时数据库路径"), QStringLiteral("path")},
        {{QStringLiteral("e"), QStringLiteral("existing-db")},
         QStringLiteral("直接使用已有数据库（跳过装载，只跑回测与归档）"), QStringLiteral("path")},
        {QStringLiteral("no-archive"),
         QStringLiteral("隔离研究运行不保存回测归档；不改变数据质量准入")},
        {QStringLiteral("measure-catalog"),
         QStringLiteral("额外测量本地回测目录身份刷新")},
        {QStringLiteral("holdout-days"),
         QStringLiteral("原始 Tick 时序样本外验证交易日数；0 关闭，最少 2"),
         QStringLiteral("days"), QStringLiteral("0")},
        {QStringLiteral("longrun"),
         QStringLiteral("重复执行严格本地原始 Tick 批量回测长稳；不导入、不归档")},
        {QStringLiteral("longrun-duration-seconds"),
         QStringLiteral("长稳持续秒数；大于 0 时覆盖轮数，最大 86400"),
         QStringLiteral("seconds"), QStringLiteral("0")},
        {QStringLiteral("longrun-rounds"),
         QStringLiteral("长稳完整轮数；持续秒数为 0 时生效"),
         QStringLiteral("count"), QStringLiteral("1")},
        {QStringLiteral("longrun-memory-warmup-rounds"),
         QStringLiteral("内存斜率排除的预热轮数"),
         QStringLiteral("count"), QStringLiteral("3")},
        {QStringLiteral("longrun-progress-seconds"),
         QStringLiteral("stderr 进度输出间隔"),
         QStringLiteral("seconds"), QStringLiteral("30")},
    });
    parser.process(application);

    const QString staging = parser.value(QStringLiteral("staging"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    const QString keepDb = parser.value(QStringLiteral("keep-db"));
    const QString existingDb = parser.value(QStringLiteral("existing-db"));
    const bool measureCatalog = parser.isSet(QStringLiteral("measure-catalog"));
    const bool noArchive = parser.isSet(QStringLiteral("no-archive"));
    const bool longrun = parser.isSet(QStringLiteral("longrun"));
    const bool allRawSymbols = parser.isSet(QStringLiteral("all-raw-symbols"));
    bool holdoutDaysOk = false;
    const int holdoutTradingDays = parser.value(
        QStringLiteral("holdout-days")).toInt(&holdoutDaysOk);
    const QString dataModeText = parser.value(QStringLiteral("data-mode"))
                                     .trimmed().toLower();
    const bool rawMode = dataModeText == QStringLiteral("raw_ticks");
    QString strategyId = parser.value(QStringLiteral("strategy")).trimmed();
    if (strategyId.isEmpty()) {
        strategyId = rawMode ? QStringLiteral("tick_momentum")
                             : QStringLiteral("sma_crossover");
    }
    QDate requestedStartDate = QDate::fromString(
        parser.value(QStringLiteral("start-date")), Qt::ISODate);
    QDate requestedEndDate = QDate::fromString(
        parser.value(QStringLiteral("end-date")), Qt::ISODate);
    QStringList symbols;
    for (const QString &value : parser.value(QStringLiteral("symbols")).split(',')) {
        const QString symbol = value.trimmed();
        if (symbol.size() == 6 && !symbols.contains(symbol)) symbols.append(symbol);
    }
    bool maximumSymbolsOk = false;
    const int maximumSymbols = parser.value(
        QStringLiteral("maximum-symbols")).toInt(&maximumSymbolsOk);
    if (allRawSymbols && rawMode && !existingDb.isEmpty()
        && QFileInfo::exists(existingDb) && maximumSymbolsOk
        && maximumSymbols >= 1 && maximumSymbols <= 10000) {
        pp::SqliteLocalDataStore discoveryStore;
        if (discoveryStore.initialize(existingDb)) {
            symbols.clear();
            const QVector<pp::Instrument> instruments =
                discoveryStore.rawBacktestInstruments(maximumSymbols);
            symbols.reserve(instruments.size());
            for (const pp::Instrument &instrument : instruments) {
                const QString symbol = instrument.symbol.trimmed();
                if (symbol.size() == 6 && !symbols.contains(symbol))
                    symbols.append(symbol);
            }
            std::sort(symbols.begin(), symbols.end());
        }
    }
    bool barsOk = false;
    const int requestedBars = parser.value(QStringLiteral("bars")).toInt(&barsOk);
    int bars = requestedBars;
    bool periodOk = false;
    const int periodSeconds = parser.value(QStringLiteral("period")).toInt(&periodOk);
    bool longrunDurationOk = false;
    const int longrunDurationSeconds = parser.value(
        QStringLiteral("longrun-duration-seconds")).toInt(&longrunDurationOk);
    bool longrunRoundsOk = false;
    const int longrunRounds = parser.value(
        QStringLiteral("longrun-rounds")).toInt(&longrunRoundsOk);
    bool longrunWarmupOk = false;
    const int longrunMemoryWarmupRounds = parser.value(
        QStringLiteral("longrun-memory-warmup-rounds")).toInt(&longrunWarmupOk);
    bool longrunProgressOk = false;
    const int longrunProgressSeconds = parser.value(
        QStringLiteral("longrun-progress-seconds")).toInt(&longrunProgressOk);
    if (outputPath.isEmpty() || symbols.isEmpty() || !barsOk || bars < 0
        || (!rawMode && dataModeText != QStringLiteral("candles"))
        || !maximumSymbolsOk || maximumSymbols < 1 || maximumSymbols > 10000
        || (allRawSymbols && (!rawMode || existingDb.isEmpty()))
        || (requestedStartDate.isValid() != requestedEndDate.isValid())
        || (requestedStartDate.isValid() && requestedStartDate > requestedEndDate)
        || !periodOk || periodSeconds < 1 || periodSeconds > 86400
        || (existingDb.isEmpty()
            && (staging.isEmpty() || !QDir(staging).exists()))
        || (!existingDb.isEmpty() && !QFileInfo::exists(existingDb))
        || !holdoutDaysOk || holdoutTradingDays < 0
        || (holdoutTradingDays > 0 && (!rawMode || holdoutTradingDays < 2))
        || !longrunDurationOk || longrunDurationSeconds < 0
        || longrunDurationSeconds > 86400
        || !longrunRoundsOk || longrunRounds < 1 || longrunRounds > 1000000
        || !longrunWarmupOk || longrunMemoryWarmupRounds < 0
        || longrunMemoryWarmupRounds > 100000
        || !longrunProgressOk || longrunProgressSeconds < 1
        || longrunProgressSeconds > 3600
        || (longrun && (!rawMode || existingDb.isEmpty()
            || !requestedStartDate.isValid() || !requestedEndDate.isValid()
            || holdoutTradingDays != 0))) {
        QTextStream(stderr) << QStringLiteral(
            "参数无效：需要 --symbols/--bars/--period/--output 以及 --staging 或 --existing-db")
                             << Qt::endl;
        return 1;
    }

    std::unique_ptr<QTemporaryDir> temporaryRoot;
    QString databasePath = keepDb;
    if (!existingDb.isEmpty()) databasePath = existingDb;
    if (databasePath.isEmpty()) {
        temporaryRoot = std::make_unique<QTemporaryDir>();
        if (!temporaryRoot->isValid()) return 2;
        databasePath = QDir(temporaryRoot->path()).filePath(
            QStringLiteral("real-load.db"));
    }

    if (longrun) {
        return runRawTickBacktestLongrun(
            application, databasePath, symbols, strategyId,
            requestedStartDate, requestedEndDate, longrunDurationSeconds,
            longrunRounds, longrunMemoryWarmupRounds,
            longrunProgressSeconds, outputPath);
    }

    QJsonArray fileResults;
    qint64 ticksWritten = 0;
    qint64 barsWritten = 0;
    int failedFiles = 0;
    qint64 schemaInitMilliseconds = 0;
    qint64 loadElapsedMilliseconds = 0;
    const bool skipLoad = !existingDb.isEmpty();
    if (!skipLoad) {
        QElapsedTimer schemaTimer;
        schemaTimer.start();
        pp::SqliteLocalDataStore store;
        if (!store.initialize(databasePath)) {
            QTextStream(stderr) << QStringLiteral("无法初始化临时 SQLite") << Qt::endl;
            return 3;
        }
        schemaInitMilliseconds = schemaTimer.elapsed();

        QElapsedTimer loadTimer;
        loadTimer.start();
        const QDir stagingRoot(staging);
        const QStringList dateDirs = stagingRoot.entryList(
            {QStringLiteral("????????")}, QDir::Dirs, QDir::Name);
        for (const QString &symbol : symbols) {
            for (const QString &dateDir : dateDirs) {
                const QDate tradingDate = QDate::fromString(dateDir, QStringLiteral("yyyyMMdd"));
                if (!tradingDate.isValid()) continue;
                const QString filePath = stagingRoot.filePath(
                    dateDir + QLatin1Char('/') + symbol + QStringLiteral(".csv"));
                if (!QFileInfo::exists(filePath)) continue;

                pp::TickDataImportRequest request;
                request.rootDirectory = stagingRoot.absolutePath();
                request.instrument = instrumentFor(symbol);
                request.tradingDate = tradingDate;
                request.format = pp::TickDataCsvFormat::Tick;
                request.periodSeconds = 60;

                qint64 parseMilliseconds = 0;
                qint64 rawDatabaseMilliseconds = 0;
                qint64 candleDatabaseMilliseconds = 0;
                QElapsedTimer timer;
                timer.start();
                const bool transactionReady = store.beginRawTradeTickImport(
                    request.instrument, tradingDate, QStringLiteral("tickdata_csv_tick"));
                const auto sink =
                    [&store, transactionReady, &rawDatabaseMilliseconds](
                        const QVector<pp::RawTradeTick> &ticks) {
                        if (!transactionReady) return false;
                        QElapsedTimer databaseTimer;
                        databaseTimer.start();
                        const bool succeeded = store.appendRawTradeTicks(ticks);
                        rawDatabaseMilliseconds += databaseTimer.elapsed();
                        return succeeded;
                    };
                pp::TickDataImportResult imported =
                    pp::TickDataCsvImporter::parse(request, {}, sink);
                parseMilliseconds = timer.elapsed();
                bool committed = false;
                if (transactionReady && imported.succeeded) {
                    timer.restart();
                    committed = store.commitRawTradeTickImport(imported);
                    rawDatabaseMilliseconds += timer.elapsed();
                } else if (transactionReady) {
                    store.rollbackRawTradeTickImport();
                }
                if (!committed) {
                    ++failedFiles;
                    continue;
                }
                const pp::RawTradeFingerprint fingerprint =
                    store.rawTradeFingerprint(request.instrument, tradingDate);
                timer.restart();
                const bool candlesStored = store.replaceRawTickCandleCache(
                    request.instrument, tradingDate, 60, fingerprint,
                    imported.candles);
                candleDatabaseMilliseconds = timer.elapsed();
                if (!candlesStored) {
                    ++failedFiles;
                    continue;
                }
                ticksWritten += imported.validRecords;
                barsWritten += imported.candles.size();
                QJsonObject fileResult;
                fileResult.insert(QStringLiteral("symbol"), symbol);
                fileResult.insert(QStringLiteral("tradingDate"),
                                  tradingDate.toString(Qt::ISODate));
                fileResult.insert(QStringLiteral("linesRead"),
                                  static_cast<double>(imported.linesRead));
                fileResult.insert(QStringLiteral("validRecords"),
                                  static_cast<double>(imported.validRecords));
                fileResult.insert(QStringLiteral("skippedRecords"),
                                  static_cast<double>(imported.skippedRecords));
                fileResult.insert(QStringLiteral("bars"),
                                  static_cast<double>(imported.candles.size()));
                fileResult.insert(QStringLiteral("parseMilliseconds"),
                                  static_cast<double>(parseMilliseconds));
                fileResult.insert(QStringLiteral("rawDatabaseMilliseconds"),
                                  static_cast<double>(rawDatabaseMilliseconds));
                fileResult.insert(QStringLiteral("candleDatabaseMilliseconds"),
                                  static_cast<double>(candleDatabaseMilliseconds));
                fileResults.append(fileResult);
            }
        }
        loadElapsedMilliseconds = loadTimer.elapsed();
    }

    qint64 catalogElapsedMilliseconds = -1;
    int catalogIdentityCount = -1;
    if (measureCatalog) {
        pp::SqliteLocalDataStore store;
        if (store.initialize(databasePath)) {
            QElapsedTimer catalogTimer;
            catalogTimer.start();
            catalogIdentityCount = store.loadBacktestDatasetIdentities().size();
            catalogElapsedMilliseconds = catalogTimer.elapsed();
        }
    }

    QJsonObject identityResult;
    QJsonArray prepareResults;
    qint64 prepareElapsedMilliseconds = 0;
    int commonAvailableBars = std::numeric_limits<int>::max();
    QDate availableStartDate;
    QDate availableEndDate;
    {
        pp::SqliteLocalDataStore store;
        if (store.initialize(databasePath)) {
            QElapsedTimer prepareTimer;
            prepareTimer.start();
            for (const QString &symbol : symbols) {
                const pp::Instrument instrument = instrumentFor(symbol);
                QElapsedTimer symbolTimer;
                symbolTimer.start();
                QElapsedTimer stageTimer;
                stageTimer.start();
                if (rawMode) {
                    const auto rawIdentity =
                        store.loadRawBacktestDatasetIdentity(instrument);
                    QJsonObject item;
                    item.insert(QStringLiteral("symbol"), symbol);
                    item.insert(QStringLiteral("identityMilliseconds"),
                                static_cast<double>(stageTimer.elapsed()));
                    item.insert(QStringLiteral("rawTickCount"),
                                static_cast<double>(rawIdentity
                                    ? rawIdentity->rawTickTotal() : 0));
                    item.insert(QStringLiteral("derivedBarCount"), 0);
                    item.insert(QStringLiteral("rebuiltDays"), 0);
                    item.insert(QStringLiteral("elapsedMilliseconds"),
                                static_cast<double>(symbolTimer.elapsed()));
                    item.insert(QStringLiteral("succeeded"),
                                rawIdentity.has_value() && !rawIdentity->isEmpty());
                    prepareResults.append(item);
                    if (!rawIdentity || rawIdentity->isEmpty()) {
                        commonAvailableBars = 0;
                        continue;
                    }
                    if (!availableStartDate.isValid()
                        || rawIdentity->firstTradingDate < availableStartDate) {
                        availableStartDate = rawIdentity->firstTradingDate;
                    }
                    if (!availableEndDate.isValid()
                        || rawIdentity->lastTradingDate > availableEndDate) {
                        availableEndDate = rawIdentity->lastTradingDate;
                    }
                    continue;
                }
                auto identity = store.loadBacktestDatasetIdentity(
                    instrument, periodSeconds);
                const qint64 identityMilliseconds = stageTimer.elapsed();
                qint64 freshnessMilliseconds = 0;
                qint64 aggregateMilliseconds = 0;
                qint64 fingerprintMilliseconds = 0;
                qint64 cacheWriteMilliseconds = 0;
                int rebuiltDays = 0;
                if (!identity || identity->isEmpty()) {
                    commonAvailableBars = 0;
                    QJsonObject item;
                    item.insert(QStringLiteral("symbol"), symbol);
                    item.insert(QStringLiteral("identityMilliseconds"),
                                static_cast<double>(identityMilliseconds));
                    item.insert(QStringLiteral("elapsedMilliseconds"),
                                static_cast<double>(symbolTimer.elapsed()));
                    item.insert(QStringLiteral("succeeded"), false);
                    prepareResults.append(item);
                    continue;
                }
                for (const pp::RawTickDayFingerprint &day : identity->days) {
                    if (day.cacheFresh) continue;
                    pp::RawTickCandleAggregator aggregator(
                        instrument, periodSeconds);
                    stageTimer.restart();
                    store.forEachRawTradeTick(
                        instrument, day.tradingDate,
                        [&aggregator](const pp::RawTradeTick &tick) {
                            aggregator.append(tick);
                            return true;
                        });
                    aggregateMilliseconds += stageTimer.elapsed();
                    pp::RawTradeFingerprint fingerprint;
                    fingerprint.count = day.rawCount;
                    fingerprint.maximumId = day.rawMaximumId;
                    fingerprint.contentSha256 = day.rawContentSha256;
                    if (fingerprint.count > 0) {
                        stageTimer.restart();
                        store.replaceRawTickCandleCache(
                            instrument, day.tradingDate, periodSeconds, fingerprint,
                            aggregator.finish());
                        cacheWriteMilliseconds += stageTimer.elapsed();
                        ++rebuiltDays;
                    }
                }
                const auto refreshedIdentity = store.loadBacktestDatasetIdentity(
                    instrument, periodSeconds);
                const qint64 derivedBarCount = refreshedIdentity
                    ? refreshedIdentity->derivedBarTotal() : 0;
                if (refreshedIdentity && !refreshedIdentity->isEmpty()) {
                    if (!availableStartDate.isValid()
                        || refreshedIdentity->firstTradingDate < availableStartDate) {
                        availableStartDate = refreshedIdentity->firstTradingDate;
                    }
                    if (!availableEndDate.isValid()
                        || refreshedIdentity->lastTradingDate > availableEndDate) {
                        availableEndDate = refreshedIdentity->lastTradingDate;
                    }
                }
                commonAvailableBars = std::min(
                    commonAvailableBars,
                    static_cast<int>(std::min<qint64>(
                        derivedBarCount, std::numeric_limits<int>::max())));
                QJsonObject item;
                item.insert(QStringLiteral("symbol"), symbol);
                item.insert(QStringLiteral("rawTickCount"),
                            static_cast<double>(refreshedIdentity
                                ? refreshedIdentity->rawTickTotal() : 0));
                item.insert(QStringLiteral("derivedBarCount"),
                            static_cast<double>(derivedBarCount));
                item.insert(QStringLiteral("identityMilliseconds"),
                            static_cast<double>(identityMilliseconds));
                item.insert(QStringLiteral("freshnessMilliseconds"),
                            static_cast<double>(freshnessMilliseconds));
                item.insert(QStringLiteral("aggregateMilliseconds"),
                            static_cast<double>(aggregateMilliseconds));
                item.insert(QStringLiteral("fingerprintMilliseconds"),
                            static_cast<double>(fingerprintMilliseconds));
                item.insert(QStringLiteral("cacheWriteMilliseconds"),
                            static_cast<double>(cacheWriteMilliseconds));
                item.insert(QStringLiteral("rebuiltDays"), rebuiltDays);
                item.insert(QStringLiteral("elapsedMilliseconds"),
                            static_cast<double>(symbolTimer.elapsed()));
                item.insert(QStringLiteral("succeeded"), true);
                prepareResults.append(item);
            }
            prepareElapsedMilliseconds = prepareTimer.elapsed();
            const auto identity = rawMode
                ? store.loadRawBacktestDatasetIdentity(instrumentFor(symbols.first()))
                : store.loadBacktestDatasetIdentity(
                      instrumentFor(symbols.first()), periodSeconds);
            if (identity) {
                identityResult.insert(QStringLiteral("datasetVersion"),
                                      identity->datasetVersion);
                identityResult.insert(QStringLiteral("dayCount"),
                                      identity->days.size());
                identityResult.insert(QStringLiteral("rawTickCount"),
                                      identity->rawTickTotal());
                identityResult.insert(QStringLiteral("derivedBarCount"),
                                      identity->derivedBarTotal());
                identityResult.insert(QStringLiteral("staleDayCount"),
                                      identity->staleDayCount);
            }
        }
    }
    if (!requestedStartDate.isValid()) requestedStartDate = availableStartDate;
    if (!requestedEndDate.isValid()) requestedEndDate = availableEndDate;
    if (!requestedStartDate.isValid() || !requestedEndDate.isValid()) {
        QTextStream(stderr) << QStringLiteral("没有可用的日期范围") << Qt::endl;
        return 5;
    }
    if (!rawMode && requestedBars == 0) bars = commonAvailableBars;
    if (!rawMode && (bars < 2 || bars == std::numeric_limits<int>::max())) {
        QTextStream(stderr) << QStringLiteral("共同可用 K 线不足，无法回测")
                            << Qt::endl;
        return 5;
    }

    QJsonObject backtestResult;
    qint64 backtestElapsedMilliseconds = 0;
    const ProcessMemorySample memoryBeforeBacktest = currentProcessMemory();
    ProcessMemorySample peakBacktestMemory = memoryBeforeBacktest;
    ProcessMemorySample memoryAfterBacktest;
    QString archiveResultId;
    qint64 archiveElapsedMilliseconds = -1;
    {
        pp::BatchBacktestService service;
        service.setDatabasePath(databasePath);
        QVector<pp::LocalHistoryDataset> datasets;
        datasets.reserve(symbols.size());
        for (const QString &symbol : symbols) {
            pp::LocalHistoryDataset dataset;
            dataset.instrument = instrumentFor(symbol);
            dataset.periodSeconds = rawMode ? 0 : periodSeconds;
            dataset.source = rawMode ? QStringLiteral("raw_trade_ticks_v1")
                                     : QStringLiteral("raw_tick_cache_v1");
            dataset.adjustment = QStringLiteral("none");
            datasets.append(dataset);
        }
        pp::BatchBacktestRequest request;
        request.requestId = 1;
        request.datasets = std::move(datasets);
        request.strategyId = strategyId;
        request.strategyParameters = {};
        request.dataMode = rawMode ? pp::BacktestDataMode::RawTicks
                                   : pp::BacktestDataMode::Candles;
        request.startDate = requestedStartDate;
        request.endDate = requestedEndDate;
        request.periodSeconds = rawMode ? 0 : periodSeconds;
        request.barCount = rawMode ? 0 : bars;
        request.initialCapital = 1000000.0;
        request.commissionRate = 0.0003;
        request.slippageRate = 0.0002;
        request.lotSize = 100;
        request.maximumActiveEngines = 20;
        request.holdoutTradingDays = holdoutTradingDays;

        int completed = 0;
        int succeeded = 0;
        QStringList errors;
        QJsonArray itemResults;
        QJsonArray boardResults;
        QString dataVersion;
        QString datasetVersion;
        int completedTradeCount = 0;
        int tPlusOneViolationCount = 0;
        int openPositionCount = 0;
        int peakResidentDatasets = 0;
        qint64 peakResidentObservations = 0;
        pp::BacktestResult retained;
        QEventLoop loop;
        QObject::connect(&service, &pp::BatchBacktestService::itemReady,
                         &loop, [&](quint64, const pp::BatchBacktestItemResult &item) {
            const ProcessMemorySample memory = currentProcessMemory();
            peakBacktestMemory.workingSetBytes = std::max(
                peakBacktestMemory.workingSetBytes, memory.workingSetBytes);
            peakBacktestMemory.privateBytes = std::max(
                peakBacktestMemory.privateBytes, memory.privateBytes);
            itemResults.append(QJsonObject::fromVariantMap(item.toVariantMap()));
            ++completed;
            if (item.succeeded) {
                ++succeeded;
                dataVersion = item.dataVersion;
                datasetVersion = item.datasetVersion;
            } else {
                errors.append(item.instrument.symbol + QStringLiteral(":") + item.error);
            }
        });
        QObject::connect(&service, &pp::BatchBacktestService::itemCompleted,
                         &loop, [&](quint64, const QString &,
                                    const pp::BacktestResult &result) {
            static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
            if (strategyId == QStringLiteral("tick_second_board")
                || strategyId == QStringLiteral("tick_third_board")) {
                int buySignals = 0;
                int capturedBoards = 0;
                int missedBoards = 0;
                int forcedExits = 0;
                for (const pp::TradeSignal &signal : result.chartSignals) {
                    if (signal.side == pp::SignalSide::Buy) ++buySignals;
                }
                for (const pp::BacktestTrade &trade : result.trades) {
                    if (trade.exitReason.contains(QStringLiteral("目标日封板命中")))
                        ++capturedBoards;
                    else if (trade.exitReason.contains(QStringLiteral("目标日未封板")))
                        ++missedBoards;
                    else ++forcedExits;
                }
                QJsonObject boardItem;
                boardItem.insert(QStringLiteral("symbol"), result.config.instrument.symbol);
                boardItem.insert(QStringLiteral("buySignals"), buySignals);
                boardItem.insert(QStringLiteral("completedTrades"), result.trades.size());
                boardItem.insert(QStringLiteral("capturedClosedBoards"), capturedBoards);
                boardItem.insert(QStringLiteral("entryDayOutcomeKnownSignals"),
                                 result.strategyEvaluationFacts.value(
                                     QStringLiteral("entryDayOutcomeKnownSignals")).toInt());
                boardItem.insert(QStringLiteral("entryDayClosedBoardSignals"),
                                 result.strategyEvaluationFacts.value(
                                     QStringLiteral("entryDayClosedBoardSignals")).toInt());
                boardItem.insert(QStringLiteral("missedClosedBoards"), missedBoards);
                boardItem.insert(QStringLiteral("forcedExits"), forcedExits);
                boardItem.insert(QStringLiteral("openPosition"),
                                 result.finalPosition.quantity > 0.0);
                boardResults.append(boardItem);
            }
            for (const pp::BacktestTrade &trade : result.trades) {
                ++completedTradeCount;
                if (trade.exitTime.toTimeZone(shanghai).date()
                    <= trade.entryTime.toTimeZone(shanghai).date()) {
                    ++tPlusOneViolationCount;
                }
            }
            if (result.finalPosition.quantity > 0.0) ++openPositionCount;
            retained = result;
        });
        QObject::connect(&service, &pp::BatchBacktestService::rawTickResidencyChanged,
                         &loop, [&](quint64, int residentDatasets,
                                    qint64 residentObservations) {
            peakResidentDatasets = std::max(
                peakResidentDatasets, residentDatasets);
            peakResidentObservations = std::max(
                peakResidentObservations, residentObservations);
        });
        QObject::connect(&service, &pp::BatchBacktestService::finished,
                         &loop, [&]() { loop.quit(); });
        QElapsedTimer backtestTimer;
        if (!service.start(request)) {
            QTextStream(stderr) << QStringLiteral("批量回测服务未能启动") << Qt::endl;
            return 4;
        }
        backtestTimer.start();
        loop.exec();
        backtestElapsedMilliseconds = backtestTimer.elapsed();
        memoryAfterBacktest = currentProcessMemory();
        peakBacktestMemory.workingSetBytes = std::max(
            peakBacktestMemory.workingSetBytes, memoryAfterBacktest.workingSetBytes);
        peakBacktestMemory.privateBytes = std::max(
            peakBacktestMemory.privateBytes, memoryAfterBacktest.privateBytes);

        backtestResult.insert(QStringLiteral("requested"),
                              static_cast<double>(request.requestId));
        backtestResult.insert(QStringLiteral("completed"), completed);
        backtestResult.insert(QStringLiteral("succeeded"), succeeded);
        backtestResult.insert(QStringLiteral("failed"), errors.size());
        backtestResult.insert(QStringLiteral("dataVersion"), dataVersion);
        backtestResult.insert(QStringLiteral("datasetVersion"), datasetVersion);
        QJsonArray errorArray;
        for (const QString &error : errors) errorArray.append(error);
        backtestResult.insert(QStringLiteral("errors"), errorArray);
        backtestResult.insert(QStringLiteral("items"), itemResults);
        if (!boardResults.isEmpty())
            backtestResult.insert(QStringLiteral("boardResults"), boardResults);
        backtestResult.insert(QStringLiteral("completedTradeCount"), completedTradeCount);
        backtestResult.insert(QStringLiteral("tPlusOneViolationCount"),
                              tPlusOneViolationCount);
        backtestResult.insert(QStringLiteral("openPositionCount"), openPositionCount);
        backtestResult.insert(QStringLiteral("peakResidentDatasets"),
                              peakResidentDatasets);
        backtestResult.insert(QStringLiteral("peakResidentObservations"),
                              static_cast<double>(peakResidentObservations));

        if (!noArchive && !retained.config.datasetIdentity.isEmpty()) {
            pp::SqliteLocalDataStore store;
            if (store.initialize(databasePath)) {
                pp::BacktestArchiveRecord archive;
                archive.resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                archive.completedAt = QDateTime::currentDateTimeUtc();
                archive.result = retained;
                QElapsedTimer archiveTimer;
                archiveTimer.start();
                if (store.saveBacktestArchive(archive)) {
                    archiveResultId = archive.resultId;
                    archiveElapsedMilliseconds = archiveTimer.elapsed();
                }
            }
        }
    }

    const qint64 databaseBytes = QFileInfo(databasePath).size()
        + QFileInfo(databasePath + QStringLiteral("-wal")).size()
        + QFileInfo(databasePath + QStringLiteral("-shm")).size();
    QJsonObject report;
    report.insert(QStringLiteral("date"),
                  QDate::currentDate().toString(Qt::ISODate));
    report.insert(QStringLiteral("stagingRoot"), staging);
    QJsonArray symbolArray;
    for (const QString &symbol : symbols) symbolArray.append(symbol);
    report.insert(QStringLiteral("symbols"), symbolArray);
    report.insert(QStringLiteral("requestedBars"), requestedBars);
    report.insert(QStringLiteral("bars"), bars);
    report.insert(QStringLiteral("dataMode"), dataModeText);
    report.insert(QStringLiteral("strategyId"), strategyId);
    report.insert(QStringLiteral("archiveSkippedByRequest"), noArchive);
    report.insert(QStringLiteral("startDate"),
                  requestedStartDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("endDate"),
                  requestedEndDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("commonAvailableBars"), commonAvailableBars);
    report.insert(QStringLiteral("periodSeconds"), periodSeconds);
    report.insert(QStringLiteral("holdoutTradingDays"), holdoutTradingDays);
    report.insert(QStringLiteral("schemaInitMilliseconds"),
                  static_cast<double>(schemaInitMilliseconds));
    report.insert(QStringLiteral("loadElapsedMilliseconds"),
                  static_cast<double>(loadElapsedMilliseconds));
    report.insert(QStringLiteral("prepareElapsedMilliseconds"),
                  static_cast<double>(prepareElapsedMilliseconds));
    report.insert(QStringLiteral("catalogElapsedMilliseconds"),
                  static_cast<double>(catalogElapsedMilliseconds));
    report.insert(QStringLiteral("catalogIdentityCount"), catalogIdentityCount);
    report.insert(QStringLiteral("preparePerSymbol"), prepareResults);
    report.insert(QStringLiteral("ticksWritten"), static_cast<double>(ticksWritten));
    report.insert(QStringLiteral("barsWritten"), static_cast<double>(barsWritten));
    report.insert(QStringLiteral("failedFiles"), failedFiles);
    report.insert(QStringLiteral("perFile"), fileResults);
    report.insert(QStringLiteral("identity"), identityResult);
    QJsonObject backtest;
    backtest.insert(QStringLiteral("elapsedMilliseconds"),
                    static_cast<double>(backtestElapsedMilliseconds));
    for (auto it = backtestResult.constBegin(); it != backtestResult.constEnd(); ++it) {
        backtest.insert(it.key(), it.value());
    }
    report.insert(QStringLiteral("backtest"), backtest);
    QJsonObject backtestMemory;
    backtestMemory.insert(QStringLiteral("workingSetBeforeBytes"),
                          static_cast<double>(memoryBeforeBacktest.workingSetBytes));
    backtestMemory.insert(QStringLiteral("workingSetPeakObservedBytes"),
                          static_cast<double>(peakBacktestMemory.workingSetBytes));
    backtestMemory.insert(QStringLiteral("workingSetAfterBytes"),
                          static_cast<double>(memoryAfterBacktest.workingSetBytes));
    backtestMemory.insert(QStringLiteral("privateBeforeBytes"),
                          static_cast<double>(memoryBeforeBacktest.privateBytes));
    backtestMemory.insert(QStringLiteral("privatePeakObservedBytes"),
                          static_cast<double>(peakBacktestMemory.privateBytes));
    backtestMemory.insert(QStringLiteral("privateAfterBytes"),
                          static_cast<double>(memoryAfterBacktest.privateBytes));
    report.insert(QStringLiteral("backtestProcessMemory"), backtestMemory);
    QJsonObject archive;
    archive.insert(QStringLiteral("resultId"), archiveResultId);
    archive.insert(QStringLiteral("elapsedMilliseconds"),
                   static_cast<double>(archiveElapsedMilliseconds));
    report.insert(QStringLiteral("archive"), archive);
    report.insert(QStringLiteral("databaseBytes"), static_cast<double>(databaseBytes));
    report.insert(QStringLiteral("databasePath"), databasePath);

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || output.write(payload) < 0) {
        return 5;
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return 0;
}
