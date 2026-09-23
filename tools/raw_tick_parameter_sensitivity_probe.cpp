#include "services/RawTickParameterSensitivityService.h"
#include "services/SqliteLocalDataStore.h"
#include "strategy/BacktestStrategyRegistry.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace {

QStringList commaSeparated(const QString &value)
{
    QStringList result;
    for (const QString &part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed)) result.append(trimmed);
    }
    return result;
}

QVector<pp::ParameterGridAxis> axesForStrategy(const QString &strategyId,
                                               bool compactGrid)
{
    pp::ParameterGridAxis lookback;
    lookback.key = QStringLiteral("lookbackTicks");
    lookback.values = compactGrid ? QVector<double> {10.0, 50.0}
                                  : QVector<double> {10.0, 20.0, 50.0};
    pp::ParameterGridAxis second;
    if (strategyId == QStringLiteral("tick_mean_reversion")) {
        second.key = QStringLiteral("entryDeviationPercent");
        second.values = compactGrid ? QVector<double> {0.05, 0.2}
                                    : QVector<double> {0.05, 0.1, 0.2};
    } else if (strategyId == QStringLiteral("tick_volume_breakout")) {
        second.key = QStringLiteral("volumeMultiplier");
        second.values = compactGrid ? QVector<double> {1.2, 2.0}
                                    : QVector<double> {1.2, 1.5, 2.0};
    } else if (strategyId == QStringLiteral("tick_directional_imbalance")) {
        second.key = QStringLiteral("entryImbalancePercent");
        second.values = compactGrid ? QVector<double> {10.0, 50.0}
                                    : QVector<double> {10.0, 30.0, 50.0};
    } else {
        second.key = QStringLiteral("thresholdPercent");
        second.values = compactGrid ? QVector<double> {0.05, 0.2}
                                    : QVector<double> {0.05, 0.1, 0.2};
    }
    return {lookback, second};
}

QVariantMap defaultParameters(const pp::BacktestStrategyDescriptor &descriptor)
{
    QVariantMap result;
    for (const pp::BacktestParameterSpec &parameter : descriptor.parameters)
        result.insert(parameter.key, parameter.defaultValue);
    return result;
}

QByteArray canonicalParameters(const QVariantMap &parameters)
{
    return QJsonDocument::fromVariant(parameters).toJson(QJsonDocument::Compact);
}

QByteArray canonicalFoldSelections(
    const pp::RawTickParameterSensitivityResult &result)
{
    QVariantList selections;
    for (const pp::RawTickParameterSensitivityFold &fold : result.folds) {
        selections.append(QVariantMap {
            {QStringLiteral("foldNumber"), fold.foldNumber},
            {QStringLiteral("selectedParameters"), fold.selectedParameters}});
    }
    return QJsonDocument::fromVariant(selections).toJson(QJsonDocument::Compact);
}

double percentile(QVector<qint64> values, double probability)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = probability * static_cast<double>(values.size() - 1);
    const qsizetype lower = static_cast<qsizetype>(std::floor(position));
    const qsizetype upper = static_cast<qsizetype>(std::ceil(position));
    if (lower == upper) return static_cast<double>(values.at(lower));
    const double fraction = position - static_cast<double>(lower);
    return static_cast<double>(values.at(lower)) * (1.0 - fraction)
        + static_cast<double>(values.at(upper)) * fraction;
}

QJsonObject timingSummary(const QVector<qint64> &values)
{
    if (values.isEmpty()) return {};
    const auto [minimum, maximum] = std::minmax_element(values.cbegin(), values.cend());
    return {{QStringLiteral("minimum"), static_cast<double>(*minimum)},
            {QStringLiteral("p50"), percentile(values, 0.50)},
            {QStringLiteral("p95"), percentile(values, 0.95)},
            {QStringLiteral("maximum"), static_cast<double>(*maximum)}};
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PPTrader Raw Tick Sensitivity Probe"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {{QStringLiteral("d"), QStringLiteral("database")},
         QStringLiteral("隔离 SQLite 数据库"), QStringLiteral("path")},
        {{QStringLiteral("o"), QStringLiteral("output")},
         QStringLiteral("结果 JSON"), QStringLiteral("path")},
        {{QStringLiteral("s"), QStringLiteral("symbols")},
         QStringLiteral("逗号分隔 A 股代码"), QStringLiteral("symbols")},
        {{QStringLiteral("g"), QStringLiteral("strategies")},
         QStringLiteral("逗号分隔 raw-only 策略"), QStringLiteral("ids"),
         QStringLiteral("tick_momentum,tick_mean_reversion,tick_volume_breakout,tick_directional_imbalance")},
        {{QStringLiteral("f"), QStringLiteral("start-date")},
         QStringLiteral("开始日期 yyyy-MM-dd"), QStringLiteral("date")},
        {{QStringLiteral("t"), QStringLiteral("end-date")},
         QStringLiteral("结束日期 yyyy-MM-dd"), QStringLiteral("date")},
        {{QStringLiteral("v"), QStringLiteral("validation-days")},
         QStringLiteral("每折验证交易日数"), QStringLiteral("count"), QStringLiteral("2")},
        {QStringLiteral("walk-forward-folds"),
         QStringLiteral("嵌套 walk-forward 外层折数；1 为单次留出"),
         QStringLiteral("count"), QStringLiteral("1")},
        {{QStringLiteral("r"), QStringLiteral("repetitions")},
         QStringLiteral("重复次数"), QStringLiteral("count"), QStringLiteral("2")},
        {QStringLiteral("request-timeout-seconds"),
         QStringLiteral("单次请求超时秒数"), QStringLiteral("seconds"),
         QStringLiteral("120")},
        {QStringLiteral("compact-grid"),
         QStringLiteral("使用 2×2 参数候选集做受控验收")},
    });
    parser.process(application);

    const QString databasePath = parser.value(QStringLiteral("database"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    const QStringList symbols = commaSeparated(parser.value(QStringLiteral("symbols")));
    const QStringList strategyIds = commaSeparated(parser.value(QStringLiteral("strategies")));
    const QDate firstDate = QDate::fromString(
        parser.value(QStringLiteral("start-date")), Qt::ISODate);
    const QDate lastDate = QDate::fromString(
        parser.value(QStringLiteral("end-date")), Qt::ISODate);
    bool validationOk = false;
    const int validationDays = parser.value(QStringLiteral("validation-days"))
                                   .toInt(&validationOk);
    bool foldCountOk = false;
    const int foldCount = parser.value(QStringLiteral("walk-forward-folds"))
                              .toInt(&foldCountOk);
    bool repetitionsOk = false;
    const int repetitions = parser.value(QStringLiteral("repetitions"))
                                .toInt(&repetitionsOk);
    bool requestTimeoutOk = false;
    const int requestTimeoutSeconds =
        parser.value(QStringLiteral("request-timeout-seconds"))
            .toInt(&requestTimeoutOk);
    const bool compactGrid = parser.isSet(QStringLiteral("compact-grid"));
    if (databasePath.isEmpty() || outputPath.isEmpty() || symbols.isEmpty()
        || strategyIds.isEmpty() || !firstDate.isValid() || !lastDate.isValid()
        || firstDate > lastDate || !validationOk || validationDays < 2
        || validationDays > 20 || !foldCountOk || foldCount < 1
        || foldCount > 20 || !repetitionsOk || repetitions < 2
        || repetitions > 10 || !requestTimeoutOk || requestTimeoutSeconds < 1
        || requestTimeoutSeconds > 3600) {
        qCritical("参数无效");
        return 1;
    }

    pp::SqliteLocalDataStore catalogStore;
    if (!catalogStore.initialize(databasePath)) {
        qCritical("隔离数据库初始化失败");
        return 2;
    }
    QHash<QString, pp::Instrument> instruments;
    for (const pp::Instrument &instrument : catalogStore.rawBacktestInstruments())
        instruments.insert(instrument.symbol, instrument);
    catalogStore.close();

    pp::BacktestStrategyRegistry registry;
    for (const QString &strategyId : strategyIds) {
        if (!registry.supportsDataMode(strategyId, QStringLiteral("raw_ticks"))) {
            qCritical().noquote() << QStringLiteral("策略不支持原始 Tick：%1").arg(strategyId);
            return 3;
        }
    }

    pp::RawTickParameterSensitivityService service;
    if (!service.start(databasePath)) {
        qCritical("参数敏感性服务启动失败");
        return 4;
    }

    QJsonArray runs;
    QHash<QString, QString> firstIdentity;
    QHash<QString, QString> firstResultId;
    QHash<QString, QByteArray> firstParameters;
    QVector<qint64> loadTimings;
    QVector<qint64> scanTimings;
    QVector<qint64> totalTimings;
    int succeeded = 0;
    int failed = 0;
    int stableSelections = 0;
    int repeatComparisons = 0;
    quint64 requestId = 0;
    bool abortRemainingRequests = false;
    bool abortedAfterCancellationAcknowledgementTimeout = false;
    QElapsedTimer wallTimer;
    wallTimer.start();

    for (int repetition = 1;
         repetition <= repetitions && !abortRemainingRequests; ++repetition) {
        for (const QString &symbol : symbols) {
            if (abortRemainingRequests) break;
            for (const QString &strategyId : strategyIds) {
                if (abortRemainingRequests) break;
                QJsonObject run;
                run.insert(QStringLiteral("repetition"), repetition);
                run.insert(QStringLiteral("symbol"), symbol);
                run.insert(QStringLiteral("strategyId"), strategyId);
                if (!instruments.contains(symbol)) {
                    run.insert(QStringLiteral("succeeded"), false);
                    run.insert(QStringLiteral("error"), QStringLiteral("本地原始 Tick 目录没有该代码"));
                    runs.append(run);
                    ++failed;
                    continue;
                }
                const auto descriptor = registry.descriptor(strategyId);
                if (!descriptor) {
                    run.insert(QStringLiteral("succeeded"), false);
                    run.insert(QStringLiteral("error"), QStringLiteral("策略描述不存在"));
                    runs.append(run);
                    ++failed;
                    continue;
                }

                pp::RawTickParameterSensitivityRequest request;
                request.strategyId = strategyId;
                request.baseParameters = defaultParameters(*descriptor);
                request.axes = axesForStrategy(strategyId, compactGrid);
                request.validationTradingDays = validationDays;
                request.walkForwardFoldCount = foldCount;
                request.maximumCombinations = 64;
                request.baseConfig.instrument = instruments.value(symbol);
                request.baseConfig.initialCapital = 1000000.0;
                request.baseConfig.commissionRate = 0.0003;
                request.baseConfig.slippageRate = 0.0002;
                request.baseConfig.lotSize = 100;

                std::optional<pp::RawTickParameterSensitivityResult> completed;
                QString failure;
                QEventLoop loop;
                const quint64 currentRequestId = ++requestId;
                const QMetaObject::Connection completedConnection = QObject::connect(
                    &service, &pp::RawTickParameterSensitivityService::completed,
                    &loop, [&](quint64 responseId,
                               const pp::RawTickParameterSensitivityResult &result) {
                        if (responseId != currentRequestId) return;
                        completed = result;
                        loop.quit();
                    });
                const QMetaObject::Connection failedConnection = QObject::connect(
                    &service, &pp::RawTickParameterSensitivityService::failed,
                    &loop, [&](quint64 responseId, const QString &message) {
                        if (responseId != currentRequestId) return;
                        failure = message;
                        loop.quit();
                    });
                bool timedOut = false;
                bool cancellationAcknowledgementTimedOut = false;
                QTimer::singleShot(requestTimeoutSeconds * 1000, &loop, [&]() {
                    timedOut = true;
                    service.cancel();
                    // cancel() 只设置原子标志。必须等工作线程发回完成/失败确认，
                    // 否则下一请求会在 busy 尚未清理时被拒绝，形成超时级联。
                    QTimer::singleShot(30000, &loop, [&]() {
                        cancellationAcknowledgementTimedOut = true;
                        loop.quit();
                    });
                });
                QElapsedTimer timer;
                timer.start();
                if (!service.request(currentRequestId, request, firstDate, lastDate)) {
                    failure = QStringLiteral("异步请求未启动");
                } else {
                    loop.exec();
                }
                const qint64 totalMilliseconds = timer.elapsed();
                QObject::disconnect(completedConnection);
                QObject::disconnect(failedConnection);
                if (timedOut) {
                    completed.reset();
                    failure = cancellationAcknowledgementTimedOut
                        ? QStringLiteral("请求超时且取消未在 30 秒内确认")
                        : QStringLiteral("请求超时（取消已确认）");
                }
                if (!completed) {
                    run.insert(QStringLiteral("succeeded"), false);
                    run.insert(QStringLiteral("error"), failure);
                    run.insert(QStringLiteral("totalMilliseconds"),
                               static_cast<double>(totalMilliseconds));
                    runs.append(run);
                    ++failed;
                    if (timedOut) abortRemainingRequests = true;
                    if (cancellationAcknowledgementTimedOut)
                        abortedAfterCancellationAcknowledgementTimeout = true;
                    continue;
                }

                const QString key = symbol + QLatin1Char('|') + strategyId;
                const QByteArray selected = completed->folds.isEmpty()
                    ? canonicalParameters(completed->selectedParameters)
                    : canonicalFoldSelections(*completed);
                bool repeatStable = true;
                if (repetition == 1) {
                    firstIdentity.insert(key, completed->identitySha256);
                    firstResultId.insert(key, completed->resultId);
                    firstParameters.insert(key, selected);
                } else {
                    ++repeatComparisons;
                    repeatStable = firstIdentity.value(key) == completed->identitySha256
                        && firstResultId.value(key) == completed->resultId
                        && firstParameters.value(key) == selected;
                    if (repeatStable) ++stableSelections;
                }

                run.insert(QStringLiteral("succeeded"), true);
                run.insert(QStringLiteral("requestId"), static_cast<double>(currentRequestId));
                run.insert(QStringLiteral("resultId"), completed->resultId);
                run.insert(QStringLiteral("identitySha256"), completed->identitySha256);
                run.insert(QStringLiteral("datasetVersion"), completed->datasetVersion);
                run.insert(QStringLiteral("candidateSetSha256"), completed->candidateSetSha256);
                run.insert(QStringLiteral("analysisMode"), completed->analysisMode);
                run.insert(QStringLiteral("walkForwardFoldCount"),
                           completed->walkForwardFoldCount);
                run.insert(QStringLiteral("candidateCount"), completed->candidates.size());
                run.insert(QStringLiteral("trainingTicks"), completed->trainingActualTicks);
                run.insert(QStringLiteral("validationTicks"), completed->validationActualTicks);
                run.insert(QStringLiteral("trainingStartDate"),
                           completed->trainingStartDate.toString(Qt::ISODate));
                run.insert(QStringLiteral("trainingEndDate"),
                           completed->trainingEndDate.toString(Qt::ISODate));
                run.insert(QStringLiteral("validationStartDate"),
                           completed->validationStartDate.toString(Qt::ISODate));
                run.insert(QStringLiteral("validationEndDate"),
                           completed->validationEndDate.toString(Qt::ISODate));
                run.insert(QStringLiteral("selectedParameters"),
                           QJsonObject::fromVariantMap(completed->selectedParameters));
                run.insert(QStringLiteral("trainingBestReturnPercent"),
                           completed->candidates.first().trainingSummary.totalReturnPercent);
                run.insert(QStringLiteral("trainingBestMaxDrawdownPercent"),
                           completed->candidates.first().trainingSummary.maxDrawdownPercent);
                run.insert(QStringLiteral("validationReturnPercent"),
                           completed->validationSummary.totalReturnPercent);
                run.insert(QStringLiteral("validationMaxDrawdownPercent"),
                           completed->validationSummary.maxDrawdownPercent);
                run.insert(QStringLiteral("validationTrades"),
                           completed->validationSummary.totalTrades);
                run.insert(QStringLiteral("validationWinningTrades"),
                           completed->validationWinningTrades);
                run.insert(QStringLiteral("validationLosingTrades"),
                           completed->validationLosingTrades);
                run.insert(QStringLiteral("validationTotalTrades"),
                           completed->validationTotalTrades);
                run.insert(QStringLiteral("validationGrossProfit"),
                           completed->validationGrossProfit);
                run.insert(QStringLiteral("validationGrossLossAbsolute"),
                           completed->validationGrossLossAbsolute);
                run.insert(QStringLiteral("validationAverageWinningNetPnl"),
                           completed->validationAverageWinningNetPnl);
                run.insert(QStringLiteral("validationAverageLosingNetPnlAbsolute"),
                           completed->validationAverageLosingNetPnlAbsolute);
                run.insert(QStringLiteral("validationPayoffRatio"),
                           completed->validationPayoffRatio);
                run.insert(QStringLiteral("validationPayoffRatioAvailable"),
                           completed->validationPayoffRatioAvailable);
                run.insert(QStringLiteral("meanValidationReturnPercent"),
                           completed->meanValidationReturnPercent);
                run.insert(QStringLiteral("medianValidationReturnPercent"),
                           completed->medianValidationReturnPercent);
                run.insert(QStringLiteral("minimumValidationReturnPercent"),
                           completed->minimumValidationReturnPercent);
                run.insert(QStringLiteral("maximumValidationReturnPercent"),
                           completed->maximumValidationReturnPercent);
                run.insert(QStringLiteral("positiveValidationFoldCount"),
                           completed->positiveValidationFoldCount);
                run.insert(QStringLiteral("nonPositiveValidationFoldCount"),
                           completed->nonPositiveValidationFoldCount);
                run.insert(QStringLiteral("parameterSelectionVariantCount"),
                           completed->parameterSelectionVariantCount);
                run.insert(QStringLiteral("parameterSelectionDistribution"),
                           QJsonArray::fromVariantList(
                               completed->parameterSelectionDistribution));
                QJsonArray foldResults;
                for (const pp::RawTickParameterSensitivityFold &fold :
                     completed->folds) {
                    foldResults.append(QJsonObject {
                        {QStringLiteral("foldNumber"), fold.foldNumber},
                        {QStringLiteral("trainingStartDate"),
                         fold.trainingStartDate.toString(Qt::ISODate)},
                        {QStringLiteral("trainingEndDate"),
                         fold.trainingEndDate.toString(Qt::ISODate)},
                        {QStringLiteral("validationStartDate"),
                         fold.validationStartDate.toString(Qt::ISODate)},
                        {QStringLiteral("validationEndDate"),
                         fold.validationEndDate.toString(Qt::ISODate)},
                        {QStringLiteral("trainingTicks"), fold.trainingActualTicks},
                        {QStringLiteral("validationTicks"), fold.validationActualTicks},
                        {QStringLiteral("trainingDataVersion"),
                         fold.trainingDataVersion},
                        {QStringLiteral("validationDataVersion"),
                         fold.validationDataVersion},
                        {QStringLiteral("selectedParameters"),
                         QJsonObject::fromVariantMap(fold.selectedParameters)},
                        {QStringLiteral("validationReturnPercent"),
                         fold.validationSummary.totalReturnPercent},
                        {QStringLiteral("validationMaxDrawdownPercent"),
                         fold.validationSummary.maxDrawdownPercent},
                        {QStringLiteral("validationTrades"),
                         fold.validationSummary.totalTrades},
                        {QStringLiteral("validationWinningTrades"),
                         fold.validationWinningTrades},
                        {QStringLiteral("validationLosingTrades"),
                         fold.validationLosingTrades},
                        {QStringLiteral("validationGrossProfit"),
                         fold.validationGrossProfit},
                        {QStringLiteral("validationGrossLossAbsolute"),
                         fold.validationGrossLossAbsolute},
                        {QStringLiteral("validationAverageWinningNetPnl"),
                         fold.validationAverageWinningNetPnl},
                        {QStringLiteral("validationAverageLosingNetPnlAbsolute"),
                         fold.validationAverageLosingNetPnlAbsolute},
                        {QStringLiteral("validationPayoffRatio"),
                         fold.validationPayoffRatio},
                        {QStringLiteral("validationPayoffRatioAvailable"),
                         fold.validationPayoffRatioAvailable}});
                }
                run.insert(QStringLiteral("folds"), foldResults);
                run.insert(QStringLiteral("loadMilliseconds"),
                           static_cast<double>(completed->loadMilliseconds));
                run.insert(QStringLiteral("scanMilliseconds"),
                           static_cast<double>(completed->scanMilliseconds));
                run.insert(QStringLiteral("totalMilliseconds"),
                           static_cast<double>(totalMilliseconds));
                run.insert(QStringLiteral("repeatStable"), repeatStable);
                runs.append(run);
                loadTimings.append(completed->loadMilliseconds);
                scanTimings.append(completed->scanMilliseconds);
                totalTimings.append(totalMilliseconds);
                ++succeeded;
            }
        }
    }
    service.stop();

    pp::SqliteLocalDataStore restoredStore;
    int archivedCount = -1;
    if (restoredStore.initialize(databasePath)) {
        archivedCount = restoredStore.loadRawTickParameterSensitivityResults(1000).size();
        restoredStore.close();
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("requestedRuns"),
                   repetitions * symbols.size() * strategyIds.size());
    summary.insert(QStringLiteral("succeededRuns"), succeeded);
    summary.insert(QStringLiteral("failedRuns"), failed);
    summary.insert(QStringLiteral("repeatComparisons"), repeatComparisons);
    summary.insert(QStringLiteral("stableSelections"), stableSelections);
    summary.insert(QStringLiteral("allRepeatsStable"),
                   repeatComparisons > 0 && stableSelections == repeatComparisons);
    summary.insert(QStringLiteral("archivedResults"), archivedCount);
    summary.insert(QStringLiteral("wallMilliseconds"),
                   static_cast<double>(wallTimer.elapsed()));
    summary.insert(QStringLiteral("requestTimeoutSeconds"), requestTimeoutSeconds);
    summary.insert(QStringLiteral("abortedAfterCancellationTimeout"),
                   abortedAfterCancellationAcknowledgementTimeout);
    summary.insert(QStringLiteral("abortedAfterRequestTimeout"),
                   abortRemainingRequests);
    summary.insert(QStringLiteral("loadMilliseconds"), timingSummary(loadTimings));
    summary.insert(QStringLiteral("scanMilliseconds"), timingSummary(scanTimings));
    summary.insert(QStringLiteral("totalMilliseconds"), timingSummary(totalTimings));

    QJsonObject report;
    report.insert(QStringLiteral("schemaVersion"), 2);
    report.insert(QStringLiteral("source"), QStringLiteral("verified_nas_node_sync"));
    report.insert(QStringLiteral("startDate"), firstDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("endDate"), lastDate.toString(Qt::ISODate));
    report.insert(QStringLiteral("validationTradingDays"), validationDays);
    report.insert(QStringLiteral("walkForwardFoldCount"), foldCount);
    report.insert(QStringLiteral("compactGrid"), compactGrid);
    report.insert(QStringLiteral("repetitions"), repetitions);
    report.insert(QStringLiteral("symbols"), QJsonArray::fromStringList(symbols));
    report.insert(QStringLiteral("strategies"), QJsonArray::fromStringList(strategyIds));
    report.insert(QStringLiteral("summary"), summary);
    report.insert(QStringLiteral("runs"), runs);

    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0) {
        qCritical("无法写入结果 JSON");
        return 5;
    }
    output.close();
    QTextStream(stdout) << QJsonDocument(summary).toJson(QJsonDocument::Compact)
                        << Qt::endl;
    return failed == 0 && repeatComparisons > 0
        && stableSelections == repeatComparisons ? 0 : 6;
}
