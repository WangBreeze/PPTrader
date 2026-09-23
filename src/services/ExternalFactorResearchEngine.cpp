#include "services/ExternalFactorResearchEngine.h"

#include "services/FactorOutOfSampleAnalyzer.h"
#include "services/FactorResearchAnalyzer.h"

#include <QMap>

#include <algorithm>
#include <cmath>
#include <optional>

namespace pp {
namespace {

struct CompletedClose {
    QDateTime completedAt;
    double close = 0.0;
};

std::optional<CompletedClose> closeAtOrBefore(
    const QVector<Candle> &candles, int periodSeconds,
    const QDateTime &timestamp)
{
    std::optional<CompletedClose> result;
    for (const Candle &candle : candles) {
        if (!candle.startTime.isValid() || candle.periodSeconds != periodSeconds
            || !std::isfinite(candle.close) || candle.close <= 0.0) {
            continue;
        }
        const QDateTime completedAt = candle.startTime.addSecs(periodSeconds);
        if (completedAt > timestamp) continue;
        if (!result || completedAt > result->completedAt)
            result = CompletedClose{completedAt, candle.close};
    }
    return result;
}

ExternalFactorResearchResult failure(const QString &message)
{
    ExternalFactorResearchResult result;
    result.error = message;
    return result;
}

} // namespace

ExternalFactorResearchResult ExternalFactorResearchEngine::analyze(
    const ExternalFactorResearchRequest &request,
    const QVector<ExternalFactorObservation> &observations,
    const QHash<QString, QVector<Candle>> &candlesBySymbol)
{
    QString policyError;
    if (request.periodSeconds <= 0)
        return failure(QStringLiteral("研究 K 线周期必须大于零"));
    if (!request.preprocessing.isValid(&policyError)
        || !request.costPolicy.isValid(&policyError)
        || !request.neutralization.isValid(&policyError)
        || !request.outOfSamplePolicy.isValid(&policyError)) {
        return failure(policyError);
    }
    if (observations.isEmpty())
        return failure(QStringLiteral("授权外部因子数据集没有观测"));

    QMap<QDateTime, QHash<QString, double>> valuesByTime;
    for (const ExternalFactorObservation &observation : observations) {
        if (!observation.timestamp.isValid()
            || observation.symbol.trimmed().isEmpty()
            || !std::isfinite(observation.value)) {
            return failure(QStringLiteral("授权外部因子观测无效"));
        }
        QHash<QString, double> &values = valuesByTime[observation.timestamp.toUTC()];
        const auto existing = values.constFind(observation.symbol);
        if (existing != values.cend() && *existing != observation.value)
            return failure(QStringLiteral("同一时刻和代码存在冲突因子值"));
        values.insert(observation.symbol, observation.value);
    }
    if (valuesByTime.size() < 2)
        return failure(QStringLiteral("至少需要两个不同因子时刻计算下一时刻收益"));

    QVector<FactorResearchFrame> frames;
    frames.reserve(valuesByTime.size() - 1);
    auto current = valuesByTime.cbegin();
    auto next = current;
    ++next;
    while (next != valuesByTime.cend()) {
        FactorResearchFrame frame;
        frame.factorTimestamp = current.key();
        frame.returnTimestamp = next.key();
        frame.factorValues = current.value();
        for (auto value = current.value().cbegin();
             value != current.value().cend(); ++value) {
            const QVector<Candle> candles = candlesBySymbol.value(value.key());
            const auto firstClose = closeAtOrBefore(
                candles, request.periodSeconds, frame.factorTimestamp);
            const auto secondClose = closeAtOrBefore(
                candles, request.periodSeconds, frame.returnTimestamp);
            if (!firstClose || !secondClose
                || secondClose->completedAt <= firstClose->completedAt) {
                continue;
            }
            frame.forwardReturns.insert(
                value.key(), secondClose->close / firstClose->close - 1.0);
        }
        frames.append(frame);
        current = next;
        ++next;
    }

    ExternalFactorResearchResult result;
    result.observationCount = observations.size();
    result.factorFrameCount = valuesByTime.size();
    result.returnFrameCount = frames.size();
    result.summary = FactorResearchAnalyzer::analyze(
        frames, request.preprocessing, request.costPolicy,
        request.neutralization);
    result.summary.outOfSample = FactorOutOfSampleAnalyzer::analyze(
        result.summary.timeSeries, request.outOfSamplePolicy);
    if (result.summary.evaluatedFrameCount < 1) {
        result.error = QStringLiteral("本地 K 线覆盖不足，未形成有效下一时刻收益帧");
        return result;
    }
    result.succeeded = true;
    return result;
}

} // namespace pp
