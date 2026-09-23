#pragma once

#include "domain/MarketTypes.h"
#include "models/LocalCandleSeriesListModel.h"

#include <QAbstractListModel>

namespace pp {

// 一个实时行情子窗口右侧的动态图表列表。所有图表共享同一份 1 分钟
// 内存行情，但各自保留周期和派生模型，最多 10 张。
class RealtimeChartPaneListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        ChartIdRole = Qt::UserRole + 1,
        PeriodSecondsRole,
        PeriodLabelRole,
        ChartModelRole,
        PointCountRole
    };

    explicit RealtimeChartPaneListModel(QObject *parent = nullptr);
    ~RealtimeChartPaneListModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    QString addChart(int periodSeconds = 0);
    bool removeChart(const QString &chartId);
    bool setChartPeriod(const QString &chartId, int periodSeconds);
    void setRawCandles(const QVector<Candle> &minuteCandles);
    void clearSeries();
    int countForPeriod(int periodSeconds) const;
    QAbstractItemModel *modelForPeriod(int periodSeconds) const;
    QVariantMap get(int row) const;

    static bool supportsPeriod(int periodSeconds);
    static QString periodLabel(int periodSeconds);

signals:
    void countChanged();

private:
    struct Pane {
        QString chartId;
        int periodSeconds = 60;
        LocalCandleSeriesListModel *model = nullptr;
    };

    int rowForId(const QString &chartId) const;
    int nextDefaultPeriod() const;
    void rebuild(Pane *pane);

    QVector<Pane *> m_panes;
    QVector<Candle> m_rawCandles;
    quint64 m_sequence = 0;
};

} // namespace pp
