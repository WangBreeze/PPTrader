#pragma once

#include "domain/MarketTypes.h"

#include <QAbstractListModel>
#include <QVector>

namespace pp {

struct CandlePoint {
    Candle candle;
    bool hasSignal = false;
    TradeSignal signal;
};

class CandleListModel final : public QAbstractListModel {
    Q_OBJECT

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
        SignalPriceRole
    };

    explicit CandleListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addPoint(const CandlePoint &point);
    void loadCandles(const QVector<Candle> &candles);
    Q_INVOKABLE void clear();

private:
    QVector<CandlePoint> m_points;
    int m_maxRows = 240;
};

} // 命名空间 pp

Q_DECLARE_METATYPE(pp::CandlePoint)
