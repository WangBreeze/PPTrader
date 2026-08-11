#include "services/CrossSectionalIndicatorEngine.h"

namespace pp {

bool CrossSectionalIndicatorEngine::addIndicator(
    std::unique_ptr<CrossSectionalIndicator> indicator)
{
    if (!indicator || indicator->id().isEmpty()) return false;
    for (const auto &existing : m_indicators) {
        if (existing->id() == indicator->id()) return false;
    }
    m_indicators.push_back(std::move(indicator));
    return true;
}

int CrossSectionalIndicatorEngine::indicatorCount() const
{
    return static_cast<int>(m_indicators.size());
}

QVector<FactorFrame> CrossSectionalIndicatorEngine::calculate(
    const MarketSnapshot &snapshot) const
{
    QVector<FactorFrame> frames;
    frames.reserve(static_cast<qsizetype>(m_indicators.size()));
    for (const auto &indicator : m_indicators) {
        if (indicator->supportsMarket(snapshot.market)) {
            frames.append(indicator->calculate(snapshot));
        }
    }
    return frames;
}

} // namespace pp
