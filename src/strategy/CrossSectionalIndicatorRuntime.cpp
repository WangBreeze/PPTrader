#include "strategy/CrossSectionalIndicatorRuntime.h"

#include <QVector>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

QVector<QString> orderedSymbols(const QHash<QString, double> &values, bool ascending)
{
    QVector<QString> symbols = values.keys().toVector();
    std::sort(symbols.begin(), symbols.end(),
              [&values, ascending](const QString &left, const QString &right) {
        const double leftValue = values.value(left);
        const double rightValue = values.value(right);
        if (leftValue != rightValue) {
            return ascending ? leftValue < rightValue : leftValue > rightValue;
        }
        return left < right;
    });
    return symbols;
}

double standardDeviation(const QHash<QString, double> &values, double mean)
{
    double squared = 0.0;
    for (double value : values) squared += (value - mean) * (value - mean);
    return std::sqrt(squared / static_cast<double>(values.size()));
}

} // namespace

QHash<QString, double> CrossSectionalIndicatorRuntime::rank(
    const QHash<QString, double> &values, bool ascending)
{
    QHash<QString, double> result;
    if (values.isEmpty()) return result;
    const QVector<QString> symbols = orderedSymbols(values, ascending);
    for (int position = 0; position < symbols.size(); ++position) {
        result.insert(symbols.at(position), static_cast<double>(position + 1));
    }
    return result;
}

QHash<QString, double> CrossSectionalIndicatorRuntime::zscore(
    const QHash<QString, double> &values)
{
    QHash<QString, double> result;
    if (values.isEmpty()) return result;
    const double average = mean(values);
    const double deviation = standardDeviation(values, average);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        result.insert(it.key(), deviation > 0.0 ? (it.value() - average) / deviation : 0.0);
    }
    return result;
}

QHash<QString, double> CrossSectionalIndicatorRuntime::percentile(
    const QHash<QString, double> &values)
{
    QHash<QString, double> result;
    if (values.isEmpty()) return result;
    const QVector<QString> symbols = orderedSymbols(values, false);
    const int count = symbols.size();
    for (int position = 0; position < count; ++position) {
        const double value = count == 1
            ? 1.0
            : 1.0 - static_cast<double>(position) / static_cast<double>(count - 1);
        result.insert(symbols.at(position), value);
    }
    return result;
}

double CrossSectionalIndicatorRuntime::mean(const QHash<QString, double> &values)
{
    if (values.isEmpty()) return 0.0;
    double sum = 0.0;
    for (double value : values) sum += value;
    return sum / static_cast<double>(values.size());
}

double CrossSectionalIndicatorRuntime::median(const QHash<QString, double> &values)
{
    if (values.isEmpty()) return 0.0;
    QVector<double> sorted = values.values().toVector();
    std::sort(sorted.begin(), sorted.end());
    const int count = sorted.size();
    if (count % 2 == 1) return sorted.at(count / 2);
    return (sorted.at(count / 2 - 1) + sorted.at(count / 2)) / 2.0;
}

} // namespace pp
