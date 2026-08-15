#pragma once

#include <QHash>
#include <QString>

namespace pp {

// 横截面指标运行时：在同一时刻的 n 只股票之间计算统计量。
// 纯函数、不持有状态；统计口径与 AShareRelativeStrengthIndicator 一致。
class CrossSectionalIndicatorRuntime {
public:
    // 排名：1-based。ascending=false 时最大值 rank=1，同值按 symbol 稳定排序。
    static QHash<QString, double> rank(const QHash<QString, double> &values, bool ascending);

    // Z-Score：(x - mean) / stddev（总体标准差，除以 N）。
    static QHash<QString, double> zscore(const QHash<QString, double> &values);

    // 分位数：1 - position/(N-1)，最高值 1.0、最低 0.0；N=1 时 1.0。
    static QHash<QString, double> percentile(const QHash<QString, double> &values);

    // 均值与中位数（标量）。
    static double mean(const QHash<QString, double> &values);
    static double median(const QHash<QString, double> &values);
};

} // namespace pp
