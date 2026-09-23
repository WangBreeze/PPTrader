#include "visualization/ChartIndicatorPreferences.h"

#include <QSettings>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace pp {

namespace {

constexpr auto settingsGroup = "chart/indicators";

template<typename T>
bool assignIfChanged(T &target, const T &value)
{
    if (target == value) return false;
    target = value;
    return true;
}

} // namespace

ChartIndicatorPreferences::ChartIndicatorPreferences(QObject *parent)
    : QObject(parent)
{
    load();
}

bool ChartIndicatorPreferences::volumeLayerEnabled() const { return m_volumeLayerEnabled; }
bool ChartIndicatorPreferences::movingAverageLayerEnabled() const { return m_movingAverageLayerEnabled; }
bool ChartIndicatorPreferences::bollingerLayerEnabled() const { return m_bollingerLayerEnabled; }
bool ChartIndicatorPreferences::rsiLayerEnabled() const { return m_rsiLayerEnabled; }
bool ChartIndicatorPreferences::macdLayerEnabled() const { return m_macdLayerEnabled; }
int ChartIndicatorPreferences::movingAverageFastPeriod() const { return m_movingAverageFastPeriod; }
int ChartIndicatorPreferences::movingAverageSlowPeriod() const { return m_movingAverageSlowPeriod; }
int ChartIndicatorPreferences::bollingerPeriod() const { return m_bollingerPeriod; }
qreal ChartIndicatorPreferences::bollingerStandardDeviations() const { return m_bollingerStandardDeviations; }
int ChartIndicatorPreferences::rsiPeriod() const { return m_rsiPeriod; }
int ChartIndicatorPreferences::macdFastPeriod() const { return m_macdFastPeriod; }
int ChartIndicatorPreferences::macdSlowPeriod() const { return m_macdSlowPeriod; }
int ChartIndicatorPreferences::macdSignalPeriod() const { return m_macdSignalPeriod; }

#define PP_BOOL_PREFERENCE_SETTER(Method, Member, Key, Signal) \
    void ChartIndicatorPreferences::Method(bool enabled) \
    { \
        if (!assignIfChanged(Member, enabled)) return; \
        save(QStringLiteral(Key), enabled); \
        emit Signal(); \
    }

PP_BOOL_PREFERENCE_SETTER(setVolumeLayerEnabled, m_volumeLayerEnabled,
                          "volumeLayerEnabled", volumeLayerEnabledChanged)
PP_BOOL_PREFERENCE_SETTER(setMovingAverageLayerEnabled, m_movingAverageLayerEnabled,
                          "movingAverageLayerEnabled", movingAverageLayerEnabledChanged)
PP_BOOL_PREFERENCE_SETTER(setBollingerLayerEnabled, m_bollingerLayerEnabled,
                          "bollingerLayerEnabled", bollingerLayerEnabledChanged)
PP_BOOL_PREFERENCE_SETTER(setRsiLayerEnabled, m_rsiLayerEnabled,
                          "rsiLayerEnabled", rsiLayerEnabledChanged)
PP_BOOL_PREFERENCE_SETTER(setMacdLayerEnabled, m_macdLayerEnabled,
                          "macdLayerEnabled", macdLayerEnabledChanged)

#undef PP_BOOL_PREFERENCE_SETTER

void ChartIndicatorPreferences::setMovingAverageFastPeriod(int period)
{
    const int value = std::clamp(period, 1, m_movingAverageSlowPeriod - 1);
    if (!assignIfChanged(m_movingAverageFastPeriod, value)) return;
    save(QStringLiteral("movingAverageFastPeriod"), value);
    emit movingAverageFastPeriodChanged();
}

void ChartIndicatorPreferences::setMovingAverageSlowPeriod(int period)
{
    const int value = std::clamp(period, m_movingAverageFastPeriod + 1, 500);
    if (!assignIfChanged(m_movingAverageSlowPeriod, value)) return;
    save(QStringLiteral("movingAverageSlowPeriod"), value);
    emit movingAverageSlowPeriodChanged();
}

void ChartIndicatorPreferences::setBollingerPeriod(int period)
{
    const int value = std::clamp(period, 2, 500);
    if (!assignIfChanged(m_bollingerPeriod, value)) return;
    save(QStringLiteral("bollingerPeriod"), value);
    emit bollingerPeriodChanged();
}

void ChartIndicatorPreferences::setBollingerStandardDeviations(qreal standardDeviations)
{
    if (!std::isfinite(standardDeviations)) return;
    const qreal value = std::clamp(standardDeviations, qreal(0.1), qreal(10.0));
    if (qFuzzyCompare(m_bollingerStandardDeviations, value)) return;
    m_bollingerStandardDeviations = value;
    save(QStringLiteral("bollingerStandardDeviations"), value);
    emit bollingerStandardDeviationsChanged();
}

void ChartIndicatorPreferences::setRsiPeriod(int period)
{
    const int value = std::clamp(period, 2, 500);
    if (!assignIfChanged(m_rsiPeriod, value)) return;
    save(QStringLiteral("rsiPeriod"), value);
    emit rsiPeriodChanged();
}

void ChartIndicatorPreferences::setMacdFastPeriod(int period)
{
    const int value = std::clamp(period, 1, m_macdSlowPeriod - 1);
    if (!assignIfChanged(m_macdFastPeriod, value)) return;
    save(QStringLiteral("macdFastPeriod"), value);
    emit macdFastPeriodChanged();
}

void ChartIndicatorPreferences::setMacdSlowPeriod(int period)
{
    const int value = std::clamp(period, m_macdFastPeriod + 1, 500);
    if (!assignIfChanged(m_macdSlowPeriod, value)) return;
    save(QStringLiteral("macdSlowPeriod"), value);
    emit macdSlowPeriodChanged();
}

void ChartIndicatorPreferences::setMacdSignalPeriod(int period)
{
    const int value = std::clamp(period, 1, 500);
    if (!assignIfChanged(m_macdSignalPeriod, value)) return;
    save(QStringLiteral("macdSignalPeriod"), value);
    emit macdSignalPeriodChanged();
}

void ChartIndicatorPreferences::setMovingAveragePeriods(int fastPeriod, int slowPeriod)
{
    const int slow = std::clamp(slowPeriod, 2, 500);
    const int fast = std::clamp(fastPeriod, 1, slow - 1);
    const bool fastChanged = assignIfChanged(m_movingAverageFastPeriod, fast);
    const bool slowChanged = assignIfChanged(m_movingAverageSlowPeriod, slow);
    if (!fastChanged && !slowChanged) return;
    saveAll();
    if (fastChanged) emit movingAverageFastPeriodChanged();
    if (slowChanged) emit movingAverageSlowPeriodChanged();
}

void ChartIndicatorPreferences::setBollingerParameters(
    int period, qreal standardDeviations)
{
    if (!std::isfinite(standardDeviations)) return;
    const int normalizedPeriod = std::clamp(period, 2, 500);
    const qreal normalizedDeviation = std::clamp(
        standardDeviations, qreal(0.1), qreal(10.0));
    const bool periodChanged = assignIfChanged(m_bollingerPeriod, normalizedPeriod);
    const bool deviationChanged = !qFuzzyCompare(
        m_bollingerStandardDeviations, normalizedDeviation);
    if (deviationChanged) m_bollingerStandardDeviations = normalizedDeviation;
    if (!periodChanged && !deviationChanged) return;
    saveAll();
    if (periodChanged) emit bollingerPeriodChanged();
    if (deviationChanged) emit bollingerStandardDeviationsChanged();
}

void ChartIndicatorPreferences::setMacdPeriods(
    int fastPeriod, int slowPeriod, int signalPeriod)
{
    const int slow = std::clamp(slowPeriod, 2, 500);
    const int fast = std::clamp(fastPeriod, 1, slow - 1);
    const int signal = std::clamp(signalPeriod, 1, 500);
    const bool fastChanged = assignIfChanged(m_macdFastPeriod, fast);
    const bool slowChanged = assignIfChanged(m_macdSlowPeriod, slow);
    const bool signalChanged = assignIfChanged(m_macdSignalPeriod, signal);
    if (!fastChanged && !slowChanged && !signalChanged) return;
    saveAll();
    if (fastChanged) emit macdFastPeriodChanged();
    if (slowChanged) emit macdSlowPeriodChanged();
    if (signalChanged) emit macdSignalPeriodChanged();
}

void ChartIndicatorPreferences::resetDefaults()
{
    const bool volumeChanged = assignIfChanged(m_volumeLayerEnabled, true);
    const bool movingAverageChanged = assignIfChanged(m_movingAverageLayerEnabled, true);
    const bool bollingerChanged = assignIfChanged(m_bollingerLayerEnabled, false);
    const bool rsiChanged = assignIfChanged(m_rsiLayerEnabled, false);
    const bool macdChanged = assignIfChanged(m_macdLayerEnabled, false);
    const bool maFastChanged = assignIfChanged(m_movingAverageFastPeriod, 5);
    const bool maSlowChanged = assignIfChanged(m_movingAverageSlowPeriod, 20);
    const bool bollingerPeriodChangedValue = assignIfChanged(m_bollingerPeriod, 20);
    const bool bollingerDeviationChanged = !qFuzzyCompare(
        m_bollingerStandardDeviations, qreal(2.0));
    m_bollingerStandardDeviations = 2.0;
    const bool rsiPeriodChangedValue = assignIfChanged(m_rsiPeriod, 14);
    const bool macdFastChangedValue = assignIfChanged(m_macdFastPeriod, 12);
    const bool macdSlowChangedValue = assignIfChanged(m_macdSlowPeriod, 26);
    const bool macdSignalChangedValue = assignIfChanged(m_macdSignalPeriod, 9);
    saveAll();
    if (volumeChanged) emit volumeLayerEnabledChanged();
    if (movingAverageChanged) emit movingAverageLayerEnabledChanged();
    if (bollingerChanged) emit bollingerLayerEnabledChanged();
    if (rsiChanged) emit rsiLayerEnabledChanged();
    if (macdChanged) emit macdLayerEnabledChanged();
    if (maFastChanged) emit movingAverageFastPeriodChanged();
    if (maSlowChanged) emit movingAverageSlowPeriodChanged();
    if (bollingerPeriodChangedValue) emit bollingerPeriodChanged();
    if (bollingerDeviationChanged) emit bollingerStandardDeviationsChanged();
    if (rsiPeriodChangedValue) emit rsiPeriodChanged();
    if (macdFastChangedValue) emit macdFastPeriodChanged();
    if (macdSlowChangedValue) emit macdSlowPeriodChanged();
    if (macdSignalChangedValue) emit macdSignalPeriodChanged();
}

void ChartIndicatorPreferences::load()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    m_volumeLayerEnabled = settings.value(
        QStringLiteral("volumeLayerEnabled"), true).toBool();
    m_movingAverageLayerEnabled = settings.value(
        QStringLiteral("movingAverageLayerEnabled"), true).toBool();
    m_bollingerLayerEnabled = settings.value(
        QStringLiteral("bollingerLayerEnabled"), false).toBool();
    m_rsiLayerEnabled = settings.value(
        QStringLiteral("rsiLayerEnabled"), false).toBool();
    m_macdLayerEnabled = settings.value(
        QStringLiteral("macdLayerEnabled"), false).toBool();
    m_movingAverageSlowPeriod = std::clamp(settings.value(
        QStringLiteral("movingAverageSlowPeriod"), 20).toInt(), 2, 500);
    m_movingAverageFastPeriod = std::clamp(settings.value(
        QStringLiteral("movingAverageFastPeriod"), 5).toInt(),
        1, m_movingAverageSlowPeriod - 1);
    m_bollingerPeriod = std::clamp(settings.value(
        QStringLiteral("bollingerPeriod"), 20).toInt(), 2, 500);
    const qreal deviation = settings.value(
        QStringLiteral("bollingerStandardDeviations"), 2.0).toReal();
    m_bollingerStandardDeviations = std::isfinite(deviation)
        ? std::clamp(deviation, qreal(0.1), qreal(10.0)) : qreal(2.0);
    m_rsiPeriod = std::clamp(settings.value(
        QStringLiteral("rsiPeriod"), 14).toInt(), 2, 500);
    m_macdSlowPeriod = std::clamp(settings.value(
        QStringLiteral("macdSlowPeriod"), 26).toInt(), 2, 500);
    m_macdFastPeriod = std::clamp(settings.value(
        QStringLiteral("macdFastPeriod"), 12).toInt(),
        1, m_macdSlowPeriod - 1);
    m_macdSignalPeriod = std::clamp(settings.value(
        QStringLiteral("macdSignalPeriod"), 9).toInt(), 1, 500);
    settings.endGroup();
}

void ChartIndicatorPreferences::save(const QString &key, const QVariant &value) const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    settings.setValue(key, value);
    settings.endGroup();
    settings.sync();
}

void ChartIndicatorPreferences::saveAll() const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(settingsGroup));
    settings.setValue(QStringLiteral("volumeLayerEnabled"), m_volumeLayerEnabled);
    settings.setValue(QStringLiteral("movingAverageLayerEnabled"),
                      m_movingAverageLayerEnabled);
    settings.setValue(QStringLiteral("bollingerLayerEnabled"), m_bollingerLayerEnabled);
    settings.setValue(QStringLiteral("rsiLayerEnabled"), m_rsiLayerEnabled);
    settings.setValue(QStringLiteral("macdLayerEnabled"), m_macdLayerEnabled);
    settings.setValue(QStringLiteral("movingAverageFastPeriod"),
                      m_movingAverageFastPeriod);
    settings.setValue(QStringLiteral("movingAverageSlowPeriod"),
                      m_movingAverageSlowPeriod);
    settings.setValue(QStringLiteral("bollingerPeriod"), m_bollingerPeriod);
    settings.setValue(QStringLiteral("bollingerStandardDeviations"),
                      m_bollingerStandardDeviations);
    settings.setValue(QStringLiteral("rsiPeriod"), m_rsiPeriod);
    settings.setValue(QStringLiteral("macdFastPeriod"), m_macdFastPeriod);
    settings.setValue(QStringLiteral("macdSlowPeriod"), m_macdSlowPeriod);
    settings.setValue(QStringLiteral("macdSignalPeriod"), m_macdSignalPeriod);
    settings.endGroup();
    settings.sync();
}

} // namespace pp
