#include "visualization/LocalCandleSeriesItem.h"

#include "models/LocalCandleSeriesListModel.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>

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
        connect(m_model, &QAbstractItemModel::modelReset,
                this, &LocalCandleSeriesItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::dataChanged,
                this, &LocalCandleSeriesItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::rowsInserted,
                this, &LocalCandleSeriesItem::rebuildModelData);
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
bool LocalCandleSeriesItem::viewportAtLeftEdge() const { return m_viewport.start == 0; }
int LocalCandleSeriesItem::geometryVertexCount() const { return m_geometryVertexCount; }
QColor LocalCandleSeriesItem::risingColor() const { return m_risingColor; }
QColor LocalCandleSeriesItem::fallingColor() const { return m_fallingColor; }
QColor LocalCandleSeriesItem::wickColor() const { return m_wickColor; }

#define PP_CANDLE_COLOR_SETTER(Method, Member) \
    void LocalCandleSeriesItem::Method(const QColor &color) \
    { \
        if (Member == color) return; \
        Member = color; markGeometryDirty(); emit colorsChanged(); \
    }
PP_CANDLE_COLOR_SETTER(setRisingColor, m_risingColor)
PP_CANDLE_COLOR_SETTER(setFallingColor, m_fallingColor)
PP_CANDLE_COLOR_SETTER(setWickColor, m_wickColor)
#undef PP_CANDLE_COLOR_SETTER

QVariantMap LocalCandleSeriesItem::findNearest(qreal x) const
{
    const int localIndex = LocalCandlePlotData::nearestIndex(m_viewport.count, width(), x);
    const int index = localIndex < 0 ? -1 : m_viewport.start + localIndex;
    QVariantMap result = index >= 0 && index < m_points.size()
        ? pointMap(m_points.at(index)) : QVariantMap{};
    if (!result.isEmpty()) result.insert(QStringLiteral("index"), index);
    return result;
}

double LocalCandleSeriesItem::priceAt(qreal y) const
{
    if (!m_range.valid || height() <= 0.0) return 0.0;
    const qreal ratio = std::clamp(y / height(), qreal(0.0), qreal(1.0));
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

QSGNode *LocalCandleSeriesItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_geometryDirty) return oldNode;
    delete oldNode;
    auto *root = new QSGNode;
    if (m_points.isEmpty() || !m_range.valid || width() <= 0.0 || height() <= 0.0) {
        m_geometryDirty = false;
        m_geometryVertexCount = 0;
        return root;
    }

    const int count = m_viewport.count;
    auto *wicks = lineNode(LocalCandlePlotData::wickVertexCount(count), m_wickColor);
    int risingCount = 0;
    for (int index = m_viewport.start;
         index < m_viewport.start + count; ++index) {
        const LocalCandleSeriesPoint &point = m_points.at(index);
        if (point.candle.close >= point.candle.open) ++risingCount;
    }
    auto *rising = triangleNode(LocalCandlePlotData::bodyVertexCount(risingCount), m_risingColor);
    auto *falling = triangleNode(
        LocalCandlePlotData::bodyVertexCount(count - risingCount), m_fallingColor);
    auto *wickVertices = wicks->geometry()->vertexDataAsPoint2D();
    auto *risingVertices = rising->geometry()->vertexDataAsPoint2D();
    auto *fallingVertices = falling->geometry()->vertexDataAsPoint2D();
    int risingOffset = 0;
    int fallingOffset = 0;
    const qreal halfWidth = LocalCandlePlotData::bodyWidth(count, width()) * 0.5;
    for (int localIndex = 0; localIndex < count; ++localIndex) {
        const Candle &candle = m_points.at(m_viewport.start + localIndex).candle;
        const qreal x = LocalCandlePlotData::xFor(localIndex, count, width());
        const qreal highY = LocalCandlePlotData::yFor(candle.high, m_range, height());
        const qreal lowY = LocalCandlePlotData::yFor(candle.low, m_range, height());
        const qreal openY = LocalCandlePlotData::yFor(candle.open, m_range, height());
        const qreal closeY = LocalCandlePlotData::yFor(candle.close, m_range, height());
        wickVertices[localIndex * 2].set(float(x), float(highY));
        wickVertices[localIndex * 2 + 1].set(float(x), float(lowY));
        QSGGeometry::Point2D *bodyVertices = candle.close >= candle.open
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
        if (candle.close >= candle.open) risingOffset += 6;
        else fallingOffset += 6;
    }
    root->appendChildNode(wicks);
    root->appendChildNode(rising);
    root->appendChildNode(falling);
    m_geometryVertexCount = LocalCandlePlotData::wickVertexCount(count)
        + LocalCandlePlotData::bodyVertexCount(count);
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
    const LocalCandlePlotRange previousRange = m_range;
    m_points.clear();
    if (const auto *typed = qobject_cast<const LocalCandleSeriesListModel *>(m_model.data())) {
        m_points = typed->points();
    } else if (m_model) {
        const QHash<int, QByteArray> roles = m_model->roleNames();
        QHash<QByteArray, int> byName;
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) byName.insert(it.value(), it.key());
        for (int row = 0; row < m_model->rowCount(); ++row) {
            const QModelIndex index = m_model->index(row, 0);
            LocalCandleSeriesPoint point;
            point.candle.startTime = m_model->data(index, byName.value("timestamp")).toDateTime();
            point.endTime = m_model->data(index, byName.value("endTime")).toDateTime();
            point.candle.open = m_model->data(index, byName.value("open")).toDouble();
            point.candle.high = m_model->data(index, byName.value("high")).toDouble();
            point.candle.low = m_model->data(index, byName.value("low")).toDouble();
            point.candle.close = m_model->data(index, byName.value("close")).toDouble();
            point.candle.volume = m_model->data(index, byName.value("volume")).toDouble();
            point.sourceCount = m_model->data(index, byName.value("sourceCount")).toInt();
            m_points.append(point);
        }
    }
    m_viewport = LocalCandlePlotData::normalizeViewport(
        m_points.size(), 0, m_points.size());
    m_range = LocalCandlePlotData::priceRange(m_points, m_viewport);
    if (previousCount != m_points.size()) emit pointCountChanged();
    if (previousRange.valid != m_range.valid || previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit viewportChanged();
}

void LocalCandleSeriesItem::applyViewport(const LocalCandleViewport &viewport)
{
    const LocalCandleViewport normalized = LocalCandlePlotData::normalizeViewport(
        m_points.size(), viewport.start, viewport.count);
    if (normalized.start == m_viewport.start && normalized.count == m_viewport.count) return;
    const LocalCandlePlotRange previousRange = m_range;
    m_viewport = normalized;
    m_range = LocalCandlePlotData::priceRange(m_points, m_viewport);
    if (previousRange.valid != m_range.valid || previousRange.minimum != m_range.minimum
        || previousRange.maximum != m_range.maximum) emit rangeChanged();
    markGeometryDirty();
    emit viewportChanged();
}

void LocalCandleSeriesItem::markGeometryDirty()
{
    m_geometryDirty = true;
    update();
    emit geometryChanged();
}

QVariantMap LocalCandleSeriesItem::pointMap(const LocalCandleSeriesPoint &point) const
{
    return {{QStringLiteral("timestamp"), point.candle.startTime},
            {QStringLiteral("endTime"), point.endTime},
            {QStringLiteral("open"), point.candle.open},
            {QStringLiteral("high"), point.candle.high},
            {QStringLiteral("low"), point.candle.low},
            {QStringLiteral("close"), point.candle.close},
            {QStringLiteral("volume"), point.candle.volume},
            {QStringLiteral("source"), point.candle.source},
            {QStringLiteral("sourceCount"), point.sourceCount}};
}

} // namespace pp
