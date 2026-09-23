#include "services/RawTickParameterSensitivityScanner.h"

#include "services/BacktestEngine.h"
#include "strategy/BacktestStrategyRegistry.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

constexpr auto kSelectionCriterion =
    "training_return_desc_drawdown_asc_parameters_v1";

QByteArray canonicalJson(const QVariant &value)
{
    return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
}

QString sha256(const QByteArray &payload)
{
    return QStringLiteral("sha256:%1").arg(QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()));
}

QVector<RawTickDayFingerprint> observedDays(const QVector<RawTradeTick> &ticks)
{
    static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    QVector<RawTickDayFingerprint> result;
    for (const RawTradeTick &tick : ticks) {
        const QDate date = tick.timestamp.toTimeZone(shanghai).date();
        if (!date.isValid()) continue;
        if (result.isEmpty() || result.last().tradingDate != date) {
            RawTickDayFingerprint day;
            day.tradingDate = date;
            result.append(day);
        }
        ++result.last().rawCount;
    }
    return result;
}

QVector<RawTradeTick> ticksForDates(const QVector<RawTradeTick> &ticks,
                                    const QDate &first, const QDate &last)
{
    static const QTimeZone shanghai(QByteArrayLiteral("Asia/Shanghai"));
    QVector<RawTradeTick> result;
    for (const RawTradeTick &tick : ticks) {
        const QDate date = tick.timestamp.toTimeZone(shanghai).date();
        if (date >= first && date <= last) result.append(tick);
    }
    return result;
}

int tickCountForDates(const QVector<RawTickDayFingerprint> &days,
                      const QDate &first, const QDate &last)
{
    qint64 count = 0;
    for (const RawTickDayFingerprint &day : days) {
        if (day.tradingDate >= first && day.tradingDate <= last)
            count += day.rawCount;
    }
    return static_cast<int>(count);
}

void populateValidationTradeStatistics(
    const QVector<BacktestTrade> &trades,
    RawTickParameterSensitivityFold *fold)
{
    if (!fold) return;
    for (const BacktestTrade &trade : trades) {
        if (trade.netPnl > 0.0) {
            ++fold->validationWinningTrades;
            fold->validationGrossProfit += trade.netPnl;
        } else if (trade.netPnl < 0.0) {
            ++fold->validationLosingTrades;
            fold->validationGrossLossAbsolute += std::abs(trade.netPnl);
        }
    }
    if (fold->validationWinningTrades > 0) {
        fold->validationAverageWinningNetPnl = fold->validationGrossProfit
            / static_cast<double>(fold->validationWinningTrades);
    }
    if (fold->validationLosingTrades > 0) {
        fold->validationAverageLosingNetPnlAbsolute =
            fold->validationGrossLossAbsolute
            / static_cast<double>(fold->validationLosingTrades);
    }
    fold->validationPayoffRatioAvailable =
        fold->validationWinningTrades > 0
        && fold->validationLosingTrades > 0
        && fold->validationAverageLosingNetPnlAbsolute > 0.0;
    if (fold->validationPayoffRatioAvailable) {
        fold->validationPayoffRatio = fold->validationAverageWinningNetPnl
            / fold->validationAverageLosingNetPnlAbsolute;
    }
}

std::optional<BacktestResult> runExactRawBacktest(
    BacktestConfig config, QVector<RawTradeTick> ticks, QString *error)
{
    if (error) error->clear();
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
    config.dataMode = BacktestDataMode::RawTicks;
    config.frequency = QStringLiteral("tick");
    config.periodSeconds = 0;
    config.sampleFrom = {};
    config.sampleTo = {};
    if (!engine.start(config, std::move(ticks))) {
        if (error) *error = engineError.isEmpty()
            ? QStringLiteral("原始 Tick 参数组合无法启动") : engineError;
        return std::nullopt;
    }
    loop.exec();
    if (!completed) {
        if (error) *error = engineError.isEmpty()
            ? QStringLiteral("原始 Tick 参数组合执行失败") : engineError;
        return std::nullopt;
    }
    return engine.result();
}

std::optional<RawTickParameterSensitivityFold> scanFold(
    const RawTickParameterSensitivityRequest &request,
    const BacktestStrategyRegistry &registry,
    const QVector<QVariantMap> &rawCombinations,
    const QVector<RawTickDayFingerprint> &days,
    const RawTickParameterSensitivityScanner::SegmentRunner &segmentRunner,
    const QDate &trainingStartDate,
    const QDate &trainingEndDate,
    const QDate &validationStartDate,
    const QDate &validationEndDate,
    int foldNumber,
    const std::atomic_bool *cancelFlag,
    QString *candidateSetSha256,
    QString *error)
{
    RawTickParameterSensitivityFold fold;
    fold.foldNumber = foldNumber;
    fold.trainingStartDate = trainingStartDate;
    fold.trainingEndDate = trainingEndDate;
    fold.validationStartDate = validationStartDate;
    fold.validationEndDate = validationEndDate;
    fold.trainingActualTicks = tickCountForDates(
        days, trainingStartDate, trainingEndDate);
    fold.validationActualTicks = tickCountForDates(
        days, validationStartDate, validationEndDate);

    QSet<QByteArray> seenParameters;
    for (const QVariantMap &combination : rawCombinations) {
        if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
            if (error) *error = QStringLiteral("原始 Tick 参数敏感性已取消");
            return std::nullopt;
        }
        QVariantMap parameters = request.baseParameters;
        for (auto it = combination.cbegin(); it != combination.cend(); ++it)
            parameters.insert(it.key(), it.value());
        QVariantMap normalized;
        QString parameterError;
        if (!registry.normalizeParameters(request.strategyId, parameters,
                                          &normalized, &parameterError)) {
            if (error) *error = QStringLiteral("参数组合无效：%1").arg(parameterError);
            return std::nullopt;
        }
        const QByteArray key = canonicalJson(normalized);
        if (seenParameters.contains(key)) continue;
        seenParameters.insert(key);

        BacktestConfig config = request.baseConfig;
        config.strategyId = request.strategyId;
        config.strategyParameters = normalized;
        config.taskId = QStringLiteral("raw-tick-sensitivity:fold:%1:training:%2")
                            .arg(foldNumber)
                            .arg(fold.candidates.size() + 1);
        QString runError;
        const auto training = segmentRunner(
            config, trainingStartDate, trainingEndDate, &runError);
        if (!training) {
            if (error) *error = runError;
            return std::nullopt;
        }
        if (training->config.actualBars != fold.trainingActualTicks
            || !training->config.dataVersion.startsWith(QStringLiteral("sha256:"))) {
            if (error) *error = QStringLiteral("训练段执行身份或 Tick 数量无效");
            return std::nullopt;
        }
        if (fold.trainingDataVersion.isEmpty()) {
            fold.trainingDataVersion = training->config.dataVersion;
        } else if (fold.trainingDataVersion != training->config.dataVersion) {
            if (error) *error = QStringLiteral("同一训练段的候选数据版本不一致");
            return std::nullopt;
        }
        RawTickParameterSensitivityCandidate candidate;
        candidate.parameters = training->config.strategyParameters;
        candidate.trainingSummary = training->summary;
        fold.candidates.append(candidate);
    }
    if (fold.candidates.isEmpty()) {
        if (error) *error = QStringLiteral("参数候选集归一化后为空");
        return std::nullopt;
    }

    std::sort(fold.candidates.begin(), fold.candidates.end(),
              [](const RawTickParameterSensitivityCandidate &left,
                 const RawTickParameterSensitivityCandidate &right) {
        if (!qFuzzyCompare(left.trainingSummary.totalReturnPercent + 1.0,
                           right.trainingSummary.totalReturnPercent + 1.0)) {
            return left.trainingSummary.totalReturnPercent
                > right.trainingSummary.totalReturnPercent;
        }
        if (!qFuzzyCompare(left.trainingSummary.maxDrawdownPercent + 1.0,
                           right.trainingSummary.maxDrawdownPercent + 1.0)) {
            return left.trainingSummary.maxDrawdownPercent
                < right.trainingSummary.maxDrawdownPercent;
        }
        return canonicalJson(left.parameters) < canonicalJson(right.parameters);
    });
    QVariantList candidateIdentities;
    for (int index = 0; index < fold.candidates.size(); ++index) {
        fold.candidates[index].rank = index + 1;
        candidateIdentities.append(fold.candidates.at(index).parameters);
    }
    std::sort(candidateIdentities.begin(), candidateIdentities.end(),
              [](const QVariant &left, const QVariant &right) {
        return canonicalJson(left) < canonicalJson(right);
    });
    const QString foldCandidateSetSha256 = sha256(
        QByteArrayLiteral("pptrader-raw-tick-candidate-set-v1\n")
        + canonicalJson(candidateIdentities));
    if (candidateSetSha256) {
        if (!candidateSetSha256->isEmpty()
            && *candidateSetSha256 != foldCandidateSetSha256) {
            if (error) *error = QStringLiteral("外层折的候选集身份不一致");
            return std::nullopt;
        }
        *candidateSetSha256 = foldCandidateSetSha256;
    }
    fold.selectedParameters = fold.candidates.first().parameters;

    BacktestConfig validationConfig = request.baseConfig;
    validationConfig.strategyId = request.strategyId;
    validationConfig.strategyParameters = fold.selectedParameters;
    validationConfig.taskId = QStringLiteral("raw-tick-sensitivity:fold:%1:validation")
                                  .arg(foldNumber);
    QString validationError;
    const auto validation = segmentRunner(
        validationConfig, validationStartDate, validationEndDate,
        &validationError);
    if (!validation) {
        if (error) *error = validationError;
        return std::nullopt;
    }
    if (validation->config.actualBars != fold.validationActualTicks
        || !validation->config.dataVersion.startsWith(QStringLiteral("sha256:"))) {
        if (error) *error = QStringLiteral("验证段执行身份或 Tick 数量无效");
        return std::nullopt;
    }
    fold.validationDataVersion = validation->config.dataVersion;
    fold.validationSummary = validation->summary;
    populateValidationTradeStatistics(validation->trades, &fold);
    return fold;
}

} // namespace

std::optional<RawTickParameterSensitivityResult>
RawTickParameterSensitivityScanner::scan(
    const RawTickParameterSensitivityRequest &request,
    QVector<RawTradeTick> ticks,
    const std::atomic_bool *cancelFlag,
    QString *error)
{
    ticks.erase(std::remove_if(ticks.begin(), ticks.end(), [&](const RawTradeTick &tick) {
        return tick.instrument.symbol != request.baseConfig.instrument.symbol
            || !tick.timestamp.isValid() || tick.price <= 0.0
            || tick.volumeShares < 0.0;
    }), ticks.end());
    std::sort(ticks.begin(), ticks.end(), [](const RawTradeTick &left,
                                             const RawTradeTick &right) {
        if (left.timestamp != right.timestamp) return left.timestamp < right.timestamp;
        return left.sourceLine < right.sourceLine;
    });
    const QVector<RawTickDayFingerprint> days = observedDays(ticks);
    const SegmentRunner runner = [&ticks](BacktestConfig config,
                                          const QDate &firstDate,
                                          const QDate &lastDate,
                                          QString *runError) {
        return runExactRawBacktest(
            std::move(config), ticksForDates(ticks, firstDate, lastDate),
            runError);
    };
    return scanSegments(request, days, runner, cancelFlag, error);
}

std::optional<RawTickParameterSensitivityResult>
RawTickParameterSensitivityScanner::scanSegments(
    const RawTickParameterSensitivityRequest &request,
    QVector<RawTickDayFingerprint> days,
    const SegmentRunner &segmentRunner,
    const std::atomic_bool *cancelFlag,
    QString *error)
{
    if (error) error->clear();
    BacktestStrategyRegistry registry;
    if (request.strategyId.trimmed().isEmpty()
        || !registry.supportsDataMode(request.strategyId,
                                      QStringLiteral("raw_ticks"))
        || request.baseConfig.instrument.market != Market::AShare
        || request.baseConfig.instrument.symbol.isEmpty()
        || request.baseConfig.initialCapital <= 0.0
        || request.baseConfig.commissionRate < 0.0
        || request.baseConfig.slippageRate < 0.0
        || request.baseConfig.lotSize <= 0
        || request.validationTradingDays < 2
        || request.walkForwardFoldCount < 1
        || request.walkForwardFoldCount > 20
        || request.maximumCombinations < 1
        || !segmentRunner) {
        if (error) *error = QStringLiteral("原始 Tick 参数敏感性请求无效");
        return std::nullopt;
    }

    days.erase(std::remove_if(days.begin(), days.end(),
                              [](const RawTickDayFingerprint &day) {
        return !day.tradingDate.isValid() || day.rawCount <= 0;
    }), days.end());
    std::sort(days.begin(), days.end(), [](const RawTickDayFingerprint &left,
                                           const RawTickDayFingerprint &right) {
        return left.tradingDate < right.tradingDate;
    });
    QVector<QDate> dates;
    dates.reserve(days.size());
    for (const RawTickDayFingerprint &day : days) {
        if (!dates.isEmpty() && dates.last() == day.tradingDate) {
            if (error) *error = QStringLiteral("原始 Tick 逐日身份包含重复交易日");
            return std::nullopt;
        }
        dates.append(day.tradingDate);
    }
    const int requiredValidationDays = request.validationTradingDays
        * request.walkForwardFoldCount;
    if (dates.size() < requiredValidationDays + 2) {
        if (error) {
            *error = QStringLiteral(
                "参数敏感性至少需要 2 个初始训练日和 %1 折×%2 个验证日；当前只有 %3 日")
                         .arg(request.walkForwardFoldCount)
                         .arg(request.validationTradingDays)
                         .arg(dates.size());
        }
        return std::nullopt;
    }

    const QVector<QVariantMap> rawCombinations =
        ParameterGridScanner::expandGrid(request.axes);
    if (rawCombinations.isEmpty()
        || rawCombinations.size() > request.maximumCombinations) {
        if (error) {
            *error = rawCombinations.isEmpty()
                ? QStringLiteral("参数候选集为空")
                : QStringLiteral("参数组合过多：%1（上限 %2）")
                      .arg(rawCombinations.size()).arg(request.maximumCombinations);
        }
        return std::nullopt;
    }

    RawTickParameterSensitivityResult result;
    result.analysisMode = request.walkForwardFoldCount > 1
        ? QStringLiteral("nested_walk_forward")
        : QStringLiteral("single_holdout");
    result.walkForwardFoldCount = request.walkForwardFoldCount;
    result.strategyId = request.strategyId;
    result.instrument = request.baseConfig.instrument;
    result.baseParameters = request.baseParameters;
    result.initialCapital = request.baseConfig.initialCapital;
    result.commissionRate = request.baseConfig.commissionRate;
    result.slippageRate = request.baseConfig.slippageRate;
    result.lotSize = request.baseConfig.lotSize;
    result.datasetVersion = request.datasetVersion;
    result.selectionCriterion = QString::fromLatin1(kSelectionCriterion);
    const int initialTrainingDays = dates.size() - requiredValidationDays;
    QVector<double> validationReturns;
    for (int foldIndex = 0; foldIndex < request.walkForwardFoldCount; ++foldIndex) {
        const int validationStartIndex = initialTrainingDays
            + foldIndex * request.validationTradingDays;
        const int validationEndIndex = validationStartIndex
            + request.validationTradingDays - 1;
        auto fold = scanFold(
            request, registry, rawCombinations, days, segmentRunner,
            dates.first(), dates.at(validationStartIndex - 1),
            dates.at(validationStartIndex), dates.at(validationEndIndex),
            foldIndex + 1, cancelFlag, &result.candidateSetSha256, error);
        if (!fold) return std::nullopt;
        validationReturns.append(fold->validationSummary.totalReturnPercent);
        if (fold->validationSummary.totalReturnPercent > 0.0)
            ++result.positiveValidationFoldCount;
        else
            ++result.nonPositiveValidationFoldCount;
        result.folds.append(*fold);
    }

    const RawTickParameterSensitivityFold &finalFold = result.folds.last();
    result.trainingStartDate = finalFold.trainingStartDate;
    result.trainingEndDate = finalFold.trainingEndDate;
    result.validationStartDate = finalFold.validationStartDate;
    result.validationEndDate = finalFold.validationEndDate;
    result.trainingActualTicks = finalFold.trainingActualTicks;
    result.validationActualTicks = finalFold.validationActualTicks;
    result.trainingDataVersion = finalFold.trainingDataVersion;
    result.validationDataVersion = finalFold.validationDataVersion;
    result.selectedParameters = finalFold.selectedParameters;
    result.validationSummary = finalFold.validationSummary;
    result.candidates = finalFold.candidates;

    double validationReturnSum = 0.0;
    for (double value : validationReturns) validationReturnSum += value;
    result.meanValidationReturnPercent = validationReturnSum
        / static_cast<double>(validationReturns.size());
    std::sort(validationReturns.begin(), validationReturns.end());
    result.minimumValidationReturnPercent = validationReturns.first();
    result.maximumValidationReturnPercent = validationReturns.last();
    const int middle = validationReturns.size() / 2;
    result.medianValidationReturnPercent = validationReturns.size() % 2 == 0
        ? (validationReturns.at(middle - 1) + validationReturns.at(middle)) / 2.0
        : validationReturns.at(middle);

    for (const RawTickParameterSensitivityFold &fold : result.folds) {
        result.validationWinningTrades += fold.validationWinningTrades;
        result.validationLosingTrades += fold.validationLosingTrades;
        result.validationTotalTrades += fold.validationSummary.totalTrades;
        result.validationGrossProfit += fold.validationGrossProfit;
        result.validationGrossLossAbsolute += fold.validationGrossLossAbsolute;
        const QByteArray key = canonicalJson(fold.selectedParameters);
        int distributionIndex = -1;
        for (int index = 0; index < result.parameterSelectionDistribution.size(); ++index) {
            if (canonicalJson(result.parameterSelectionDistribution.at(index)
                                  .toMap().value(QStringLiteral("parameters"))) == key) {
                distributionIndex = index;
                break;
            }
        }
        if (distributionIndex < 0) {
            result.parameterSelectionDistribution.append(QVariantMap {
                {QStringLiteral("parameters"), fold.selectedParameters},
                {QStringLiteral("count"), 1},
                {QStringLiteral("foldNumbers"), QVariantList {fold.foldNumber}}});
        } else {
            QVariantMap entry = result.parameterSelectionDistribution
                                    .at(distributionIndex).toMap();
            entry.insert(QStringLiteral("count"),
                         entry.value(QStringLiteral("count")).toInt() + 1);
            QVariantList foldNumbers = entry.value(QStringLiteral("foldNumbers")).toList();
            foldNumbers.append(fold.foldNumber);
            entry.insert(QStringLiteral("foldNumbers"), foldNumbers);
            result.parameterSelectionDistribution[distributionIndex] = entry;
        }
    }
    std::sort(result.parameterSelectionDistribution.begin(),
              result.parameterSelectionDistribution.end(),
              [](const QVariant &left, const QVariant &right) {
        const QVariantMap leftMap = left.toMap();
        const QVariantMap rightMap = right.toMap();
        if (leftMap.value(QStringLiteral("count")).toInt()
            != rightMap.value(QStringLiteral("count")).toInt()) {
            return leftMap.value(QStringLiteral("count")).toInt()
                > rightMap.value(QStringLiteral("count")).toInt();
        }
        return canonicalJson(leftMap.value(QStringLiteral("parameters")))
            < canonicalJson(rightMap.value(QStringLiteral("parameters")));
    });
    result.parameterSelectionVariantCount =
        result.parameterSelectionDistribution.size();
    if (result.validationWinningTrades > 0) {
        result.validationAverageWinningNetPnl = result.validationGrossProfit
            / static_cast<double>(result.validationWinningTrades);
    }
    if (result.validationLosingTrades > 0) {
        result.validationAverageLosingNetPnlAbsolute =
            result.validationGrossLossAbsolute
            / static_cast<double>(result.validationLosingTrades);
    }
    result.validationPayoffRatioAvailable = result.validationWinningTrades > 0
        && result.validationLosingTrades > 0
        && result.validationAverageLosingNetPnlAbsolute > 0.0;
    if (result.validationPayoffRatioAvailable) {
        result.validationPayoffRatio = result.validationAverageWinningNetPnl
            / result.validationAverageLosingNetPnlAbsolute;
    }

    const QVariantMap identity {
        {QStringLiteral("strategyId"), result.strategyId},
        {QStringLiteral("symbol"), result.instrument.symbol},
        {QStringLiteral("baseParameters"), result.baseParameters},
        {QStringLiteral("initialCapital"), result.initialCapital},
        {QStringLiteral("commissionRate"), result.commissionRate},
        {QStringLiteral("slippageRate"), result.slippageRate},
        {QStringLiteral("lotSize"), result.lotSize},
        {QStringLiteral("candidateSetSha256"), result.candidateSetSha256},
        {QStringLiteral("selectionCriterion"), result.selectionCriterion},
        {QStringLiteral("trainingStartDate"), result.trainingStartDate.toString(Qt::ISODate)},
        {QStringLiteral("trainingEndDate"), result.trainingEndDate.toString(Qt::ISODate)},
        {QStringLiteral("validationStartDate"), result.validationStartDate.toString(Qt::ISODate)},
        {QStringLiteral("validationEndDate"), result.validationEndDate.toString(Qt::ISODate)},
        {QStringLiteral("trainingDataVersion"), result.trainingDataVersion},
        {QStringLiteral("validationDataVersion"), result.validationDataVersion},
        {QStringLiteral("datasetVersion"), result.datasetVersion},
        {QStringLiteral("selectedParameters"), result.selectedParameters}};
    if (request.walkForwardFoldCount == 1) {
        result.identitySha256 = sha256(
            QByteArrayLiteral("pptrader-raw-tick-sensitivity-v1\n")
            + canonicalJson(identity));
    } else {
        QVariantList foldIdentities;
        for (const RawTickParameterSensitivityFold &fold : result.folds) {
            foldIdentities.append(QVariantMap {
                {QStringLiteral("foldNumber"), fold.foldNumber},
                {QStringLiteral("trainingStartDate"),
                 fold.trainingStartDate.toString(Qt::ISODate)},
                {QStringLiteral("trainingEndDate"),
                 fold.trainingEndDate.toString(Qt::ISODate)},
                {QStringLiteral("validationStartDate"),
                 fold.validationStartDate.toString(Qt::ISODate)},
                {QStringLiteral("validationEndDate"),
                 fold.validationEndDate.toString(Qt::ISODate)},
                {QStringLiteral("trainingDataVersion"), fold.trainingDataVersion},
                {QStringLiteral("validationDataVersion"), fold.validationDataVersion},
                {QStringLiteral("selectedParameters"), fold.selectedParameters}});
        }
        QVariantMap nestedIdentity = identity;
        nestedIdentity.insert(QStringLiteral("analysisMode"), result.analysisMode);
        nestedIdentity.insert(QStringLiteral("walkForwardFoldCount"),
                              result.walkForwardFoldCount);
        nestedIdentity.insert(QStringLiteral("validationTradingDays"),
                              request.validationTradingDays);
        nestedIdentity.insert(QStringLiteral("folds"), foldIdentities);
        result.identitySha256 = sha256(
            QByteArrayLiteral("pptrader-raw-tick-nested-walk-forward-v1\n")
            + canonicalJson(nestedIdentity));
    }
    return result;
}

} // namespace pp
