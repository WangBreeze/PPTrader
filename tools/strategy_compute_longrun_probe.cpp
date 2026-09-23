#include "services/FactorResearchAnalyzer.h"
#include "strategy/CustomBacktestStrategy.h"
#include "strategy/CustomIndicatorRuntime.h"
#include "strategy/CustomStrategyCompiler.h"
#include "strategy/CustomStrategyValidator.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

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

struct RoundResult {
    bool succeeded = false;
    QString error;
    qint64 generationMicroseconds = 0;
    qint64 indicatorMicroseconds = 0;
    qint64 strategyMicroseconds = 0;
    qint64 factorMicroseconds = 0;
    qint64 totalMicroseconds = 0;
    int signalCount = 0;
    int validIndicatorCount = 0;
    int evaluatedFactorFrames = 0;
    double checksum = 0.0;
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

QJsonObject indicator(const QString &name, int period)
{
    return {{QStringLiteral("indicator"), name},
            {QStringLiteral("period"), period}};
}

QJsonObject generatedStrategyDocument()
{
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("strategyId"), QStringLiteral("custom_acceptance_sma_cross")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("name"), QStringLiteral("长稳验收均线策略")},
        {QStringLiteral("description"), QStringLiteral("受控生成、校验与计算验收")},
        {QStringLiteral("author"), QStringLiteral("PPTrader Acceptance")},
        {QStringLiteral("market"), QStringLiteral("A股")},
        {QStringLiteral("executionScope"), QStringLiteral("backtest_only")},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-28T00:00:00.000Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-08-28T00:00:00.000Z")},
        {QStringLiteral("parameters"), QJsonArray{}},
        {QStringLiteral("entry"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("crossesAbove")},
            {QStringLiteral("left"), indicator(QStringLiteral("SMA"), 5)},
            {QStringLiteral("right"), indicator(QStringLiteral("SMA"), 20)}}},
        {QStringLiteral("exit"), QJsonObject{
            {QStringLiteral("op"), QStringLiteral("crossesBelow")},
            {QStringLiteral("left"), indicator(QStringLiteral("SMA"), 5)},
            {QStringLiteral("right"), indicator(QStringLiteral("SMA"), 20)}}}
    };
}

QVector<pp::Candle> generatedCandles(int count)
{
    pp::Instrument instrument;
    instrument.symbol = QStringLiteral("600519");
    instrument.name = QStringLiteral("贵州茅台");
    instrument.market = pp::Market::AShare;
    instrument.venue = QStringLiteral("XSHG");

    QVector<pp::Candle> candles;
    candles.reserve(count);
    const QDateTime origin(QDate(2025, 1, 2), QTime(1, 30),
                           QTimeZone::utc());
    double previousClose = 100.0;
    for (int row = 0; row < count; ++row) {
        const double wave = std::sin(static_cast<double>(row) * 0.071) * 2.5;
        const double trend = static_cast<double>(row % 600) * 0.002;
        const double close = 100.0 + wave + trend;
        pp::Candle candle;
        candle.instrument = instrument;
        candle.startTime = origin.addSecs(static_cast<qint64>(row) * 60);
        candle.periodSeconds = 60;
        candle.open = previousClose;
        candle.close = close;
        candle.high = std::max(candle.open, candle.close) + 0.35;
        candle.low = std::min(candle.open, candle.close) - 0.35;
        candle.volume = 100000.0 + static_cast<double>((row * 7919) % 50000);
        candle.buyVolume = candle.volume * (0.45 + 0.1 * (row % 3));
        candle.sellVolume = candle.volume * 0.35;
        candle.neutralVolume = std::max(
            0.0, candle.volume - candle.buyVolume - candle.sellVolume);
        candle.directionalVolumeKnown = true;
        candle.source = QStringLiteral("controlled_strategy_compute_probe");
        candles.append(candle);
        previousClose = close;
    }
    return candles;
}

QVector<pp::CompiledCustomIndicator> allIndicators()
{
    QVector<pp::CompiledCustomIndicator> indicators;
    indicators.append({pp::CustomIndicatorKind::Sma, 5});
    indicators.append({pp::CustomIndicatorKind::Ema, 8});
    indicators.append({pp::CustomIndicatorKind::Rsi, 14});

    pp::CompiledCustomIndicator macd;
    macd.kind = pp::CustomIndicatorKind::Macd;
    macd.fastPeriod = 12;
    macd.slowPeriod = 26;
    macd.signalPeriod = 9;
    macd.output = QStringLiteral("histogram");
    indicators.append(macd);

    pp::CompiledCustomIndicator bollinger;
    bollinger.kind = pp::CustomIndicatorKind::Bollinger;
    bollinger.period = 20;
    bollinger.standardDeviations = 2.0;
    bollinger.output = QStringLiteral("middle");
    indicators.append(bollinger);

    indicators.append({pp::CustomIndicatorKind::Atr, 14});
    indicators.append({pp::CustomIndicatorKind::VolumeSma, 20});
    indicators.append({pp::CustomIndicatorKind::Roc, 12});
    indicators.append({pp::CustomIndicatorKind::Vwap});
    indicators.append({pp::CustomIndicatorKind::PpFlow});
    return indicators;
}

QVector<pp::FactorResearchFrame> generatedFactorFrames(int frameCount,
                                                       int symbolCount)
{
    QVector<pp::FactorResearchFrame> frames;
    frames.reserve(frameCount);
    const QDateTime origin(QDate(2025, 1, 2), QTime(1, 30),
                           QTimeZone::utc());
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        pp::FactorResearchFrame frame;
        frame.factorTimestamp = origin.addSecs(frameIndex * 300);
        frame.returnTimestamp = origin.addSecs((frameIndex + 1) * 300);
        for (int symbolIndex = 0; symbolIndex < symbolCount; ++symbolIndex) {
            const QString symbol = QStringLiteral("%1").arg(
                symbolIndex + 1, 6, 10, QLatin1Char('0'));
            const double value = static_cast<double>(symbolIndex + 1)
                + std::sin(static_cast<double>(frameIndex + symbolIndex) * 0.13);
            frame.factorValues.insert(symbol, value);
            frame.forwardReturns.insert(symbol, value * 0.0001
                + static_cast<double>(frameIndex) * 0.000001);
        }
        frames.append(std::move(frame));
    }
    return frames;
}

RoundResult executeRound(const QVector<pp::Candle> &candles,
                         const QVector<pp::FactorResearchFrame> &factorFrames)
{
    RoundResult result;
    QElapsedTimer totalTimer;
    totalTimer.start();

    QElapsedTimer timer;
    timer.start();
    const QByteArray json = QJsonDocument(generatedStrategyDocument())
                                .toJson(QJsonDocument::Compact);
    pp::CustomStrategyValidator validator;
    pp::CustomStrategyDefinition definition;
    QString error;
    if (!validator.parseAndValidate(QString::fromUtf8(json), &definition, &error)) {
        result.error = error;
        return result;
    }
    pp::CustomStrategyCompiler compiler;
    const auto plan = compiler.compile(definition, {}, &error);
    if (!plan) {
        result.error = error;
        return result;
    }
    result.generationMicroseconds = timer.nsecsElapsed() / 1000;

    timer.restart();
    pp::CustomIndicatorRuntime indicatorRuntime(allIndicators());
    for (const pp::Candle &candle : candles) indicatorRuntime.append(candle);
    double indicatorChecksum = 0.0;
    for (int index = 0; index < allIndicators().size(); ++index) {
        const auto value = indicatorRuntime.value(index);
        if (!value || !std::isfinite(*value)) {
            result.error = QStringLiteral("白名单指标 %1 未产生有限结果").arg(index);
            return result;
        }
        indicatorChecksum += *value * static_cast<double>(index + 1);
        ++result.validIndicatorCount;
    }
    result.indicatorMicroseconds = timer.nsecsElapsed() / 1000;

    timer.restart();
    pp::CustomBacktestStrategy strategy(*plan);
    QObject::connect(&strategy, &pp::Strategy::signalGenerated,
                     &strategy, [&result](const pp::TradeSignal &) {
        ++result.signalCount;
    });
    for (const pp::Candle &candle : candles) strategy.onCandle(candle);
    result.strategyMicroseconds = timer.nsecsElapsed() / 1000;

    timer.restart();
    pp::FactorPreprocessingPolicy policy;
    policy.winsorizationPolicy = QStringLiteral("percentile_clip");
    policy.lowerQuantile = 0.01;
    policy.upperQuantile = 0.99;
    pp::FactorResearchCostPolicy costPolicy;
    costPolicy.oneWayCostBps = 5.0;
    const pp::FactorResearchSummary factorSummary =
        pp::FactorResearchAnalyzer::analyze(factorFrames, policy, costPolicy);
    result.factorMicroseconds = timer.nsecsElapsed() / 1000;
    result.evaluatedFactorFrames = factorSummary.evaluatedFrameCount;
    if (factorSummary.evaluatedFrameCount != factorFrames.size()
        || !std::isfinite(factorSummary.meanIc)
        || !std::isfinite(factorSummary.meanRankIc)) {
        result.error = QStringLiteral("因子统计结果不完整或非有限值");
        return result;
    }

    result.checksum = indicatorChecksum
        + static_cast<double>(result.signalCount) * 100000.0
        + factorSummary.meanIc * 1000.0
        + factorSummary.meanRankIc * 100.0;
    result.totalMicroseconds = totalTimer.nsecsElapsed() / 1000;
    result.succeeded = std::isfinite(result.checksum);
    if (!result.succeeded) result.error = QStringLiteral("计算校验和非有限值");
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(
        QStringLiteral("pptrader_strategy_compute_longrun"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("r"), QStringLiteral("rounds")},
         QStringLiteral("Measured rounds"), QStringLiteral("count"),
         QStringLiteral("100")},
        {{QStringLiteral("t"), QStringLiteral("duration-seconds")},
         QStringLiteral("Run complete rounds until this duration; overrides rounds"),
         QStringLiteral("seconds"), QStringLiteral("0")},
        {{QStringLiteral("b"), QStringLiteral("bars")},
         QStringLiteral("Candles evaluated per round"), QStringLiteral("count"),
         QStringLiteral("5000")},
        {{QStringLiteral("s"), QStringLiteral("factor-symbols")},
         QStringLiteral("Symbols per factor frame"), QStringLiteral("count"),
         QStringLiteral("100")},
        {{QStringLiteral("f"), QStringLiteral("factor-frames")},
         QStringLiteral("Factor frames per round"), QStringLiteral("count"),
         QStringLiteral("60")},
        {{QStringLiteral("w"), QStringLiteral("warmup-rounds")},
         QStringLiteral("Unmeasured warmup rounds"), QStringLiteral("count"),
         QStringLiteral("3")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("Optional JSON output path"), QStringLiteral("path")},
        {{QStringLiteral("p"), QStringLiteral("progress-seconds")},
         QStringLiteral("Progress report interval"), QStringLiteral("seconds"),
         QStringLiteral("30")},
        {QStringLiteral("sample-every"),
         QStringLiteral("Store timing and memory every N rounds"),
         QStringLiteral("count"), QStringLiteral("10")}
    });
    parser.process(application);

    const int requestedRounds = std::max(
        1, parser.value(QStringLiteral("rounds")).toInt());
    const int durationSeconds = std::clamp(
        parser.value(QStringLiteral("duration-seconds")).toInt(), 0, 86400);
    const int barCount = std::clamp(
        parser.value(QStringLiteral("bars")).toInt(), 100, 100000);
    const int factorSymbols = std::clamp(
        parser.value(QStringLiteral("factor-symbols")).toInt(), 5, 5207);
    const int factorFrames = std::clamp(
        parser.value(QStringLiteral("factor-frames")).toInt(), 1, 2000);
    const int warmupRounds = std::clamp(
        parser.value(QStringLiteral("warmup-rounds")).toInt(), 0, 100);
    const int progressSeconds = std::clamp(
        parser.value(QStringLiteral("progress-seconds")).toInt(), 1, 3600);
    const int sampleEvery = std::clamp(
        parser.value(QStringLiteral("sample-every")).toInt(), 1, 10000);

    const QVector<pp::Candle> candles = generatedCandles(barCount);
    const QVector<pp::FactorResearchFrame> frames = generatedFactorFrames(
        factorFrames, factorSymbols);
    for (int round = 0; round < warmupRounds; ++round) {
        const RoundResult warmup = executeRound(candles, frames);
        if (!warmup.succeeded) {
            QTextStream(stderr) << QStringLiteral("预热失败：")
                                << warmup.error << Qt::endl;
            return 2;
        }
    }

    QVector<qint64> generationDurations;
    QVector<qint64> indicatorDurations;
    QVector<qint64> strategyDurations;
    QVector<qint64> factorDurations;
    QVector<qint64> totalDurations;
    QVector<ProcessMemorySample> memorySamples;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    QElapsedTimer progressTimer;
    progressTimer.start();
    memorySamples.append(currentProcessMemory(0));

    int completedRounds = 0;
    int failedRounds = 0;
    qint64 generatedDocuments = 0;
    qint64 evaluatedCandles = 0;
    qint64 evaluatedFactorPairs = 0;
    double firstChecksum = 0.0;
    double lastChecksum = 0.0;
    bool deterministic = true;
    QString firstError;
    RoundResult lastResult;

    while (true) {
        const RoundResult result = executeRound(candles, frames);
        if (!result.succeeded) {
            ++failedRounds;
            if (firstError.isEmpty()) firstError = result.error;
            break;
        }
        ++completedRounds;
        ++generatedDocuments;
        evaluatedCandles += static_cast<qint64>(barCount) * 2;
        evaluatedFactorPairs += static_cast<qint64>(factorFrames) * factorSymbols;
        lastResult = result;
        if (completedRounds == 1 || completedRounds % sampleEvery == 0) {
            generationDurations.append(result.generationMicroseconds);
            indicatorDurations.append(result.indicatorMicroseconds);
            strategyDurations.append(result.strategyMicroseconds);
            factorDurations.append(result.factorMicroseconds);
            totalDurations.append(result.totalMicroseconds);
            memorySamples.append(currentProcessMemory(elapsedTimer.elapsed()));
        }
        if (completedRounds == 1) firstChecksum = result.checksum;
        lastChecksum = result.checksum;
        const double tolerance = std::max(1.0, std::abs(firstChecksum)) * 1.0e-10;
        deterministic = deterministic
            && std::abs(lastChecksum - firstChecksum) <= tolerance;
        if (progressTimer.elapsed() >= static_cast<qint64>(progressSeconds) * 1000) {
            QTextStream(stderr)
                << QStringLiteral("progress rounds=%1 elapsedSeconds=%2 privateMiB=%3")
                       .arg(completedRounds)
                       .arg(elapsedTimer.elapsed() / 1000)
                       .arg(currentProcessMemory(elapsedTimer.elapsed()).privateBytes
                                / 1048576.0,
                            0, 'f', 1)
                << Qt::endl;
            progressTimer.restart();
        }

        const bool durationReached = durationSeconds > 0
            && elapsedTimer.elapsed() >= static_cast<qint64>(durationSeconds) * 1000;
        const bool roundLimitReached = durationSeconds == 0
            && completedRounds >= requestedRounds;
        if (durationReached || roundLimitReached) break;
    }

    if (completedRounds > 1 && completedRounds % sampleEvery != 0
        && lastResult.succeeded) {
        generationDurations.append(lastResult.generationMicroseconds);
        indicatorDurations.append(lastResult.indicatorMicroseconds);
        strategyDurations.append(lastResult.strategyMicroseconds);
        factorDurations.append(lastResult.factorMicroseconds);
        totalDurations.append(lastResult.totalMicroseconds);
        memorySamples.append(currentProcessMemory(elapsedTimer.elapsed()));
    }

    quint64 privateMaximum = 0;
    quint64 workingSetMaximum = 0;
    for (const ProcessMemorySample &sample : memorySamples) {
        privateMaximum = std::max(privateMaximum, sample.privateBytes);
        workingSetMaximum = std::max(workingSetMaximum, sample.workingSetBytes);
    }

    QJsonObject report;
    report.insert(QStringLiteral("requestedRounds"), requestedRounds);
    report.insert(QStringLiteral("requestedDurationSeconds"), durationSeconds);
    report.insert(QStringLiteral("warmupRounds"), warmupRounds);
    report.insert(QStringLiteral("sampleEvery"), sampleEvery);
    report.insert(QStringLiteral("timingSampleCount"), totalDurations.size());
    report.insert(QStringLiteral("completedRounds"), completedRounds);
    report.insert(QStringLiteral("failedRounds"), failedRounds);
    report.insert(QStringLiteral("elapsedMs"),
                  static_cast<double>(elapsedTimer.elapsed()));
    report.insert(QStringLiteral("barsPerRound"), barCount);
    report.insert(QStringLiteral("factorSymbols"), factorSymbols);
    report.insert(QStringLiteral("factorFramesPerRound"), factorFrames);
    report.insert(QStringLiteral("generatedDocuments"),
                  static_cast<double>(generatedDocuments));
    report.insert(QStringLiteral("evaluatedCandles"),
                  static_cast<double>(evaluatedCandles));
    report.insert(QStringLiteral("evaluatedFactorPairs"),
                  static_cast<double>(evaluatedFactorPairs));
    report.insert(QStringLiteral("generationP50Us"),
                  percentile(generationDurations, 0.50));
    report.insert(QStringLiteral("generationP95Us"),
                  percentile(generationDurations, 0.95));
    report.insert(QStringLiteral("indicatorP50Us"),
                  percentile(indicatorDurations, 0.50));
    report.insert(QStringLiteral("indicatorP95Us"),
                  percentile(indicatorDurations, 0.95));
    report.insert(QStringLiteral("strategyP50Us"),
                  percentile(strategyDurations, 0.50));
    report.insert(QStringLiteral("strategyP95Us"),
                  percentile(strategyDurations, 0.95));
    report.insert(QStringLiteral("factorP50Us"),
                  percentile(factorDurations, 0.50));
    report.insert(QStringLiteral("factorP95Us"),
                  percentile(factorDurations, 0.95));
    report.insert(QStringLiteral("roundP50Us"),
                  percentile(totalDurations, 0.50));
    report.insert(QStringLiteral("roundP95Us"),
                  percentile(totalDurations, 0.95));
    report.insert(QStringLiteral("deterministic"), deterministic);
    report.insert(QStringLiteral("checksumFirst"), firstChecksum);
    report.insert(QStringLiteral("checksumLast"), lastChecksum);
    report.insert(QStringLiteral("firstError"), firstError);
    if (!memorySamples.isEmpty()) {
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

    const QByteArray payload = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString outputPath = parser.value(QStringLiteral("output"));
    if (!outputPath.isEmpty()) {
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(payload) != payload.size() || !output.commit()) {
            QTextStream(stderr) << QStringLiteral("无法写入结果 JSON") << Qt::endl;
            return 3;
        }
    }
    QTextStream(stdout) << QString::fromUtf8(payload) << Qt::endl;
    return failedRounds == 0 && deterministic ? 0 : 4;
}
