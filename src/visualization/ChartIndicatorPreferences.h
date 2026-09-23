#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace pp {

class ChartIndicatorPreferences : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool volumeLayerEnabled READ volumeLayerEnabled WRITE setVolumeLayerEnabled
                   NOTIFY volumeLayerEnabledChanged)
    Q_PROPERTY(bool movingAverageLayerEnabled READ movingAverageLayerEnabled
                   WRITE setMovingAverageLayerEnabled NOTIFY movingAverageLayerEnabledChanged)
    Q_PROPERTY(bool bollingerLayerEnabled READ bollingerLayerEnabled
                   WRITE setBollingerLayerEnabled NOTIFY bollingerLayerEnabledChanged)
    Q_PROPERTY(bool rsiLayerEnabled READ rsiLayerEnabled WRITE setRsiLayerEnabled
                   NOTIFY rsiLayerEnabledChanged)
    Q_PROPERTY(bool macdLayerEnabled READ macdLayerEnabled WRITE setMacdLayerEnabled
                   NOTIFY macdLayerEnabledChanged)
    Q_PROPERTY(int movingAverageFastPeriod READ movingAverageFastPeriod
                   WRITE setMovingAverageFastPeriod NOTIFY movingAverageFastPeriodChanged)
    Q_PROPERTY(int movingAverageSlowPeriod READ movingAverageSlowPeriod
                   WRITE setMovingAverageSlowPeriod NOTIFY movingAverageSlowPeriodChanged)
    Q_PROPERTY(int bollingerPeriod READ bollingerPeriod WRITE setBollingerPeriod
                   NOTIFY bollingerPeriodChanged)
    Q_PROPERTY(qreal bollingerStandardDeviations READ bollingerStandardDeviations
                   WRITE setBollingerStandardDeviations
                   NOTIFY bollingerStandardDeviationsChanged)
    Q_PROPERTY(int rsiPeriod READ rsiPeriod WRITE setRsiPeriod NOTIFY rsiPeriodChanged)
    Q_PROPERTY(int macdFastPeriod READ macdFastPeriod WRITE setMacdFastPeriod
                   NOTIFY macdFastPeriodChanged)
    Q_PROPERTY(int macdSlowPeriod READ macdSlowPeriod WRITE setMacdSlowPeriod
                   NOTIFY macdSlowPeriodChanged)
    Q_PROPERTY(int macdSignalPeriod READ macdSignalPeriod WRITE setMacdSignalPeriod
                   NOTIFY macdSignalPeriodChanged)

public:
    explicit ChartIndicatorPreferences(QObject *parent = nullptr);

    bool volumeLayerEnabled() const;
    bool movingAverageLayerEnabled() const;
    bool bollingerLayerEnabled() const;
    bool rsiLayerEnabled() const;
    bool macdLayerEnabled() const;
    int movingAverageFastPeriod() const;
    int movingAverageSlowPeriod() const;
    int bollingerPeriod() const;
    qreal bollingerStandardDeviations() const;
    int rsiPeriod() const;
    int macdFastPeriod() const;
    int macdSlowPeriod() const;
    int macdSignalPeriod() const;

    void setVolumeLayerEnabled(bool enabled);
    void setMovingAverageLayerEnabled(bool enabled);
    void setBollingerLayerEnabled(bool enabled);
    void setRsiLayerEnabled(bool enabled);
    void setMacdLayerEnabled(bool enabled);
    void setMovingAverageFastPeriod(int period);
    void setMovingAverageSlowPeriod(int period);
    void setBollingerPeriod(int period);
    void setBollingerStandardDeviations(qreal standardDeviations);
    void setRsiPeriod(int period);
    void setMacdFastPeriod(int period);
    void setMacdSlowPeriod(int period);
    void setMacdSignalPeriod(int period);

    Q_INVOKABLE void setMovingAveragePeriods(int fastPeriod, int slowPeriod);
    Q_INVOKABLE void setBollingerParameters(int period, qreal standardDeviations);
    Q_INVOKABLE void setMacdPeriods(int fastPeriod, int slowPeriod, int signalPeriod);
    Q_INVOKABLE void resetDefaults();

signals:
    void volumeLayerEnabledChanged();
    void movingAverageLayerEnabledChanged();
    void bollingerLayerEnabledChanged();
    void rsiLayerEnabledChanged();
    void macdLayerEnabledChanged();
    void movingAverageFastPeriodChanged();
    void movingAverageSlowPeriodChanged();
    void bollingerPeriodChanged();
    void bollingerStandardDeviationsChanged();
    void rsiPeriodChanged();
    void macdFastPeriodChanged();
    void macdSlowPeriodChanged();
    void macdSignalPeriodChanged();

private:
    void load();
    void save(const QString &key, const QVariant &value) const;
    void saveAll() const;

    bool m_volumeLayerEnabled = true;
    bool m_movingAverageLayerEnabled = true;
    bool m_bollingerLayerEnabled = false;
    bool m_rsiLayerEnabled = false;
    bool m_macdLayerEnabled = false;
    int m_movingAverageFastPeriod = 5;
    int m_movingAverageSlowPeriod = 20;
    int m_bollingerPeriod = 20;
    qreal m_bollingerStandardDeviations = 2.0;
    int m_rsiPeriod = 14;
    int m_macdFastPeriod = 12;
    int m_macdSlowPeriod = 26;
    int m_macdSignalPeriod = 9;
};

} // namespace pp
