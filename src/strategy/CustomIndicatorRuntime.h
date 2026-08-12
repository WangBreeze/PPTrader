#pragma once

#include "domain/MarketTypes.h"
#include "strategy/CustomStrategyCompiler.h"

#include <QDate>
#include <QVector>

#include <optional>

namespace pp {

class CustomIndicatorRuntime {
public:
    explicit CustomIndicatorRuntime(QVector<CompiledCustomIndicator> indicators = {});

    void reset();
    void append(const Candle &candle);
    std::optional<double> value(int indicatorIndex) const;
    int barCount() const;

private:
    struct EmaAccumulator {
        int seedCount = 0;
        double seedSum = 0.0;
        std::optional<double> value;
    };
    struct IndicatorState {
        EmaAccumulator primary;
        EmaAccumulator secondary;
        EmaAccumulator signal;
        std::optional<double> current;
        QDate sessionDate;
        double vwapNumerator = 0.0;
        double vwapVolume = 0.0;
        double cumulativePvt = 0.0;
    };

    static std::optional<double> windowAverage(const QVector<double> &values, int period);
    static std::optional<double> updateEma(EmaAccumulator *state, double input, int period);
    void updateIndicator(int index, const Candle &candle);

    QVector<CompiledCustomIndicator> m_indicators;
    QVector<IndicatorState> m_states;
    QVector<double> m_closes;
    QVector<double> m_volumes;
    QVector<double> m_trueRanges;
    std::optional<double> m_previousClose;
};

} // namespace pp
