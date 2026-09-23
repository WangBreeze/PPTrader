#include "services/FactorResearchAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pp {

namespace {

double quantile(QVector<double> values, double probability)
{
    std::sort(values.begin(), values.end());
    if (values.isEmpty()) return 0.0;
    if (values.size() == 1) return values.first();
    const double position = probability * static_cast<double>(values.size() - 1);
    const int lower = static_cast<int>(std::floor(position));
    const int upper = static_cast<int>(std::ceil(position));
    if (lower == upper) return values.at(lower);
    const double fraction = position - static_cast<double>(lower);
    return values.at(lower) * (1.0 - fraction) + values.at(upper) * fraction;
}

double pearson(const QVector<double> &left, const QVector<double> &right,
               bool *valid)
{
    if (valid) *valid = false;
    if (left.size() != right.size() || left.size() < 2) return 0.0;
    const double leftMean = std::accumulate(left.cbegin(), left.cend(), 0.0)
        / static_cast<double>(left.size());
    const double rightMean = std::accumulate(right.cbegin(), right.cend(), 0.0)
        / static_cast<double>(right.size());
    double covariance = 0.0;
    double leftSquared = 0.0;
    double rightSquared = 0.0;
    for (int index = 0; index < left.size(); ++index) {
        const double leftDelta = left.at(index) - leftMean;
        const double rightDelta = right.at(index) - rightMean;
        covariance += leftDelta * rightDelta;
        leftSquared += leftDelta * leftDelta;
        rightSquared += rightDelta * rightDelta;
    }
    const double denominator = std::sqrt(leftSquared * rightSquared);
    if (denominator <= 0.0 || !std::isfinite(denominator)) return 0.0;
    if (valid) *valid = true;
    return covariance / denominator;
}

QVector<double> averageRanks(const QVector<double> &values)
{
    QVector<int> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&values](int left, int right) {
        return values.at(left) < values.at(right);
    });
    QVector<double> ranks(values.size());
    int begin = 0;
    while (begin < order.size()) {
        int end = begin + 1;
        while (end < order.size()
               && values.at(order.at(end)) == values.at(order.at(begin))) {
            ++end;
        }
        const double average = (static_cast<double>(begin + 1)
                                + static_cast<double>(end)) / 2.0;
        for (int index = begin; index < end; ++index)
            ranks[order.at(index)] = average;
        begin = end;
    }
    return ranks;
}

double sampleStandardDeviation(const QVector<double> &values)
{
    if (values.size() < 2) return 0.0;
    const double mean = std::accumulate(values.cbegin(), values.cend(), 0.0)
        / static_cast<double>(values.size());
    double squaredDeviation = 0.0;
    for (double value : values)
        squaredDeviation += (value - mean) * (value - mean);
    return std::sqrt(squaredDeviation
                     / static_cast<double>(values.size() - 1));
}

struct HacInference {
    bool available = false;
    double standardError = 0.0;
    double confidenceLower = 0.0;
    double confidenceUpper = 0.0;
    double twoSidedPValue = 0.0;
};

HacInference neweyWestMeanInference(const QVector<double> &values, int lag)
{
    HacInference result;
    if (values.size() < 20 || lag < 0 || lag >= values.size()) return result;
    const double count = static_cast<double>(values.size());
    const double mean = std::accumulate(values.cbegin(), values.cend(), 0.0)
        / count;
    double longRunVariance = 0.0;
    for (int offset = 0; offset <= lag; ++offset) {
        double covariance = 0.0;
        for (int index = offset; index < values.size(); ++index) {
            covariance += (values.at(index) - mean)
                * (values.at(index - offset) - mean);
        }
        covariance /= count;
        if (offset == 0) {
            longRunVariance = covariance;
        } else {
            const double weight = 1.0
                - static_cast<double>(offset) / static_cast<double>(lag + 1);
            longRunVariance += 2.0 * weight * covariance;
        }
    }
    if (!std::isfinite(longRunVariance) || longRunVariance <= 0.0)
        return result;
    result.standardError = std::sqrt(longRunVariance / count);
    if (!std::isfinite(result.standardError) || result.standardError <= 0.0)
        return {};
    constexpr double CriticalValue95 = 1.959963984540054;
    result.confidenceLower = mean - CriticalValue95 * result.standardError;
    result.confidenceUpper = mean + CriticalValue95 * result.standardError;
    result.twoSidedPValue = std::erfc(
        std::abs(mean / result.standardError) / std::sqrt(2.0));
    result.available = std::isfinite(result.twoSidedPValue);
    return result;
}

bool sameNonZeroSign(const QVector<FactorTemporalSegment> &segments,
                     bool rank)
{
    if (segments.isEmpty()) return false;
    const double first = rank ? segments.first().meanRankIc
                              : segments.first().meanIc;
    if (first == 0.0) return false;
    for (const FactorTemporalSegment &segment : segments) {
        const double value = rank ? segment.meanRankIc : segment.meanIc;
        if (value == 0.0 || std::signbit(value) != std::signbit(first))
            return false;
    }
    return true;
}

} // namespace

FactorResearchSummary FactorResearchAnalyzer::analyze(
    const QVector<FactorResearchFrame> &frames,
    const FactorPreprocessingPolicy &policy,
    const FactorResearchCostPolicy &costPolicy,
    const FactorNeutralizationPolicy &neutralizationPolicy)
{
    FactorResearchSummary result;
    result.preprocessing = policy;
    result.costPolicy = costPolicy;
    result.neutralizationPolicy = neutralizationPolicy;
    if (!policy.isValid() || !costPolicy.isValid()
        || !neutralizationPolicy.isValid()) {
        return result;
    }

    double icSum = 0.0;
    double rankIcSum = 0.0;
    QVector<double> icValues;
    QVector<double> rankIcValues;
    constexpr int quantileCount = 5;
    QVector<double> quantileSums(quantileCount, 0.0);
    QVector<double> quantileNetSums(quantileCount, 0.0);
    QVector<double> quantileCostSums(quantileCount, 0.0);
    QVector<double> quantileTurnoverSums(quantileCount, 0.0);
    QVector<int> quantileFrameCounts(quantileCount, 0);
    QVector<int> quantileTurnoverCounts(quantileCount, 0);
    QVector<QHash<QString, double>> previousQuantileWeights(quantileCount);
    bool hasPreviousQuantiles = false;
    double topMinusBottomSum = 0.0;
    double topMinusBottomNetSum = 0.0;
    double topMinusBottomCostSum = 0.0;
    double topMinusBottomTurnoverSum = 0.0;
    int positiveIcFrames = 0;
    for (const FactorResearchFrame &frame : frames) {
        result.candidatePairCount += frame.factorValues.size();
        QVector<double> factors;
        QVector<double> returns;
        QVector<QString> symbols;
        factors.reserve(frame.factorValues.size());
        returns.reserve(frame.factorValues.size());
        symbols.reserve(frame.factorValues.size());
        for (auto iterator = frame.factorValues.cbegin();
             iterator != frame.factorValues.cend(); ++iterator) {
            const auto forward = frame.forwardReturns.constFind(iterator.key());
            if (forward == frame.forwardReturns.cend()
                || !std::isfinite(iterator.value())
                || !std::isfinite(forward.value())) {
                continue;
            }
            symbols.append(iterator.key());
            factors.append(iterator.value());
            returns.append(forward.value());
        }
        result.validPairCount += factors.size();
        if (factors.size() < policy.minimumPairs) {
            ++result.skippedFrameCount;
            continue;
        }
        if (policy.winsorizationPolicy == QStringLiteral("percentile_clip")) {
            const double lower = quantile(factors, policy.lowerQuantile);
            const double upper = quantile(factors, policy.upperQuantile);
            for (double &factor : factors)
                factor = std::clamp(factor, lower, upper);
        }
        if (neutralizationPolicy.enabled()) {
            QHash<QString, QVector<int>> indicesByGroup;
            bool coverageComplete = true;
            for (int index = 0; index < symbols.size(); ++index) {
                const QString group = frame.neutralizationGroups
                    .value(symbols.at(index)).trimmed();
                if (group.isEmpty()) {
                    coverageComplete = false;
                    break;
                }
                indicesByGroup[group].append(index);
            }
            if (!coverageComplete) {
                ++result.neutralizationSkippedFrameCount;
                ++result.skippedFrameCount;
                continue;
            }
            QStringList groups = indicesByGroup.keys();
            std::sort(groups.begin(), groups.end());
            QVector<double> neutralizedFactors;
            QVector<double> neutralizedReturns;
            QVector<QString> neutralizedSymbols;
            for (const QString &group : groups) {
                const QVector<int> &indices = indicesByGroup[group];
                if (indices.size() < neutralizationPolicy.minimumGroupSize) {
                    result.smallGroupDroppedPairCount += indices.size();
                    continue;
                }
                double groupMean = 0.0;
                for (int index : indices) groupMean += factors.at(index);
                groupMean /= static_cast<double>(indices.size());
                for (int index : indices) {
                    neutralizedFactors.append(factors.at(index) - groupMean);
                    neutralizedReturns.append(returns.at(index));
                    neutralizedSymbols.append(symbols.at(index));
                }
            }
            factors = neutralizedFactors;
            returns = neutralizedReturns;
            symbols = neutralizedSymbols;
            result.neutralizedPairCount += factors.size();
            ++result.neutralizationAppliedFrameCount;
            if (factors.size() < policy.minimumPairs) {
                ++result.skippedFrameCount;
                continue;
            }
        }
        bool icValid = false;
        bool rankIcValid = false;
        const double ic = pearson(factors, returns, &icValid);
        const double rankIc = pearson(averageRanks(factors),
                                      averageRanks(returns), &rankIcValid);
        if (!icValid || !rankIcValid) {
            ++result.skippedFrameCount;
            continue;
        }
        icSum += ic;
        rankIcSum += rankIc;
        icValues.append(ic);
        rankIcValues.append(rankIc);
        result.timeSeries.append({frame.factorTimestamp, frame.returnTimestamp,
                                  static_cast<int>(frame.factorValues.size()),
                                  static_cast<int>(factors.size()),
                                  ic, rankIc});
        if (factors.size() >= quantileCount) {
            QVector<int> order(factors.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&factors, &symbols](int left, int right) {
                if (factors.at(left) != factors.at(right))
                    return factors.at(left) < factors.at(right);
                return symbols.at(left) < symbols.at(right);
            });
            QVector<double> frameSums(quantileCount, 0.0);
            QVector<int> frameCounts(quantileCount, 0);
            QVector<QHash<QString, double>> currentQuantileWeights(quantileCount);
            for (int rank = 0; rank < order.size(); ++rank) {
                const int quantile = rank * quantileCount / order.size();
                frameSums[quantile] += returns.at(order.at(rank));
                ++frameCounts[quantile];
            }
            QVector<double> frameMeans(quantileCount, 0.0);
            QVector<double> frameCosts(quantileCount, 0.0);
            QVector<double> frameTurnovers(quantileCount, 0.0);
            for (int quantile = 0; quantile < quantileCount; ++quantile) {
                for (int rank = 0; rank < order.size(); ++rank) {
                    if (rank * quantileCount / order.size() == quantile) {
                        currentQuantileWeights[quantile].insert(
                            symbols.at(order.at(rank)),
                            1.0 / static_cast<double>(frameCounts.at(quantile)));
                    }
                }
                frameMeans[quantile] = frameSums.at(quantile)
                    / static_cast<double>(frameCounts.at(quantile));
                if (hasPreviousQuantiles) {
                    QSet<QString> symbolsInTransition;
                    for (auto it = previousQuantileWeights.at(quantile).cbegin();
                         it != previousQuantileWeights.at(quantile).cend(); ++it) {
                        symbolsInTransition.insert(it.key());
                    }
                    for (auto it = currentQuantileWeights.at(quantile).cbegin();
                         it != currentQuantileWeights.at(quantile).cend(); ++it) {
                        symbolsInTransition.insert(it.key());
                    }
                    double absoluteWeightChange = 0.0;
                    for (const QString &symbol : symbolsInTransition) {
                        absoluteWeightChange += std::abs(
                            currentQuantileWeights.at(quantile).value(symbol)
                            - previousQuantileWeights.at(quantile).value(symbol));
                    }
                    frameTurnovers[quantile] = absoluteWeightChange / 2.0;
                    frameCosts[quantile] = absoluteWeightChange
                        * costPolicy.oneWayCostBps / 10000.0;
                    quantileTurnoverSums[quantile] += frameTurnovers.at(quantile);
                    ++quantileTurnoverCounts[quantile];
                }
                quantileSums[quantile] += frameMeans.at(quantile);
                quantileCostSums[quantile] += frameCosts.at(quantile);
                quantileNetSums[quantile] += frameMeans.at(quantile)
                    - frameCosts.at(quantile);
                ++quantileFrameCounts[quantile];
            }
            const double spreadGross = frameMeans.last() - frameMeans.first();
            const double spreadCost = frameCosts.last() + frameCosts.first();
            topMinusBottomSum += spreadGross;
            topMinusBottomCostSum += spreadCost;
            topMinusBottomNetSum += spreadGross - spreadCost;
            if (hasPreviousQuantiles) {
                topMinusBottomTurnoverSum += frameTurnovers.last()
                    + frameTurnovers.first();
                ++result.turnoverObservationCount;
            }
            ++result.topMinusBottomFrameCount;
            previousQuantileWeights = currentQuantileWeights;
            hasPreviousQuantiles = true;
        }
        if (ic > 0.0) ++positiveIcFrames;
        ++result.evaluatedFrameCount;
    }
    result.coverageRatio = result.candidatePairCount > 0
        ? static_cast<double>(result.validPairCount)
              / static_cast<double>(result.candidatePairCount)
        : 0.0;
    if (result.evaluatedFrameCount > 0) {
        result.meanIc = icSum / static_cast<double>(result.evaluatedFrameCount);
        result.meanRankIc = rankIcSum
            / static_cast<double>(result.evaluatedFrameCount);
        result.positiveIcFrameRatio = static_cast<double>(positiveIcFrames)
            / static_cast<double>(result.evaluatedFrameCount);
    }
    result.icStandardDeviation = sampleStandardDeviation(icValues);
    result.rankIcStandardDeviation = sampleStandardDeviation(rankIcValues);
    result.icirAvailable = icValues.size() >= 2
        && result.icStandardDeviation > 0.0;
    result.rankIcirAvailable = rankIcValues.size() >= 2
        && result.rankIcStandardDeviation > 0.0;
    if (result.icirAvailable)
        result.icInformationRatio = result.meanIc / result.icStandardDeviation;
    if (result.rankIcirAvailable) {
        result.rankIcInformationRatio = result.meanRankIc
            / result.rankIcStandardDeviation;
    }
    result.inferenceFrameCount = icValues.size();
    if (result.inferenceFrameCount >= 20) {
        result.hacLag = std::clamp(
            static_cast<int>(std::floor(
                4.0 * std::pow(
                    static_cast<double>(result.inferenceFrameCount) / 100.0,
                    2.0 / 9.0))),
            0, result.inferenceFrameCount - 1);
        const HacInference icInference = neweyWestMeanInference(
            icValues, result.hacLag);
        result.icInferenceAvailable = icInference.available;
        result.icMeanStandardError = icInference.standardError;
        result.icConfidenceLower = icInference.confidenceLower;
        result.icConfidenceUpper = icInference.confidenceUpper;
        result.icTwoSidedPValue = icInference.twoSidedPValue;
        const HacInference rankInference = neweyWestMeanInference(
            rankIcValues, result.hacLag);
        result.rankIcInferenceAvailable = rankInference.available;
        result.rankIcMeanStandardError = rankInference.standardError;
        result.rankIcConfidenceLower = rankInference.confidenceLower;
        result.rankIcConfidenceUpper = rankInference.confidenceUpper;
        result.rankIcTwoSidedPValue = rankInference.twoSidedPValue;
    }
    if (result.timeSeries.size() >= 9) {
        constexpr int SegmentCount = 3;
        result.temporalSegments.reserve(SegmentCount);
        for (int segmentIndex = 0; segmentIndex < SegmentCount; ++segmentIndex) {
            const int begin = segmentIndex * result.timeSeries.size()
                / SegmentCount;
            const int end = (segmentIndex + 1) * result.timeSeries.size()
                / SegmentCount;
            double segmentIcSum = 0.0;
            double segmentRankIcSum = 0.0;
            for (int index = begin; index < end; ++index) {
                segmentIcSum += result.timeSeries.at(index).ic;
                segmentRankIcSum += result.timeSeries.at(index).rankIc;
            }
            const int count = end - begin;
            result.temporalSegments.append({
                segmentIndex + 1,
                result.timeSeries.at(begin).factorTimestamp,
                result.timeSeries.at(end - 1).factorTimestamp,
                count,
                segmentIcSum / static_cast<double>(count),
                segmentRankIcSum / static_cast<double>(count)});
        }
        result.temporalStabilityAvailable = true;
        result.icSameSignAcrossSegments = sameNonZeroSign(
            result.temporalSegments, false);
        result.rankIcSameSignAcrossSegments = sameNonZeroSign(
            result.temporalSegments, true);
    }
    result.quantileCount = quantileCount;
    result.quantileReturns.reserve(quantileCount);
    for (int quantile = 0; quantile < quantileCount; ++quantile) {
        const int frameCount = quantileFrameCounts.at(quantile);
        result.quantileReturns.append(
            {quantile + 1, frameCount,
             frameCount > 0
                 ? quantileSums.at(quantile) / static_cast<double>(frameCount)
                 : 0.0,
             frameCount > 0
                 ? quantileNetSums.at(quantile) / static_cast<double>(frameCount)
                 : 0.0,
             quantileTurnoverCounts.at(quantile) > 0
                 ? quantileTurnoverSums.at(quantile)
                       / static_cast<double>(quantileTurnoverCounts.at(quantile))
                 : 0.0,
             frameCount > 0
                 ? quantileCostSums.at(quantile) / static_cast<double>(frameCount)
                 : 0.0,
             quantileTurnoverCounts.at(quantile)});
    }
    if (result.topMinusBottomFrameCount > 0) {
        result.topMinusBottomMeanReturn = topMinusBottomSum
            / static_cast<double>(result.topMinusBottomFrameCount);
        result.topMinusBottomMeanNetReturn = topMinusBottomNetSum
            / static_cast<double>(result.topMinusBottomFrameCount);
        result.topMinusBottomMeanTransactionCost = topMinusBottomCostSum
            / static_cast<double>(result.topMinusBottomFrameCount);
    }
    if (result.turnoverObservationCount > 0) {
        result.topMinusBottomMeanOneWayTurnover = topMinusBottomTurnoverSum
            / static_cast<double>(result.turnoverObservationCount);
    }
    return result;
}

} // namespace pp
