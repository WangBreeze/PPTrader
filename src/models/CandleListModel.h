#pragma once

#include "domain/BacktestTypes.h"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

namespace pp {

struct CandlePoint {
    Candle candle;
    bool hasSignal = false;
    TradeSignal signal;
};

class CandleListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SymbolRole = Qt::UserRole + 1,
        TimeRole,
        OpenRole,
        HighRole,
        LowRole,
        CloseRole,
        VolumeRole,
        HasSignalRole,
        SignalSideRole,
        SignalReasonRole,
        SignalPriceRole,
        TimestampRole,
        EndTimeRole,
        PeriodSecondsRole,
        SourceRole,
        SourceCountRole
    };

    explicit CandleListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    Q_INVOKABLE QVariantMap get(int row) const;

    void addPoint(const CandlePoint &point);
    void loadCandles(const QVector<Candle> &candles);
    void loadBacktestResult(const BacktestResult &result);
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    QVector<CandlePoint> m_points;
    int m_maxRows = 5000;
};

} // 命名空间 pp

Q_DECLARE_METATYPE(pp::CandlePoint)
