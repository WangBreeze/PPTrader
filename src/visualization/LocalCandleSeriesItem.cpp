#include "visualization/LocalCandleSeriesItem.h"

#include "models/LocalCandleSeriesListModel.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QLocale>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pp {

namespace {

QSGGeometryNode *lineNode(int vertexCount, const QColor &color)
{
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertexCount);
    geometry->setDrawingMode(QSGGeometry::DrawLines);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

QSGGeometryNode *triangleNode(int vertexCount, const QColor &color)
{
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertexCount);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(color);
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

int seriesSegmentVertexCount(const QVector<double> &values, int start, int count)
{
    int vertices = 0;
    const int end = std::min(int(values.size()), start + count);
    for (int index = std::max(1, start + 1); index < end; ++index) {
        if (std::isfinite(values.at(index - 1)) && std::isfinite(values.at(index)))
            vertices += 2;
    }
    return vertices;
}

QSGGeometryNode *seriesNode(const QVector<double> &values, int start, int count,
                            qreal width, qreal top, qreal height,
                            const LocalCandlePlotRange &range,
                            const QColor &color)
{
    const int vertexCount = seriesSegmentVertexCount(values, start, count);
    if (vertexCount <= 0) return nullptr;
    QSGGeometryNode *node = lineNode(vertexCount, color);
    auto *vertices = node->geometry()->vertexDataAsPoint2D();
    int offset = 0;
    const int end = std::min(int(values.size()), start + count);
    for (int index = std::max(1, start + 1); index < end; ++index) {
        const double previous = values.at(index - 1);
        const double current = values.at(index);
        if (!std::isfinite(previous) || !std::isfinite(current)) continue;
        const int previousLocal = index - 1 - start;
        const int currentLocal = index - start;
        vertices[offset++].set(
            float(LocalCandlePlotData::xFor(previousLocal, count, width)),
            float(top + LocalCandlePlotData::yFor(previous, range, height)));
        vertices[offset++].set(
            float(LocalCandlePlotData::xFor(currentLocal, count, width)),
            float(top + LocalCandlePlotData::yFor(current, range, height)));
    }
    return node;
}

QSGGeometryNode *lodSeriesNode(
    const QVector<double> &values,
    const QVector<LocalCandleRenderBucket> &buckets,
    const LocalCandleViewport &viewport, qreal width, qreal top, qreal height,
    const LocalCandlePlotRange &range, const QColor &color)
{
    QVector<QPointF> lineVertices;
    lineVertices.reserve(buckets.size() * 4);
    bool hasPrevious = false;
    int previousIndex = -1;
    double previousValue = 0.0;
    auto appendSample = [&](int index, double value) {
        if (hasPrevious) {
            const int previousLocal = previousIndex - viewport.start;
            const int currentLocal = index - viewport.start;
            lineVertices.append(QPointF(
                LocalCandlePlotData::xFor(previousLocal, viewport.count, width),
                top + LocalCandlePlotData::yFor(previousValue, range, height)));
            lineVertices.append(QPointF(
                LocalCandlePlotData::xFor(currentLocal, viewport.count, width),
                top + LocalCandlePlotData::yFor(value, range, height)));
        }
        previousIndex = index;
        previousValue = value;
        hasPrevious = true;
    };
    for (const LocalCandleRenderBucket &bucket : buckets) {
        int minimumIndex = -1;
        int maximumIndex = -1;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        const int end = std::min(bucket.lastIndex, int(values.size()) - 1);
        for (int index = bucket.firstIndex; index <= end; ++index) {
            const double value = values.at(index);
            if (!std::isfinite(value)) continue;
            if (value < minimum) {
                minimum = value;
                minimumIndex = index;
            }
            if (value > maximum) {
                maximum = value;
                maximumIndex = index;
            }
        }
        if (minimumIndex < 0) {
            hasPrevious = false;
            continue;
        }
        if (minimumIndex == maximumIndex) {
            appendSample(minimumIndex, minimum);
        } else if (minimumIndex < maximumIndex) {
            appendSample(minimumIndex, minimum);
            appendSample(maximumIndex, maximum);
        } else {
            appendSample(maximumIndex, maximum);
            appendSample(minimumIndex, minimum);
        }
    }
    if (lineVertices.isEmpty()) return nullptr;
    QSGGeometryNode *node = lineNode(lineVertices.size(), color);
    auto *vertices = node->geometry()->vertexDataAsPoint2D();
    for (int index = 0; index < lineVertices.size(); ++index) {
        vertices[index].set(float(lineVertices.at(index).x()),
                            float(lineVertices.at(index).y()));
    }
    return node;
}

} // namespace

LocalCandleSeriesItem::LocalCandleSeriesItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QAbstractItemModel *LocalCandleSeriesItem::model() const { return m_model.data(); }

void LocalCandleSeriesItem::setModel(QAbstractItemModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
            // A reset means the series identity may have changed (symbol/day/source).
            // Do not carry a viewport from the previous series into the new one.
            m_viewport = {};
            m_pendingPrependedRows = 0;
            if (!qFuzzyCompare(m_priceScaleFactor, qreal(1.0))) {
                m_priceScaleFactor = 1.0;
                emit priceScaleChanged();
            }
            rebuildModelData();
        });
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &) {
            if (!updateTypedModelRows(topLeft.row(), bottomRight.row()))
                rebuildModelData();
        });
        connect(m_model, &QAbstractItemModel::rowsInserted, this,
                [this](const QModelIndex &, int first, int last) {
            if (appendTypedModelRows(first, last)) return;
            if (first == 0 && last >= first && !m_points.isEmpty())
                m_pendingPrependedRows += last - first + 1;
            rebuildModelData();
        });
        connect(m_model, &QAbstractItemModel::rowsRemoved,
                this, &LocalCandleSeriesItem::rebuildModelData);
        connect(m_model, &QObject::destroyed, this, [this]() {
            m_model = nullptr;
            rebuildModelData();
            emit modelChanged();
        });
    }
    rebuildModelData();
    emit modelChanged();
}

int LocalCandleSeriesItem::pointCount() const { return m_points.size(); }
double LocalCandleSeriesItem::minimumPrice() const { return m_range.minimum; }
double LocalCandleSeriesItem::maximumPrice() const { return m_range.maximum; }
int LocalCandleSeriesItem::visibleStart() const { return m_viewport.start; }
int LocalCandleSeriesItem::visibleCount() const { return m_viewport.count; }
int LocalCandleSeriesItem::renderBucketCount() const { return m_renderBucketCount; }
bool LocalCandleSeriesItem::levelOfDetailActive() const
{
    return m_renderBucketCount > 0 && m_renderBucketCount < m_viewport.count;
}
qreal LocalCandleSeriesItem::averageBarsPerRenderBucket() const
{
    return m_renderBucketCount > 0
        ? qreal(m_viewport.count) / qreal(m_renderBucketCount) : 0.0;
}
bool LocalCandleSeriesItem::viewportAtLeftEdge() const { return m_viewport.start == 0; }
bool LocalCandleSeriesItem::viewportAtRightEdge() const
{
    return m_viewport.count > 0
        && m_viewport.start + m_viewport.count >= m_points.size();
}
qreal LocalCandleSeriesItem::priceScaleFactor() const { return m_priceScaleFactor; }
bool LocalCandleSeriesItem::automaticPriceRange() const
{
    return qFuzzyCompare(m_priceScaleFactor, qreal(1.0));
}
int LocalCandleSeriesItem::geometryVertexCount() const { return m_geometryVertexCount; }
int LocalCandleSeriesItem::signalCount() const { return m_signalCount; }
bool LocalCandleSeriesItem::showSignals() const { return m_showSignals; }
bool LocalCandleSeriesItem::showVolume() const { return m_showVolume; }
bool LocalCandleSeriesItem::showMovingAverages() const { return m_showMovingAverages; }
bool LocalCandleSeriesItem::showBollingerBands() const { return m_showBollingerBands; }
bool LocalCandleSeriesItem::showRsi() const { return m_showRsi; }
bool LocalCandleSeriesItem::showMacd() const { return m_showMacd; }
int LocalCandleSeriesItem::movingAverageFastPeriod() const
{
    return m_movingAverageFastPeriod;
}
int LocalCandleSeriesItem::movingAverageSlowPeriod() const
{
    return m_movingAverageSlowPeriod;
}
int LocalCandleSeriesItem::bollingerPeriod() const { return m_bollingerPeriod; }
qreal LocalCandleSeriesItem::bollingerStandardDeviations() const
{
    return m_bollingerStandardDeviations;
}
int LocalCandleSeriesItem::rsiPeriod() const { return m_rsiPeriod; }
int LocalCandleSeriesItem::macdFastPeriod() const { return m_macdFastPeriod; }
int LocalCandleSeriesItem::macdSlowPeriod() const { return m_macdSlowPeriod; }
int LocalCandleSeriesItem::macdSignalPeriod() const { return m_macdSignalPeriod; }
qreal LocalCandleSeriesItem::pricePlotRatio() const
{
    const int oscillatorCount = int(m_showRsi) + int(m_showMacd);
    if (oscillatorCount == 0) return m_showVolume ? qreal(0.72) : qreal(1.0);
    if (oscillatorCount == 1) return m_showVolume ? qreal(0.52) : qreal(0.72);
    return m_showVolume ? qreal(0.42) : qreal(0.56);
}
qreal LocalCandleSeriesItem::volumePlotTop() const
{
    if (!m_showVolume) return 0.0;
    const int oscillatorCount = int(m_showRsi) + int(m_showMacd);
    return oscillatorCount == 0 ? qreal(0.78)
        : oscillatorCount == 1 ? qreal(0.56) : qreal(0.46);
}
qreal LocalCandleSeriesItem::volumePlotRatio() const
{
    if (!m_showVolume) return 0.0;
    const int oscillatorCount = int(m_showRsi) + int(m_showMacd);
    return oscillatorCount == 0 ? qreal(0.20)
        : oscillatorCount == 1 ? qreal(0.14) : qreal(0.12);
}
qreal LocalCandleSeriesItem::rsiPlotTop() const
{
    if (!m_showRsi) return 0.0;
    const int oscillatorCount = int(m_showRsi) + int(m_showMacd);
    if (oscillatorCount == 1) return m_showVolume ? qreal(0.74) : qreal(0.76);
    return m_showVolume ? qreal(0.62) : qreal(0.60);
}
qreal LocalCandleSeriesItem::rsiPlotRatio() const
{
    if (!m_showRsi) return 0.0;
    const int oscillatorCount = int(m_showRsi) + int(m_showMacd);
    if (oscillatorCount == 1) return m_showVolume ? qreal(0.24) : qreal(0.22);
    return m_showVolume ? qreal(0.16) : qreal(0.18);
}
qreal LocalCandleSeriesItem::macdPlotTop() const
{
    if (!m_showMacd) return 0.0;
    if (m_showRsi) return qreal(0.82);
    return m_showVolume ? qreal(0.74) : qreal(0.76);
}
qreal LocalCandleSeriesItem::macdPlotRatio() const
{
    if (!m_showMacd) return 0.0;
    if (m_showRsi) return m_showVolume ? qreal(0.16) : qreal(0.18);
    return m_showVolume ? qreal(0.24) : qreal(0.22);
}
double LocalCandleSeriesItem::maximumVolume() const { return m_maximumVolume; }
int LocalCandleSeriesItem::volumeVertexCount() const { return m_volumeVertexCount; }
int LocalCandleSeriesItem::indicatorVertexCount() const { return m_indicatorVertexCount; }
int LocalCandleSeriesItem::oscillatorVertexCount() const
{
    return m_oscillatorVertexCount;
}
qulonglong LocalCandleSeriesItem::fullIndicatorRebuildCount() const
{
    return m_fullIndicatorRebuildCount;
}
qulonglong LocalCandleSeriesItem::incrementalIndicatorUpdateCount() const
{
    return m_incrementalIndicatorUpdateCount;
}
QVariantList LocalCandleSeriesItem::priceAxisTicks() const { return m_priceAxisTicks; }
QVariantList LocalCandleSeriesItem::timeAxisTicks() const { return m_timeAxisTicks; }
QVariantList LocalCandleSeriesItem::macdAxisTicks() const { return m_macdAxisTicks; }
QColor LocalCandleSeriesItem::risingColor() const { return m_risingColor; }
QColor LocalCandleSeriesItem::fallingColor() const { return m_fallingColor; }
QColor LocalCandleSeriesItem::wickColor() const { return m_wickColor; }
QColor LocalCandleSeriesItem::buySignalColor() const { return m_buySignalColor; }
QColor LocalCandleSeriesItem::sellSignalColor() const { return m_sellSignalColor; }

void LocalCandleSeriesItem::setShowSignals(bool showSignals)
{
    if (m_showSignals == showSignals) return;
    m_showSignals = showSignals;
    markGeometryDirty();
    emit showSignalsChanged();
}

void LocalCandleSeriesItem::setShowVolume(bool showVolume)
{
    if (m_showVolume == showVolume) return;
    m_showVolume = showVolume;
    recalculatePriceRange();
    markGeometryDirty();
    emit overlayOptionsChanged();
}

void LocalCandleSeriesItem::setShowMovingAverages(bool showMovingAverages)
{
    if (m_showMovingAverages == showMovingAverages) return;
    m_showMovingAverages = showMovingAverages;
    recalculatePriceRange();
    markGeometryDirty();
    emit overlayOptionsChanged();
}

void LocalCandleSeriesItem::setShowBollingerBands(bool showBollingerBands)
{
    if (m_showBollingerBands == showBollingerBands) return;
    m_showBollingerBands = showBollingerBands;
    recalculatePriceRange();
    markGeometryDirty();
    emit overlayOptionsChanged();
}

void LocalCandleSeriesItem::setShowRsi(bool showRsi)
{
    if (m_showRsi == showRsi) return;
    m_showRsi = showRsi;
    recalculatePriceRange();
    markGeometryDirty();
    emit overlayOptionsChanged();
}

void LocalCandleSeriesItem::setShowMacd(bool showMacd)
{
    if (m_showMacd == showMacd) return;
    m_showMacd = showMacd;
    recalculatePriceRange();
    markGeometryDirty();
    emit overlayOptionsChanged();
}

void LocalCandleSeriesItem::setMovingAverageFastPeriod(int period)
{
    const int value = std::clamp(period, 1, m_movingAverageSlowPeriod - 1);
    if (m_movingAverageFastPeriod == value) return;
    m_movingAverageFastPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setMovingAverageSlowPeriod(int period)
{
    const int value = std::clamp(period, m_movingAverageFastPeriod + 1, 500);
    if (m_movingAverageSlowPeriod == value) return;
    m_movingAverageSlowPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setBollingerPeriod(int period)
{
    const int value = std::clamp(period, 2, 500);
    if (m_bollingerPeriod == value) return;
    m_bollingerPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setBollingerStandardDeviations(qreal standardDeviations)
{
    if (!std::isfinite(standardDeviations)) return;
    const qreal value = std::clamp(standardDeviations, qreal(0.1), qreal(10.0));
    if (qFuzzyCompare(m_bollingerStandardDeviations, value)) return;
    m_bollingerStandardDeviations = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setRsiPeriod(int period)
{
    const int value = std::clamp(period, 2, 500);
    if (m_rsiPeriod == value) return;
    m_rsiPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setMacdFastPeriod(int period)
{
    const int value = std::clamp(period, 1, m_macdSlowPeriod - 1);
    if (m_macdFastPeriod == value) return;
    m_macdFastPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setMacdSlowPeriod(int period)
{
    const int value = std::clamp(period, m_macdFastPeriod + 1, 500);
    if (m_macdSlowPeriod == value) return;
    m_macdSlowPeriod = value;
    indicatorParametersDidChange();
}

void LocalCandleSeriesItem::setMacdSignalPeriod(int period)
{
    const int value = std::clamp(period, 1, 500);
    if (m_macdSignalPeriod == value) return;
    m_macdSignalPeriod = value;
    indicatorParametersDidChange();
}

#define PP_CANDLE_COLOR_SETTER(Method, Member) \
    void LocalCandleSeriesItem::Method(const QColor &color) \
    { \
        if (Member == color) return; \
        Member = color; markGeometryDirty(); emit colorsChanged(); \
    }
PP_CANDLE_COLOR_SETTER(setRisingColor, m_risingColor)
PP_CANDLE_COLOR_SETTER(setFallingColor, m_fallingColor)
PP_CANDLE_COLOR_SETTER(setWickColor, m_wickColor)
PP_CANDLE_COLOR_SETTER(setBuySignalColor, m_buySignalColor)
PP_CANDLE_COLOR_SETTER(setSellSignalColor, m_sellSignalColor)
#undef PP_CANDLE_COLOR_SETTER

QVariantMap LocalCandleSeriesItem::findNearest(qreal x) const
{
    const int localIndex = LocalCandlePlotData::nearestIndex(m_viewport.count, width(), x);
    const int index = localIndex < 0 ? -1 : m_viewport.start + localIndex;
    QVariantMap result = index >= 0 && index < m_points.size()
        ? pointMap(index) : QVariantMap{};
    if (!result.isEmpty()) result.insert(QStringLiteral("index"), index);
    return result;
}

double LocalCandleSeriesItem::priceAt(qreal y) const
{
    const qreal plotHeight = height() * pricePlotRatio();
    if (!m_range.valid || plotHeight <= 0.0) return 0.0;
    const qreal ratio = std::clamp(y / plotHeight, qreal(0.0), qreal(1.0));
    return m_range.maximum - ratio * (m_range.maximum - m_range.minimum);
}

void LocalCandleSeriesItem::zoomAt(qreal x, qreal factor)
{
    if (width() <= 0.0) return;
    applyViewport(LocalCandlePlotData::zoomViewport(
        m_points.size(), m_viewport, x / width(), factor));
}

void LocalCandleSeriesItem::panByPixels(qreal deltaX)
{
    if (width() <= 0.0 || m_viewport.count <= 0) return;
    const qreal pixelsPerBar = width() / qreal(m_viewport.count);
    const int barDelta = int(std::round(-deltaX / pixelsPerBar));
    if (barDelta != 0) applyViewport(LocalCandlePlotData::panViewport(
        m_points.size(), m_viewport, barDelta));
}

void LocalCandleSeriesItem::resetViewport()
{
    applyViewport({0, int(m_points.size())});
}

void LocalCandleSeriesItem::scrollToLatest()
{
    if (m_points.isEmpty()) return;
    const int totalCount = int(m_points.size());
    const int count = m_viewport.count > 0 ? m_viewport.count : totalCount;
    applyViewport({totalCount - count, count});
}

void LocalCandleSeriesItem::scalePriceRange(qreal factor)
{
    if (!std::isfinite(factor) || factor <= 0.0 || !m_range.valid) return;
    const qreal next = std::clamp(m_priceScaleFactor * factor,
                                  qreal(0.05), qreal(8.0));
    if (qFuzzyCompare(next, m_priceScaleFactor)) return;
    const LocalCandlePlotRange previousRange = m_range;
    m_priceScaleFactor = next;
    recalculatePriceRange();
    if (previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit priceScaleChanged();
}

void LocalCandleSeriesItem::resetPriceRange()
{
    if (qFuzzyCompare(m_priceScaleFactor, qreal(1.0))) return;
    const LocalCandlePlotRange previousRange = m_range;
    m_priceScaleFactor = 1.0;
    recalculatePriceRange();
    if (previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit priceScaleChanged();
}

QString LocalCandleSeriesItem::pointTextAt(qreal x) const
{
    const int localIndex = LocalCandlePlotData::nearestIndex(
        m_viewport.count, width(), x);
    const int index = localIndex < 0 ? -1 : m_viewport.start + localIndex;
    if (index < 0 || index >= m_points.size()) return {};
    const LocalCandleSeriesPoint &point = m_points.at(index);
    const SignalVisual &signal = m_signals.at(index);
    const Candle &candle = point.candle;
    const QLocale locale = QLocale::c();
    QString text = QStringLiteral("%1,%2,O=%3,H=%4,L=%5,C=%6,V=%7,source=%8,sourceCount=%9")
        .arg(candle.instrument.symbol,
             candle.startTime.toUTC().toString(Qt::ISODate),
             locale.toString(candle.open, 'f', 4),
             locale.toString(candle.high, 'f', 4),
             locale.toString(candle.low, 'f', 4),
             locale.toString(candle.close, 'f', 4),
             locale.toString(candle.volume, 'f', 0),
             candle.source)
        .arg(point.sourceCount);
    if (signal.present) {
        text += QStringLiteral(",signal=%1,signalPrice=%2,reason=%3")
            .arg(signal.side, locale.toString(signal.price, 'f', 4), signal.reason);
    }
    return text;
}

bool LocalCandleSeriesItem::copyNearestToClipboard(qreal x) const
{
    const QString text = pointTextAt(x);
    if (text.isEmpty() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(text);
    return true;
}

QSGNode *LocalCandleSeriesItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_geometryDirty) return oldNode;
    delete oldNode;
    auto *root = new QSGNode;
    if (m_points.isEmpty() || !m_range.valid || width() <= 0.0 || height() <= 0.0) {
        m_geometryDirty = false;
        m_geometryVertexCount = 0;
        m_renderBucketCount = 0;
        m_volumeVertexCount = 0;
        m_indicatorVertexCount = 0;
        m_oscillatorVertexCount = 0;
        return root;
    }

    const int count = m_viewport.count;
    const QVector<LocalCandleRenderBucket> renderBuckets =
        LocalCandlePlotData::renderBuckets(m_points, m_viewport, width());
    const int renderCount = int(renderBuckets.size());
    const bool levelOfDetail = renderCount < count;
    m_renderBucketCount = renderCount;
    const qreal priceHeight = height() * pricePlotRatio();
    auto *wicks = lineNode(
        LocalCandlePlotData::wickVertexCount(renderCount), m_wickColor);
    int risingCount = 0;
    for (const LocalCandleRenderBucket &bucket : renderBuckets) {
        if (bucket.close >= bucket.open) ++risingCount;
    }
    auto *rising = triangleNode(LocalCandlePlotData::bodyVertexCount(risingCount), m_risingColor);
    auto *falling = triangleNode(
        LocalCandlePlotData::bodyVertexCount(renderCount - risingCount), m_fallingColor);
    int buySignalCount = 0;
    int sellSignalCount = 0;
    if (m_showSignals) {
        for (int index = m_viewport.start;
             index < m_viewport.start + count; ++index) {
            if (!m_signals.at(index).present) continue;
            if (m_signals.at(index).buy) ++buySignalCount;
            else ++sellSignalCount;
        }
    }
    QSGGeometryNode *buySignals = buySignalCount > 0
        ? triangleNode(LocalCandlePlotData::signalMarkerVertexCount(buySignalCount),
                       m_buySignalColor) : nullptr;
    QSGGeometryNode *sellSignals = sellSignalCount > 0
        ? triangleNode(LocalCandlePlotData::signalMarkerVertexCount(sellSignalCount),
                       m_sellSignalColor) : nullptr;
    auto *wickVertices = wicks->geometry()->vertexDataAsPoint2D();
    auto *risingVertices = rising->geometry()->vertexDataAsPoint2D();
    auto *fallingVertices = falling->geometry()->vertexDataAsPoint2D();
    auto *buySignalVertices = buySignals
        ? buySignals->geometry()->vertexDataAsPoint2D() : nullptr;
    auto *sellSignalVertices = sellSignals
        ? sellSignals->geometry()->vertexDataAsPoint2D() : nullptr;
    int risingOffset = 0;
    int fallingOffset = 0;
    int buySignalOffset = 0;
    int sellSignalOffset = 0;
    const qreal halfWidth = LocalCandlePlotData::bodyWidth(renderCount, width()) * 0.5;
    const qreal markerHalfWidth = std::clamp(
        width() / qreal(std::max(1, renderCount)) * 0.42, qreal(3.0), qreal(7.0));
    const qreal markerHeight = markerHalfWidth * 1.6;
    for (int bucketIndex = 0; bucketIndex < renderCount; ++bucketIndex) {
        const LocalCandleRenderBucket &bucket = renderBuckets.at(bucketIndex);
        const qreal x = LocalCandlePlotData::xFor(
            bucketIndex, renderCount, width());
        const qreal highY = LocalCandlePlotData::yFor(
            bucket.high, m_range, priceHeight);
        const qreal lowY = LocalCandlePlotData::yFor(
            bucket.low, m_range, priceHeight);
        const qreal openY = LocalCandlePlotData::yFor(
            bucket.open, m_range, priceHeight);
        const qreal closeY = LocalCandlePlotData::yFor(
            bucket.close, m_range, priceHeight);
        wickVertices[bucketIndex * 2].set(float(x), float(highY));
        wickVertices[bucketIndex * 2 + 1].set(float(x), float(lowY));
        QSGGeometry::Point2D *bodyVertices = bucket.close >= bucket.open
            ? risingVertices + risingOffset : fallingVertices + fallingOffset;
        qreal top = std::min(openY, closeY);
        qreal bottom = std::max(openY, closeY);
        if (bottom - top < 1.0) {
            top -= 0.5;
            bottom += 0.5;
        }
        bodyVertices[0].set(float(x - halfWidth), float(top));
        bodyVertices[1].set(float(x + halfWidth), float(top));
        bodyVertices[2].set(float(x + halfWidth), float(bottom));
        bodyVertices[3].set(float(x - halfWidth), float(top));
        bodyVertices[4].set(float(x + halfWidth), float(bottom));
        bodyVertices[5].set(float(x - halfWidth), float(bottom));
        if (bucket.close >= bucket.open) risingOffset += 6;
        else fallingOffset += 6;
    }
    if (m_showSignals) {
        for (int localIndex = 0; localIndex < count; ++localIndex) {
            const int absoluteIndex = m_viewport.start + localIndex;
            const SignalVisual &signal = m_signals.at(absoluteIndex);
            if (!signal.present) continue;
            const Candle &candle = m_points.at(absoluteIndex).candle;
            const qreal x = LocalCandlePlotData::xFor(localIndex, count, width());
            const double markerPrice = std::isfinite(signal.price) && signal.price > 0.0
                ? signal.price : (signal.buy ? candle.low : candle.high);
            const qreal markerY = LocalCandlePlotData::yFor(
                markerPrice, m_range, priceHeight);
            QSGGeometry::Point2D *markerVertices = signal.buy
                ? buySignalVertices + buySignalOffset
                : sellSignalVertices + sellSignalOffset;
            if (signal.buy) {
                markerVertices[0].set(float(x), float(markerY - markerHeight));
                markerVertices[1].set(
                    float(x - markerHalfWidth), float(markerY + markerHalfWidth));
                markerVertices[2].set(
                    float(x + markerHalfWidth), float(markerY + markerHalfWidth));
                buySignalOffset += 3;
            } else {
                markerVertices[0].set(float(x), float(markerY + markerHeight));
                markerVertices[1].set(
                    float(x - markerHalfWidth), float(markerY - markerHalfWidth));
                markerVertices[2].set(
                    float(x + markerHalfWidth), float(markerY - markerHalfWidth));
                sellSignalOffset += 3;
            }
        }
    }
    root->appendChildNode(wicks);
    root->appendChildNode(rising);
    root->appendChildNode(falling);
    if (buySignals) root->appendChildNode(buySignals);
    if (sellSignals) root->appendChildNode(sellSignals);
    m_volumeVertexCount = 0;
    if (m_showVolume && m_maximumVolume > 0.0) {
        QColor risingVolumeColor = m_risingColor;
        QColor fallingVolumeColor = m_fallingColor;
        risingVolumeColor.setAlpha(150);
        fallingVolumeColor.setAlpha(150);
        QSGGeometryNode *risingVolume = triangleNode(
            LocalCandlePlotData::bodyVertexCount(risingCount), risingVolumeColor);
        QSGGeometryNode *fallingVolume = triangleNode(
            LocalCandlePlotData::bodyVertexCount(renderCount - risingCount),
            fallingVolumeColor);
        auto *risingVolumeVertices = risingVolume->geometry()->vertexDataAsPoint2D();
        auto *fallingVolumeVertices = fallingVolume->geometry()->vertexDataAsPoint2D();
        int risingVolumeOffset = 0;
        int fallingVolumeOffset = 0;
        const qreal volumeTop = height() * volumePlotTop();
        const qreal volumeBottom = height()
            * (volumePlotTop() + volumePlotRatio());
        const qreal volumeHeight = std::max(qreal(1.0), volumeBottom - volumeTop);
        for (int bucketIndex = 0; bucketIndex < renderCount; ++bucketIndex) {
            const LocalCandleRenderBucket &bucket = renderBuckets.at(bucketIndex);
            const qreal x = LocalCandlePlotData::xFor(
                bucketIndex, renderCount, width());
            const qreal ratio = std::clamp(
                qreal(bucket.peakVolume / m_maximumVolume), qreal(0.0), qreal(1.0));
            const qreal top = volumeBottom - ratio * volumeHeight;
            QSGGeometry::Point2D *vertices = bucket.close >= bucket.open
                ? risingVolumeVertices + risingVolumeOffset
                : fallingVolumeVertices + fallingVolumeOffset;
            vertices[0].set(float(x - halfWidth), float(top));
            vertices[1].set(float(x + halfWidth), float(top));
            vertices[2].set(float(x + halfWidth), float(volumeBottom));
            vertices[3].set(float(x - halfWidth), float(top));
            vertices[4].set(float(x + halfWidth), float(volumeBottom));
            vertices[5].set(float(x - halfWidth), float(volumeBottom));
            if (bucket.close >= bucket.open) risingVolumeOffset += 6;
            else fallingVolumeOffset += 6;
        }
        root->appendChildNode(risingVolume);
        root->appendChildNode(fallingVolume);
        m_volumeVertexCount = LocalCandlePlotData::bodyVertexCount(renderCount);
    }

    m_indicatorVertexCount = 0;
    m_oscillatorVertexCount = 0;
    const auto appendPriceSeries = [&](const QVector<double> &values,
                                       const QColor &color) {
        QSGGeometryNode *node = levelOfDetail
            ? lodSeriesNode(values, renderBuckets, m_viewport,
                            width(), 0.0, priceHeight, m_range, color)
            : seriesNode(values, m_viewport.start, count,
                         width(), 0.0, priceHeight, m_range, color);
        if (!node) return;
        m_indicatorVertexCount += node->geometry()->vertexCount();
        root->appendChildNode(node);
    };
    if (m_showMovingAverages) {
        appendPriceSeries(m_smaFast, QColor(QStringLiteral("#f2c94c")));
        appendPriceSeries(m_smaSlow, QColor(QStringLiteral("#56ccf2")));
    }
    if (m_showBollingerBands) {
        const QColor bandColor(QStringLiteral("#9b7cff"));
        appendPriceSeries(m_bollinger.upper, bandColor);
        appendPriceSeries(m_bollinger.middle, QColor(QStringLiteral("#c3b5ff")));
        appendPriceSeries(m_bollinger.lower, bandColor);
    }

    const auto appendOscillatorSeries = [&](const QVector<double> &values,
                                            qreal top, qreal plotHeight,
                                            const LocalCandlePlotRange &range,
                                            const QColor &color) {
        QSGGeometryNode *node = levelOfDetail
            ? lodSeriesNode(values, renderBuckets, m_viewport,
                            width(), top, plotHeight, range, color)
            : seriesNode(values, m_viewport.start, count,
                         width(), top, plotHeight, range, color);
        if (!node) return;
        const int vertices = node->geometry()->vertexCount();
        m_indicatorVertexCount += vertices;
        m_oscillatorVertexCount += vertices;
        root->appendChildNode(node);
    };
    if (m_showRsi) {
        const qreal top = height() * rsiPlotTop();
        const qreal plotHeight = height() * rsiPlotRatio();
        appendOscillatorSeries(m_rsi, top, plotHeight,
                               {0.0, 100.0, true},
                               QColor(QStringLiteral("#f2c94c")));
    }
    if (m_showMacd && m_macdRange.valid) {
        const qreal top = height() * macdPlotTop();
        const qreal plotHeight = height() * macdPlotRatio();
        QVector<QPair<int, double>> histogramSamples;
        histogramSamples.reserve(levelOfDetail ? renderCount * 2 : count);
        if (levelOfDetail) {
            for (const LocalCandleRenderBucket &bucket : renderBuckets) {
                int positiveIndex = -1;
                int negativeIndex = -1;
                double positiveMaximum = 0.0;
                double negativeMinimum = 0.0;
                const int end = std::min(
                    bucket.lastIndex, int(m_macd.histogram.size()) - 1);
                for (int index = bucket.firstIndex; index <= end; ++index) {
                    const double value = m_macd.histogram.at(index);
                    if (!std::isfinite(value)) continue;
                    if (value >= 0.0 && (positiveIndex < 0 || value > positiveMaximum)) {
                        positiveIndex = index;
                        positiveMaximum = value;
                    } else if (value < 0.0
                               && (negativeIndex < 0 || value < negativeMinimum)) {
                        negativeIndex = index;
                        negativeMinimum = value;
                    }
                }
                if (positiveIndex >= 0)
                    histogramSamples.append({positiveIndex, positiveMaximum});
                if (negativeIndex >= 0)
                    histogramSamples.append({negativeIndex, negativeMinimum});
            }
        } else {
            const int end = std::min(int(m_macd.histogram.size()),
                                     m_viewport.start + count);
            for (int index = m_viewport.start; index < end; ++index) {
                const double value = m_macd.histogram.at(index);
                if (std::isfinite(value)) histogramSamples.append({index, value});
            }
        }
        int positiveCount = 0;
        int negativeCount = 0;
        for (const auto &sample : histogramSamples) {
            sample.second >= 0.0 ? ++positiveCount : ++negativeCount;
        }
        QSGGeometryNode *positiveBars = positiveCount > 0
            ? triangleNode(LocalCandlePlotData::bodyVertexCount(positiveCount),
                           m_risingColor) : nullptr;
        QSGGeometryNode *negativeBars = negativeCount > 0
            ? triangleNode(LocalCandlePlotData::bodyVertexCount(negativeCount),
                           m_fallingColor) : nullptr;
        auto *positiveVertices = positiveBars
            ? positiveBars->geometry()->vertexDataAsPoint2D() : nullptr;
        auto *negativeVertices = negativeBars
            ? negativeBars->geometry()->vertexDataAsPoint2D() : nullptr;
        int positiveOffset = 0;
        int negativeOffset = 0;
        const qreal zeroY = top + LocalCandlePlotData::yFor(
            0.0, m_macdRange, plotHeight);
        for (const auto &sample : histogramSamples) {
            const int localIndex = sample.first - m_viewport.start;
            const double value = sample.second;
            const qreal x = LocalCandlePlotData::xFor(localIndex, count, width());
            const qreal valueY = top + LocalCandlePlotData::yFor(
                value, m_macdRange, plotHeight);
            QSGGeometry::Point2D *vertices = value >= 0.0
                ? positiveVertices + positiveOffset : negativeVertices + negativeOffset;
            const qreal barTop = std::min(zeroY, valueY);
            const qreal barBottom = std::max(zeroY, valueY);
            vertices[0].set(float(x - halfWidth), float(barTop));
            vertices[1].set(float(x + halfWidth), float(barTop));
            vertices[2].set(float(x + halfWidth), float(barBottom));
            vertices[3].set(float(x - halfWidth), float(barTop));
            vertices[4].set(float(x + halfWidth), float(barBottom));
            vertices[5].set(float(x - halfWidth), float(barBottom));
            if (value >= 0.0) positiveOffset += 6;
            else negativeOffset += 6;
        }
        if (positiveBars) root->appendChildNode(positiveBars);
        if (negativeBars) root->appendChildNode(negativeBars);
        const int histogramVertices = LocalCandlePlotData::bodyVertexCount(
            positiveCount + negativeCount);
        m_indicatorVertexCount += histogramVertices;
        m_oscillatorVertexCount += histogramVertices;
        appendOscillatorSeries(m_macd.line, top, plotHeight, m_macdRange,
                               QColor(QStringLiteral("#56ccf2")));
        appendOscillatorSeries(m_macd.signal, top, plotHeight, m_macdRange,
                               QColor(QStringLiteral("#f2c94c")));
    }
    m_geometryVertexCount = LocalCandlePlotData::wickVertexCount(renderCount)
        + LocalCandlePlotData::bodyVertexCount(renderCount)
        + LocalCandlePlotData::signalMarkerVertexCount(
            buySignalCount + sellSignalCount)
        + m_volumeVertexCount + m_indicatorVertexCount;
    m_geometryDirty = false;
    return root;
}

void LocalCandleSeriesItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) markGeometryDirty();
}

void LocalCandleSeriesItem::rebuildModelData()
{
    const int previousCount = m_points.size();
    const int previousSignalCount = m_signalCount;
    const LocalCandleViewport previousViewport = m_viewport;
    const bool wasAtRightEdge = previousCount > 0
        && previousViewport.start + previousViewport.count >= previousCount;
    const LocalCandlePlotRange previousRange = m_range;
    m_points.clear();
    m_signals.clear();
    m_signalCount = 0;
    if (const auto *typed = qobject_cast<const LocalCandleSeriesListModel *>(m_model.data())) {
        m_points = typed->points();
        m_signals.resize(m_points.size());
    } else if (m_model) {
        const QHash<int, QByteArray> roles = m_model->roleNames();
        QHash<QByteArray, int> byName;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) byName.insert(it.value(), it.key());
        const auto roleValue = [this, &byName](const QModelIndex &index,
                                               const QByteArray &name) {
            const auto it = byName.constFind(name);
            return it == byName.cend() ? QVariant{} : m_model->data(index, it.value());
        };
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QModelIndex index = m_model->index(row, 0);
            LocalCandleSeriesPoint point;
            point.candle.instrument.symbol = roleValue(index, "symbol").toString();
            point.candle.startTime = roleValue(index, "timestamp").toDateTime();
            point.endTime = roleValue(index, "endTime").toDateTime();
            point.candle.periodSeconds = roleValue(index, "periodSeconds").toInt();
            point.candle.open = roleValue(index, "open").toDouble();
            point.candle.high = roleValue(index, "high").toDouble();
            point.candle.low = roleValue(index, "low").toDouble();
            point.candle.close = roleValue(index, "close").toDouble();
            point.candle.volume = roleValue(index, "volume").toDouble();
            point.candle.source = roleValue(index, "source").toString();
            point.sourceCount = roleValue(index, "sourceCount").toInt();
            if (point.sourceCount <= 0) point.sourceCount = 1;
            if (!point.endTime.isValid() && point.candle.startTime.isValid()) {
                point.endTime = point.candle.startTime.addSecs(
                    std::max(1, point.candle.periodSeconds));
            }
            m_points.append(point);

            SignalVisual signal;
            signal.present = roleValue(index, "hasSignal").toBool();
            if (signal.present) {
                signal.side = roleValue(index, "signalSide").toString();
                signal.buy = signal.side == QStringLiteral("买入")
                    || signal.side.compare(QStringLiteral("buy"), Qt::CaseInsensitive) == 0;
                signal.reason = roleValue(index, "signalReason").toString();
                signal.price = roleValue(index, "signalPrice").toDouble();
                ++m_signalCount;
            }
            m_signals.append(signal);
        }
    }
    finishModelDataChange(previousCount, previousSignalCount, previousViewport,
                          wasAtRightEdge, previousRange, 0);
}

bool LocalCandleSeriesItem::updateTypedModelRows(int first, int last)
{
    const auto *typed = qobject_cast<const LocalCandleSeriesListModel *>(
        m_model.data());
    if (!typed || first < 0 || last < first || last >= m_points.size()
        || typed->points().size() != m_points.size()) {
        return false;
    }
    const int previousCount = m_points.size();
    const int previousSignalCount = m_signalCount;
    const LocalCandleViewport previousViewport = m_viewport;
    const bool wasAtRightEdge = previousCount > 0
        && previousViewport.start + previousViewport.count >= previousCount;
    const LocalCandlePlotRange previousRange = m_range;
    const QVector<LocalCandleSeriesPoint> &source = typed->points();
    for (int row = first; row <= last; ++row) m_points[row] = source.at(row);
    finishModelDataChange(previousCount, previousSignalCount, previousViewport,
                          wasAtRightEdge, previousRange, first);
    return true;
}

bool LocalCandleSeriesItem::appendTypedModelRows(int first, int last)
{
    const auto *typed = qobject_cast<const LocalCandleSeriesListModel *>(
        m_model.data());
    const int addedCount = last - first + 1;
    if (!typed || addedCount <= 0 || first != m_points.size()
        || typed->points().size() != m_points.size() + addedCount
        || last != typed->points().size() - 1) {
        return false;
    }
    const int previousCount = m_points.size();
    const int previousSignalCount = m_signalCount;
    const LocalCandleViewport previousViewport = m_viewport;
    const bool wasAtRightEdge = previousCount > 0
        && previousViewport.start + previousViewport.count >= previousCount;
    const LocalCandlePlotRange previousRange = m_range;
    const QVector<LocalCandleSeriesPoint> &source = typed->points();
    m_points.reserve(source.size());
    for (int row = first; row <= last; ++row) m_points.append(source.at(row));
    m_signals.resize(m_points.size());
    finishModelDataChange(previousCount, previousSignalCount, previousViewport,
                          wasAtRightEdge, previousRange, first);
    return true;
}

void LocalCandleSeriesItem::finishModelDataChange(
    int previousCount, int previousSignalCount,
    const LocalCandleViewport &previousViewport, bool wasAtRightEdge,
    const LocalCandlePlotRange &previousRange, int derivedStart)
{
    if (m_pendingPrependedRows > 0) {
        m_viewport = LocalCandlePlotData::viewportAfterPrepend(
            m_points.size(), previousViewport, m_pendingPrependedRows);
        m_pendingPrependedRows = 0;
    } else {
        m_viewport = LocalCandlePlotData::viewportAfterDataChange(
            m_points.size(), previousCount, previousViewport, wasAtRightEdge);
    }
    if (derivedStart <= 0) rebuildDerivedData();
    else rebuildDerivedDataFrom(derivedStart);
    recalculatePriceRange();
    if (previousCount != m_points.size()) emit pointCountChanged();
    if (previousSignalCount != m_signalCount) emit signalCountChanged();
    if (previousRange.valid != m_range.valid || previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit viewportChanged();
}

void LocalCandleSeriesItem::rebuildDerivedData()
{
    m_smaFast = LocalCandlePlotData::simpleMovingAverage(
        m_points, m_movingAverageFastPeriod);
    m_smaSlow = LocalCandlePlotData::simpleMovingAverage(
        m_points, m_movingAverageSlowPeriod);
    m_bollinger = LocalCandlePlotData::bollingerBands(
        m_points, m_bollingerPeriod, m_bollingerStandardDeviations);
    rebuildRsiCache(0);
    rebuildMacdCache(0);
    ++m_fullIndicatorRebuildCount;
    emit indicatorCacheStatisticsChanged();
}

void LocalCandleSeriesItem::rebuildDerivedDataFrom(int first)
{
    const int pointCount = m_points.size();
    const int macdSeedIndex = m_macdSlowPeriod + m_macdSignalPeriod - 2;
    const bool cacheShapeValid = first > std::max(m_rsiPeriod, macdSeedIndex)
        && m_smaFast.size() >= first && m_smaSlow.size() >= first
        && m_bollinger.upper.size() >= first
        && m_bollinger.middle.size() >= first
        && m_bollinger.lower.size() >= first
        && m_rsi.size() >= first && m_rsiAverageGain.size() >= first
        && m_rsiAverageLoss.size() >= first
        && m_macd.line.size() >= first && m_macd.signal.size() >= first
        && m_macd.histogram.size() >= first
        && m_macdFastEma.size() >= first && m_macdSlowEma.size() >= first;
    if (!cacheShapeValid || pointCount - first > 4096
        || !std::isfinite(m_rsiAverageGain.value(first - 1))
        || !std::isfinite(m_rsiAverageLoss.value(first - 1))
        || !std::isfinite(m_macdFastEma.value(first - 1))
        || !std::isfinite(m_macdSlowEma.value(first - 1))
        || !std::isfinite(m_macd.signal.value(first - 1))) {
        rebuildDerivedData();
        return;
    }

    const double missing = std::numeric_limits<double>::quiet_NaN();
    const auto extend = [pointCount, missing](QVector<double> &values) {
        const int previousSize = values.size();
        values.resize(pointCount);
        std::fill(values.begin() + previousSize, values.end(), missing);
    };
    extend(m_smaFast);
    extend(m_smaSlow);
    extend(m_bollinger.upper);
    extend(m_bollinger.middle);
    extend(m_bollinger.lower);

    const auto updateAverage = [this, first, pointCount, missing](
        QVector<double> &values, int period) {
        for (int index = first; index < pointCount; ++index) {
            values[index] = missing;
            if (index + 1 < period) continue;
            double sum = 0.0;
            bool valid = true;
            for (int source = index - period + 1; source <= index; ++source) {
                const double close = m_points.at(source).candle.close;
                if (!std::isfinite(close)) {
                    valid = false;
                    break;
                }
                sum += close;
            }
            if (valid) values[index] = sum / double(period);
        }
    };
    updateAverage(m_smaFast, m_movingAverageFastPeriod);
    updateAverage(m_smaSlow, m_movingAverageSlowPeriod);

    for (int index = first; index < pointCount; ++index) {
        m_bollinger.upper[index] = missing;
        m_bollinger.middle[index] = missing;
        m_bollinger.lower[index] = missing;
        if (index + 1 < m_bollingerPeriod) continue;
        double sum = 0.0;
        double squaredSum = 0.0;
        bool valid = true;
        for (int source = index - m_bollingerPeriod + 1;
             source <= index; ++source) {
            const double close = m_points.at(source).candle.close;
            if (!std::isfinite(close)) {
                valid = false;
                break;
            }
            sum += close;
            squaredSum += close * close;
        }
        if (!valid) continue;
        const double mean = sum / double(m_bollingerPeriod);
        const double variance = std::max(
            0.0, squaredSum / double(m_bollingerPeriod) - mean * mean);
        const double offset = m_bollingerStandardDeviations
            * std::sqrt(variance);
        m_bollinger.middle[index] = mean;
        m_bollinger.upper[index] = mean + offset;
        m_bollinger.lower[index] = mean - offset;
    }
    rebuildRsiCache(first);
    rebuildMacdCache(first);
    ++m_incrementalIndicatorUpdateCount;
    emit indicatorCacheStatisticsChanged();
}

void LocalCandleSeriesItem::rebuildRsiCache(int first)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    const int pointCount = m_points.size();
    if (first <= m_rsiPeriod) {
        m_rsi.fill(missing, pointCount);
        m_rsiAverageGain.fill(missing, pointCount);
        m_rsiAverageLoss.fill(missing, pointCount);
        if (m_rsiPeriod <= 1 || pointCount <= m_rsiPeriod) return;
        double gainSum = 0.0;
        double lossSum = 0.0;
        for (int index = 1; index <= m_rsiPeriod; ++index) {
            const double previous = m_points.at(index - 1).candle.close;
            const double current = m_points.at(index).candle.close;
            if (!std::isfinite(previous) || !std::isfinite(current)) return;
            const double change = current - previous;
            gainSum += std::max(0.0, change);
            lossSum += std::max(0.0, -change);
        }
        m_rsiAverageGain[m_rsiPeriod] = gainSum / double(m_rsiPeriod);
        m_rsiAverageLoss[m_rsiPeriod] = lossSum / double(m_rsiPeriod);
        first = m_rsiPeriod;
    } else {
        const int previousSize = m_rsi.size();
        m_rsi.resize(pointCount);
        m_rsiAverageGain.resize(pointCount);
        m_rsiAverageLoss.resize(pointCount);
        std::fill(m_rsi.begin() + previousSize, m_rsi.end(), missing);
        std::fill(m_rsiAverageGain.begin() + previousSize,
                  m_rsiAverageGain.end(), missing);
        std::fill(m_rsiAverageLoss.begin() + previousSize,
                  m_rsiAverageLoss.end(), missing);
    }
    const auto rsiValue = [](double gain, double loss) {
        if (gain <= 0.0 && loss <= 0.0) return 50.0;
        if (loss <= 0.0) return 100.0;
        if (gain <= 0.0) return 0.0;
        return 100.0 - 100.0 / (1.0 + gain / loss);
    };
    if (first == m_rsiPeriod) {
        m_rsi[first] = rsiValue(
            m_rsiAverageGain.at(first), m_rsiAverageLoss.at(first));
        ++first;
    }
    for (int index = first; index < pointCount; ++index) {
        const double previous = m_points.at(index - 1).candle.close;
        const double current = m_points.at(index).candle.close;
        if (!std::isfinite(previous) || !std::isfinite(current)) {
            m_rsi[index] = missing;
            m_rsiAverageGain[index] = 0.0;
            m_rsiAverageLoss[index] = 0.0;
            continue;
        }
        const double change = current - previous;
        const double gain = (m_rsiAverageGain.at(index - 1)
            * double(m_rsiPeriod - 1) + std::max(0.0, change))
            / double(m_rsiPeriod);
        const double loss = (m_rsiAverageLoss.at(index - 1)
            * double(m_rsiPeriod - 1) + std::max(0.0, -change))
            / double(m_rsiPeriod);
        m_rsiAverageGain[index] = gain;
        m_rsiAverageLoss[index] = loss;
        m_rsi[index] = rsiValue(gain, loss);
    }
}

void LocalCandleSeriesItem::rebuildMacdCache(int first)
{
    const double missing = std::numeric_limits<double>::quiet_NaN();
    const int pointCount = m_points.size();
    const int signalSeedIndex = m_macdSlowPeriod + m_macdSignalPeriod - 2;
    if (first <= signalSeedIndex) {
        m_macdFastEma.fill(missing, pointCount);
        m_macdSlowEma.fill(missing, pointCount);
        m_macd.line.fill(missing, pointCount);
        m_macd.signal.fill(missing, pointCount);
        m_macd.histogram.fill(missing, pointCount);
        if (m_macdFastPeriod <= 0 || m_macdSlowPeriod <= m_macdFastPeriod
            || m_macdSignalPeriod <= 0 || pointCount == 0) return;
        const auto buildEma = [this, pointCount, missing](
            QVector<double> &values, int period) {
            if (pointCount < period) return;
            double seed = 0.0;
            for (int index = 0; index < period; ++index) {
                const double close = m_points.at(index).candle.close;
                if (!std::isfinite(close)) return;
                seed += close;
            }
            values[period - 1] = seed / double(period);
            const double multiplier = 2.0 / double(period + 1);
            for (int index = period; index < pointCount; ++index) {
                const double close = m_points.at(index).candle.close;
                if (!std::isfinite(close)
                    || !std::isfinite(values.at(index - 1))) continue;
                values[index] = (close - values.at(index - 1)) * multiplier
                    + values.at(index - 1);
            }
        };
        buildEma(m_macdFastEma, m_macdFastPeriod);
        buildEma(m_macdSlowEma, m_macdSlowPeriod);
        for (int index = m_macdSlowPeriod - 1; index < pointCount; ++index) {
            if (std::isfinite(m_macdFastEma.at(index))
                && std::isfinite(m_macdSlowEma.at(index))) {
                m_macd.line[index] = m_macdFastEma.at(index)
                    - m_macdSlowEma.at(index);
            }
        }
        if (pointCount <= signalSeedIndex) return;
        double signalSeed = 0.0;
        for (int index = m_macdSlowPeriod - 1;
             index <= signalSeedIndex; ++index) {
            if (!std::isfinite(m_macd.line.at(index))) return;
            signalSeed += m_macd.line.at(index);
        }
        m_macd.signal[signalSeedIndex] = signalSeed
            / double(m_macdSignalPeriod);
        m_macd.histogram[signalSeedIndex] = m_macd.line.at(signalSeedIndex)
            - m_macd.signal.at(signalSeedIndex);
        first = signalSeedIndex + 1;
    } else {
        const int previousSize = m_macd.line.size();
        m_macdFastEma.resize(pointCount);
        m_macdSlowEma.resize(pointCount);
        m_macd.line.resize(pointCount);
        m_macd.signal.resize(pointCount);
        m_macd.histogram.resize(pointCount);
        std::fill(m_macdFastEma.begin() + previousSize,
                  m_macdFastEma.end(), missing);
        std::fill(m_macdSlowEma.begin() + previousSize,
                  m_macdSlowEma.end(), missing);
        std::fill(m_macd.line.begin() + previousSize, m_macd.line.end(), missing);
        std::fill(m_macd.signal.begin() + previousSize,
                  m_macd.signal.end(), missing);
        std::fill(m_macd.histogram.begin() + previousSize,
                  m_macd.histogram.end(), missing);
    }
    const double fastMultiplier = 2.0 / double(m_macdFastPeriod + 1);
    const double slowMultiplier = 2.0 / double(m_macdSlowPeriod + 1);
    const double signalMultiplier = 2.0 / double(m_macdSignalPeriod + 1);
    for (int index = first; index < pointCount; ++index) {
        const double close = m_points.at(index).candle.close;
        if (std::isfinite(close)
            && std::isfinite(m_macdFastEma.at(index - 1))) {
            m_macdFastEma[index] = (close - m_macdFastEma.at(index - 1))
                * fastMultiplier + m_macdFastEma.at(index - 1);
        } else {
            m_macdFastEma[index] = missing;
        }
        if (std::isfinite(close)
            && std::isfinite(m_macdSlowEma.at(index - 1))) {
            m_macdSlowEma[index] = (close - m_macdSlowEma.at(index - 1))
                * slowMultiplier + m_macdSlowEma.at(index - 1);
        } else {
            m_macdSlowEma[index] = missing;
        }
        if (std::isfinite(m_macdFastEma.at(index))
            && std::isfinite(m_macdSlowEma.at(index))) {
            m_macd.line[index] = m_macdFastEma.at(index)
                - m_macdSlowEma.at(index);
        } else {
            m_macd.line[index] = missing;
        }
        if (std::isfinite(m_macd.line.at(index))
            && std::isfinite(m_macd.signal.at(index - 1))) {
            m_macd.signal[index] = (m_macd.line.at(index)
                - m_macd.signal.at(index - 1)) * signalMultiplier
                + m_macd.signal.at(index - 1);
            m_macd.histogram[index] = m_macd.line.at(index)
                - m_macd.signal.at(index);
        } else {
            m_macd.signal[index] = missing;
            m_macd.histogram[index] = missing;
        }
    }
}

void LocalCandleSeriesItem::indicatorParametersDidChange()
{
    rebuildDerivedData();
    recalculatePriceRange();
    markGeometryDirty();
    emit indicatorParametersChanged();
}

void LocalCandleSeriesItem::applyViewport(const LocalCandleViewport &viewport)
{
    const LocalCandleViewport normalized = LocalCandlePlotData::normalizeViewport(
        m_points.size(), viewport.start, viewport.count);
    if (normalized.start == m_viewport.start && normalized.count == m_viewport.count) return;
    const LocalCandlePlotRange previousRange = m_range;
    m_viewport = normalized;
    recalculatePriceRange();
    if (previousRange.valid != m_range.valid || previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit viewportChanged();
}

void LocalCandleSeriesItem::recalculatePriceRange()
{
    LocalCandlePlotRange automaticRange =
        LocalCandlePlotData::priceRange(m_points, m_viewport);
    if (automaticRange.valid) {
        const int end = std::min(int(m_points.size()), m_viewport.start + m_viewport.count);
        const auto includeSeries = [&](const QVector<double> &values) {
            for (int index = m_viewport.start; index < end; ++index) {
                const double value = values.value(
                    index, std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(value) || value <= 0.0) continue;
                automaticRange.minimum = std::min(automaticRange.minimum, value);
                automaticRange.maximum = std::max(automaticRange.maximum, value);
            }
        };
        if (m_showMovingAverages) {
            includeSeries(m_smaFast);
            includeSeries(m_smaSlow);
        }
        if (m_showBollingerBands) {
            includeSeries(m_bollinger.upper);
            includeSeries(m_bollinger.middle);
            includeSeries(m_bollinger.lower);
        }
    }
    m_range = LocalCandlePlotData::scaledPriceRange(
        automaticRange, m_priceScaleFactor);

    m_macdRange = LocalCandlePlotData::valueRange(
        {m_macd.line, m_macd.signal, m_macd.histogram}, m_viewport, true);

    m_maximumVolume = 0.0;
    const int end = std::min(int(m_points.size()), m_viewport.start + m_viewport.count);
    for (int index = m_viewport.start; index < end; ++index) {
        const double volume = m_points.at(index).candle.volume;
        if (std::isfinite(volume) && volume > m_maximumVolume)
            m_maximumVolume = volume;
    }
    rebuildAxisTicks();
}

void LocalCandleSeriesItem::rebuildAxisTicks()
{
    QVariantList priceTicks;
    const QVector<double> values = LocalCandlePlotData::nicePriceTicks(m_range, 6);
    int decimals = 2;
    if (values.size() >= 2) {
        const double step = std::abs(values.at(1) - values.at(0));
        if (step < 0.01) decimals = 4;
        else if (step < 0.1) decimals = 3;
    }
    const QLocale locale = QLocale::c();
    for (double value : values) {
        const double position = (m_range.maximum - value)
            / (m_range.maximum - m_range.minimum);
        priceTicks.append(QVariantMap {
            {QStringLiteral("value"), value},
            {QStringLiteral("label"), locale.toString(value, 'f', decimals)},
            {QStringLiteral("position"), std::clamp(position, 0.0, 1.0)}
        });
    }

    QVariantList timeTicks;
    const QVector<int> indices = LocalCandlePlotData::timeTickIndices(m_viewport, 6);
    qint64 spanSeconds = 0;
    if (indices.size() >= 2) {
        const QDateTime first = m_points.value(indices.first()).candle.startTime;
        const QDateTime last = m_points.value(indices.last()).candle.startTime;
        if (first.isValid() && last.isValid()) spanSeconds = first.secsTo(last);
    }
    const QString format = spanSeconds <= 2 * 86400
        ? QStringLiteral("MM-dd HH:mm")
        : spanSeconds <= 370LL * 86400 ? QStringLiteral("MM-dd")
                                      : QStringLiteral("yyyy-MM");
    for (int index : indices) {
        const QDateTime timestamp = m_points.value(index).candle.startTime;
        const double position = m_viewport.count > 0
            ? (double(index - m_viewport.start) + 0.5) / double(m_viewport.count)
            : 0.0;
        timeTicks.append(QVariantMap {
            {QStringLiteral("index"), index},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("label"), timestamp.isValid()
                ? timestamp.toLocalTime().toString(format) : QStringLiteral("--")},
            {QStringLiteral("position"), std::clamp(position, 0.0, 1.0)}
        });
    }

    QVariantList macdTicks;
    const QVector<double> macdValues = LocalCandlePlotData::nicePriceTicks(
        m_macdRange, 3);
    int macdDecimals = 3;
    if (macdValues.size() >= 2) {
        const double step = std::abs(macdValues.at(1) - macdValues.at(0));
        if (step >= 1.0) macdDecimals = 2;
        else if (step < 0.01) macdDecimals = 4;
    }
    for (double value : macdValues) {
        const double position = (m_macdRange.maximum - value)
            / (m_macdRange.maximum - m_macdRange.minimum);
        macdTicks.append(QVariantMap {
            {QStringLiteral("value"), value},
            {QStringLiteral("label"), locale.toString(value, 'f', macdDecimals)},
            {QStringLiteral("position"), std::clamp(position, 0.0, 1.0)}
        });
    }
    m_priceAxisTicks = priceTicks;
    m_timeAxisTicks = timeTicks;
    m_macdAxisTicks = macdTicks;
    emit axisTicksChanged();
}

void LocalCandleSeriesItem::markGeometryDirty()
{
    m_geometryDirty = true;
    update();
    emit geometryChanged();
}

QVariantMap LocalCandleSeriesItem::pointMap(int index) const
{
    if (index < 0 || index >= m_points.size()) return {};
    const LocalCandleSeriesPoint &point = m_points.at(index);
    const SignalVisual &signal = m_signals.at(index);
    const auto valueAt = [index](const QVector<double> &values) {
        return index >= 0 && index < values.size() ? values.at(index)
            : std::numeric_limits<double>::quiet_NaN();
    };
    return {{QStringLiteral("timestamp"), point.candle.startTime},
            {QStringLiteral("endTime"), point.endTime},
            {QStringLiteral("periodSeconds"), point.candle.periodSeconds},
            {QStringLiteral("open"), point.candle.open},
            {QStringLiteral("high"), point.candle.high},
            {QStringLiteral("low"), point.candle.low},
            {QStringLiteral("close"), point.candle.close},
            {QStringLiteral("volume"), point.candle.volume},
            {QStringLiteral("source"), point.candle.source},
            {QStringLiteral("sourceCount"), point.sourceCount},
            {QStringLiteral("hasSignal"), signal.present},
            {QStringLiteral("signalSide"), signal.side},
            {QStringLiteral("signalReason"), signal.reason},
            {QStringLiteral("signalPrice"), signal.price},
            {QStringLiteral("ma5"), valueAt(m_smaFast)},
            {QStringLiteral("ma20"), valueAt(m_smaSlow)},
            {QStringLiteral("movingAverageFast"), valueAt(m_smaFast)},
            {QStringLiteral("movingAverageSlow"), valueAt(m_smaSlow)},
            {QStringLiteral("movingAverageFastPeriod"), m_movingAverageFastPeriod},
            {QStringLiteral("movingAverageSlowPeriod"), m_movingAverageSlowPeriod},
            {QStringLiteral("bollingerUpper"), valueAt(m_bollinger.upper)},
            {QStringLiteral("bollingerMiddle"), valueAt(m_bollinger.middle)},
            {QStringLiteral("bollingerLower"), valueAt(m_bollinger.lower)},
            {QStringLiteral("bollingerPeriod"), m_bollingerPeriod},
            {QStringLiteral("rsi"), valueAt(m_rsi)},
            {QStringLiteral("rsiPeriod"), m_rsiPeriod},
            {QStringLiteral("macd"), valueAt(m_macd.line)},
            {QStringLiteral("macdSignal"), valueAt(m_macd.signal)},
            {QStringLiteral("macdHistogram"), valueAt(m_macd.histogram)},
            {QStringLiteral("macdFastPeriod"), m_macdFastPeriod},
            {QStringLiteral("macdSlowPeriod"), m_macdSlowPeriod},
            {QStringLiteral("macdSignalPeriod"), m_macdSignalPeriod}};
}

} // namespace pp
