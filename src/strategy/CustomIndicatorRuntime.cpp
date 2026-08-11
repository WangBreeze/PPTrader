#include "strategy/CustomIndicatorRuntime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

namespace {

double missingValue()
{
    return std::numeric_limits<double>::quiet_NaN();
}

bool validPrice(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool validVolume(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

CustomIndicatorRuntime::CustomIndicatorRuntime(QVector<CompiledCustomIndicator> indicators)
    : m_indicators(std::move(indicators))
    , m_states(m_indicators.size())
{
}

void CustomIndicatorRuntime::reset()
{
    m_states = QVector<IndicatorState>(m_indicators.size());
    m_closes.clear();
    m_volumes.clear();
    m_trueRanges.clear();
    m_previousClose.reset();
}

void CustomIndicatorRuntime::append(const Candle &candle)
{
    const double close = validPrice(candle.close) ? candle.close : missingValue();
    m_closes.append(close);
    m_volumes.append(validVolume(candle.volume) ? candle.volume : missingValue());

    double trueRange = missingValue();
    if (validPrice(candle.high) && validPrice(candle.low) && candle.high >= candle.low) {
        trueRange = candle.high - candle.low;
        if (m_previousClose) {
            trueRange = std::max({trueRange, std::abs(candle.high - *m_previousClose),
                                  std::abs(candle.low - *m_previousClose)});
        }
    }
    m_trueRanges.append(trueRange);
    if (std::isfinite(close)) m_previousClose = close;
    else m_previousClose.reset();

    for (int index = 0; index < m_indicators.size(); ++index) updateIndicator(index);
}

std::optional<double> CustomIndicatorRuntime::value(int indicatorIndex) const
{
    if (indicatorIndex < 0 || indicatorIndex >= m_states.size()) return std::nullopt;
    return m_states.at(indicatorIndex).current;
}

int CustomIndicatorRuntime::barCount() const
{
    return m_closes.size();
}

std::optional<double> CustomIndicatorRuntime::windowAverage(
    const QVector<double> &values, int period)
{
    if (period <= 0 || values.size() < period) return std::nullopt;
    double total = 0.0;
    for (int row = values.size() - period; row < values.size(); ++row) {
        if (!std::isfinite(values.at(row))) return std::nullopt;
        total += values.at(row);
    }
    return total / period;
}

std::optional<double> CustomIndicatorRuntime::updateEma(
    EmaAccumulator *state, double input, int period)
{
    if (!std::isfinite(input) || period <= 0) {
        *state = {};
        return std::nullopt;
    }
    if (state->value) {
        const double alpha = 2.0 / (period + 1.0);
        state->value = alpha * input + (1.0 - alpha) * *state->value;
        return state->value;
    }
    state->seedSum += input;
    ++state->seedCount;
    if (state->seedCount < period) return std::nullopt;
    state->value = state->seedSum / period;
    return state->value;
}

void CustomIndicatorRuntime::updateIndicator(int index)
{
    const CompiledCustomIndicator &indicator = m_indicators.at(index);
    IndicatorState &state = m_states[index];
    state.current.reset();
    switch (indicator.kind) {
    case CustomIndicatorKind::Sma:
        state.current = windowAverage(m_closes, indicator.period);
        break;
    case CustomIndicatorKind::VolumeSma:
        state.current = windowAverage(m_volumes, indicator.period);
        break;
    case CustomIndicatorKind::Ema:
        state.current = updateEma(&state.primary, m_closes.last(), indicator.period);
        break;
    case CustomIndicatorKind::Rsi: {
        if (m_closes.size() < indicator.period + 1) break;
        double gains = 0.0;
        double losses = 0.0;
        for (int row = m_closes.size() - indicator.period; row < m_closes.size(); ++row) {
            const double current = m_closes.at(row);
            const double previous = m_closes.at(row - 1);
            if (!std::isfinite(current) || !std::isfinite(previous)) return;
            const double change = current - previous;
            if (change > 0.0) gains += change;
            else losses -= change;
        }
        if (losses <= 0.0) state.current = 100.0;
        else state.current = 100.0 - 100.0 / (1.0 + gains / losses);
        break;
    }
    case CustomIndicatorKind::Atr:
        state.current = windowAverage(m_trueRanges, indicator.period);
        break;
    case CustomIndicatorKind::Bollinger: {
        const auto middle = windowAverage(m_closes, indicator.period);
        if (!middle) break;
        double squared = 0.0;
        for (int row = m_closes.size() - indicator.period; row < m_closes.size(); ++row) {
            const double difference = m_closes.at(row) - *middle;
            squared += difference * difference;
        }
        const double deviation = std::sqrt(squared / indicator.period)
            * indicator.standardDeviations;
        if (indicator.output == QStringLiteral("upper")) state.current = *middle + deviation;
        else if (indicator.output == QStringLiteral("lower")) state.current = *middle - deviation;
        else state.current = middle;
        break;
    }
    case CustomIndicatorKind::Macd: {
        const double close = m_closes.last();
        const auto fast = updateEma(&state.primary, close, indicator.fastPeriod);
        const auto slow = updateEma(&state.secondary, close, indicator.slowPeriod);
        if (!fast || !slow) {
            state.signal = {};
            break;
        }
        const double macd = *fast - *slow;
        if (indicator.output == QStringLiteral("macd")) {
            state.current = macd;
            break;
        }
        const auto signal = updateEma(&state.signal, macd, indicator.signalPeriod);
        if (!signal) break;
        state.current = indicator.output == QStringLiteral("signal") ? *signal : macd - *signal;
        break;
    }
    }
}

} // namespace pp
