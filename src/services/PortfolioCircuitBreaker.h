#pragma once

#include "domain/PortfolioTypes.h"

#include <algorithm>

namespace pp {

// 组合级锁存熔断器。阈值为 0 时关闭；触发后本次回测不再恢复开仓。
// 累计亏损相对初始资金计算，峰值回撤相对运行期间最高权益计算。
class PortfolioCircuitBreaker final {
public:
    explicit PortfolioCircuitBreaker(double initialCapital)
        : m_initialCapital(initialCapital), m_peakEquity(initialCapital)
    {
    }

    bool evaluate(double equity, const QDateTime &barTime,
                  const PortfolioDefinition &definition,
                  PortfolioRiskSummary &summary)
    {
        m_peakEquity = std::max(m_peakEquity, equity);
        const double lossPercent = m_initialCapital > 0.0
            ? std::max(0.0, (m_initialCapital - equity) / m_initialCapital * 100.0)
            : 0.0;
        const double drawdownPercent = m_peakEquity > 0.0
            ? std::max(0.0, (m_peakEquity - equity) / m_peakEquity * 100.0)
            : 0.0;
        summary.maximumObservedLossPercent = std::max(
            summary.maximumObservedLossPercent, lossPercent);
        summary.maximumObservedDrawdownPercent = std::max(
            summary.maximumObservedDrawdownPercent, drawdownPercent);
        if (m_triggered) return true;

        const bool lossTriggered = definition.maximumPortfolioLossPercent > 0.0
            && lossPercent + 1e-12 >= definition.maximumPortfolioLossPercent;
        const bool drawdownTriggered =
            definition.maximumPortfolioDrawdownPercent > 0.0
            && drawdownPercent + 1e-12
                   >= definition.maximumPortfolioDrawdownPercent;
        if (!lossTriggered && !drawdownTriggered) return false;

        m_triggered = true;
        summary.circuitBreakerTriggered = true;
        summary.circuitBreakerReason = lossTriggered && drawdownTriggered
            ? QStringLiteral("loss_and_drawdown")
            : lossTriggered ? QStringLiteral("loss_limit")
                            : QStringLiteral("drawdown_limit");
        summary.circuitBreakerTriggeredAt = barTime;
        summary.circuitBreakerEquity = equity;
        summary.circuitBreakerLossPercent = lossPercent;
        summary.circuitBreakerDrawdownPercent = drawdownPercent;
        return true;
    }

    bool triggered() const { return m_triggered; }

private:
    double m_initialCapital = 0.0;
    double m_peakEquity = 0.0;
    bool m_triggered = false;
};

} // namespace pp
