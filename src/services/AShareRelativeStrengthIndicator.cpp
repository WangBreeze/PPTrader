#include "services/AShareRelativeStrengthIndicator.h"

#include <algorithm>
#include <cmath>

namespace pp {

QString AShareRelativeStrengthIndicator::id() const
{
    return QStringLiteral("ashare_relative_strength");
}

QString AShareRelativeStrengthIndicator::displayName() const
{
    return QStringLiteral("A 股横截面相对强弱");
}

bool AShareRelativeStrengthIndicator::supportsMarket(Market market) const
{
    return market == Market::AShare;
}

FactorFrame AShareRelativeStrengthIndicator::calculate(const MarketSnapshot &snapshot) const
{
    FactorFrame frame;
    frame.indicatorId = id();
    frame.indicatorName = displayName();
    frame.market = snapshot.market;
    frame.timestamp = snapshot.timestamp;
    frame.batchId = snapshot.batchId;
    frame.values.reserve(snapshot.quotes.size());

    QVector<int> validIndices;
    validIndices.reserve(snapshot.quotes.size());
    double sum = 0.0;
    for (const CrossSectionalQuote &quote : snapshot.quotes) {
        FactorValue value;
        value.instrument = quote.instrument;
        if (quote.instrument.market != Market::AShare) {
            value.invalidReason = QStringLiteral("指标仅支持 A 股");
        } else if (quote.suspended) {
            value.invalidReason = QStringLiteral("停牌标的");
        } else if (quote.stale) {
            value.invalidReason = QStringLiteral("行情已过期");
        } else if (quote.price <= 0.0 || quote.previousClose <= 0.0) {
            value.invalidReason = QStringLiteral("缺少有效现价或昨收价");
        } else {
            value.valid = true;
            value.rawValue = quote.price / quote.previousClose - 1.0;
            sum += value.rawValue;
            validIndices.append(frame.values.size());
        }
        frame.values.append(value);
    }

    if (validIndices.isEmpty()) return frame;

    const double mean = sum / static_cast<double>(validIndices.size());
    double squaredDeviation = 0.0;
    for (int index : validIndices) {
        const double deviation = frame.values[index].rawValue - mean;
        squaredDeviation += deviation * deviation;
    }
    const double standardDeviation = std::sqrt(
        squaredDeviation / static_cast<double>(validIndices.size()));
    for (int index : validIndices) {
        frame.values[index].zScore = standardDeviation > 0.0
            ? (frame.values[index].rawValue - mean) / standardDeviation : 0.0;
    }

    std::sort(validIndices.begin(), validIndices.end(), [&frame](int left, int right) {
        const double leftValue = frame.values[left].rawValue;
        const double rightValue = frame.values[right].rawValue;
        if (leftValue != rightValue) return leftValue > rightValue;
        return frame.values[left].instrument.symbol < frame.values[right].instrument.symbol;
    });
    const int validCount = validIndices.size();
    for (int position = 0; position < validCount; ++position) {
        FactorValue &value = frame.values[validIndices[position]];
        value.rank = position + 1;
        value.percentile = validCount == 1
            ? 1.0
            : 1.0 - static_cast<double>(position) / static_cast<double>(validCount - 1);
    }
    return frame;
}

} // namespace pp
