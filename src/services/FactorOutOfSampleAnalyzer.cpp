#include "services/FactorOutOfSampleAnalyzer.h"

#include <cmath>
#include <numeric>

namespace pp {

namespace {

double mean(const QVector<FactorResearchPoint> &series, int from, int count,
            bool rank)
{
    double sum = 0.0;
    for (int index = from; index < from + count; ++index)
        sum += rank ? series.at(index).rankIc : series.at(index).ic;
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
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
    const double valueMean = std::accumulate(
        values.cbegin(), values.cend(), 0.0) / count;
    double longRunVariance = 0.0;
    for (int offset = 0; offset <= lag; ++offset) {
        double covariance = 0.0;
        for (int index = offset; index < values.size(); ++index) {
            covariance += (values.at(index) - valueMean)
                * (values.at(index - offset) - valueMean);
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
    result.confidenceLower = valueMean
        - CriticalValue95 * result.standardError;
    result.confidenceUpper = valueMean
        + CriticalValue95 * result.standardError;
    result.twoSidedPValue = std::erfc(
        std::abs(valueMean / result.standardError) / std::sqrt(2.0));
    result.available = std::isfinite(result.twoSidedPValue);
    return result;
}

} // namespace

FactorOutOfSampleSummary FactorOutOfSampleAnalyzer::analyze(
    const QVector<FactorResearchPoint> &timeSeries,
    const FactorOutOfSamplePolicy &policy)
{
    FactorOutOfSampleSummary result;
    result.policy = policy;
    result.totalFrameCount = timeSeries.size();
    QString policyError;
    if (!policy.isValid(&policyError)) {
        result.availabilityReason = policyError;
        return result;
    }
    for (int index = 0; index < timeSeries.size(); ++index) {
        const FactorResearchPoint &point = timeSeries.at(index);
        if (!point.factorTimestamp.isValid() || !point.returnTimestamp.isValid()
            || !std::isfinite(point.ic) || !std::isfinite(point.rankIc)
            || (index > 0
                && timeSeries.at(index - 1).factorTimestamp
                       >= point.factorTimestamp)) {
            result.availabilityReason = QStringLiteral(
                "IC 序列必须按有效因子时刻严格递增且数值有限");
            return result;
        }
    }
    if (timeSeries.size()
        < policy.initialTrainingFrames + policy.evaluationFrames) {
        result.availabilityReason = QStringLiteral(
            "有效帧不足以形成首个完整样本外测试窗");
        return result;
    }

    int evaluationStart = policy.initialTrainingFrames;
    int windowNumber = 1;
    while (evaluationStart + policy.evaluationFrames <= timeSeries.size()) {
        FactorOutOfSampleWindow window;
        window.window = windowNumber++;
        window.trainingFrameCount = evaluationStart;
        window.evaluationFrameCount = policy.evaluationFrames;
        window.trainingFromFactorTimestamp = timeSeries.first().factorTimestamp;
        window.trainingToFactorTimestamp =
            timeSeries.at(evaluationStart - 1).factorTimestamp;
        window.evaluationFromFactorTimestamp =
            timeSeries.at(evaluationStart).factorTimestamp;
        window.evaluationToFactorTimestamp = timeSeries.at(
            evaluationStart + policy.evaluationFrames - 1).factorTimestamp;
        window.meanIc = mean(timeSeries, evaluationStart,
                             policy.evaluationFrames, false);
        window.meanRankIc = mean(timeSeries, evaluationStart,
                                 policy.evaluationFrames, true);
        result.windows.append(window);
        evaluationStart += policy.stepFrames;
    }

    result.evaluationFrameCount =
        result.windows.size() * policy.evaluationFrames;
    result.evaluationFromFactorTimestamp =
        result.windows.first().evaluationFromFactorTimestamp;
    result.evaluationToFactorTimestamp =
        result.windows.last().evaluationToFactorTimestamp;
    double icSum = 0.0;
    double rankIcSum = 0.0;
    QVector<double> evaluationIcValues;
    QVector<double> evaluationRankIcValues;
    evaluationIcValues.reserve(result.evaluationFrameCount);
    evaluationRankIcValues.reserve(result.evaluationFrameCount);
    for (const FactorOutOfSampleWindow &window : result.windows) {
        icSum += window.meanIc * window.evaluationFrameCount;
        rankIcSum += window.meanRankIc * window.evaluationFrameCount;
    }
    for (int start = policy.initialTrainingFrames;
         start + policy.evaluationFrames <= timeSeries.size();
         start += policy.stepFrames) {
        for (int index = start; index < start + policy.evaluationFrames; ++index) {
            evaluationIcValues.append(timeSeries.at(index).ic);
            evaluationRankIcValues.append(timeSeries.at(index).rankIc);
        }
    }
    result.meanIc = icSum / result.evaluationFrameCount;
    result.meanRankIc = rankIcSum / result.evaluationFrameCount;
    result.inferenceFrameCount = evaluationIcValues.size();
    if (result.inferenceFrameCount >= 20) {
        result.hacLag = std::clamp(
            static_cast<int>(std::floor(
                4.0 * std::pow(
                    static_cast<double>(result.inferenceFrameCount) / 100.0,
                    2.0 / 9.0))),
            0, result.inferenceFrameCount - 1);
        const HacInference icInference = neweyWestMeanInference(
            evaluationIcValues, result.hacLag);
        result.icInferenceAvailable = icInference.available;
        result.icMeanStandardError = icInference.standardError;
        result.icConfidenceLower = icInference.confidenceLower;
        result.icConfidenceUpper = icInference.confidenceUpper;
        result.icTwoSidedPValue = icInference.twoSidedPValue;
        const HacInference rankInference = neweyWestMeanInference(
            evaluationRankIcValues, result.hacLag);
        result.rankIcInferenceAvailable = rankInference.available;
        result.rankIcMeanStandardError = rankInference.standardError;
        result.rankIcConfidenceLower = rankInference.confidenceLower;
        result.rankIcConfidenceUpper = rankInference.confidenceUpper;
        result.rankIcTwoSidedPValue = rankInference.twoSidedPValue;
    }
    result.available = true;
    return result;
}

} // namespace pp
