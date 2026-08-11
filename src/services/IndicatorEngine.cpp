#include "services/IndicatorEngine.h"

namespace pp {

std::optional<double> IndicatorEngine::simpleMovingAverage(const QVector<double> &values, int period)
{
    if (period <= 0 || values.size() < period) {
        return std::nullopt;
    }

    double sum = 0.0;
    for (int index = values.size() - period; index < values.size(); ++index) {
        sum += values.at(index);
    }

    return sum / period;
}

std::optional<double> IndicatorEngine::rateOfChange(const QVector<double> &values, int period)
{
    if (period <= 0 || values.size() <= period) {
        return std::nullopt;
    }

    const double previous = values.at(values.size() - period - 1);
    if (previous == 0.0) {
        return std::nullopt;
    }

    const double current = values.back();
    return (current - previous) / previous;
}

} // 命名空间 pp
