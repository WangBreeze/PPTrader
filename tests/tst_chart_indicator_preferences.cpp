#include "visualization/ChartIndicatorPreferences.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace pp;

class ChartIndicatorPreferencesTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void persistsValidatedPreferencesAcrossInstances();
    void normalizesMalformedStoredValues();
    void resetRestoresDocumentedDefaults();

private:
    QTemporaryDir m_settingsDirectory;
};

void ChartIndicatorPreferencesTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("PPTraderTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ChartIndicatorPreferences"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       m_settingsDirectory.path());
}

void ChartIndicatorPreferencesTest::cleanup()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void ChartIndicatorPreferencesTest::persistsValidatedPreferencesAcrossInstances()
{
    {
        ChartIndicatorPreferences preferences;
        preferences.setVolumeLayerEnabled(false);
        preferences.setBollingerLayerEnabled(true);
        preferences.setRsiLayerEnabled(true);
        preferences.setMacdLayerEnabled(true);
        preferences.setMovingAveragePeriods(8, 34);
        preferences.setBollingerParameters(30, 2.5);
        preferences.setRsiPeriod(21);
        preferences.setMacdPeriods(7, 31, 11);
    }

    ChartIndicatorPreferences restored;
    QVERIFY(!restored.volumeLayerEnabled());
    QVERIFY(restored.bollingerLayerEnabled());
    QVERIFY(restored.rsiLayerEnabled());
    QVERIFY(restored.macdLayerEnabled());
    QCOMPARE(restored.movingAverageFastPeriod(), 8);
    QCOMPARE(restored.movingAverageSlowPeriod(), 34);
    QCOMPARE(restored.bollingerPeriod(), 30);
    QCOMPARE(restored.bollingerStandardDeviations(), qreal(2.5));
    QCOMPARE(restored.rsiPeriod(), 21);
    QCOMPARE(restored.macdFastPeriod(), 7);
    QCOMPARE(restored.macdSlowPeriod(), 31);
    QCOMPARE(restored.macdSignalPeriod(), 11);
}

void ChartIndicatorPreferencesTest::normalizesMalformedStoredValues()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("chart/indicators"));
    settings.setValue(QStringLiteral("movingAverageFastPeriod"), 900);
    settings.setValue(QStringLiteral("movingAverageSlowPeriod"), -20);
    settings.setValue(QStringLiteral("bollingerPeriod"), 0);
    settings.setValue(QStringLiteral("bollingerStandardDeviations"), 99.0);
    settings.setValue(QStringLiteral("rsiPeriod"), 999);
    settings.setValue(QStringLiteral("macdFastPeriod"), 500);
    settings.setValue(QStringLiteral("macdSlowPeriod"), 1);
    settings.setValue(QStringLiteral("macdSignalPeriod"), 0);
    settings.endGroup();
    settings.sync();

    ChartIndicatorPreferences preferences;
    QCOMPARE(preferences.movingAverageFastPeriod(), 1);
    QCOMPARE(preferences.movingAverageSlowPeriod(), 2);
    QCOMPARE(preferences.bollingerPeriod(), 2);
    QCOMPARE(preferences.bollingerStandardDeviations(), qreal(10.0));
    QCOMPARE(preferences.rsiPeriod(), 500);
    QCOMPARE(preferences.macdFastPeriod(), 1);
    QCOMPARE(preferences.macdSlowPeriod(), 2);
    QCOMPARE(preferences.macdSignalPeriod(), 1);

    preferences.setMovingAveragePeriods(500, 5);
    QCOMPARE(preferences.movingAverageFastPeriod(), 4);
    QCOMPARE(preferences.movingAverageSlowPeriod(), 5);
    preferences.setMacdPeriods(90, 30, 800);
    QCOMPARE(preferences.macdFastPeriod(), 29);
    QCOMPARE(preferences.macdSlowPeriod(), 30);
    QCOMPARE(preferences.macdSignalPeriod(), 500);
}

void ChartIndicatorPreferencesTest::resetRestoresDocumentedDefaults()
{
    ChartIndicatorPreferences preferences;
    preferences.setVolumeLayerEnabled(false);
    preferences.setMovingAverageLayerEnabled(false);
    preferences.setBollingerLayerEnabled(true);
    preferences.setRsiLayerEnabled(true);
    preferences.setMacdLayerEnabled(true);
    preferences.setMovingAveragePeriods(9, 40);
    preferences.setBollingerParameters(35, 3.0);
    preferences.setRsiPeriod(28);
    preferences.setMacdPeriods(8, 35, 12);
    preferences.resetDefaults();

    QVERIFY(preferences.volumeLayerEnabled());
    QVERIFY(preferences.movingAverageLayerEnabled());
    QVERIFY(!preferences.bollingerLayerEnabled());
    QVERIFY(!preferences.rsiLayerEnabled());
    QVERIFY(!preferences.macdLayerEnabled());
    QCOMPARE(preferences.movingAverageFastPeriod(), 5);
    QCOMPARE(preferences.movingAverageSlowPeriod(), 20);
    QCOMPARE(preferences.bollingerPeriod(), 20);
    QCOMPARE(preferences.bollingerStandardDeviations(), qreal(2.0));
    QCOMPARE(preferences.rsiPeriod(), 14);
    QCOMPARE(preferences.macdFastPeriod(), 12);
    QCOMPARE(preferences.macdSlowPeriod(), 26);
    QCOMPARE(preferences.macdSignalPeriod(), 9);
}

QTEST_GUILESS_MAIN(ChartIndicatorPreferencesTest)
#include "tst_chart_indicator_preferences.moc"
