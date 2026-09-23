#include "models/RealtimeChartPaneListModel.h"

#include "services/TencentMinuteBarService.h"

#include <algorithm>

namespace pp {

namespace {
constexpr int kMaximumCharts = 10;
constexpr int kDefaultPeriods[] = {60, 300, 600, 900, 1800, 3600};
}

RealtimeChartPaneListModel::RealtimeChartPaneListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    addChart(60);
}

RealtimeChartPaneListModel::~RealtimeChartPaneListModel()
{
    qDeleteAll(m_panes);
}

int RealtimeChartPaneListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_panes.size();
}

QVariant RealtimeChartPaneListModel::data(
    const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_panes.size())
        return {};
    const Pane *pane = m_panes.at(index.row());
    switch (role) {
    case ChartIdRole: return pane->chartId;
    case PeriodSecondsRole: return pane->periodSeconds;
    case PeriodLabelRole: return periodLabel(pane->periodSeconds);
    case ChartModelRole:
        return QVariant::fromValue(static_cast<QObject *>(pane->model));
    case PointCountRole: return pane->model->count();
    default: return {};
    }
}

QHash<int, QByteArray> RealtimeChartPaneListModel::roleNames() const
{
    return {
        {ChartIdRole, "chartId"},
        {PeriodSecondsRole, "periodSeconds"},
        {PeriodLabelRole, "periodLabel"},
        {ChartModelRole, "chartModel"},
        {PointCountRole, "pointCount"}
    };
}

int RealtimeChartPaneListModel::count() const { return m_panes.size(); }

QString RealtimeChartPaneListModel::addChart(int periodSeconds)
{
    if (m_panes.size() >= kMaximumCharts) return {};
    if (periodSeconds == 0) periodSeconds = nextDefaultPeriod();
    if (!supportsPeriod(periodSeconds)) return {};

    const int row = m_panes.size();
    beginInsertRows({}, row, row);
    auto *pane = new Pane;
    pane->chartId = QStringLiteral("chart-%1").arg(++m_sequence);
    pane->periodSeconds = periodSeconds;
    pane->model = new LocalCandleSeriesListModel(this);
    m_panes.append(pane);
    rebuild(pane);
    endInsertRows();
    emit countChanged();
    return pane->chartId;
}

bool RealtimeChartPaneListModel::removeChart(const QString &chartId)
{
    if (m_panes.size() <= 1) return false;
    const int row = rowForId(chartId);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    Pane *pane = m_panes.takeAt(row);
    endRemoveRows();
    pane->model->deleteLater();
    delete pane;
    emit countChanged();
    return true;
}

bool RealtimeChartPaneListModel::setChartPeriod(
    const QString &chartId, int periodSeconds)
{
    if (!supportsPeriod(periodSeconds)) return false;
    const int row = rowForId(chartId);
    if (row < 0) return false;
    Pane *pane = m_panes.at(row);
    if (pane->periodSeconds == periodSeconds) return true;
    pane->periodSeconds = periodSeconds;
    rebuild(pane);
    emit dataChanged(index(row), index(row),
                     {PeriodSecondsRole, PeriodLabelRole,
                      ChartModelRole, PointCountRole});
    return true;
}

void RealtimeChartPaneListModel::setRawCandles(
    const QVector<Candle> &minuteCandles)
{
    m_rawCandles = minuteCandles;
    for (Pane *pane : m_panes) rebuild(pane);
    if (!m_panes.isEmpty())
        emit dataChanged(index(0), index(m_panes.size() - 1),
                         {ChartModelRole, PointCountRole});
}

void RealtimeChartPaneListModel::clearSeries()
{
    m_rawCandles.clear();
    for (Pane *pane : m_panes) pane->model->clear();
    if (!m_panes.isEmpty())
        emit dataChanged(index(0), index(m_panes.size() - 1),
                         {ChartModelRole, PointCountRole});
}

int RealtimeChartPaneListModel::countForPeriod(int periodSeconds) const
{
    for (const Pane *pane : m_panes) {
        if (pane->periodSeconds == periodSeconds) return pane->model->count();
    }
    return 0;
}

QAbstractItemModel *RealtimeChartPaneListModel::modelForPeriod(
    int periodSeconds) const
{
    for (const Pane *pane : m_panes) {
        if (pane->periodSeconds == periodSeconds) return pane->model;
    }
    return nullptr;
}

QVariantMap RealtimeChartPaneListModel::get(int row) const
{
    if (row < 0 || row >= m_panes.size()) return {};
    const Pane *pane = m_panes.at(row);
    return {
        {QStringLiteral("chartId"), pane->chartId},
        {QStringLiteral("periodSeconds"), pane->periodSeconds},
        {QStringLiteral("periodLabel"), periodLabel(pane->periodSeconds)},
        {QStringLiteral("chartModel"),
         QVariant::fromValue(static_cast<QObject *>(pane->model))},
        {QStringLiteral("pointCount"), pane->model->count()}
    };
}

bool RealtimeChartPaneListModel::supportsPeriod(int periodSeconds)
{
    return std::find(std::begin(kDefaultPeriods), std::end(kDefaultPeriods),
                     periodSeconds) != std::end(kDefaultPeriods);
}

QString RealtimeChartPaneListModel::periodLabel(int periodSeconds)
{
    switch (periodSeconds) {
    case 60: return QStringLiteral("1 分钟");
    case 300: return QStringLiteral("5 分钟");
    case 600: return QStringLiteral("10 分钟");
    case 900: return QStringLiteral("15 分钟");
    case 1800: return QStringLiteral("30 分钟");
    case 3600: return QStringLiteral("60 分钟");
    default: return QStringLiteral("未知周期");
    }
}

int RealtimeChartPaneListModel::rowForId(const QString &chartId) const
{
    for (int row = 0; row < m_panes.size(); ++row) {
        if (m_panes.at(row)->chartId == chartId) return row;
    }
    return -1;
}

int RealtimeChartPaneListModel::nextDefaultPeriod() const
{
    for (const int period : kDefaultPeriods) {
        const bool used = std::any_of(
            m_panes.cbegin(), m_panes.cend(), [period](const Pane *pane) {
                return pane->periodSeconds == period;
            });
        if (!used) return period;
    }
    return 60;
}

void RealtimeChartPaneListModel::rebuild(Pane *pane)
{
    const QVector<Candle> candles = TencentMinuteBarService::aggregateCandles(
        m_rawCandles, pane->periodSeconds);
    QVector<LocalCandleSeriesPoint> points;
    points.reserve(candles.size());
    for (const Candle &candle : candles) {
        LocalCandleSeriesPoint point;
        point.candle = candle;
        point.endTime = candle.startTime.addSecs(candle.periodSeconds);
        point.sourceCount = std::max(1, candle.periodSeconds / 60);
        points.append(point);
    }
    pane->model->setPoints(points);
}

} // namespace pp
