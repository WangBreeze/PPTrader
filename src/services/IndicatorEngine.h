#pragma once

#include <QVector>

#include <optional>

namespace pp {

class IndicatorEngine {
public:
    static std::optional<double> simpleMovingAverage(const QVector<double> &values, int period);
    static std::optional<double> rateOfChange(const QVector<double> &values, int period);
};

} // 命名空间 pp
