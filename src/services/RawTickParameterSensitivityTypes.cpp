#include "services/RawTickParameterSensitivityScanner.h"

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

QVariantMap summaryMap(const PerformanceSummary &summary)
{
    return {{QStringLiteral("initialCapital"), summary.initialCapital},
            {QStringLiteral("finalEquity"), summary.finalEquity},
            {QStringLiteral("totalReturnPercent"), summary.totalReturnPercent},
            {QStringLiteral("maxDrawdownPercent"), summary.maxDrawdownPercent},
            {QStringLiteral("winRatePercent"), summary.winRatePercent},
            {QStringLiteral("sharpeRatio"), summary.sharpeRatio},
            {QStringLiteral("totalCommission"), summary.totalCommission},
            {QStringLiteral("totalTrades"), summary.totalTrades},
            {QStringLiteral("winningTrades"), summary.winningTrades},
            {QStringLiteral("losingTrades"), summary.losingTrades}};
}

PerformanceSummary summaryFromMap(const QVariantMap &value)
{
    PerformanceSummary summary;
    summary.initialCapital = value.value(QStringLiteral("initialCapital")).toDouble();
    summary.finalEquity = value.value(QStringLiteral("finalEquity")).toDouble();
    summary.totalReturnPercent = value.value(QStringLiteral("totalReturnPercent")).toDouble();
    summary.maxDrawdownPercent = value.value(QStringLiteral("maxDrawdownPercent")).toDouble();
    summary.winRatePercent = value.value(QStringLiteral("winRatePercent")).toDouble();
    summary.sharpeRatio = value.value(QStringLiteral("sharpeRatio")).toDouble();
    summary.totalCommission = value.value(QStringLiteral("totalCommission")).toDouble();
    summary.totalTrades = value.value(QStringLiteral("totalTrades")).toInt();
    summary.winningTrades = value.value(QStringLiteral("winningTrades")).toInt();
    summary.losingTrades = value.value(QStringLiteral("losingTrades")).toInt();
    return summary;
}

QDate dateFromVariant(const QVariant &value)
{
    const QDate direct = value.toDate();
    return direct.isValid() ? direct
                            : QDate::fromString(value.toString(), Qt::ISODate);
}

bool isSha256(const QString &value)
{
    const QString digest = value.startsWith(QStringLiteral("sha256:"))
        ? value.mid(7) : value;
    if (digest.size() != 64) return false;
    for (const QChar character : digest) {
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9')
              || (code >= 'a' && code <= 'f')
              || (code >= 'A' && code <= 'F'))) return false;
    }
    return true;
}

bool approximatelyEqual(double left, double right)
{
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= scale * 1e-9;
}

bool validationTradeStatisticsAreConsistent(
    int totalTrades,
    int winningTrades,
    int losingTrades,
    double grossProfit,
    double grossLossAbsolute,
    double averageWinningNetPnl,
    double averageLosingNetPnlAbsolute,
    double payoffRatio,
    bool payoffRatioAvailable)
{
    if (totalTrades < 0 || winningTrades < 0 || losingTrades < 0
        || winningTrades + losingTrades > totalTrades
        || !std::isfinite(grossProfit) || grossProfit < 0.0
        || !std::isfinite(grossLossAbsolute) || grossLossAbsolute < 0.0
        || !std::isfinite(averageWinningNetPnl)
        || averageWinningNetPnl < 0.0
        || !std::isfinite(averageLosingNetPnlAbsolute)
        || averageLosingNetPnlAbsolute < 0.0
        || !std::isfinite(payoffRatio) || payoffRatio < 0.0) {
        return false;
    }
    if (!payoffRatioAvailable) return approximatelyEqual(payoffRatio, 0.0);
    if (winningTrades <= 0 || losingTrades <= 0
        || grossProfit <= 0.0 || grossLossAbsolute <= 0.0
        || averageWinningNetPnl <= 0.0
        || averageLosingNetPnlAbsolute <= 0.0) {
        return false;
    }
    const double expectedAverageWin = grossProfit
        / static_cast<double>(winningTrades);
    const double expectedAverageLoss = grossLossAbsolute
        / static_cast<double>(losingTrades);
    const double expectedPayoff = expectedAverageWin / expectedAverageLoss;
    return approximatelyEqual(averageWinningNetPnl, expectedAverageWin)
        && approximatelyEqual(averageLosingNetPnlAbsolute, expectedAverageLoss)
        && approximatelyEqual(payoffRatio, expectedPayoff);
}

} // namespace

QVariantMap RawTickParameterSensitivityCandidate::toVariantMap() const
{
    return {{QStringLiteral("rank"), rank},
            {QStringLiteral("parameters"), parameters},
            {QStringLiteral("trainingSummary"), summaryMap(trainingSummary)},
            {QStringLiteral("trainingReturnPercent"),
             trainingSummary.totalReturnPercent},
            {QStringLiteral("trainingMaxDrawdownPercent"),
             trainingSummary.maxDrawdownPercent},
            {QStringLiteral("trainingTrades"), trainingSummary.totalTrades}};
}

QVariantMap RawTickParameterSensitivityFold::toVariantMap() const
{
    QVariantList candidateMaps;
    candidateMaps.reserve(candidates.size());
    for (const RawTickParameterSensitivityCandidate &candidate : candidates)
        candidateMaps.append(candidate.toVariantMap());
    return {{QStringLiteral("foldNumber"), foldNumber},
            {QStringLiteral("trainingStartDate"), trainingStartDate},
            {QStringLiteral("trainingEndDate"), trainingEndDate},
            {QStringLiteral("validationStartDate"), validationStartDate},
            {QStringLiteral("validationEndDate"), validationEndDate},
            {QStringLiteral("trainingActualTicks"), trainingActualTicks},
            {QStringLiteral("validationActualTicks"), validationActualTicks},
            {QStringLiteral("trainingDataVersion"), trainingDataVersion},
            {QStringLiteral("validationDataVersion"), validationDataVersion},
            {QStringLiteral("selectedParameters"), selectedParameters},
            {QStringLiteral("validationSummary"), summaryMap(validationSummary)},
            {QStringLiteral("validationReturnPercent"),
             validationSummary.totalReturnPercent},
            {QStringLiteral("validationMaxDrawdownPercent"),
             validationSummary.maxDrawdownPercent},
            {QStringLiteral("validationTrades"), validationSummary.totalTrades},
            {QStringLiteral("validationWinningTrades"), validationWinningTrades},
            {QStringLiteral("validationLosingTrades"), validationLosingTrades},
            {QStringLiteral("validationGrossProfit"), validationGrossProfit},
            {QStringLiteral("validationGrossLossAbsolute"),
             validationGrossLossAbsolute},
            {QStringLiteral("validationAverageWinningNetPnl"),
             validationAverageWinningNetPnl},
            {QStringLiteral("validationAverageLosingNetPnlAbsolute"),
             validationAverageLosingNetPnlAbsolute},
            {QStringLiteral("validationPayoffRatio"), validationPayoffRatio},
            {QStringLiteral("validationPayoffRatioAvailable"),
             validationPayoffRatioAvailable},
            {QStringLiteral("candidates"), candidateMaps}};
}

std::optional<RawTickParameterSensitivityFold>
RawTickParameterSensitivityFold::fromVariantMap(const QVariantMap &value)
{
    RawTickParameterSensitivityFold fold;
    fold.foldNumber = value.value(QStringLiteral("foldNumber")).toInt();
    fold.trainingStartDate = dateFromVariant(
        value.value(QStringLiteral("trainingStartDate")));
    fold.trainingEndDate = dateFromVariant(
        value.value(QStringLiteral("trainingEndDate")));
    fold.validationStartDate = dateFromVariant(
        value.value(QStringLiteral("validationStartDate")));
    fold.validationEndDate = dateFromVariant(
        value.value(QStringLiteral("validationEndDate")));
    fold.trainingActualTicks = value.value(
        QStringLiteral("trainingActualTicks")).toInt();
    fold.validationActualTicks = value.value(
        QStringLiteral("validationActualTicks")).toInt();
    fold.trainingDataVersion = value.value(
        QStringLiteral("trainingDataVersion")).toString();
    fold.validationDataVersion = value.value(
        QStringLiteral("validationDataVersion")).toString();
    fold.selectedParameters = value.value(
        QStringLiteral("selectedParameters")).toMap();
    fold.validationSummary = summaryFromMap(
        value.value(QStringLiteral("validationSummary")).toMap());
    fold.validationWinningTrades = value.value(
        QStringLiteral("validationWinningTrades"),
        fold.validationSummary.winningTrades).toInt();
    fold.validationLosingTrades = value.value(
        QStringLiteral("validationLosingTrades"),
        fold.validationSummary.losingTrades).toInt();
    // 旧归档及早期调用方只填写 PerformanceSummary。序列化结构后来加入
    // 独立盈亏统计键后，默认 0 会遮蔽 summary 中的真实成交计数。
    if (fold.validationWinningTrades == 0
        && fold.validationLosingTrades == 0
        && (fold.validationSummary.winningTrades > 0
            || fold.validationSummary.losingTrades > 0)) {
        fold.validationWinningTrades = fold.validationSummary.winningTrades;
        fold.validationLosingTrades = fold.validationSummary.losingTrades;
    }
    fold.validationGrossProfit = value.value(
        QStringLiteral("validationGrossProfit")).toDouble();
    fold.validationGrossLossAbsolute = value.value(
        QStringLiteral("validationGrossLossAbsolute")).toDouble();
    fold.validationAverageWinningNetPnl = value.value(
        QStringLiteral("validationAverageWinningNetPnl")).toDouble();
    fold.validationAverageLosingNetPnlAbsolute = value.value(
        QStringLiteral("validationAverageLosingNetPnlAbsolute")).toDouble();
    fold.validationPayoffRatio = value.value(
        QStringLiteral("validationPayoffRatio")).toDouble();
    fold.validationPayoffRatioAvailable = value.value(
        QStringLiteral("validationPayoffRatioAvailable"), false).toBool();
    for (const QVariant &candidateValue : value.value(
             QStringLiteral("candidates")).toList()) {
        const QVariantMap candidateMap = candidateValue.toMap();
        RawTickParameterSensitivityCandidate candidate;
        candidate.rank = candidateMap.value(QStringLiteral("rank")).toInt();
        candidate.parameters = candidateMap.value(
            QStringLiteral("parameters")).toMap();
        candidate.trainingSummary = summaryFromMap(candidateMap.value(
            QStringLiteral("trainingSummary")).toMap());
        fold.candidates.append(candidate);
    }
    if (fold.foldNumber < 1 || !fold.trainingStartDate.isValid()
        || fold.trainingEndDate < fold.trainingStartDate
        || fold.validationStartDate <= fold.trainingEndDate
        || fold.validationEndDate < fold.validationStartDate
        || fold.trainingActualTicks <= 0 || fold.validationActualTicks <= 0
        || !isSha256(fold.trainingDataVersion)
        || !isSha256(fold.validationDataVersion)
        || fold.selectedParameters.isEmpty() || fold.candidates.isEmpty()
        || fold.validationWinningTrades != fold.validationSummary.winningTrades
        || fold.validationLosingTrades != fold.validationSummary.losingTrades
        || !validationTradeStatisticsAreConsistent(
            fold.validationSummary.totalTrades,
            fold.validationWinningTrades,
            fold.validationLosingTrades,
            fold.validationGrossProfit,
            fold.validationGrossLossAbsolute,
            fold.validationAverageWinningNetPnl,
            fold.validationAverageLosingNetPnlAbsolute,
            fold.validationPayoffRatio,
            fold.validationPayoffRatioAvailable)) {
        return std::nullopt;
    }
    return fold;
}

QVariantMap RawTickParameterSensitivityResult::toVariantMap() const
{
    QVariantList candidateMaps;
    candidateMaps.reserve(candidates.size());
    for (const RawTickParameterSensitivityCandidate &candidate : candidates)
        candidateMaps.append(candidate.toVariantMap());
    QVariantList foldMaps;
    foldMaps.reserve(folds.size());
    for (const RawTickParameterSensitivityFold &fold : folds)
        foldMaps.append(fold.toVariantMap());
    return {{QStringLiteral("resultId"), resultId},
            {QStringLiteral("completedAt"), completedAt},
            {QStringLiteral("analysisMode"), analysisMode},
            {QStringLiteral("walkForwardFoldCount"), walkForwardFoldCount},
            {QStringLiteral("strategyId"), strategyId},
            {QStringLiteral("symbol"), instrument.symbol},
            {QStringLiteral("name"), instrument.name},
            {QStringLiteral("market"), static_cast<int>(instrument.market)},
            {QStringLiteral("venue"), instrument.venue},
            {QStringLiteral("baseParameters"), baseParameters},
            {QStringLiteral("initialCapital"), initialCapital},
            {QStringLiteral("commissionRate"), commissionRate},
            {QStringLiteral("slippageRate"), slippageRate},
            {QStringLiteral("lotSize"), lotSize},
            {QStringLiteral("trainingStartDate"), trainingStartDate},
            {QStringLiteral("trainingEndDate"), trainingEndDate},
            {QStringLiteral("validationStartDate"), validationStartDate},
            {QStringLiteral("validationEndDate"), validationEndDate},
            {QStringLiteral("trainingActualTicks"), trainingActualTicks},
            {QStringLiteral("validationActualTicks"), validationActualTicks},
            {QStringLiteral("loadMilliseconds"), loadMilliseconds},
            {QStringLiteral("scanMilliseconds"), scanMilliseconds},
            {QStringLiteral("trainingDataVersion"), trainingDataVersion},
            {QStringLiteral("validationDataVersion"), validationDataVersion},
            {QStringLiteral("datasetVersion"), datasetVersion},
            {QStringLiteral("candidateSetSha256"), candidateSetSha256},
            {QStringLiteral("identitySha256"), identitySha256},
            {QStringLiteral("selectionCriterion"), selectionCriterion},
            {QStringLiteral("selectedParameters"), selectedParameters},
            {QStringLiteral("validationSummary"), summaryMap(validationSummary)},
            {QStringLiteral("validationReturnPercent"),
             validationSummary.totalReturnPercent},
            {QStringLiteral("validationMaxDrawdownPercent"),
             validationSummary.maxDrawdownPercent},
            {QStringLiteral("validationTrades"), validationSummary.totalTrades},
            {QStringLiteral("candidates"), candidateMaps},
            {QStringLiteral("folds"), foldMaps},
            {QStringLiteral("meanValidationReturnPercent"),
             meanValidationReturnPercent},
            {QStringLiteral("medianValidationReturnPercent"),
             medianValidationReturnPercent},
            {QStringLiteral("minimumValidationReturnPercent"),
             minimumValidationReturnPercent},
            {QStringLiteral("maximumValidationReturnPercent"),
             maximumValidationReturnPercent},
            {QStringLiteral("positiveValidationFoldCount"),
             positiveValidationFoldCount},
            {QStringLiteral("nonPositiveValidationFoldCount"),
             nonPositiveValidationFoldCount},
            {QStringLiteral("validationWinningTrades"), validationWinningTrades},
            {QStringLiteral("validationLosingTrades"), validationLosingTrades},
            {QStringLiteral("validationTotalTrades"), validationTotalTrades},
            {QStringLiteral("validationGrossProfit"), validationGrossProfit},
            {QStringLiteral("validationGrossLossAbsolute"),
             validationGrossLossAbsolute},
            {QStringLiteral("validationAverageWinningNetPnl"),
             validationAverageWinningNetPnl},
            {QStringLiteral("validationAverageLosingNetPnlAbsolute"),
             validationAverageLosingNetPnlAbsolute},
            {QStringLiteral("validationPayoffRatio"), validationPayoffRatio},
            {QStringLiteral("validationPayoffRatioAvailable"),
             validationPayoffRatioAvailable},
            {QStringLiteral("parameterSelectionVariantCount"),
             parameterSelectionVariantCount},
            {QStringLiteral("parameterSelectionDistribution"),
             parameterSelectionDistribution},
            {QStringLiteral("researchOnly"), researchOnly}};
}

std::optional<RawTickParameterSensitivityResult>
RawTickParameterSensitivityResult::fromVariantMap(const QVariantMap &value)
{
    RawTickParameterSensitivityResult result;
    result.resultId = value.value(QStringLiteral("resultId")).toString();
    result.completedAt = value.value(QStringLiteral("completedAt")).toDateTime();
    if (!result.completedAt.isValid()) {
        result.completedAt = QDateTime::fromString(
            value.value(QStringLiteral("completedAt")).toString(), Qt::ISODateWithMs);
    }
    result.analysisMode = value.value(
        QStringLiteral("analysisMode"), QStringLiteral("single_holdout")).toString();
    result.walkForwardFoldCount = value.value(
        QStringLiteral("walkForwardFoldCount"), 1).toInt();
    result.strategyId = value.value(QStringLiteral("strategyId")).toString();
    result.instrument.symbol = value.value(QStringLiteral("symbol")).toString();
    result.instrument.name = value.value(QStringLiteral("name")).toString();
    result.instrument.market = static_cast<Market>(
        value.value(QStringLiteral("market")).toInt());
    result.instrument.venue = value.value(QStringLiteral("venue")).toString();
    result.baseParameters = value.value(QStringLiteral("baseParameters")).toMap();
    result.initialCapital = value.value(QStringLiteral("initialCapital")).toDouble();
    result.commissionRate = value.value(QStringLiteral("commissionRate")).toDouble();
    result.slippageRate = value.value(QStringLiteral("slippageRate")).toDouble();
    result.lotSize = value.value(QStringLiteral("lotSize"), 100).toInt();
    result.trainingStartDate = dateFromVariant(value.value(QStringLiteral("trainingStartDate")));
    result.trainingEndDate = dateFromVariant(value.value(QStringLiteral("trainingEndDate")));
    result.validationStartDate = dateFromVariant(value.value(QStringLiteral("validationStartDate")));
    result.validationEndDate = dateFromVariant(value.value(QStringLiteral("validationEndDate")));
    result.trainingActualTicks = value.value(QStringLiteral("trainingActualTicks")).toInt();
    result.validationActualTicks = value.value(QStringLiteral("validationActualTicks")).toInt();
    result.loadMilliseconds = value.value(QStringLiteral("loadMilliseconds")).toLongLong();
    result.scanMilliseconds = value.value(QStringLiteral("scanMilliseconds")).toLongLong();
    result.trainingDataVersion = value.value(QStringLiteral("trainingDataVersion")).toString();
    result.validationDataVersion = value.value(QStringLiteral("validationDataVersion")).toString();
    result.datasetVersion = value.value(QStringLiteral("datasetVersion")).toString();
    result.candidateSetSha256 = value.value(QStringLiteral("candidateSetSha256")).toString();
    result.identitySha256 = value.value(QStringLiteral("identitySha256")).toString();
    result.selectionCriterion = value.value(QStringLiteral("selectionCriterion")).toString();
    result.selectedParameters = value.value(QStringLiteral("selectedParameters")).toMap();
    result.validationSummary = summaryFromMap(
        value.value(QStringLiteral("validationSummary")).toMap());
    result.researchOnly = value.value(QStringLiteral("researchOnly"), true).toBool();
    for (const QVariant &candidateValue : value.value(QStringLiteral("candidates")).toList()) {
        const QVariantMap candidateMap = candidateValue.toMap();
        RawTickParameterSensitivityCandidate candidate;
        candidate.rank = candidateMap.value(QStringLiteral("rank")).toInt();
        candidate.parameters = candidateMap.value(QStringLiteral("parameters")).toMap();
        candidate.trainingSummary = summaryFromMap(
            candidateMap.value(QStringLiteral("trainingSummary")).toMap());
        result.candidates.append(candidate);
    }
    for (const QVariant &foldValue : value.value(QStringLiteral("folds")).toList()) {
        const auto fold = RawTickParameterSensitivityFold::fromVariantMap(
            foldValue.toMap());
        if (!fold) return std::nullopt;
        result.folds.append(*fold);
    }
    result.meanValidationReturnPercent = value.value(
        QStringLiteral("meanValidationReturnPercent"),
        result.validationSummary.totalReturnPercent).toDouble();
    result.medianValidationReturnPercent = value.value(
        QStringLiteral("medianValidationReturnPercent"),
        result.validationSummary.totalReturnPercent).toDouble();
    result.minimumValidationReturnPercent = value.value(
        QStringLiteral("minimumValidationReturnPercent"),
        result.validationSummary.totalReturnPercent).toDouble();
    result.maximumValidationReturnPercent = value.value(
        QStringLiteral("maximumValidationReturnPercent"),
        result.validationSummary.totalReturnPercent).toDouble();
    result.positiveValidationFoldCount = value.value(
        QStringLiteral("positiveValidationFoldCount"),
        result.validationSummary.totalReturnPercent > 0.0 ? 1 : 0).toInt();
    result.nonPositiveValidationFoldCount = value.value(
        QStringLiteral("nonPositiveValidationFoldCount"),
        result.validationSummary.totalReturnPercent > 0.0 ? 0 : 1).toInt();
    result.validationWinningTrades = value.value(
        QStringLiteral("validationWinningTrades"),
        result.validationSummary.winningTrades).toInt();
    result.validationLosingTrades = value.value(
        QStringLiteral("validationLosingTrades"),
        result.validationSummary.losingTrades).toInt();
    result.validationTotalTrades = value.value(
        QStringLiteral("validationTotalTrades"),
        result.validationSummary.totalTrades).toInt();
    result.validationGrossProfit = value.value(
        QStringLiteral("validationGrossProfit")).toDouble();
    result.validationGrossLossAbsolute = value.value(
        QStringLiteral("validationGrossLossAbsolute")).toDouble();
    result.validationAverageWinningNetPnl = value.value(
        QStringLiteral("validationAverageWinningNetPnl")).toDouble();
    result.validationAverageLosingNetPnlAbsolute = value.value(
        QStringLiteral("validationAverageLosingNetPnlAbsolute")).toDouble();
    result.validationPayoffRatio = value.value(
        QStringLiteral("validationPayoffRatio")).toDouble();
    result.validationPayoffRatioAvailable = value.value(
        QStringLiteral("validationPayoffRatioAvailable"), false).toBool();
    if (result.validationWinningTrades == 0
        && result.validationLosingTrades == 0
        && result.validationTotalTrades == 0) {
        if (!result.folds.isEmpty()) {
            for (const RawTickParameterSensitivityFold &fold : result.folds) {
                result.validationWinningTrades += fold.validationWinningTrades;
                result.validationLosingTrades += fold.validationLosingTrades;
                result.validationTotalTrades += fold.validationSummary.totalTrades;
            }
        } else if (result.validationSummary.totalTrades > 0) {
            result.validationWinningTrades =
                result.validationSummary.winningTrades;
            result.validationLosingTrades =
                result.validationSummary.losingTrades;
            result.validationTotalTrades = result.validationSummary.totalTrades;
        }
    }
    const bool hasAggregateTradeStatistics =
        value.contains(QStringLiteral("validationWinningTrades"))
        && value.contains(QStringLiteral("validationLosingTrades"))
        && value.contains(QStringLiteral("validationTotalTrades"))
        && value.contains(QStringLiteral("validationGrossProfit"))
        && value.contains(QStringLiteral("validationGrossLossAbsolute"))
        && value.contains(QStringLiteral("validationAverageWinningNetPnl"))
        && value.contains(QStringLiteral("validationAverageLosingNetPnlAbsolute"))
        && value.contains(QStringLiteral("validationPayoffRatio"))
        && value.contains(QStringLiteral("validationPayoffRatioAvailable"));
    bool aggregateTradeStatisticsConsistent = true;
    if (hasAggregateTradeStatistics) {
        // 现行扫描器会为 single_holdout 保留一折明细；旧归档和部分调用方
        // 只有汇总。两种形态都必须校验，但不能要求单段旧记录凭空拥有折。
        if (!result.folds.isEmpty()) {
            int foldedWinningTrades = 0;
            int foldedLosingTrades = 0;
            int foldedTotalTrades = 0;
            double foldedGrossProfit = 0.0;
            double foldedGrossLossAbsolute = 0.0;
            for (const RawTickParameterSensitivityFold &fold : result.folds) {
                foldedWinningTrades += fold.validationWinningTrades;
                foldedLosingTrades += fold.validationLosingTrades;
                foldedTotalTrades += fold.validationSummary.totalTrades;
                foldedGrossProfit += fold.validationGrossProfit;
                foldedGrossLossAbsolute += fold.validationGrossLossAbsolute;
            }
            aggregateTradeStatisticsConsistent = !result.folds.isEmpty()
                && result.validationWinningTrades == foldedWinningTrades
                && result.validationLosingTrades == foldedLosingTrades
                && result.validationTotalTrades == foldedTotalTrades
                && approximatelyEqual(
                    result.validationGrossProfit, foldedGrossProfit)
                && approximatelyEqual(
                    result.validationGrossLossAbsolute,
                    foldedGrossLossAbsolute);
        } else {
            aggregateTradeStatisticsConsistent = result.validationWinningTrades
                    == result.validationSummary.winningTrades
                && result.validationLosingTrades
                    == result.validationSummary.losingTrades
                && result.validationTotalTrades
                    == result.validationSummary.totalTrades;
        }
        aggregateTradeStatisticsConsistent = aggregateTradeStatisticsConsistent
            && validationTradeStatisticsAreConsistent(
                result.validationTotalTrades,
                result.validationWinningTrades,
                result.validationLosingTrades,
                result.validationGrossProfit,
                result.validationGrossLossAbsolute,
                result.validationAverageWinningNetPnl,
                result.validationAverageLosingNetPnlAbsolute,
                result.validationPayoffRatio,
                result.validationPayoffRatioAvailable);
    }
    result.parameterSelectionVariantCount = value.value(
        QStringLiteral("parameterSelectionVariantCount"), 1).toInt();
    result.parameterSelectionDistribution = value.value(
        QStringLiteral("parameterSelectionDistribution")).toList();
    if (result.resultId.isEmpty() || !result.completedAt.isValid()
        || result.strategyId.isEmpty() || result.instrument.symbol.isEmpty()
        || !result.trainingStartDate.isValid() || !result.trainingEndDate.isValid()
        || !result.validationStartDate.isValid() || !result.validationEndDate.isValid()
        || result.initialCapital <= 0.0 || result.commissionRate < 0.0
        || result.slippageRate < 0.0 || result.lotSize <= 0
        || !isSha256(result.trainingDataVersion)
        || !isSha256(result.validationDataVersion)
        || !isSha256(result.datasetVersion)
        || !isSha256(result.candidateSetSha256)
        || !isSha256(result.identitySha256)
        || result.walkForwardFoldCount < 1 || result.walkForwardFoldCount > 20
        || (result.analysisMode != QStringLiteral("single_holdout")
            && result.analysisMode != QStringLiteral("nested_walk_forward"))
        || result.candidates.isEmpty() || result.selectedParameters.isEmpty()
        || (result.analysisMode == QStringLiteral("nested_walk_forward")
            && result.folds.size() != result.walkForwardFoldCount)
        || (result.analysisMode == QStringLiteral("single_holdout")
            && result.folds.size() > 1)
        || !aggregateTradeStatisticsConsistent
        || !result.researchOnly) return std::nullopt;
    return result;
}

} // namespace pp
