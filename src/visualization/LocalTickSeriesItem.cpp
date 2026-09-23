#include "visualization/LocalTickSeriesItem.h"

#include "models/LocalTickSeriesListModel.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <algorithm>
#include <cmath>

namespace pp {

namespace {
QSGGeometryNode *nodeFor(QSGGeometry::DrawingMode mode, int count, const QColor &color)
{
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), count);
    geometry->setDrawingMode(mode);
    geometry->setVertexDataPattern(QSGGeometry::StaticPattern);
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial; material->setColor(color);
    node->setMaterial(material); node->setFlag(QSGNode::OwnsMaterial);
    return node;
}
QString directionText(RawTradeDirection value)
{ return value == RawTradeDirection::Buy ? QStringLiteral("买盘") : value == RawTradeDirection::Sell ? QStringLiteral("卖盘") : QStringLiteral("中性盘"); }
}

LocalTickSeriesItem::LocalTickSeriesItem(QQuickItem *parent) : QQuickItem(parent)
{ setFlag(ItemHasContents, true); }
QAbstractItemModel *LocalTickSeriesItem::model() const { return m_model.data(); }
void LocalTickSeriesItem::setModel(QAbstractItemModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::modelReset, this, &LocalTickSeriesItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::dataChanged, this, &LocalTickSeriesItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &LocalTickSeriesItem::rebuildModelData);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &LocalTickSeriesItem::rebuildModelData);
    }
    rebuildModelData(); emit modelChanged();
}
int LocalTickSeriesItem::pointCount() const { return m_points.size(); }
int LocalTickSeriesItem::visibleStart() const { return m_viewport.start; }
int LocalTickSeriesItem::visibleCount() const { return m_viewport.count; }
double LocalTickSeriesItem::minimumPrice() const { return m_range.minimum; }
double LocalTickSeriesItem::maximumPrice() const { return m_range.maximum; }
QColor LocalTickSeriesItem::lineColor() const { return m_lineColor; }
QColor LocalTickSeriesItem::buyColor() const { return m_buyColor; }
QColor LocalTickSeriesItem::sellColor() const { return m_sellColor; }
QColor LocalTickSeriesItem::neutralColor() const { return m_neutralColor; }
#define SET_COLOR(Name, Member) void LocalTickSeriesItem::Name(const QColor &v) { if (Member == v) return; Member = v; markDirty(); emit colorsChanged(); }
SET_COLOR(setLineColor, m_lineColor) SET_COLOR(setBuyColor, m_buyColor)
SET_COLOR(setSellColor, m_sellColor) SET_COLOR(setNeutralColor, m_neutralColor)
#undef SET_COLOR
QVariantMap LocalTickSeriesItem::findNearest(qreal x) const
{
    const int local = LocalTickPlotData::nearestIndex(m_viewport.count, width(), x);
    const int index = local < 0 ? -1 : m_viewport.start + local;
    QVariantMap result = index >= 0 && index < m_points.size() ? pointMap(m_points.at(index)) : QVariantMap{};
    if (!result.isEmpty()) result.insert(QStringLiteral("index"), index);
    return result;
}
double LocalTickSeriesItem::priceAt(qreal y) const
{ if (!m_range.valid || height() <= 0) return 0; return m_range.maximum - std::clamp(y / height(), qreal(0), qreal(1)) * (m_range.maximum - m_range.minimum); }
void LocalTickSeriesItem::zoomAt(qreal x, qreal factor)
{ if (width() > 0) applyViewport(LocalTickPlotData::zoomViewport(m_points.size(), m_viewport, x / width(), factor)); }
void LocalTickSeriesItem::panByPixels(qreal deltaX)
{ if (width() > 0 && m_viewport.count > 0) applyViewport(LocalTickPlotData::panViewport(m_points.size(), m_viewport, int(std::round(-deltaX / (width() / m_viewport.count))))); }
void LocalTickSeriesItem::resetViewport() { applyViewport({0, int(m_points.size())}); }

QSGNode *LocalTickSeriesItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_dirty) return oldNode;
    delete oldNode;
    auto *root = new QSGNode;
    if (!m_range.valid || !m_viewport.count || width() <= 0 || height() <= 0) { m_dirty = false; return root; }
    const int count = m_viewport.count;
    auto *line = nodeFor(QSGGeometry::DrawLineStrip, count, m_lineColor);
    auto *vertices = line->geometry()->vertexDataAsPoint2D();
    int buyCount = 0, sellCount = 0, neutralCount = 0;
    const bool drawMarkers = count <= 20000;
    for (int i = 0; i < count; ++i) {
        const auto &point = m_points.at(m_viewport.start + i);
        vertices[i].set(float(LocalTickPlotData::xFor(i, count, width())),
                        float(LocalTickPlotData::yFor(point.tick.price, m_range, height())));
        if (!drawMarkers) continue;
        if (point.tick.direction == RawTradeDirection::Buy) ++buyCount;
        else if (point.tick.direction == RawTradeDirection::Sell) ++sellCount;
        else ++neutralCount;
    }
    root->appendChildNode(line);
    if (drawMarkers) {
        auto *buy = nodeFor(QSGGeometry::DrawPoints, buyCount, m_buyColor);
        auto *sell = nodeFor(QSGGeometry::DrawPoints, sellCount, m_sellColor);
        auto *neutral = nodeFor(QSGGeometry::DrawPoints, neutralCount, m_neutralColor);
        int bi = 0, si = 0, ni = 0;
        for (int i = 0; i < count; ++i) {
            const auto &point = m_points.at(m_viewport.start + i);
            QSGGeometryNode *target = point.tick.direction == RawTradeDirection::Buy ? buy : point.tick.direction == RawTradeDirection::Sell ? sell : neutral;
            int *offset = point.tick.direction == RawTradeDirection::Buy ? &bi : point.tick.direction == RawTradeDirection::Sell ? &si : &ni;
            target->geometry()->vertexDataAsPoint2D()[(*offset)++].set(
                float(LocalTickPlotData::xFor(i, count, width())),
                float(LocalTickPlotData::yFor(point.tick.price, m_range, height())));
        }
        root->appendChildNode(buy); root->appendChildNode(sell); root->appendChildNode(neutral);
    }
    m_dirty = false;
    return root;
}
void LocalTickSeriesItem::geometryChange(const QRectF &n, const QRectF &o)
{ QQuickItem::geometryChange(n, o); if (n.size() != o.size()) markDirty(); }
void LocalTickSeriesItem::rebuildModelData()
{
    const int oldCount = m_points.size();
    m_points.clear();
    if (const auto *typed = qobject_cast<const LocalTickSeriesListModel *>(m_model.data())) m_points = typed->points();
    m_viewport = LocalTickPlotData::normalizeViewport(m_points.size(), 0, m_points.size());
    m_range = LocalTickPlotData::priceRange(m_points, m_viewport);
    if (oldCount != m_points.size()) emit pointCountChanged();
    emit rangeChanged(); emit viewportChanged(); markDirty();
}
void LocalTickSeriesItem::markDirty() { m_dirty = true; update(); }
void LocalTickSeriesItem::applyViewport(const LocalTickViewport &viewport)
{
    const auto normalized = LocalTickPlotData::normalizeViewport(m_points.size(), viewport.start, viewport.count);
    if (normalized.start == m_viewport.start && normalized.count == m_viewport.count) return;
    m_viewport = normalized; m_range = LocalTickPlotData::priceRange(m_points, m_viewport);
    emit viewportChanged(); emit rangeChanged(); markDirty();
}
QVariantMap LocalTickSeriesItem::pointMap(const LocalTickSeriesPoint &point) const
{
    return {{QStringLiteral("timestamp"), point.tick.timestamp}, {QStringLiteral("eventIndex"), point.eventIndex},
            {QStringLiteral("price"), point.tick.price}, {QStringLiteral("volumeShares"), point.tick.volumeShares},
            {QStringLiteral("amount"), point.tick.amount}, {QStringLiteral("directionText"), directionText(point.tick.direction)},
            {QStringLiteral("sourceLine"), point.tick.sourceLine}, {QStringLiteral("representedCount"), point.representedCount}};
}

} // namespace pp
