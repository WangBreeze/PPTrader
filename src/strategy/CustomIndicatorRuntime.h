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
    int residentObservationCount() const;

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
        double cumulativeObv = 0.0;
        std::optional<double> obvPreviousClose;
        double kValue = 50.0;
        double dValue = 50.0;
        QVector<double> stochRawK;
        QVector<double> stochSmoothedK;
        int superTrCount = 0;
        double superTrSum = 0.0;
        std::optional<double> superAtr;
        double superUpper = 0.0;
        double superLower = 0.0;
        bool superUp = false;
    };

    static std::optional<double> windowAverage(const QVector<double> &values, int period);
    static std::optional<double> updateEma(EmaAccumulator *state, double input, int period);
    void updateIndicator(int index, const Candle &candle);

    QVector<CompiledCustomIndicator> m_indicators;
    QVector<IndicatorState> m_states;
    QVector<double> m_closes;
    QVector<double> m_volumes;
    QVector<double> m_trueRanges;
    QVector<double> m_typicalPrices;
    QVector<double> m_highs;
    QVector<double> m_lows;
    std::optional<double> m_previousClose;
    int m_observationCount = 0;
    int m_historyLimit = 2;
};

} // namespace pp
