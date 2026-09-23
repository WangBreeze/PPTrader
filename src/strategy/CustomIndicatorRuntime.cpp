#include "strategy/CustomIndicatorRuntime.h"

#include <QTimeZone>

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
    m_typicalPrices.clear();
    m_highs.clear();
    m_lows.clear();
    m_previousClose.reset();
}

void CustomIndicatorRuntime::append(const Candle &candle)
{
    const double close = validPrice(candle.close) ? candle.close : missingValue();
    m_closes.append(close);
    m_volumes.append(validVolume(candle.volume) ? candle.volume : missingValue());
    const bool validHlc = std::isfinite(close) && validPrice(candle.high)
        && validPrice(candle.low) && candle.low <= close && close <= candle.high;
    m_typicalPrices.append(validHlc
        ? candle.high / 3.0 + candle.low / 3.0 + close / 3.0 : missingValue());
    m_highs.append(validHlc ? candle.high : missingValue());
    m_lows.append(validHlc ? candle.low : missingValue());

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

    for (int index = 0; index < m_indicators.size(); ++index) updateIndicator(index, candle);
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

void CustomIndicatorRuntime::updateIndicator(int index, const Candle &candle)
{
    const CompiledCustomIndicator &indicator = m_indicators.at(index);
    IndicatorState &state = m_states[index];
    state.current.reset();
    switch (indicator.kind) {
    case CustomIndicatorKind::Sma:
        state.current = windowAverage(m_closes, indicator.period);
        break;
    case CustomIndicatorKind::Wma: {
        if (!windowAverage(m_closes, indicator.period)) break;
        double weighted = 0.0;
        for (int offset = 0; offset < indicator.period; ++offset) {
            weighted += m_closes.at(m_closes.size() - indicator.period + offset)
                * (offset + 1.0);
        }
        state.current = weighted / (indicator.period * (indicator.period + 1.0) / 2.0);
        break;
    }
    case CustomIndicatorKind::Bias: {
        const auto average = windowAverage(m_closes, indicator.period);
        if (average && std::isfinite(*average) && *average > 0.0) {
            state.current = (m_closes.last() / *average - 1.0) * 100.0;
        }
        break;
    }
    case CustomIndicatorKind::Cci: {
        const auto average = windowAverage(m_typicalPrices, indicator.period);
        if (!average || !std::isfinite(*average)) break;
        double deviation = 0.0;
        for (int row = m_typicalPrices.size() - indicator.period;
             row < m_typicalPrices.size(); ++row) {
            deviation += std::abs(m_typicalPrices.at(row) - *average);
        }
        deviation /= indicator.period;
        if (!std::isfinite(deviation)) break;
        state.current = deviation > 0.0
            ? (m_typicalPrices.last() - *average) / (0.015 * deviation) : 0.0;
        break;
    }
    case CustomIndicatorKind::Wr: {
        if (!windowAverage(m_highs, indicator.period)
            || !windowAverage(m_lows, indicator.period)) break;
        const double high = *std::max_element(m_highs.cend() - indicator.period, m_highs.cend());
        const double low = *std::min_element(m_lows.cend() - indicator.period, m_lows.cend());
        state.current = high > low ? (m_closes.last() - high) / (high - low) * 100.0 : -50.0;
        break;
    }
    case CustomIndicatorKind::Donchian: {
        if (!windowAverage(m_highs, indicator.period)
            || !windowAverage(m_lows, indicator.period)) break;
        const double high = *std::max_element(m_highs.cend() - indicator.period, m_highs.cend());
        const double low = *std::min_element(m_lows.cend() - indicator.period, m_lows.cend());
        if (indicator.output == QStringLiteral("upper")) state.current = high;
        else if (indicator.output == QStringLiteral("lower")) state.current = low;
        else state.current = high / 2.0 + low / 2.0;
        break;
    }
    case CustomIndicatorKind::Obv: {
        const double close = m_closes.last();
        const double volume = m_volumes.last();
        if (!std::isfinite(close) || !std::isfinite(volume)) {
            state.obvPreviousClose.reset();
            break;
        }
        double nextObv = state.cumulativeObv;
        if (state.obvPreviousClose) {
            if (close > *state.obvPreviousClose) nextObv += volume;
            else if (close < *state.obvPreviousClose) nextObv -= volume;
        }
        if (!std::isfinite(nextObv)) {
            state.obvPreviousClose.reset();
            break;
        }
        state.cumulativeObv = nextObv;
        state.obvPreviousClose = close;
        state.current = state.cumulativeObv;
        break;
    }
    case CustomIndicatorKind::Mfi: {
        if (!windowAverage(m_typicalPrices, indicator.period + 1)
            || !windowAverage(m_volumes, indicator.period)) break;
        double positive = 0.0;
        double negative = 0.0;
        for (int row = m_typicalPrices.size() - indicator.period;
             row < m_typicalPrices.size(); ++row) {
            const double typicalPrice = m_typicalPrices.at(row);
            const double previousPrice = m_typicalPrices.at(row - 1);
            const double moneyFlow = typicalPrice * m_volumes.at(row);
            if (!std::isfinite(moneyFlow)) return;
            if (typicalPrice > previousPrice) positive += moneyFlow;
            else if (typicalPrice < previousPrice) negative += moneyFlow;
        }
        const double total = positive + negative;
        if (!std::isfinite(positive) || !std::isfinite(negative) || !std::isfinite(total)) break;
        if (positive == 0.0 && negative == 0.0) state.current = 50.0;
        else if (negative == 0.0) state.current = 100.0;
        else state.current = positive / total * 100.0;
        break;
    }
    case CustomIndicatorKind::Kdj: {
        if (!windowAverage(m_highs, indicator.period)
            || !windowAverage(m_lows, indicator.period)) {
            state.kValue = state.dValue = 50.0;
            break;
        }
        const double high = *std::max_element(m_highs.cend() - indicator.period, m_highs.cend());
        const double low = *std::min_element(m_lows.cend() - indicator.period, m_lows.cend());
        const double rsv = high > low ? (m_closes.last() - low) / (high - low) * 100.0 : 50.0;
        state.kValue += (rsv - state.kValue) / indicator.kPeriod;
        state.dValue += (state.kValue - state.dValue) / indicator.dPeriod;
        if (indicator.output == QStringLiteral("k")) state.current = state.kValue;
        else if (indicator.output == QStringLiteral("d")) state.current = state.dValue;
        else state.current = 3.0 * state.kValue - 2.0 * state.dValue;
        break;
    }
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
    case CustomIndicatorKind::Roc: {
        const int row = m_closes.size() - indicator.period - 1;
        if (row < 0) break;
        const double current = m_closes.last();
        const double previous = m_closes.at(row);
        if (!std::isfinite(current) || !std::isfinite(previous)
            || std::abs(previous) < 1.0e-12) {
            break;
        }
        state.current = (current - previous) / previous * 100.0;
        break;
    }
    case CustomIndicatorKind::Vwap: {
        static const QTimeZone shanghaiTimeZone("Asia/Shanghai");
        const QDate sessionDate = candle.startTime.isValid()
            ? candle.startTime.toTimeZone(shanghaiTimeZone).date() : QDate{};
        if (sessionDate.isValid() && state.sessionDate.isValid()
            && sessionDate != state.sessionDate) {
            state.sessionDate = sessionDate;
            state.vwapNumerator = 0.0;
            state.vwapVolume = 0.0;
        } else if (sessionDate.isValid() && !state.sessionDate.isValid()) {
            state.sessionDate = sessionDate;
        }
        const double close = m_closes.last();
        const double high = validPrice(candle.high) ? candle.high : missingValue();
        const double low = validPrice(candle.low) ? candle.low : missingValue();
        const double volume = validVolume(candle.volume) ? candle.volume : missingValue();
        if (std::isfinite(close) && std::isfinite(high) && std::isfinite(low)
            && std::isfinite(volume) && volume > 0.0) {
            const double typicalPrice = (high + low + close) / 3.0;
            state.vwapNumerator += typicalPrice * volume;
            state.vwapVolume += volume;
        }
        if (state.vwapVolume > 0.0) state.current = state.vwapNumerator / state.vwapVolume;
        break;
    }
    case CustomIndicatorKind::PpFlow: {
        if (m_closes.size() < 2) break;
        const double close = m_closes.last();
        const double previousClose = m_closes.at(m_closes.size() - 2);
        const double volume = m_volumes.last();
        if (std::isfinite(previousClose) && std::abs(previousClose) > 1.0e-12
            && std::isfinite(close) && std::isfinite(volume)) {
            state.cumulativePvt += (close - previousClose) / previousClose * volume;
        }
        state.current = state.cumulativePvt;
        break;
    }
    }
    // 新增指标不向条件表达式发布溢出结果，既有指标的数值口径保持不变。
    const bool expandedIndicator = indicator.kind == CustomIndicatorKind::Wma
        || indicator.kind == CustomIndicatorKind::Bias || indicator.kind == CustomIndicatorKind::Cci
        || indicator.kind == CustomIndicatorKind::Wr || indicator.kind == CustomIndicatorKind::Donchian
        || indicator.kind == CustomIndicatorKind::Obv || indicator.kind == CustomIndicatorKind::Mfi
        || indicator.kind == CustomIndicatorKind::Kdj;
    if (expandedIndicator && state.current && !std::isfinite(*state.current)) state.current.reset();
}

} // namespace pp
